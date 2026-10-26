// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

static volatile DWORD g_pid = 0;
static volatile LONG  g_hit = 0;
static volatile LONG  g_hitKind = 0;
static HWND           g_hitHwnd = nullptr;
static POINT          g_hitPt = { 0, 0 };

static HANDLE  g_proc = nullptr;
static DWORD   g_procPid = 0;

static bool PidOfWindow(HWND hwnd, DWORD& pid)
{
    DWORD p = 0;
    GetWindowThreadProcessId(hwnd, &p);
    pid = p;
    return p == g_pid && p != 0;
}

static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_hit == 0) {
        switch (wp) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_NCMBUTTONDOWN: {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
            if (ms) {
                HWND under = WindowFromPoint(ms->pt);
                HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
                DWORD pid = 0;
                if (root && PidOfWindow(root, pid)) {
                    g_hitHwnd = root;
                    g_hitPt = ms->pt;
                    InterlockedExchange(&g_hitKind, 1);
                    InterlockedExchange(&g_hit, 1);
                } else if (under && PidOfWindow(under, pid)) {
                    g_hitHwnd = under;
                    g_hitPt = ms->pt;
                    InterlockedExchange(&g_hitKind, 1);
                    InterlockedExchange(&g_hit, 1);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD evt, HWND hwnd, LONG, LONG, DWORD, DWORD)
{
    if (g_hit != 0) return;
    if (evt != EVENT_SYSTEM_FOREGROUND) return;
    DWORD pid = 0;
    if (hwnd && PidOfWindow(hwnd, pid)) {
        g_hitHwnd = hwnd;
        GetCursorPos(&g_hitPt);
        InterlockedExchange(&g_hitKind, 2);
        InterlockedExchange(&g_hit, 1);
    }
}

static BOOL WINAPI CtrlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (g_proc && g_procPid) {
            NTSTATUS st = DoResume(g_proc);
            std::string err;
            StateDelete(g_procPid, err);
            Log("watch: console ctrl event %lu - resumed pid %lu status=0x%08lX, state removed",
                (unsigned long)type, (unsigned long)g_procPid, (unsigned long)st);
        }
    }
    return FALSE;
}

int CmdWatch(const std::wstring& spec)
{
    ProcSnapshot p;
    std::string err;
    if (!PickOne(spec, p, err)) { Out("ERROR: %s\n", err.c_str()); return 2; }

    StateFile pre;
    if (StateRead(p.pid, pre, err) && pre.exists && !g_ctx.force) {
        Out("ERROR: pid %lu already has a freeze state file. Thaw it first (RamStasis.exe thaw %lu) or use --force.\n",
            (unsigned long)p.pid, (unsigned long)p.pid);
        return 5;
    }

    HANDLE h = nullptr;
    const DWORD access = PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                         PROCESS_SET_QUOTA | PROCESS_VM_OPERATION;
    if (OpenTarget(p.pid, access, h, err) != 0) { Out("ERROR: %s\n", err.c_str()); return 4; }

    std::vector<HWND> hwnds;
    unsigned nwin = CountTargetWindows(p.pid, &hwnds);

    ULONGLONG createTime = 0;
    {
        ULONGLONG k = 0, u = 0;
        GetProcCpu(h, k, u, createTime);
    }

    MemNumbers mb, ma;
    SysPerf spb, spa;
    PageFileNumbers pfb, pfa;
    GetMem(h, mb);
    GetSysPerf(spb);
    GetPageFile(pfb);

    std::vector<WinRec> wins;
    std::vector<DWORD> one;
    one.push_back(p.pid);
    unsigned nMin = MinimizeWindowsForPids(one, wins);
    VerifyMinimized(wins, 500);
    Sleep(150);

    SuspendResult sr;
    if (!DoSuspend(h, p.pid, sr)) {
        Out("ERROR: NtSuspendProcess failed, status=0x%08lX\n", (unsigned long)sr.status);
        std::vector<std::string> notes;
        RestoreWindows(wins, notes);
        CloseHandle(h);
        return 6;
    }
    BOOL ews = FALSE;
    for (int attempt = 0; attempt < 2; ++attempt) {
        SetLastError(0);
        ews = EmptyWorkingSet(h);
        Sleep(80);
        GetMem(h, ma);
        if (ma.ok && mb.ok && ma.workingSet < mb.workingSet) break;
    }
    GetSysPerf(spa);
    GetPageFile(pfa);

    std::string serr;
    StateFile sf;
    sf.version = 2;
    sf.rootPid = p.pid;
    sf.image = p.imagePath;
    sf.suspendedAt = NowUnix();
    {
        StateProc sp;
        sp.pid = p.pid;
        sp.createTime = createTime;
        sp.depth = 0;
        sp.suspendCalls = 1;
        sp.wsBefore = mb.workingSet;
        sp.wsAfter = ma.workingSet;
        sp.commit = mb.privateUsage;
        sp.exe = p.exeName;
        for (size_t k = 0; k < sr.before.size(); ++k) {
            LONG base = sr.before[k].suspendCount;
            sp.threads.push_back(std::make_pair(sr.before[k].tid, base < 0 ? 0 : base));
        }
        sp.extraThreads = sr.extraSuspended;
        sf.procs.push_back(sp);
    }
    for (size_t k = 0; k < wins.size(); ++k) {
        StateWin w;
        w.pid = wins[k].pid;
        w.hwnd = (unsigned long long)(ULONG_PTR)wins[k].hwnd;
        w.visible = wins[k].visibleBefore;
        w.iconic = wins[k].iconicBefore;
        w.minimizedByUs = wins[k].minimizedByUs;
        w.left = wins[k].rect.left;
        w.top = wins[k].rect.top;
        w.right = wins[k].rect.right;
        w.bottom = wins[k].rect.bottom;
        sf.wins.push_back(w);
    }
    bool stateOk = StateWrite(sf, serr);
    g_proc = h;
    g_procPid = p.pid;
    g_pid = p.pid;
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (!g_ctx.json) {
        Out("=== WATCH  pid %lu  %s ===\n", (unsigned long)p.pid, W2U(p.exeName).c_str());
        Out("  windows found        : %u (%u minimized by frz, no injection)\n", nwin, nMin);
        Out("  NtSuspendProcess     : %s\n", NtStatusText(sr.status).c_str());
        Out("  EmptyWorkingSet      : BOOL=%d\n", (int)ews);
        Out("  working set          : %s -> %s\n",
            Bytes(mb.workingSet).c_str(), Bytes(ma.workingSet).c_str());
        Out("  state file           : %s (%s)\n", W2U(StatePathFor(p.pid)).c_str(),
            stateOk ? "written" : "FAILED");
        Out("\n  mechanism            : WH_MOUSE_LL hook + WindowFromPoint hit-test (primary)\n"
            "                         SetWinEventHook(EVENT_SYSTEM_FOREGROUND) (secondary)\n"
            "  why                  : a suspended process cannot process mouse input, cannot take\n"
            "                         the foreground and cannot answer messages, so foreground\n"
            "                         polling would never fire. The low-level hook is delivered to\n"
            "                         THIS (awake) process by the input stack, and win32k still\n"
            "                         resolves the window under the cursor without the target's\n"
            "                         threads, so the click is detected while it is hung.\n");
        Out("  armed                : click any window of pid %lu to thaw it\n", (unsigned long)p.pid);
        Out("  timeout              : %d ms (leaves it frozen + state file on timeout)\n", g_ctx.timeoutMs);
    }

    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!hook) {
        Out("ERROR: SetWindowsHookExW(WH_MOUSE_LL) failed, win32=%lu\n", (unsigned long)GetLastError());
        DoResume(h);
        CloseHandle(h);
        return 7;
    }
    HWINEVENTHOOK weh = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    ULONGLONG cpuBefore = 0;
    {
        ULONGLONG k = 0, u = 0, c = 0;
        if (GetProcCpu(h, k, u, c)) cpuBefore = k + u;
    }

    ULONGLONG tStart = QpcMs();
    ULONGLONG tClick = 0;
    bool timedOut = false;
    for (;;) {
        if (g_hit) break;
        if ((int)(QpcMs() - tStart) >= g_ctx.timeoutMs) { timedOut = true; break; }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_hit) break;
    }
    tClick = QpcMs();

    if (timedOut) {
        UnhookWindowsHookEx(hook);
        if (weh) UnhookWinEvent(weh);
        SetConsoleCtrlHandler(CtrlHandler, FALSE);
        g_proc = nullptr;
        Out("\nTIMEOUT after %d ms: no click on pid %lu. Process is still FROZEN and the state file is kept.\n"
            "  wake it with: RamStasis.exe thaw %lu\n",
            g_ctx.timeoutMs, (unsigned long)p.pid, (unsigned long)p.pid);
        Log("watch: timeout, pid %lu left frozen", (unsigned long)p.pid);
        CloseHandle(h);
        return 2;
    }

    char title[256] = { 0 };
    if (g_hitHwnd) GetWindowTextA(g_hitHwnd, title, (int)sizeof(title));
    LONG kind = InterlockedCompareExchange(&g_hitKind, 0, 0);

    ULONGLONG t0 = QpcMs();
    NTSTATUS rs = DoResume(h);
    ULONGLONG t1 = QpcMs();

    {
        std::vector<std::string> notes;
        RestoreWindows(wins, notes);
        if (!g_ctx.json)
            for (size_t i = 0; i < notes.size(); ++i) Out("  restore              : %s\n", notes[i].c_str());
    }

    double cpuLat = -1.0;
    bool msgOk = false, cpuOk = false;
    double msgLat = MeasureWakeMs(h, p.pid, cpuBefore, 5000.0, cpuLat, msgOk, cpuOk);

    UnhookWindowsHookEx(hook);
    if (weh) UnhookWinEvent(weh);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    g_proc = nullptr;

    Sleep(60);
    MemNumbers mThaw;
    GetMem(h, mThaw);
    GetSysPerf(spa);
    GetPageFile(pfa);
    std::vector<ThreadInfo> ta = EnumThreads(p.pid, false);
    unsigned stillSusp = 0;
    for (size_t i = 0; i < ta.size(); ++i)
        if (ta[i].suspendCount > 0) stillSusp++;

    bool ok = (rs == kStatusSuccess) && stillSusp == 0;
    if (ok) { std::string derr; StateDelete(p.pid, derr); }

    double clickToResume = (double)(t1 - t0);
    double clickToResponsive = msgOk ? clickToResume + msgLat : -1.0;

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "watch");
        j.un("pid", p.pid);
        j.sw("exe", p.exeName);
        j.un("windows", nwin);
        j.s("mechanism", "WH_MOUSE_LL + WindowFromPoint (primary), SetWinEventHook(EVENT_SYSTEM_FOREGROUND) (secondary)");
        j.s("trigger", kind == 1 ? "mouse_click" : "foreground_event");
        j.un("trigger_hwnd", (unsigned long long)(ULONG_PTR)g_hitHwnd);
        j.s("trigger_window_title", title);
        j.n("trigger_x", (long long)g_hitPt.x);
        j.n("trigger_y", (long long)g_hitPt.y);
        j.obj("freeze");
        j.s("nt_suspend_process", NtStatusText(sr.status).c_str());
        j.b("empty_working_set", ews != FALSE);
        j.member("before", mb);
        j.member("after_pageout", ma);
        j.endObj();
        j.obj("wake");
        j.s("nt_resume_process", NtStatusText(rs).c_str());
        j.f("click_to_resume_ms", clickToResume, 2);
        j.b("message_ok", msgOk);
        j.f("click_to_responsive_ms", clickToResponsive, 2);
        j.b("cpu_advance_ok", cpuOk);
        j.f("click_to_cpu_ms", cpuLat, 2);
        j.un("threads_still_suspended", stillSusp);
        j.endObj();
        j.b("state_file_written", stateOk);
        j.b("ok", ok);
        j.endObj();
        Out("\n");
    } else {
        Out("\n-- CLICK DETECTED (%s) --\n", kind == 1 ? "mouse button" : "foreground change");
        Out("  window               : 0x%08llX  \"%s\"\n", (unsigned long long)(ULONG_PTR)g_hitHwnd, title);
        Out("  click at             : (%ld, %ld)\n", (long)g_hitPt.x, (long)g_hitPt.y);
        Out("  detection            : %llu ms after arming\n", (unsigned long long)(tClick - tStart));
        Out("  NtResumeProcess      : %s (call %.2f ms)\n", NtStatusText(rs).c_str(), clickToResume);
        Out("  CLICK -> RESUME      : %.2f ms\n", clickToResume);
        if (msgOk) Out("  click -> responsive  : %.2f ms (SendMessageTimeout WM_NULL round trip)\n", clickToResponsive);
        else       Out("  click -> responsive  : no window answered within 5000 ms\n");
        if (cpuOk) Out("  click -> CPU moved   : %.2f ms\n", cpuLat);
        Out("  threads still susp.  : %u\n", stillSusp);
        Out("\n-- MEMORY --\n");
        Out("  working set          : %s -> %s -> %s\n",
            Bytes(mb.workingSet).c_str(), Bytes(ma.workingSet).c_str(), Bytes(mThaw.workingSet).c_str());
        Out("  private (commit)     : %s -> %s\n",
            Bytes(mb.privateUsage).c_str(), Bytes(mThaw.privateUsage).c_str());
        if (pfb.ok && pfa.ok)
            Out("  pagefile in use      : %s -> %s\n",
                Bytes(pfb.totalInUseBytes).c_str(), Bytes(pfa.totalInUseBytes).c_str());
        Out("  result               : %s\n", ok ? "thawed, balanced" : "THAW INCOMPLETE");
    }

    CloseHandle(h);
    return ok ? 0 : 6;
}
