// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frzui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <tlhelp32.h>
#include <thread>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <dwmapi.h>
#include <winver.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")

#ifndef FRZ_NTSTATUS_DEFINED
#define FRZ_NTSTATUS_DEFINED
typedef LONG NTSTATUS;
#endif
typedef NTSTATUS(NTAPI* PFN_NtResumeProcess)(HANDLE);
typedef NTSTATUS(NTAPI* PFN_NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct VmCountersEx2 {
    SIZE_T peakVirtualSize;
    SIZE_T virtualSize;
    ULONG  pageFaultCount;
    SIZE_T peakWorkingSetSize;
    SIZE_T workingSetSize;
    SIZE_T quotaPeakPagedPoolUsage;
    SIZE_T quotaPagedPoolUsage;
    SIZE_T quotaPeakNonPagedPoolUsage;
    SIZE_T quotaNonPagedPoolUsage;
    SIZE_T pagefileUsage;
    SIZE_T peakPagefileUsage;
    SIZE_T privateUsage;
    SIZE_T privateWorkingSetSize;
    SIZE_T sharedWorkingSetSize;
    SIZE_T shareCount;
    SIZE_T commitCharge;
};

Caps g_caps;

namespace {

void ReadPipeToEnd(HANDLE h, std::string* into)
{
    char buf[4096];
    for (;;) {
        DWORD got = 0;
        BOOL rd = ReadFile(h, buf, sizeof(buf), &got, nullptr);
        if (!rd || got == 0) break;
        into->append(buf, got);
    }
}

}

std::wstring UiExeDir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return L".";
    std::wstring p(buf, len);
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : p.substr(0, k);
}

std::wstring UiRootDir()
{

    std::wstring d = UiExeDir();
    size_t k = d.find_last_of(L"\\/");
    if (k != std::wstring::npos && k > 2 && _wcsicmp(d.c_str() + k + 1, L"bin") == 0)
        return d.substr(0, k);
    return d;
}

// RamStasis now embeds the engine. The UI spawns itself; the engine CLI is
// dispatched from wWinMain whenever the first argument is an engine command.
std::wstring EngineExePath()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return UiExeDir() + L"\\RamStasis.exe";
    return std::wstring(buf, len);
}
std::wstring EngineStateDir()  { return UiRootDir() + L"\\state"; }

EngineResult RunEngine(const wchar_t* args)
{
    EngineResult r;
    std::wstring cmd = L"\"" + EngineExePath() + L"\" " + args;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rdOut = nullptr, wrOut = nullptr, rdErr = nullptr, wrErr = nullptr;
    if (!CreatePipe(&rdOut, &wrOut, &sa, 0)) { r.spawnErr = (long)GetLastError(); return r; }
    if (!CreatePipe(&rdErr, &wrErr, &sa, 0)) {
        r.spawnErr = (long)GetLastError();
        CloseHandle(rdOut); CloseHandle(wrOut); return r;
    }
    SetHandleInformation(rdOut, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(rdErr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wrOut;
    si.hStdError = wrErr;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    BOOL okc = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wrOut);
    CloseHandle(wrErr);
    if (!okc) { r.spawnErr = (long)GetLastError(); CloseHandle(rdOut); CloseHandle(rdErr); return r; }

    std::string outText, errText;
    std::thread tOut(ReadPipeToEnd, rdOut, &outText);
    std::thread tErr(ReadPipeToEnd, rdErr, &errText);
    tOut.join();
    tErr.join();
    r.out.swap(outText);
    r.err.swap(errText);
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = (long)code;
    r.ran = true;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rdOut);
    CloseHandle(rdErr);
    return r;
}

void EngineCapabilityProbe()
{
    EngineResult r = RunEngine(L"help");
    g_caps.probed = true;
    if (!r.ran) {
        LogUi("caps: engine probe failed to spawn (spawnErr=%ld)", r.spawnErr);
        g_caps.hasTree = false;
        g_caps.hasGuard = false;
        return;
    }
    g_caps.hasTree  = r.out.find("--no-tree") != std::string::npos ||
                      r.err.find("--no-tree") != std::string::npos;
    g_caps.hasGuard = r.merged().find("guard") != std::string::npos;
    LogUi("caps: hasTree=%d hasGuard=%d", (int)g_caps.hasTree, (int)g_caps.hasGuard);
}

