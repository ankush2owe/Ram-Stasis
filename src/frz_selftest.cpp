// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

namespace {

const wchar_t* kClass = L"FrzSelfTestChildWnd";
const char*    kMapFmt = "Local\\frz_selftest_%lu_%llu";

struct Shm {
    volatile LONG64 counter;
    volatile LONG64 iterations;
    volatile LONG64 lastQpc;
    volatile LONG64 window;
    volatile LONG64 stop;
    volatile LONG64 ready;
};

Shm* g_shm = nullptr;
HANDLE g_map = nullptr;

LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (g_shm && g_shm->stop) { DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI WorkerProc(LPVOID)
{
    Shm* s = g_shm;
    if (!s) return 1;
    for (;;) {
        if (s->stop) break;
        InterlockedIncrement64(&s->counter);
        s->lastQpc = (LONG64)QpcMs();
        s->iterations = s->counter;
        if ((s->counter & 0x3FFFF) == 0) Sleep(0);
    }
    return 0;
}

struct ShmTree {
    volatile LONG64 counter[4];
    volatile LONG64 ready[4];
    volatile LONG64 stop;
};

ShmTree* g_tree = nullptr;

DWORD WINAPI TreeWorker(LPVOID p)
{
    int slot = (int)(INT_PTR)p;
    ShmTree* s = g_tree;
    if (!s) return 1;
    for (;;) {
        if (s->stop) break;
        InterlockedIncrement64(&s->counter[slot]);
        if ((s->counter[slot] & 0x3FFFF) == 0) Sleep(0);
    }
    return 0;
}

const wchar_t* kWinClass = L"FrzSelfTestWinChild";

struct ShmWin {
    volatile LONG64 counter;
    volatile LONG64 ready;
    volatile LONG64 stop;
    volatile LONG64 hwndA;
    volatile LONG64 hwndB;
};

ShmWin* g_win = nullptr;

LRESULT CALLBACK WinChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (g_win && g_win->stop) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI WinWorker(LPVOID)
{
    ShmWin* s = g_win;
    if (!s) return 1;
    for (;;) {
        if (s->stop) break;
        InterlockedIncrement64(&s->counter);
        if ((s->counter & 0x3FFFF) == 0) Sleep(0);
    }
    return 0;
}

}

int ChildMain(const std::wstring& shmName)
{
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               (DWORD)sizeof(Shm), shmName.c_str());
    if (!g_map) return 90;
    g_shm = (Shm*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shm));
    if (!g_shm) return 91;
    ZeroMemory(g_shm, sizeof(Shm));

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return 92;

    HWND hwnd = CreateWindowExW(0, kClass, L"frz selftest child", WS_OVERLAPPEDWINDOW,
                                0, 0, 240, 120, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 93;
    g_shm->window = (LONG64)(ULONG_PTR)hwnd;
    SetTimer(hwnd, 1, 100, nullptr);

    HANDLE th = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
    if (!th) return 94;
    CloseHandle(th);
    g_shm->ready = 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_shm->stop = 1;
    UnmapViewOfFile(g_shm);
    CloseHandle(g_map);
    return 0;
}

int ChildTreeMain(const std::wstring& shmName, int leafCount, int slot)
{
    if (leafCount < 1) leafCount = 1;
    if (leafCount > 4) leafCount = 4;
    if (slot < 0 || slot >= leafCount) slot = 0;

    HANDLE map = nullptr;
    if (slot == 0) {
        map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 (DWORD)sizeof(ShmTree), shmName.c_str());
    } else {
        for (int i = 0; i < 200 && !map; ++i) {
            map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, shmName.c_str());
            if (!map) Sleep(10);
        }
    }
    if (!map) return 90;
    g_tree = (ShmTree*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmTree));
    if (!g_tree) return 91;

    if (slot == 0) {
        ZeroMemory(g_tree, sizeof(ShmTree));
        wchar_t exe[1024];
        if (GetModuleFileNameW(nullptr, exe, 1024) == 0) return 92;
        for (int i = 1; i < leafCount; ++i) {
            wchar_t cmd[1600];
            swprintf_s(cmd, L"\"%s\" __tree \"%s\" %d %d", exe, shmName.c_str(), leafCount, i);
            STARTUPINFOW si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi;
            ZeroMemory(&pi, sizeof(pi));
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, DETACHED_PROCESS,
                               nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }

    HANDLE th = CreateThread(nullptr, 0, TreeWorker, (LPVOID)(INT_PTR)slot, 0, nullptr);
    if (!th) return 94;
    CloseHandle(th);
    g_tree->ready[slot] = 1;

    while (!g_tree->stop) Sleep(20);
    return 0;
}

