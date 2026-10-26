// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

#include <fcntl.h>
#include <io.h>

static const char* kUsage =
"RamStasis.exe - freeze a Windows process tree, page it out, wake it up\n"
"\n"
"usage (CLI mode; the GUI is RamStasis.exe with no command):\n"
"  RamStasis.exe list    [--json]\n"
"  RamStasis.exe tree    <pid|exe> [--json]\n"
"  RamStasis.exe guard   [--json]\n"
"  RamStasis.exe freeze  <pid|exe> [--json] [--quiet] [--no-tree] [--force]\n"
"  RamStasis.exe thaw    <pid|exe> [--json] [--quiet]\n"
"  RamStasis.exe thaw    --orphans [--json] [--quiet] [--force]\n"
"  RamStasis.exe watch   <pid|exe> [--json] [--timeout ms]\n"
"  RamStasis.exe stats   <pid|exe> [--json] [--quiet]\n"
"  RamStasis.exe selftest [--json]\n"
"\n"
"options:\n"
"  --json            machine-readable output on stdout (errors still go to stderr,\n"
"                    plus a JSON error object on stdout)\n"
"  --quiet           suppress the tables; NEVER suppresses the commit/pagefile warning\n"
"  --no-tree         freeze only the named process (default is the whole tree)\n"
"  --all             allow an ambiguous exe name (first match of many)\n"
"  --force           override the anti-cheat guard. Prints a loud ban-risk warning.\n"
"  --timeout <ms>    watch timeout, default 120000\n"
"  --state-dir <p>   where freeze state files live (default <root>\\state)\n"
"  --log <p>         run log path (default <root>\\logs\\frz-YYYYMMDD.log)\n"
"                    <root> = exe folder, or its parent when the exe sits in bin\n"
"  --verbose\n"
"\n"
"freeze sequence (children first, then root):\n"
"  0. anti-cheat guard: process names + parent + loaded modules + kernel drivers.\n"
"     Refuses with exit 8, no handle on the target, nothing written.\n"
"  1. OpenProcess(SUSPEND_RESUME|QUERY_INFORMATION|VM_READ|SET_QUOTA|VM_OPERATION)\n"
"  2. ShowWindowAsync(hwnd, SW_MINIMIZE) on every top-level window - NO INJECTION.\n"
"     Records IsWindowVisible + IsIconic + the window rect per window.\n"
"  3. Sleep(150) so DWM/WDDM act on the minimize.\n"
"  4. ntdll!NtSuspendProcess, then re-read every thread's suspend count and\n"
"     individually suspend anything that escaped the iterative walk.\n"
"  5. psapi!EmptyWorkingSet per pid, VERIFIED against WorkingSetSize actually dropping.\n"
"  6. state file: every pid, its per-thread suspend counts, every minimized window.\n"
"thaw (root first, then children):\n"
"   refuses without a state file; refuses a recycled pid; resumes exactly once;\n"
"   restores only the windows frz itself minimized.\n";

namespace {

// When a GUI-subsystem process runs from a terminal by hand, attach to the
// parent console and rebind stdout/stderr to it so CLI output is visible.
void AttachParentConsole()
{
    FILE* fIn = nullptr;
    FILE* fOut = nullptr;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    freopen_s(&fIn, "CONIN$", "r", stdin);
    freopen_s(&fOut, "CONOUT$", "w", stdout);
    freopen_s(&fOut, "CONOUT$", "w", stderr);
}

bool IsEngineCommand(const wchar_t* arg)
{
    static const wchar_t* kEngineCmds[] = {
        L"list", L"tree", L"guard", L"freeze", L"thaw", L"watch", L"stats",
        L"selftest", L"help", L"__child", L"__tree", L"__win", nullptr };
    for (int i = 0; kEngineCmds[i]; ++i)
        if (wcscmp(arg, kEngineCmds[i]) == 0) return true;
    return false;
}

}

bool FrzEngineCliRequest()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool engine = false;
    for (int i = 1; i < argc && !engine; ++i) {
        const wchar_t* a = argv[i];
        if (wcscmp(a, L"--help") == 0 || wcscmp(a, L"-h") == 0 || wcscmp(a, L"/?") == 0 ||
            IsEngineCommand(a)) {
            engine = true;
        }
    }
    LocalFree(argv);
    return engine;
}

namespace {

// A WINDOWS-subsystem process marks CRT fd 0/1/2 as invalid (-2) even when the
// launcher passed real handles with STARTF_USESTDHANDLES (pipes, file
// redirects). Rebind them from GetStdHandle so the embedded engine writes to
// exactly the handles the caller gave it.
void BindStdHandle(int fd, DWORD stdId, int modeFlags)
{
    if (_get_osfhandle(fd) != -2) return;
    HANDLE h = GetStdHandle(stdId);
    if (!h || h == INVALID_HANDLE_VALUE) return;
    int src = _open_osfhandle((intptr_t)h, modeFlags);
    if (src == -1) return;
    if (src != fd) {
        _dup2(src, fd);
        _close(src);
    }
}

}