namespace {

std::string ReadWholeFile(const std::wstring& path, bool* okOut)
{
    if (okOut) *okOut = false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(f);
        return std::string();
    }
    std::string text((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(f, &text[0], (DWORD)text.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok) return std::string();
    text.resize(got);
    if (okOut) *okOut = true;
    return text;
}

void ParseStateText(const std::string& text, StateFileInfo& out)
{
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        if (line.compare(0, 10, "frz-state ") == 0) {
            out.version = atoi(line.c_str() + 10);
            continue;
        }
        if (line.compare(0, 5, "root ") == 0) {
            out.rootPid = strtoull(line.c_str() + 5, nullptr, 10);
            continue;
        }
        if (line.compare(0, 6, "image ") == 0) {
            out.image = Utf8ToWPublic(line.substr(6));
            continue;
        }
        if (line.compare(0, 13, "suspended_at ") == 0) {
            out.suspendedAt = strtoull(line.c_str() + 13, nullptr, 10);
            continue;
        }
        if (line.compare(0, 5, "proc ") == 0) {
            unsigned long long pid = 0, ct = 0, wsb = 0, wsa = 0, com = 0;
            int depth = 0, alive = 1;
            unsigned calls = 0;
            int n = sscanf_s(line.c_str() + 5, "%llu %llu %d %u %llu %llu %llu %d",
                             &pid, &ct, &depth, &calls, &wsb, &wsa, &com, &alive);
            if (n < 7) continue;
            StateProcRec p;
            p.pid = pid;
            p.createTime = ct;
            p.depth = depth;
            p.suspendCalls = calls;
            p.wsBefore = wsb;
            p.wsAfter = wsa;
            p.commit = com;
            p.alive = alive;
            out.procs.push_back(p);
            out.wsBeforeSum += wsb;
            out.commitSum += com;
            continue;
        }
        if (line.compare(0, 4, "exe ") == 0) {
            unsigned long long pid = strtoull(line.c_str() + 4, nullptr, 10);
            const char* nm = strchr(line.c_str() + 4, ' ');
            if (!nm) continue;
            ++nm;
            for (size_t i = 0; i < out.procs.size(); ++i) {
                if (out.procs[i].pid == pid) { out.procs[i].exe = Utf8ToWPublic(nm); break; }
            }
            continue;
        }
        if (line.compare(0, 4, "win ") == 0) {
            unsigned long long pid = 0, hwnd = 0;
            int vis = 0, icon = 0, byus = 0;
            long l = 0, t = 0, rr = 0, b = 0;
            int n = sscanf_s(line.c_str() + 4, "%llu %llu %d %d %d %ld %ld %ld %ld",
                             &pid, &hwnd, &vis, &icon, &byus, &l, &t, &rr, &b);
            if (n != 9) continue;
            StateWinRec rec;
            rec.pid = pid;
            rec.rect.left = (LONG)l; rec.rect.top = (LONG)t;
            rec.rect.right = (LONG)rr; rec.rect.bottom = (LONG)b;
            rec.visible = vis != 0;
            rec.iconic = icon != 0;
            out.wins.push_back(rec);
            continue;
        }
    }
}

}

bool ReadStateFile(unsigned long long rootPid, StateFileInfo& out)
{
    out = StateFileInfo();
    std::wstring path = EngineStateDir() + L"\\frz-" + std::to_wstring(rootPid) + L".state";
    bool ok = false;
    std::string text = ReadWholeFile(path, &ok);
    if (!ok) return false;
    out.path = path;
    ParseStateText(text, out);
    return out.version >= 1 && out.rootPid != 0;
}

static bool StateRootStillMatches(const StateFileInfo& info)
{
    if (info.rootPid == 0) return false;
    unsigned long long recorded = 0;
    for (size_t i = 0; i < info.procs.size(); ++i) {
        if (info.procs[i].pid == info.rootPid) {
            recorded = info.procs[i].createTime;
            break;
        }
    }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)info.rootPid);
    if (!h) {

        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    FILETIME created, exited, kernel, user;
    bool ok = GetProcessTimes(h, &created, &exited, &kernel, &user) != FALSE;
    CloseHandle(h);
    if (!ok) return false;
    unsigned long long live = ((unsigned long long)created.dwHighDateTime << 32) |
                              (unsigned long long)created.dwLowDateTime;
    return recorded == 0 || live == recorded;
}