int ChildWinMain(const std::wstring& shmName)
{
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                    (DWORD)sizeof(ShmWin), shmName.c_str());
    if (!map) return 90;
    g_win = (ShmWin*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmWin));
    if (!g_win) return 91;
    ZeroMemory(g_win, sizeof(ShmWin));

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WinChildProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWinClass;
    if (!RegisterClassExW(&wc)) return 92;

    HWND a = CreateWindowExW(0, kWinClass, L"frz selftest visible", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 120, nullptr, nullptr, inst, nullptr);
    if (!a) return 93;
    HWND b = CreateWindowExW(0, kWinClass, L"frz selftest already minimized", WS_OVERLAPPEDWINDOW,
                             220, 0, 200, 120, nullptr, nullptr, inst, nullptr);
    if (!b) return 94;
    ShowWindow(a, SW_SHOWNOACTIVATE);
    ShowWindow(b, SW_SHOWMINIMIZED);

    g_win->hwndA = (LONG64)(ULONG_PTR)a;
    g_win->hwndB = (LONG64)(ULONG_PTR)b;
    SetTimer(a, 1, 100, nullptr);

    HANDLE th = CreateThread(nullptr, 0, WinWorker, nullptr, 0, nullptr);
    if (!th) return 95;
    CloseHandle(th);
    g_win->ready = 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_win->stop = 1;
    return 0;
}

namespace {

struct Check {
    int pass = 0;
    int fail = 0;
    bool jsonMode = false;
    std::vector<std::string> names;
    std::vector<bool> results;

    void add(const char* name, bool ok, const char* detail)
    {
        names.push_back(name);
        results.push_back(ok);
        if (ok) { pass++; if (!jsonMode) Out("   [PASS] %s\n", detail ? detail : name); }
        else    { fail++; if (!jsonMode) Out("   [FAIL] %s\n", detail ? detail : name); }
    }
};

static void KillChild(PROCESS_INFORMATION& pi, Shm* shm, HANDLE hProc, bool suspended)
{
    if (shm) shm->stop = 1;
    if (hProc && suspended) DoResume(hProc);
    if (pi.hProcess) {
        if (WaitForSingleObject(pi.hProcess, 3000) != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 99);
            WaitForSingleObject(pi.hProcess, 2000);
        }
    }
}

static bool SpawnSelf(const std::wstring& exePath, const std::wstring& args, PROCESS_INFORMATION& pi)
{
    std::wstring cmd = L"\"" + exePath + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                        nullptr, nullptr, &si, &pi))
        return false;
    return true;
}

static void KillPid(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) return;
    TerminateProcess(h, 99);
    WaitForSingleObject(h, 2000);
    CloseHandle(h);
}

static unsigned CountSuspendedThreads(DWORD pid, unsigned* total)
{
    std::vector<ThreadInfo> th = EnumThreads(pid, false);
    unsigned susp = 0;
    for (size_t i = 0; i < th.size(); ++i)
        if (th[i].suspendCount > 0) susp++;
    if (total) *total = (unsigned)th.size();
    return susp;
}

}