void EngineStdioInit()
{
    BindStdHandle(0, STD_INPUT_HANDLE, _O_RDONLY | _O_TEXT);
    BindStdHandle(1, STD_OUTPUT_HANDLE, _O_WRONLY | _O_TEXT);
    BindStdHandle(2, STD_ERROR_HANDLE, _O_WRONLY | _O_TEXT);

    // No redirected handles at all means a human ran it from a terminal:
    // attach to the parent console and rebind stdout/stderr there instead.
    if (_get_osfhandle(1) == -2 && _get_osfhandle(2) == -2)
        AttachParentConsole();
}

int FrzEngineMain()
{
    EngineStdioInit();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        fprintf(stderr, "CommandLineToArgvW failed\n");
        return 2;
    }

    wchar_t exePath[1024];
    DWORD n = GetModuleFileNameW(nullptr, exePath, 1024);
    if (n == 0) exePath[0] = 0;

    std::vector<std::wstring> pos;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--json") {
            g_ctx.json = true;
        } else if (a == L"--verbose") {
            g_ctx.verbose = true;
        } else if (a == L"--all") {
            g_ctx.all = true;
        } else if (a == L"--orphans") {
            g_ctx.orphans = true;
        } else if (a == L"--force") {
            g_ctx.force = true;
        } else if (a == L"--quiet") {
            g_ctx.quiet = true;
        } else if (a == L"--no-tree") {
            g_ctx.noTree = true;
        } else if (a == L"--tree") {
            g_ctx.noTree = false;
        } else if (a == L"--timeout" && i + 1 < argc) {
            g_ctx.timeoutMs = (int)_wtoi(argv[++i]);
        } else if (a == L"--state-dir" && i + 1 < argc) {
            g_ctx.stateDir = argv[++i];
        } else if (a == L"--log" && i + 1 < argc) {
            g_ctx.logPath = argv[++i];
        } else if (a == L"--help" || a == L"-h" || a == L"/?") {
            pos.insert(pos.begin(), L"help");
        } else if (!a.empty() && a[0] == L'-') {
            fprintf(stderr, "unknown option: %s\n", W2U(a).c_str());
            LocalFree(argv);
            return 2;
        } else {
            pos.push_back(a);
        }
    }

    LogInit(exePath);
    Log("argv: %s", W2U(GetCommandLineW()).c_str());

    if (pos.empty()) {
        fputs(kUsage, stdout);
        LocalFree(argv);
        return 2;
    }

    std::wstring cmd = pos[0];
    std::string cmdA = W2U(cmd);
    SetCmd(cmdA.c_str());

    std::wstring target = pos.size() > 1 ? pos[1] : L"";
    int rc = 2;

    if (cmd == L"list") {
        rc = CmdList();
    } else if (cmd == L"guard") {
        rc = CmdGuard();
    } else if (cmd == L"tree") {
        if (target.empty()) {
            Err("ERROR: tree needs a <pid|exe> target");
            rc = 2;
        } else {
            rc = CmdTree(target);
        }
    } else if (cmd == L"thaw" && target.empty() && g_ctx.orphans) {
        rc = CmdThawOrphans();
    } else if (cmd == L"freeze" || cmd == L"thaw" || cmd == L"watch" || cmd == L"stats") {
        if (target.empty()) {
            Err("ERROR: %s needs a <pid|exe> target", cmdA.c_str());
            rc = 2;
        } else if (cmd == L"freeze") {
            rc = CmdFreeze(target);
        } else if (cmd == L"thaw") {
            rc = CmdThaw(target);
        } else if (cmd == L"watch") {
            rc = CmdWatch(target);
        } else {
            rc = CmdStats(target);
        }
    } else if (cmd == L"selftest") {
        rc = CmdSelftest();
    } else if (cmd == L"__child") {
        rc = ChildMain(target);
    } else if (cmd == L"__tree") {
        int leaves = pos.size() > 2 ? _wtoi(pos[2].c_str()) : 3;
        int slot = pos.size() > 3 ? _wtoi(pos[3].c_str()) : 0;
        rc = ChildTreeMain(target, leaves, slot);
    } else if (cmd == L"__win") {
        rc = ChildWinMain(target);
    } else if (cmd == L"help" || cmd == L"--help") {
        fputs(kUsage, stdout);
        rc = 0;
    } else {
        fprintf(stderr, "unknown command: %s\n\n", cmdA.c_str());
        fputs(kUsage, stdout);
        rc = 2;
    }

    Log("exit %d", rc);
    LocalFree(argv);
    return rc;
}