bool ReadStateDirFrozen(std::vector<unsigned long long>& pids, std::vector<StateWinRec>& wins,
                        std::vector<StateFileInfo>* infos)
{
    std::wstring dir = EngineStateDir();
    std::wstring pat = dir + L"\\frz-*.state";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring path = dir + L"\\" + fd.cFileName;
            bool ok = false;
            std::string text = ReadWholeFile(path, &ok);
            if (ok) {
                StateFileInfo info;
                info.path = path;
                ParseStateText(text, info);
                if (info.rootPid != 0 && StateRootStillMatches(info)) {
                    pids.push_back(info.rootPid);
                    for (size_t i = 0; i < info.wins.size(); ++i) wins.push_back(info.wins[i]);
                    if (infos) infos->push_back(info);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return !pids.empty();
}

unsigned long long StateCommitSum(unsigned long long rootPid)
{
    StateFileInfo info;
    if (!ReadStateFile(rootPid, info)) return 0;
    return info.commitSum;
}

namespace {

struct ProcEntry {
    DWORD pid = 0;
    DWORD ppid = 0;
    DWORD threads = 0;
    wchar_t exe[MAX_PATH];
};

bool TakeProcessSnapshot(std::vector<ProcEntry>& out)
{
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcEntry e;
            e.pid = pe.th32ProcessID;
            e.ppid = pe.th32ParentProcessID;
            e.threads = pe.cntThreads;
            wcsncpy_s(e.exe, pe.szExeFile, _TRUNCATE);
            out.push_back(e);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return !out.empty();
}

const ProcEntry* FindProc(const std::vector<ProcEntry>& v, DWORD pid)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].pid == pid) return &v[i];
    return nullptr;
}

bool IsCoreSystemExe(const wchar_t* exe)
{
    static const wchar_t* kCore[] = {
        L"dwm.exe", L"svchost.exe", L"explorer.exe", L"csrss.exe", L"wininit.exe",
        L"lsass.exe", L"services.exe", L"System", L"Registry", L"smss.exe",
        L"winlogon.exe", L"WmiPrvSE.exe", L"dllhost.exe", L"fontdrvhost.exe",
        L"ShellExperienceHost.exe", L"StartMenuExperienceHost.exe",
        L"SearchHost.exe", L"SearchIndexer.exe", L"RuntimeBroker.exe",
        L"TextInputHost.exe", L"ShellHost.exe", L"conhost.exe", L"OpenConsole.exe",
        L"WindowsTerminal.exe", L"ApplicationFrameHost.exe", L"wudfhost.exe",
        L"spoolsv.exe", L"taskhostw.exe", L"msdtc.exe", L"SecurityHealthSystray.exe",
        L"SecurityHealthService.exe", L"msmpeng.exe", L"audiodg.exe", L"ctfmon.exe",
        L"LogonUI.exe", L"winlogon.exe", L"dwm.exe", L"memcompression.exe",
        L"Registry", L"System", L"Idle", L"sihost.exe", L"mousocoreworker.exe",
        L"MusNotification.exe", L"PickerHost.exe", L"SystemSettings.exe",
        L"backgroundTaskHost.exe", L"PrintIsolation.exe", L"wudfhost.exe",
        L"LockApp.exe", L"ShellHWND", L"vmmemWSL", L"vmmem", L"wslservice.exe",
        L"wslhost.exe", L"wslrelayd.exe", L"conhost.exe", L"nvcontainer.exe",
        L"Everything.exe", nullptr };
    for (int i = 0; kCore[i]; ++i) {
        if (_wcsicmp(exe, kCore[i]) == 0) return true;
    }
    return false;
}

}

bool IsElevatedUi()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION te;
    DWORD ret = 0;
    bool elev = false;
    if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &ret)) elev = te.TokenIsElevated != 0;
    CloseHandle(tok);
    return elev;
}

bool TryEnableDebugPrivilege()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return false;
    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(tok);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(tok);
    return ok && (err == ERROR_SUCCESS);
}

namespace {

bool QueryProcessMemory(DWORD pid, unsigned long long& ws, unsigned long long& commit)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h && GetLastError() == ERROR_ACCESS_DENIED && IsElevatedUi()) {
        static bool tried = false;
        if (!tried) {
            tried = true;
            if (TryEnableDebugPrivilege()) {
                h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            }
        }
    }
    if (!h) return false;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    bool ok = GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)) != FALSE;
    if (ok) {
        ws = (unsigned long long)pmc.WorkingSetSize;
        commit = (unsigned long long)pmc.PrivateUsage;
    }
    CloseHandle(h);
    return ok;
}

bool QueryProcessImage(DWORD pid, std::wstring& out)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h && GetLastError() == ERROR_ACCESS_DENIED && IsElevatedUi()) {
        static bool tried = false;
        if (!tried) {
            tried = true;
            if (TryEnableDebugPrivilege()) {
                h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            }
        }
    }
    if (!h) return false;
    wchar_t buf[MAX_PATH];
    DWORD need = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(h, 0, buf, &need) != FALSE;
    CloseHandle(h);
    if (!ok) return false;
    out = buf;
    return true;
}

}