int CmdSelftest()
{
    Check ck;
    ck.jsonMode = g_ctx.json;

    wchar_t exePath[1024];
    DWORD n = GetModuleFileNameW(nullptr, exePath, 1024);
    if (n == 0 || n >= 1024) { Out("ERROR: GetModuleFileName failed\n"); return 1; }

    char mapName[128];
    sprintf_s(mapName, kMapFmt, (unsigned long)GetCurrentProcessId(), (unsigned long long)QpcMs());
    std::wstring wmap = U2W(std::string(mapName));

    std::wstring cmd = L"\"";
    cmd += exePath;
    cmd += L"\" __child \"";
    cmd += wmap;
    cmd += L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!g_ctx.json) {
        Out("=== SELFTEST ===\n");
        Out("  exe                  : %s\n", W2U(exePath).c_str());
        Out("  shared section       : %s\n", mapName);
    }

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        Out("ERROR: CreateProcessW failed, win32=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    DWORD childPid = pi.dwProcessId;

    HANDLE map = nullptr;
    Shm* shm = nullptr;
    ULONGLONG tOpen = QpcMs();
    while (QpcMs() - tOpen < 10000) {
        map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wmap.c_str());
        if (map) break;
        Sleep(20);
    }
    if (!map) {
        Out("ERROR: child never created the shared section (%s)\n", mapName);
        KillChild(pi, nullptr, nullptr, false);
        return 1;
    }
    shm = (Shm*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shm));
    if (!shm) {
        Out("ERROR: MapViewOfFile failed, win32=%lu\n", (unsigned long)GetLastError());
        KillChild(pi, nullptr, nullptr, false);
        return 1;
    }
    tOpen = QpcMs();
    while (QpcMs() - tOpen < 5000 && !shm->ready) Sleep(10);

    HWND childWnd = (HWND)(ULONG_PTR)shm->window;
    if (!g_ctx.json) {
        Out("  child pid            : %lu\n", (unsigned long)childPid);
        Out("  child window         : 0x%08llX\n", (unsigned long long)(ULONG_PTR)childWnd);
    }

    HANDLE h = nullptr;
    std::string err;
    const DWORD access = PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                         PROCESS_SET_QUOTA | PROCESS_VM_OPERATION;
    if (OpenTarget(childPid, access, h, err) != 0) {
        Out("ERROR: %s\n", err.c_str());
        KillChild(pi, shm, nullptr, false);
        return 1;
    }

    std::vector<ThreadInfo> th0 = EnumThreads(childPid, false);
    if (!g_ctx.json) Out("  child threads        : %u\n", (unsigned)th0.size());

    Sleep(150);
    LONG64 c0 = shm->counter;
    Sleep(300);
    LONG64 c1 = shm->counter;
    if (!g_ctx.json) {
        Out("\n1. baseline: child is running\n");
        Out("   counter t0           : %lld\n", (long long)c0);
        Out("   counter t0+300ms     : %lld   (+%lld)\n", (long long)c1, (long long)(c1 - c0));
    }
    ck.add("baseline_counter_advances", (c1 - c0) > 0, "counter advances before freeze");

    SuspendResult sr;
    bool suspOk = DoSuspend(h, childPid, sr);
    if (!g_ctx.json) {
        Out("\n2. suspend\n");
        Out("   NtSuspendProcess      : %s\n", NtStatusText(sr.status).c_str());
        Out("   threads               : %u seen, %u suspend count +1\n",
            sr.threadsSeen, sr.threadsIncremented);
        for (size_t i = 0; i < sr.after.size(); ++i)
            Out("     tid %-7lu suspend_count=%ld\n", (unsigned long)sr.after[i].tid, (long)sr.after[i].suspendCount);
    }
    ck.add("nt_suspend_success", suspOk && sr.status == kStatusSuccess, "NtSuspendProcess returned STATUS_SUCCESS");
    ck.add("suspend_counts_balanced", sr.threadsSeen > 0 && sr.threadsIncremented == sr.threadsSeen,
           "every thread suspend count went baseline -> baseline+1");

    LONG64 cFreeze = shm->counter;
    LONG64 qFreeze = shm->lastQpc;

    Sleep(1000);
    LONG64 cFrozen = shm->counter;
    if (!g_ctx.json) {
        Out("\n3. frozen: counter must not advance\n");
        Out("   counter at freeze    : %lld\n", (long long)cFreeze);
        Out("   counter at +1000ms   : %lld   (+%lld)\n", (long long)cFrozen, (long long)(cFrozen - cFreeze));
    }
    ck.add("counter_frozen", (cFrozen - cFreeze) == 0, "counter delta over 1000 ms is exactly 0");

    DWORD_PTR res = 0;
    SetLastError(0);
    ULONGLONG tMsg = QpcMs();
    LRESULT lr = SendMessageTimeoutW(childWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &res);
    ULONGLONG msgMs = QpcMs() - tMsg;
    DWORD msgErr = GetLastError();
    if (!g_ctx.json) {
        Out("\n4. window is hung\n");
        Out("   SendMessageTimeout    : ret=%lld win32=%lu in %llu ms\n",
            (long long)lr, (unsigned long)msgErr, (unsigned long long)msgMs);
    }
    ck.add("send_message_times_out", lr == 0 && msgErr == ERROR_TIMEOUT,
           "SendMessageTimeout(WM_NULL, 500ms) returned 0 / ERROR_TIMEOUT");

    MemNumbers mBefore, mAfter;
    GetMem(h, mBefore);

    SetLastError(0);
    BOOL ews = EmptyWorkingSet(h);
    DWORD ewsErr = GetLastError();
    Sleep(80);

    ULONGLONG k0 = 0, u0 = 0, c0t = 0, k1 = 0, u1 = 0, c1t = 0;
    bool cpuOk1 = GetProcCpu(h, k0, u0, c0t);
    Sleep(2000);
    bool cpuOk2 = GetProcCpu(h, k1, u1, c1t);
    long long cpuDelta = (long long)((k1 + u1) - (k0 + u0));

    if (!g_ctx.json) {
        Out("\n5. CPU time while suspended (2000 ms)\n");
        if (cpuOk1 && cpuOk2) {
            Out("   CPU before           : %.4f s (k %.4f + u %.4f)\n",
                (double)(k0 + u0) / 1e7, (double)k0 / 1e7, (double)u0 / 1e7);
            Out("   CPU after            : %.4f s (k %.4f + u %.4f)\n",
                (double)(k1 + u1) / 1e7, (double)k1 / 1e7, (double)u1 / 1e7);
            Out("   delta                : %lld x 100ns\n", cpuDelta);
        } else {
            Out("   GetProcessTimes FAILED\n");
        }
    }
    ck.add("cpu_delta_zero", cpuOk1 && cpuOk2 && cpuDelta == 0, "process CPU time delta over 2000 ms is exactly 0");

    GetMem(h, mAfter);
    if (!g_ctx.json) {
        Out("\n6. page out (EmptyWorkingSet)\n");
        Out("   return               : BOOL=%d win32=%lu\n", (int)ews, (unsigned long)ewsErr);
        Out("   working set          : %s -> %s\n",
            Bytes(mBefore.workingSet).c_str(), Bytes(mAfter.workingSet).c_str());
        Out("   private (commit)     : %s -> %s\n",
            Bytes(mBefore.privateUsage).c_str(), Bytes(mAfter.privateUsage).c_str());
        Out("   pagefile usage       : %s -> %s\n",
            Bytes(mBefore.pagefileUsage).c_str(), Bytes(mAfter.pagefileUsage).c_str());
    }
    ck.add("empty_working_set_ok", ews != FALSE, "EmptyWorkingSet returned nonzero");
    ck.add("working_set_dropped", mAfter.workingSet < mBefore.workingSet, "working set decreased");

    ULONGLONG cpuBeforeResume = k1 + u1;
    LONG64 cPreResume = shm->counter;
    ULONGLONG tResume = QpcMs();
    NTSTATUS rs = DoResume(h);
    ULONGLONG resumeCallMs = QpcMs() - tResume;

    double cpuLat = -1.0;
    bool mOk = false, cOk = false;
    double msgLat = MeasureWakeMs(h, childPid, cpuBeforeResume, 5000.0, cpuLat, mOk, cOk);

    LONG64 cThaw = shm->counter;
    LONG64 qThaw = shm->lastQpc;

    std::vector<ThreadInfo> th2 = EnumThreads(childPid, false);
    unsigned stillSusp = 0;
    for (size_t i = 0; i < th2.size(); ++i) if (th2[i].suspendCount > 0) stillSusp++;

    Sleep(300);
    LONG64 cAfter = shm->counter;

    if (!g_ctx.json) {
        Out("\n7. thaw\n");
        Out("   NtResumeProcess       : %s (call %.2f ms)\n", NtStatusText(rs).c_str(), (double)resumeCallMs);
        Out("   threads still susp.   : %u\n", stillSusp);
        Out("   counter at thaw       : %lld\n", (long long)cPreResume);
        Out("   counter at +300ms     : %lld   (+%lld)\n", (long long)cAfter, (long long)(cAfter - cThaw));
        Out("   counter at freeze     : %lld\n", (long long)cFreeze);
        Out("   frozen interval       : %lld ms (last increment before -> after)\n", (long long)(qThaw - qFreeze));
        Out("   wake latency          : %.2f ms (resume call+message round trip)\n", msgLat >= 0 ? msgLat : 0.0);
        Out("   cpu advance latency   : %.2f ms\n", cpuLat >= 0 ? cpuLat : 0.0);
    }
    ck.add("nt_resume_success", rs == kStatusSuccess, "NtResumeProcess returned STATUS_SUCCESS");
    ck.add("resume_counts_balanced", stillSusp == 0 && !th2.empty(), "no thread is left with suspend count > 0");
    ck.add("counter_resumed", (cAfter - cThaw) > 0, "counter advances again after thaw");
    ck.add("no_lost_increments", cPreResume == cFreeze, "counter at the instant of thaw == counter at freeze (no lost or duplicated increments)");
    ck.add("wake_message_ok", mOk, "target answered a message after resume");

    shm->stop = 1;
    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 0xFFFFFFFF;
    if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
        TerminateProcess(pi.hProcess, 99);
        WaitForSingleObject(pi.hProcess, 2000);
        exitCode = 99;
    }
    DWORD childThreads = (DWORD)th0.size();

    if (!g_ctx.json) {
        Out("\n8. teardown\n");
        Out("   child exit code      : %lu\n", (unsigned long)exitCode);
        Out("   child threads at start: %lu\n", (unsigned long)childThreads);
    }

    char mapTree[128];
    sprintf_s(mapTree, "Local\\frz_tree_%lu_%llu", (unsigned long)GetCurrentProcessId(),
              (unsigned long long)QpcMs());
    std::wstring wmapTree = U2W(std::string(mapTree));

    PROCESS_INFORMATION piTree;
    ZeroMemory(&piTree, sizeof(piTree));
    bool treeSpawned = SpawnSelf(exePath, L"__tree \"" + wmapTree + L"\" 3 0", piTree);

    HANDLE mapT = nullptr;
    ShmTree* shmT = nullptr;
    if (treeSpawned) {
        ULONGLONG t0 = QpcMs();
        while (QpcMs() - t0 < 10000) {
            mapT = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wmapTree.c_str());
            if (mapT) break;
            Sleep(20);
        }
        if (mapT) shmT = (ShmTree*)MapViewOfFile(mapT, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmTree));
    }

    bool treeReady = false;
    if (shmT) {
        ULONGLONG t0 = QpcMs();
        while (QpcMs() - t0 < 6000) {
            if (shmT->ready[0] && shmT->ready[1] && shmT->ready[2]) { treeReady = true; break; }
            Sleep(10);
        }
    }

    std::vector<ProcSnapshot> tree;
    if (treeReady) tree = BuildTree(piTree.dwProcessId, true);

    if (!g_ctx.json) {
        Out("\n9. PROCESS TREE (parent + 2 children)\n");
        Out("   root pid             : %lu\n", (unsigned long)piTree.dwProcessId);
        Out("   tree size            : %u\n", (unsigned)tree.size());
    }

    ck.add("tree_has_three_processes", treeReady && tree.size() == 3,
           "BuildTree resolved the parent and both children");

    LONG64 c0t3[3] = { 0, 0, 0 }, c1t3[3] = { 0, 0, 0 };
    if (treeReady && shmT) {
        for (int i = 0; i < 3; ++i) c0t3[i] = shmT->counter[i];
        Sleep(300);
        for (int i = 0; i < 3; ++i) c1t3[i] = shmT->counter[i];
    }
    bool treeRunning = treeReady;
    for (int i = 0; i < 3; ++i) if (c1t3[i] - c0t3[i] <= 0) treeRunning = false;
    if (!g_ctx.json && shmT) {
        Out("   counters +300ms      : %lld / %lld / %lld\n",
            (long long)(c1t3[0] - c0t3[0]), (long long)(c1t3[1] - c0t3[1]), (long long)(c1t3[2] - c0t3[2]));
    }
    ck.add("tree_baseline_all_running", treeRunning, "all 3 processes in the tree are advancing their counter");

    int freezeRc = -1;
    if (treeRunning) {
        g_ctx.quiet = true;
        freezeRc = CmdFreeze(std::to_wstring(piTree.dwProcessId));
        g_ctx.quiet = false;
    }

    LONG64 f0[3] = { 0, 0, 0 }, f1[3] = { 0, 0, 0 };
    if (shmT) {
        for (int i = 0; i < 3; ++i) f0[i] = shmT->counter[i];
        Sleep(1000);
        for (int i = 0; i < 3; ++i) f1[i] = shmT->counter[i];
    }
    bool allFrozen = (freezeRc == 0);
    for (int i = 0; i < 3; ++i) if (f1[i] - f0[i] != 0) allFrozen = false;

    unsigned runningThreads = 0, totalThreads = 0;
    for (size_t i = 0; i < tree.size(); ++i) {
        unsigned tot = 0;
        unsigned susp = CountSuspendedThreads(tree[i].pid, &tot);
        totalThreads += tot;
        runningThreads += (tot - susp);
    }
    if (!g_ctx.json) {
        Out("   freeze exit          : %d\n", freezeRc);
        Out("   counters while frozen: %lld / %lld / %lld (over 1000 ms)\n",
            (long long)(f1[0] - f0[0]), (long long)(f1[1] - f0[1]), (long long)(f1[2] - f0[2]));
        Out("   threads              : %u total, %u not suspended\n", totalThreads, runningThreads);
    }
    ck.add("tree_freeze_all_counters_frozen", allFrozen && totalThreads > 0,
           "all 3 counters are exactly 0 over 1000 ms after the tree freeze");
    ck.add("tree_freeze_no_thread_left_running", runningThreads == 0 && totalThreads > 0,
           "every thread of every pid in the tree has suspend count > 0");

    StateFile stf;
    std::string stErr;
    bool stOk = StateRead(piTree.dwProcessId, stf, stErr);
    unsigned trimVerified = 0;
    unsigned winRecs = 0;
    for (size_t i = 0; i < stf.procs.size(); ++i)
        if (stf.procs[i].alive && stf.procs[i].wsAfter < stf.procs[i].wsBefore) trimVerified++;
    for (size_t i = 0; i < stf.wins.size(); ++i) winRecs++;
    if (!g_ctx.json) {
        Out("   state file           : %s (%s)\n", stf.rawPath.c_str(), stOk ? "read" : "MISSING");
        Out("   state procs/windows  : %u / %u, trim verified on %u\n",
            (unsigned)stf.procs.size(), winRecs, trimVerified);
    }
    ck.add("tree_state_file_covers_tree", stOk && stf.procs.size() == 3,
           "the state file records every pid in the tree");

    int thawRc = -1;
    if (allFrozen) {
        g_ctx.quiet = true;
        thawRc = CmdThaw(std::to_wstring(piTree.dwProcessId));
        g_ctx.quiet = false;
    }

    LONG64 t0c[3] = { 0, 0, 0 }, t1c[3] = { 0, 0, 0 };
    if (shmT) {
        for (int i = 0; i < 3; ++i) t0c[i] = shmT->counter[i];
        Sleep(500);
        for (int i = 0; i < 3; ++i) t1c[i] = shmT->counter[i];
    }
    bool allResumed = (thawRc == 0);
    for (int i = 0; i < 3; ++i) if (t1c[i] - t0c[i] <= 0) allResumed = false;

    unsigned stillTree = 0;
    for (size_t i = 0; i < tree.size(); ++i) {
        unsigned tot = 0;
        stillTree += CountSuspendedThreads(tree[i].pid, &tot);
    }
    if (!g_ctx.json) {
        Out("   thaw exit            : %d\n", thawRc);
        Out("   counters after thaw  : %lld / %lld / %lld (+500 ms)\n",
            (long long)(t1c[0] - t0c[0]), (long long)(t1c[1] - t0c[1]), (long long)(t1c[2] - t0c[2]));
        Out("   threads still susp.  : %u\n", stillTree);
    }
    ck.add("tree_trim_verified_per_pid",
           trimVerified == 3,
           "EmptyWorkingSet verified against WorkingSetSize for every pid in the tree");
    ck.add("tree_thaw_all_resumed", allResumed, "all 3 counters advance again after the tree thaw");
    ck.add("tree_thaw_no_thread_left_suspended", stillTree == 0,
           "no thread in any pid of the tree is left suspended");

    char mapWin[128];
    sprintf_s(mapWin, "Local\\frz_win_%lu_%llu", (unsigned long)GetCurrentProcessId(),
              (unsigned long long)(QpcMs() + 5));
    std::wstring wmapWin = U2W(std::string(mapWin));

    PROCESS_INFORMATION piWin;
    ZeroMemory(&piWin, sizeof(piWin));
    bool winSpawned = SpawnSelf(exePath, L"__win \"" + wmapWin + L"\"", piWin);

    HANDLE mapW = nullptr;
    ShmWin* shmW = nullptr;
    if (winSpawned) {
        ULONGLONG t0 = QpcMs();
        while (QpcMs() - t0 < 10000) {
            mapW = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wmapWin.c_str());
            if (mapW) break;
            Sleep(20);
        }
        if (mapW) shmW = (ShmWin*)MapViewOfFile(mapW, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmWin));
    }
    bool winReady = false;
    if (shmW) {
        ULONGLONG t0 = QpcMs();
        while (QpcMs() - t0 < 6000 && !shmW->ready) Sleep(10);
        winReady = shmW->ready != 0;
    }

    HWND wA = shmW ? (HWND)(ULONG_PTR)shmW->hwndA : nullptr;
    HWND wB = shmW ? (HWND)(ULONG_PTR)shmW->hwndB : nullptr;

    bool aVisibleBefore = winReady && wA && IsWindow(wA) && IsWindowVisible(wA) && !IsIconic(wA);
    bool bMinBefore = winReady && wB && IsWindow(wB) && IsIconic(wB);
    if (!g_ctx.json) {
        Out("\n10. MINIMIZE / RESTORE (ghost suppression, no injection)\n");
        Out("   child pid            : %lu\n", (unsigned long)piWin.dwProcessId);
        Out("   window A (visible)   : hwnd 0x%08llX IsWindowVisible=%d IsIconic=%d\n",
            (unsigned long long)(ULONG_PTR)wA, wA ? (int)IsWindowVisible(wA) : -1, wA ? (int)IsIconic(wA) : -1);
        Out("   window B (minimized) : hwnd 0x%08llX IsWindowVisible=%d IsIconic=%d\n",
            (unsigned long long)(ULONG_PTR)wB, wB ? (int)IsWindowVisible(wB) : -1, wB ? (int)IsIconic(wB) : -1);
    }
    ck.add("window_visible_before", aVisibleBefore,
           "the visible test window is visible and not iconic before the freeze");
    ck.add("window_already_minimized_before", bMinBefore,
           "the second test window is already minimized before the freeze");

    int winFreezeRc = -1;
    if (winReady) {
        g_ctx.quiet = true;
        winFreezeRc = CmdFreeze(std::to_wstring(piWin.dwProcessId));
        g_ctx.quiet = false;
    }

    bool aMinimized = false;
    for (int i = 0; i < 30 && wA && IsWindow(wA); ++i) {
        if (IsIconic(wA)) { aMinimized = true; break; }
        Sleep(50);
    }
    if (!g_ctx.json) {
        Out("   freeze exit          : %d\n", winFreezeRc);
        Out("   A after freeze       : IsIconic=%d\n", wA && IsWindow(wA) ? (int)IsIconic(wA) : -1);
    }
    ck.add("window_minimized_by_freeze", winFreezeRc == 0 && aMinimized,
           "ShowWindowAsync(SW_MINIMIZE) put the visible window into the iconic state");

    StateFile sw;
    std::string swErr;
    bool swOk = StateRead(piWin.dwProcessId, sw, swErr);
    bool aOwned = false, bNotOwned = false, rectOk = false;
    for (size_t i = 0; i < sw.wins.size(); ++i) {
        const StateWin& w = sw.wins[i];
        if ((HWND)(ULONG_PTR)w.hwnd == wA) {
            aOwned = w.minimizedByUs;
            rectOk = (w.right - w.left) > 0 && (w.bottom - w.top) > 0;
        }
        if ((HWND)(ULONG_PTR)w.hwnd == wB) bNotOwned = !w.minimizedByUs;
    }
    if (!g_ctx.json) {
        Out("   state windows        : %u (A owned=%d, B owned=%d, rect recorded=%d)\n",
            (unsigned)sw.wins.size(), aOwned ? 1 : 0, bNotOwned ? 0 : 1, rectOk ? 1 : 0);
    }
    ck.add("window_state_records_rect_for_overlay", swOk && aOwned && bNotOwned && rectOk,
           "state file carries the last-known rect + visible/iconic + minimized_by_us per window");

    int winThawRc = -1;
    if (winFreezeRc == 0) {
        g_ctx.quiet = true;
        winThawRc = CmdThaw(std::to_wstring(piWin.dwProcessId));
        g_ctx.quiet = false;
    }

    bool aRestored = false;
    for (int i = 0; i < 30 && wA && IsWindow(wA); ++i) {
        if (!IsIconic(wA)) { aRestored = true; break; }
        Sleep(50);
    }
    bool bStillMin = wB && IsWindow(wB) && IsIconic(wB);
    if (!g_ctx.json) {
        Out("   thaw exit            : %d\n", winThawRc);
        Out("   A after thaw         : IsIconic=%d\n", wA && IsWindow(wA) ? (int)IsIconic(wA) : -1);
        Out("   B after thaw         : IsIconic=%d (must stay 1)\n", wB && IsWindow(wB) ? (int)IsIconic(wB) : -1);
    }
    ck.add("window_restored_by_thaw", winThawRc == 0 && aRestored,
           "the window frz minimized was restored by thaw");
    ck.add("window_already_minimized_not_restored", bStillMin,
           "the window that was already minimized before the freeze is still minimized");

    std::string comp, fam;
    bool eac = AcMatchName("EasyAntiCheat.exe", false, comp, fam) && fam == "Easy Anti-Cheat"
            && AcMatchName("eacclient.dll", false, comp, fam)
            && AcMatchName("EasyHook.dll", false, comp, fam)
            && AcMatchName("eacupdater.exe", false, comp, fam)
            && AcMatchName("EasyAntiCheat.sys", true, comp, fam);
    bool be = AcMatchName("BEService.exe", false, comp, fam) && fam == "BattlEye"
           && AcMatchName("BEDaisy.sys", true, comp, fam)
           && AcMatchName("beservice_x64.exe", false, comp, fam)
           && AcMatchName("BEService.dll", false, comp, fam);
    bool vg = AcMatchName("vgk.sys", true, comp, fam) && fam == "Vanguard"
           && AcMatchName("vgc.sys", true, comp, fam);
    bool generic = AcMatchName("\\SystemRoot\\system32\\drivers\\myanticheat_x64.sys", true, comp, fam)
                && AcMatchName("xigncode.sys", true, comp, fam)
                && AcMatchName("nprotect_gameguard.sys", true, comp, fam)
                && !AcMatchName("myanticheat_x64.sys", false, comp, fam);
    bool clean = !AcMatchName("notepad.exe", false, comp, fam)
              && !AcMatchName("kernel32.dll", false, comp, fam)
              && !AcMatchName("frz.exe", false, comp, fam)
              && !AcMatchName("chrome.dll", false, comp, fam);

    if (!g_ctx.json) {
        Out("\n11. ANTI-CHEAT GUARD\n");
        Out("   matcher EasyAntiCheat: %s\n", eac ? "flagged" : "MISSED");
        Out("   matcher BattlEye     : %s\n", be ? "flagged" : "MISSED");
        Out("   matcher Vanguard     : %s\n", vg ? "flagged" : "MISSED");
        Out("   matcher generic .sys : %s\n", generic ? "flagged" : "MISSED");
        Out("   matcher clean names  : %s\n", clean ? "not flagged" : "FALSE POSITIVE");
    }
    ck.add("guard_matcher_eac", eac, "EasyAntiCheat.exe/.sys, eacclient.dll, EasyHook.dll, eacup* are flagged");
    ck.add("guard_matcher_battleye", be, "BEService.exe/.dll, BEDaisy.sys, beservice* are flagged");
    ck.add("guard_matcher_vanguard", vg, "vgk.sys and vgc.sys are flagged");
    ck.add("guard_matcher_generic_driver", generic,
           "a kernel driver name containing 'anticheat'/'xigncode' is flagged; a user-mode module is not");
    ck.add("guard_matcher_clean", clean, "notepad.exe / kernel32.dll / frz.exe / chrome.dll are not flagged");

    std::vector<AcHit> treeHits;
    unsigned gProcs = 0, gMods = 0;
    bool gScanned = GuardCheckTree(piWin.dwProcessId, treeHits, gProcs, gMods);
    if (!g_ctx.json) {
        Out("   clean tree scan      : %u process(es), %u module(s), %u hit(s)\n",
            gProcs, gMods, (unsigned)treeHits.size());
    }
    ck.add("guard_clean_tree_not_flagged", gScanned && treeHits.empty() && gMods > 0,
           "a clean tree (target + its parent, real module lists read) produces no hit");

    std::vector<AcHit> synth;
    {
        AcHit hh;
        hh.component = "easyanticheat.exe";
        hh.family = "Easy Anti-Cheat";
        hh.source = "process";
        hh.where = "pid 4242 (EasyAntiCheat.exe)";
        synth.push_back(hh);
    }
    bool blocks = GuardBlocks(synth, false) && !GuardBlocks(synth, true) &&
                  !GuardBlocks(std::vector<AcHit>(), false);
    ck.add("guard_synthetic_eac_refused", blocks,
           "a synthetic EasyAntiCheat.exe hit blocks freeze, and --force overrides it");

    wchar_t tmpDir[MAX_PATH];
    DWORD tlen = GetTempPathW(MAX_PATH, tmpDir);
    std::wstring fakePath;
    if (tlen > 0 && tlen < MAX_PATH) fakePath = std::wstring(tmpDir) + L"EasyAntiCheat.exe";
    bool fakeCopied = !fakePath.empty() && CopyFileW(exePath, fakePath.c_str(), FALSE) != FALSE;

    PROCESS_INFORMATION piFake;
    ZeroMemory(&piFake, sizeof(piFake));
    bool fakeSpawned = false;
    if (fakeCopied) {
        char mapFake[128];
        sprintf_s(mapFake, "Local\\frz_fake_%lu_%llu", (unsigned long)GetCurrentProcessId(),
                  (unsigned long long)(QpcMs() + 11));
        fakeSpawned = SpawnSelf(fakePath, L"__child \"" + U2W(std::string(mapFake)) + L"\"", piFake);
    }

    int guardRc = -1;
    bool fakeStillRunning = false;
    bool fakeSuspended = false;
    if (fakeSpawned) {
        Sleep(400);
        g_ctx.quiet = true;
        guardRc = CmdFreeze(std::to_wstring(piFake.dwProcessId));
        g_ctx.quiet = false;
        unsigned tot = 0;
        unsigned susp = CountSuspendedThreads(piFake.dwProcessId, &tot);
        fakeSuspended = susp > 0;
        DWORD ec = 0;
        fakeStillRunning = GetExitCodeProcess(piFake.hProcess, &ec) && ec == STILL_ACTIVE;
        if (!g_ctx.json) {
            Out("   real freeze of a fake: exit %d (expect 8), still running=%d, threads suspended=%u\n",
                guardRc, fakeStillRunning ? 1 : 0, susp);
        }
    }
    ck.add("guard_refuses_real_eac_named_process", fakeSpawned && guardRc == 8,
           "freeze of a process really named EasyAntiCheat.exe exits 8");
    ck.add("guard_refusal_did_not_touch_target", fakeSpawned && fakeStillRunning && !fakeSuspended,
           "the refused process was not suspended and not opened for freezing");

    if (fakeSpawned) {
        KillPid(piFake.dwProcessId);
        CloseHandle(piFake.hThread);
        CloseHandle(piFake.hProcess);
    }
    if (!fakePath.empty()) DeleteFileW(fakePath.c_str());

    if (shmT) shmT->stop = 1;
    if (shmW) shmW->stop = 1;
    if (piWin.hProcess) {
        if (WaitForSingleObject(piWin.hProcess, 3000) != WAIT_OBJECT_0)
            TerminateProcess(piWin.hProcess, 99);
        CloseHandle(piWin.hThread);
        CloseHandle(piWin.hProcess);
    }
    if (piTree.hProcess) {
        if (WaitForSingleObject(piTree.hProcess, 3000) != WAIT_OBJECT_0)
            TerminateProcess(piTree.hProcess, 99);
        CloseHandle(piTree.hThread);
        CloseHandle(piTree.hProcess);
    }
    for (size_t i = 0; i < tree.size(); ++i)
        if (tree[i].pid != piTree.dwProcessId) KillPid(tree[i].pid);
    if (shmW) UnmapViewOfFile(shmW);
    if (mapW) CloseHandle(mapW);
    if (shmT) UnmapViewOfFile(shmT);
    if (mapT) CloseHandle(mapT);

    if (!g_ctx.json) {
        Out("\nRESULT: %d passed, %d failed\n", ck.pass, ck.fail);
        Out("%s\n", ck.fail == 0 ? "SELFTEST PASS" : "SELFTEST FAIL");
    }

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "selftest");
        j.un("child_pid", childPid);
        j.un("child_window", (unsigned long long)(ULONG_PTR)childWnd);
        j.obj("frozen");
        j.un("counter_at_freeze", (unsigned long long)cFreeze);
        j.un("counter_1000ms_later", (unsigned long long)cFrozen);
        j.un("counter_at_thaw", (unsigned long long)cThaw);
        j.un("counter_300ms_after_thaw", (unsigned long long)cAfter);
        j.n("cpu_delta_100ns", cpuDelta);
        j.un("send_message_timeout_ms", (unsigned long long)msgMs);
        j.un("send_message_win32_error", msgErr);
        j.endObj();
        j.obj("page_out");
        j.b("empty_working_set_ok", ews != FALSE);
        j.un("working_set_before", mBefore.workingSet);
        j.un("working_set_after", mAfter.workingSet);
        j.un("private_before", mBefore.privateUsage);
        j.un("private_after", mAfter.privateUsage);
        j.endObj();
        j.obj("thaw");
        j.n("ntstatus", (long long)rs);
        j.f("resume_call_ms", (double)resumeCallMs, 2);
        j.b("message_ok", mOk);
        j.f("wake_latency_ms", msgLat, 2);
        j.f("cpu_latency_ms", cpuLat, 2);
        j.un("threads_still_suspended", stillSusp);
        j.endObj();
        j.obj("tree");
        j.un("processes", (unsigned long long)tree.size());
        j.n("freeze_exit", freezeRc);
        j.n("thaw_exit", thawRc);
        j.n("counter_delta_frozen_0", (long long)(f1[0] - f0[0]));
        j.n("counter_delta_frozen_1", (long long)(f1[1] - f0[1]));
        j.n("counter_delta_frozen_2", (long long)(f1[2] - f0[2]));
        j.n("counter_delta_thawed_0", (long long)(t1c[0] - t0c[0]));
        j.n("counter_delta_thawed_1", (long long)(t1c[1] - t0c[1]));
        j.n("counter_delta_thawed_2", (long long)(t1c[2] - t0c[2]));
        j.un("threads_total", totalThreads);
        j.un("threads_not_suspended_frozen", runningThreads);
        j.un("threads_still_suspended_thawed", stillTree);
        j.un("state_procs", (unsigned long long)stf.procs.size());
        j.un("trim_verified", trimVerified);
        j.endObj();
        j.obj("window");
        j.n("freeze_exit", winFreezeRc);
        j.n("thaw_exit", winThawRc);
        j.b("visible_window_minimized", aMinimized);
        j.b("visible_window_restored", aRestored);
        j.b("already_minimized_still_minimized", bStillMin);
        j.un("state_windows", (unsigned long long)sw.wins.size());
        j.endObj();
        j.obj("guard");
        j.n("freeze_exit_on_fake_eac", guardRc);
        j.b("fake_eac_still_running", fakeStillRunning);
        j.un("clean_tree_modules", gMods);
        j.endObj();
        j.obj("checks");
        for (size_t i = 0; i < ck.names.size(); ++i) j.b(ck.names[i].c_str(), ck.results[i]);
        j.endObj();
        j.un("passed", (unsigned long long)ck.pass);
        j.un("failed", (unsigned long long)ck.fail);
        j.b("ok", ck.fail == 0);
        j.endObj();
        Out("\n");
    }

    UnmapViewOfFile(shm);
    CloseHandle(map);
    CloseHandle(h);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ck.fail == 0 ? 0 : 1;
}