bool IsCoreSystemProcess(unsigned long long pid)
{
    if (pid == 0 || pid == 4) return true;
    std::wstring image;
    if (!QueryProcessImage((DWORD)pid, image)) {

        return true;
    }
    const wchar_t* base = wcsrchr(image.c_str(), L'\\');
    const wchar_t* exe = base ? base + 1 : image.c_str();
    return IsCoreSystemExe(exe);
}

unsigned long long ProcessCreateTimeUi(unsigned long long pid)
{
    if (pid == 0) return 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return 0;
    FILETIME created, exited, kernel, user;
    ZeroMemory(&created, sizeof(created));
    bool ok = GetProcessTimes(h, &created, &exited, &kernel, &user) != FALSE;
    CloseHandle(h);
    if (!ok) return 0;
    return ((unsigned long long)created.dwHighDateTime << 32) |
           (unsigned long long)created.dwLowDateTime;
}

bool IsProcessInstanceAlive(unsigned long long pid, unsigned long long expectedCreateTime)
{
    if (pid == 0) return false;
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) return true;
        return false;
    }
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    if (alive && expectedCreateTime != 0) {
        FILETIME created, exited, kernel, user;
        ZeroMemory(&created, sizeof(created));
        if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
            unsigned long long live = ((unsigned long long)created.dwHighDateTime << 32) |
                                      (unsigned long long)created.dwLowDateTime;
            if (live != expectedCreateTime) alive = false;
        }
    }
    CloseHandle(h);
    return alive;
}

bool ProcessPrivateWorkingSetUi(unsigned long long pid, unsigned long long& bytes)
{
    bytes = 0;
    if (pid == 0) return false;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_NtQueryInformationProcess query = ntdll
        ? reinterpret_cast<PFN_NtQueryInformationProcess>(
              GetProcAddress(ntdll, "NtQueryInformationProcess"))
        : nullptr;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;

    if (query) {
        VmCountersEx2 vm;
        ZeroMemory(&vm, sizeof(vm));
        ULONG returned = 0;
        NTSTATUS st = query(h, 3, &vm, sizeof(vm), &returned);
        const ULONG need = (ULONG)(offsetof(VmCountersEx2, privateWorkingSetSize) + sizeof(SIZE_T));
        if (st >= 0 && returned >= need) {
            bytes = (unsigned long long)vm.privateWorkingSetSize;
            CloseHandle(h);
            return true;
        }
    }

    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    bool ok = GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)) != FALSE;
    if (ok) bytes = (unsigned long long)pmc.WorkingSetSize;
    CloseHandle(h);
    return ok;
}

bool TreePrivateWorkingSetUi(unsigned long long rootPid, bool tree, int& procCount,
                             unsigned long long& privateWs, unsigned long long& commit)
{
    procCount = 0;
    privateWs = 0;
    commit = 0;
    if (rootPid == 0) return false;

    std::vector<DWORD> pids;
    if (tree) {
        std::vector<ProcEntry> snap;
        if (!TakeProcessSnapshot(snap)) return false;
        if (!FindProc(snap, (DWORD)rootPid)) return false;
        pids.push_back((DWORD)rootPid);
        for (size_t i = 0; i < pids.size(); ++i) {
            for (size_t k = 0; k < snap.size(); ++k) {
                if (snap[k].ppid != pids[i]) continue;
                bool dup = false;
                for (size_t q = 0; q < pids.size(); ++q) {
                    if (pids[q] == snap[k].pid) { dup = true; break; }
                }
                if (!dup) pids.push_back(snap[k].pid);
            }
        }
    } else {
        pids.push_back((DWORD)rootPid);
    }

    for (size_t i = 0; i < pids.size(); ++i) {
        unsigned long long privateBytes = 0;
        unsigned long long publicWs = 0;
        unsigned long long com = 0;
        if (!ProcessPrivateWorkingSetUi(pids[i], privateBytes)) continue;
        QueryProcessMemory(pids[i], publicWs, com);
        privateWs += privateBytes;
        commit += com;
        ++procCount;
    }
    return procCount > 0;
}

int CleanupStaleFrozenStateFiles()
{
    std::wstring dir = EngineStateDir();
    std::wstring pattern = dir + L"\\frz-*.state";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;

    int removed = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        StateFileInfo info;
        info.path = path;
        bool ok = false;
        std::string text = ReadWholeFile(path, &ok);
        if (!ok) continue;
        ParseStateText(text, info);
        if (info.rootPid == 0) continue;

        bool live = false;
        for (size_t i = 0; i < info.procs.size(); ++i) {
            if (IsProcessInstanceAlive(info.procs[i].pid, info.procs[i].createTime)) {
                live = true;
                break;
            }
        }
        if (live) continue;
        if (DeleteFileW(path.c_str())) {
            ++removed;
            LogUi("startup cleanup: removed stale state %ls", path.c_str());
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return removed;
}

bool LiveTreeSummary(unsigned long long rootPid, int& procCount,
                     unsigned long long& wsNow, unsigned long long& commitNow)
{
    procCount = 0;
    wsNow = 0;
    commitNow = 0;
    std::vector<ProcEntry> snap;
    if (!TakeProcessSnapshot(snap)) return false;
    if (!FindProc(snap, (DWORD)rootPid)) return false;

    std::vector<DWORD> tree;
    tree.push_back((DWORD)rootPid);
    for (size_t i = 0; i < tree.size(); ++i) {
        for (size_t k = 0; k < snap.size(); ++k) {
            if (snap[k].ppid == tree[i]) {
                bool dup = false;
                for (size_t q = 0; q < tree.size(); ++q) if (tree[q] == snap[k].pid) { dup = true; break; }
                if (!dup) tree.push_back(snap[k].pid);
            }
        }
    }
    for (size_t i = 0; i < tree.size(); ++i) {
        unsigned long long ws = 0, com = 0;
        if (QueryProcessMemory(tree[i], ws, com)) {
            wsNow += ws;
            commitNow += com;
        }
    }
    procCount = (int)tree.size();
    return procCount > 0;
}

bool IsTreeMember(unsigned long long rootPid, unsigned long long pid)
{
    if (pid == 0 || rootPid == 0) return false;
    if (pid == rootPid) return true;
    std::vector<ProcEntry> snap;
    if (!TakeProcessSnapshot(snap)) return false;
    DWORD cur = (DWORD)pid;
    for (int guard = 0; guard < 64; ++guard) {
        const ProcEntry* e = FindProc(snap, cur);
        if (!e) return false;
        if (e->ppid == cur) return false;
        cur = e->ppid;
        if (cur == (DWORD)rootPid) return true;
        if (cur == 0 || cur == 4) return false;
    }
    return false;
}

std::wstring ExeBaseName(const std::wstring& path)
{
    size_t k = path.find_last_of(L"\\/");
    return k == std::wstring::npos ? path : path.substr(k + 1);
}

std::wstring FriendlyExeName(const std::wstring& image)
{
    std::wstring base = ExeBaseName(image);
    std::wstring fallback = base;
    size_t dot = fallback.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) fallback = fallback.substr(0, dot);
    if (image.empty()) return fallback;

    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(image.c_str(), &dummy);
    if (size == 0) return fallback;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(image.c_str(), 0, size, data.data())) return fallback;

    struct LangAndCodePage { WORD language; WORD codePage; };
    void* raw = nullptr;
    UINT rawLen = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", &raw, &rawLen) ||
        !raw || rawLen < sizeof(LangAndCodePage)) {
        return fallback;
    }
    const LangAndCodePage* trans = static_cast<const LangAndCodePage*>(raw);
    UINT count = rawLen / (UINT)sizeof(LangAndCodePage);
    for (UINT i = 0; i < count; ++i) {
        wchar_t sub[96];
        swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                   (unsigned)trans[i].language, (unsigned)trans[i].codePage);
        void* descRaw = nullptr;
        UINT descLen = 0;
        if (VerQueryValueW(data.data(), sub, &descRaw, &descLen) && descRaw && descLen > 1) {
            const wchar_t* desc = static_cast<const wchar_t*>(descRaw);
            std::wstring out(desc);
            while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
            if (!out.empty()) return out;
        }
    }
    return fallback;
}

namespace {

struct FoundWindow {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    bool iconic = false;
};

BOOL CALLBACK CollectVisibleWindow(HWND hwnd, LPARAM param)
{
    std::vector<FoundWindow>* list = reinterpret_cast<std::vector<FoundWindow>*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowTextLengthW(hwnd) <= 0) return TRUE;
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) return TRUE;

    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (SUCCEEDED(hr) && cloaked != 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == 4) return TRUE;

    wchar_t title[512];
    int n = GetWindowTextW(hwnd, title, (int)(sizeof(title) / sizeof(title[0])));
    if (n <= 0) return TRUE;

    FoundWindow w;
    w.hwnd = hwnd;
    w.pid = pid;
    w.title.assign(title, (size_t)n);
    w.iconic = IsIconic(hwnd) != FALSE;
    list->push_back(w);
    return TRUE;
}

bool IsShellOrSystemSpawner(const wchar_t* exe)
{
    static const wchar_t* kSpawners[] = {
        L"explorer.exe", L"sihost.exe", L"svchost.exe", L"RuntimeBroker.exe",
        L"services.exe", L"wininit.exe", L"cmd.exe", L"powershell.exe",
        L"pwsh.exe", L"conhost.exe", L"OpenConsole.exe", L"dllhost.exe",
        L"taskhostw.exe", L"ApplicationFrameHost.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"LockApp.exe",
        nullptr
    };
    for (int i = 0; kSpawners[i]; ++i) {
        if (_wcsicmp(exe, kSpawners[i]) == 0) return true;
    }
    return false;
}

DWORD ParentOf(const std::vector<ProcEntry>& snap,
               const std::unordered_map<DWORD, size_t>& index, DWORD pid)
{
    std::unordered_map<DWORD, size_t>::const_iterator it = index.find(pid);
    if (it == index.end()) return 0;
    return snap[it->second].ppid;
}

const wchar_t* ExeOf(const std::vector<ProcEntry>& snap,
                     const std::unordered_map<DWORD, size_t>& index, DWORD pid)
{
    std::unordered_map<DWORD, size_t>::const_iterator it = index.find(pid);
    if (it == index.end()) return L"";
    return snap[it->second].exe;
}

DWORD ResolveRootPid(const std::vector<ProcEntry>& snap,
                     const std::unordered_map<DWORD, size_t>& index, DWORD pid)
{
    std::unordered_map<DWORD, size_t>::const_iterator start = index.find(pid);
    if (start == index.end()) return 0;

    DWORD cur = pid;
    std::unordered_set<DWORD> seen;
    for (int depth = 0; depth < 64; ++depth) {
        if (!seen.insert(cur).second) break;
        const wchar_t* curExe = ExeOf(snap, index, cur);
        if (cur == 0 || cur == 4) break;
        if (IsCoreSystemExe(curExe)) break;
        DWORD parent = ParentOf(snap, index, cur);
        if (parent == 0 || parent == 4 || parent == cur) break;
        const wchar_t* parentExe = ExeOf(snap, index, parent);
        if (parentExe[0] == 0) break;
        if (IsShellOrSystemSpawner(parentExe)) break;
        if (IsCoreSystemExe(parentExe)) break;
        cur = parent;
    }
    return cur;
}

struct TitleSearch {
    DWORD pid;
    std::wstring title;
    unsigned count;
};

BOOL CALLBACK FindTitleWindow(HWND hwnd, LPARAM param)
{
    TitleSearch* ctx = reinterpret_cast<TitleSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) return TRUE;
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (SUCCEEDED(hr) && cloaked != 0) return TRUE;
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    ++ctx->count;
    if (ctx->title.empty()) {
        wchar_t buf[512];
        int n = GetWindowTextW(hwnd, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        if (n > 0) ctx->title.assign(buf, (size_t)n);
    }
    return TRUE;
}

}

unsigned CountWindowsForPid(unsigned long long pid)
{
    TitleSearch ctx;
    ctx.pid = (DWORD)pid;
    ctx.count = 0;
    EnumWindows(FindTitleWindow, (LPARAM)&ctx);
    return ctx.count;
}

std::wstring PrimaryWindowTitleForPid(unsigned long long pid)
{
    TitleSearch ctx;
    ctx.pid = (DWORD)pid;
    ctx.count = 0;
    EnumWindows(FindTitleWindow, (LPARAM)&ctx);
    return ctx.title;
}

unsigned long long ResolveAppRootPid(unsigned long long pid)
{
    if (pid == 0 || pid == 4) return 0;
    std::vector<ProcEntry> snap;
    if (!TakeProcessSnapshot(snap)) return 0;
    std::unordered_map<DWORD, size_t> index;
    for (size_t i = 0; i < snap.size(); ++i) index[snap[i].pid] = i;
    return ResolveRootPid(snap, index, (DWORD)pid);
}

bool SnapshotApps(std::vector<PickRow>& out, const std::vector<GuardComp>& guards)
{
    out.clear();
    std::vector<ProcEntry> snap;
    if (!TakeProcessSnapshot(snap)) return false;

    std::unordered_map<DWORD, size_t> index;
    for (size_t i = 0; i < snap.size(); ++i) index[snap[i].pid] = i;

    std::unordered_map<unsigned long long, std::wstring> protectedPids;
    for (size_t i = 0; i < guards.size(); ++i) {
        if (guards[i].pid == 0) continue;
        protectedPids[guards[i].pid] = guards[i].component;
    }

    std::unordered_map<DWORD, size_t> rowIndex;
    std::vector<DWORD> rootOfProc(snap.size(), 0);

    for (size_t i = 0; i < snap.size(); ++i) {
        DWORD pid = snap[i].pid;
        if (pid == 0 || pid == 4) continue;
        if (IsCoreSystemExe(snap[i].exe)) continue;

        DWORD root = ResolveRootPid(snap, index, pid);
        if (root == 0 || root == 4) continue;
        rootOfProc[i] = root;

        std::unordered_map<DWORD, size_t>::const_iterator rootIt = index.find(root);
        if (rootIt == index.end()) continue;
        const ProcEntry& rootProc = snap[rootIt->second];
        if (IsCoreSystemExe(rootProc.exe)) continue;

        size_t ri;
        std::unordered_map<DWORD, size_t>::iterator found = rowIndex.find(root);
        if (found == rowIndex.end()) {
            PickRow row;
            row.rootPid = root;
            row.createTime = ProcessCreateTimeUi(root);
            row.exe = rootProc.exe;
            QueryProcessImage(root, row.image);
            row.friendlyName = FriendlyExeName(row.image.empty()
                ? std::wstring(L"\\") + rootProc.exe : row.image);
            if (row.friendlyName.empty()) row.friendlyName = ExeBaseName(rootProc.exe);
            out.push_back(row);
            ri = out.size() - 1;
            rowIndex[root] = ri;
        } else {
            ri = found->second;
        }

        unsigned long long ws = 0, commit = 0;
        QueryProcessMemory(pid, ws, commit);
        out[ri].treeWs += ws;
        out[ri].treeCommit += commit;

        if (pid != root) {
            ChildProcEntry child;
            child.pid = pid;
            child.exe = snap[i].exe;
            child.ws = ws;
            child.commit = commit;
            out[ri].childProcs.push_back(child);
        }
    }

    std::vector<FoundWindow> windows;
    EnumWindows(CollectVisibleWindow, (LPARAM)&windows);
    HWND foreground = GetForegroundWindow();
    for (size_t i = 0; i < windows.size(); ++i) {
        const FoundWindow& w = windows[i];
        std::unordered_map<DWORD, size_t>::const_iterator it = index.find(w.pid);
        if (it == index.end()) continue;
        if (IsCoreSystemExe(snap[it->second].exe)) continue;

        DWORD root = ResolveRootPid(snap, index, w.pid);
        if (root == 0 || root == 4) continue;
        std::unordered_map<DWORD, size_t>::const_iterator rootIt = index.find(root);
        if (rootIt == index.end()) continue;
        if (IsCoreSystemExe(snap[rootIt->second].exe)) continue;

        size_t ri;
        std::unordered_map<DWORD, size_t>::iterator found = rowIndex.find(root);
        if (found == rowIndex.end()) {
            const ProcEntry& rootProc = snap[rootIt->second];
            PickRow row;
            row.rootPid = root;
            row.createTime = ProcessCreateTimeUi(root);
            row.exe = rootProc.exe;
            QueryProcessImage(root, row.image);
            row.friendlyName = FriendlyExeName(row.image.empty()
                ? std::wstring(L"\\") + rootProc.exe : row.image);
            if (row.friendlyName.empty()) row.friendlyName = ExeBaseName(rootProc.exe);
            out.push_back(row);
            ri = out.size() - 1;
            rowIndex[root] = ri;
        } else {
            ri = found->second;
        }

        OwnedWindowEntry win;
        win.hwnd = w.hwnd;
        win.pid = w.pid;
        win.title = w.title;
        win.isIconic = w.iconic;
        out[ri].windows.push_back(win);
        out[ri].hasVisibleWindow = true;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        PickRow& row = out[i];
        if (row.windows.empty()) {
            row.primaryWindowTitle = row.friendlyName.empty() ? row.exe : row.friendlyName;
            continue;
        }
        for (size_t k = 0; k < row.windows.size(); ++k) {
            if (row.windows[k].hwnd == foreground) {
                row.primaryWindowTitle = row.windows[k].title;
                break;
            }
        }
        if (row.primaryWindowTitle.empty()) {
            for (size_t k = 0; k < row.windows.size(); ++k) {
                if (!row.windows[k].isIconic) {
                    row.primaryWindowTitle = row.windows[k].title;
                    break;
                }
            }
        }
        if (row.primaryWindowTitle.empty()) row.primaryWindowTitle = row.windows[0].title;
        if (row.primaryWindowTitle.empty()) row.primaryWindowTitle = row.friendlyName;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        PickRow& row = out[i];
        std::unordered_map<unsigned long long, std::wstring>::const_iterator hit =
            protectedPids.find(row.rootPid);
        if (hit != protectedPids.end()) {
            row.protectedHit = true;
            row.protectedBy = hit->second;
        }
        for (size_t k = 0; k < row.childProcs.size() && !row.protectedHit; ++k) {
            hit = protectedPids.find(row.childProcs[k].pid);
            if (hit != protectedPids.end()) {
                row.protectedHit = true;
                row.protectedBy = hit->second;
            }
        }
    }

    for (size_t i = 0; i < out.size(); ++i) {
        std::sort(out[i].childProcs.begin(), out[i].childProcs.end(),
                  [](const ChildProcEntry& a, const ChildProcEntry& b) {
                      if (a.ws != b.ws) return a.ws > b.ws;
                      return a.pid < b.pid;
                  });
    }
    std::sort(out.begin(), out.end(), [](const PickRow& a, const PickRow& b) {
        if (a.hasVisibleWindow != b.hasVisibleWindow) return a.hasVisibleWindow;
        if (a.treeWs != b.treeWs) return a.treeWs > b.treeWs;
        return a.rootPid < b.rootPid;
    });
    return true;
}

int SweepThawStateFilesInProcess(const std::vector<unsigned long long>& rootPids)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    PFN_NtResumeProcess resume =
        reinterpret_cast<PFN_NtResumeProcess>(GetProcAddress(ntdll, "NtResumeProcess"));
    if (!resume) {
        LogUi("shutdown fallback: ntdll!NtResumeProcess not available");
        return 0;
    }

    std::vector<unsigned long long> targets;
    if (!rootPids.empty()) {
        for (size_t i = 0; i < rootPids.size(); ++i) {
            StateFileInfo info;
            if (!ReadStateFile(rootPids[i], info)) continue;
            for (size_t k = 0; k < info.procs.size(); ++k) targets.push_back(info.procs[k].pid);
        }
    } else {
        std::wstring dir = EngineStateDir();
        std::wstring pattern = dir + L"\\frz-*.state";
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                bool ok = false;
                std::string text = ReadWholeFile(dir + L"\\" + fd.cFileName, &ok);
                if (!ok) continue;
                StateFileInfo info;
                ParseStateText(text, info);
                for (size_t k = 0; k < info.procs.size(); ++k) targets.push_back(info.procs[k].pid);
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }

    std::unordered_set<unsigned long long> seen;
    int resumed = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        unsigned long long pid = targets[i];
        if (pid == 0 || !seen.insert(pid).second) continue;
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, (DWORD)pid);
        if (!h) {
            LogUi("shutdown fallback: open pid=%llu failed win32=%lu",
                  pid, (unsigned long)GetLastError());
            continue;
        }
        NTSTATUS status = resume(h);
        CloseHandle(h);
        if (status >= 0) {
            ++resumed;
            LogUi("shutdown fallback: resumed pid=%llu", pid);
        } else {
            LogUi("shutdown fallback: NtResumeProcess pid=%llu status=0x%08lX",
                  pid, (unsigned long)status);
        }
    }
    LogUi("shutdown fallback: %d process(es) resumed in-process", resumed);
    return resumed;
}

bool PollTelemetry(Telemetry& out)
{
    PERFORMANCE_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    pi.cb = sizeof(pi);
    if (!GetPerformanceInfo(&pi, sizeof(pi))) return false;
    ULONGLONG page = (ULONGLONG)pi.PageSize;
    if (page == 0) page = 4096;
    out.commitUsed  = (unsigned long long)pi.CommitTotal * page;
    out.commitLimit = (unsigned long long)pi.CommitLimit * page;
    out.commitPeak  = (unsigned long long)pi.CommitPeak * page;
    out.ramTotal    = (unsigned long long)pi.PhysicalTotal * page;
    unsigned long long avail = (unsigned long long)pi.PhysicalAvailable * page;
    out.ramUsed = out.ramTotal > avail ? out.ramTotal - avail : 0;
    out.freeCommit = out.commitLimit > out.commitUsed ? out.commitLimit - out.commitUsed : 0;
    out.commitFrac = out.commitLimit ? (double)out.commitUsed / (double)out.commitLimit : 0.0;

    static Telemetry prev;
    static bool havePrev = false;
    bool changed = !havePrev ||
        prev.commitUsed != out.commitUsed || prev.commitLimit != out.commitLimit ||
        prev.commitPeak != out.commitPeak || prev.ramTotal != out.ramTotal ||
        prev.ramUsed != out.ramUsed || prev.freeCommit != out.freeCommit;
    prev = out;
    havePrev = true;
    return changed;
}

bool CommitHeadroomLow(unsigned long long& freeBytes, unsigned long long& limitBytes)
{
    Telemetry t;
    if (!PollTelemetry(t)) return false;
    freeBytes = t.freeCommit;
    limitBytes = t.commitLimit;
    if (t.commitLimit == 0) return false;
    return t.freeCommit * 100 < t.commitLimit * 15;
}
