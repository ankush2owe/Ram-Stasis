// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

#include <tlhelp32.h>

static const DWORD kFreezeAccess = PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_READ | PROCESS_SET_QUOTA | PROCESS_VM_OPERATION;

DWORD OpenTarget(DWORD pid, DWORD access, HANDLE& h, std::string& err)
{
    h = OpenProcess(access, FALSE, pid);
    if (h) { err.clear(); return 0; }
    DWORD e = GetLastError();
    if (e == ERROR_ACCESS_DENIED && IsElevated()) {
        static bool triedPriv = false;
        if (!triedPriv) {
            triedPriv = true;
            if (FrzTryEnableDebugPrivilege()) {
                h = OpenProcess(access, FALSE, pid);
                if (h) { err.clear(); return 0; }
                e = GetLastError();
            }
        }
    }
    char buf[512];
    if (e == ERROR_ACCESS_DENIED) {
        sprintf_s(buf, sizeof(buf),
                  "OpenProcess(pid=%lu, access=0x%lX) -> ERROR_ACCESS_DENIED (5). "
                  "Target is at a higher integrity level or is a protected process. frz runs elevated=%s.",
                  (unsigned long)pid, (unsigned long)access, IsElevated() ? "yes" : "NO - re-run from an elevated console");
    } else if (e == ERROR_INVALID_PARAMETER) {
        sprintf_s(buf, sizeof(buf), "OpenProcess(pid=%lu) -> ERROR_INVALID_PARAMETER (87): no such process", (unsigned long)pid);
    } else {
        sprintf_s(buf, sizeof(buf), "OpenProcess(pid=%lu) -> win32=%lu", (unsigned long)pid, (unsigned long)e);
    }
    err = buf;
    return e;
}

bool NtProc(PFN_NtSuspendProcess* suspend, PFN_NtResumeProcess* resume, std::string& err)
{
    static PFN_NtSuspendProcess s = nullptr;
    static PFN_NtResumeProcess r = nullptr;
    static std::string cachedErr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) ntdll = LoadLibraryW(L"ntdll.dll");
        if (!ntdll) {
            cachedErr = "LoadLibrary(ntdll.dll) failed, win32=" + std::to_string((unsigned long long)GetLastError());
        } else {
            s = (PFN_NtSuspendProcess)(void*)GetProcAddress(ntdll, "NtSuspendProcess");
            r = (PFN_NtResumeProcess)(void*)GetProcAddress(ntdll, "NtResumeProcess");
            if (!s) cachedErr += "GetProcAddress(ntdll!NtSuspendProcess) failed. ";
            if (!r) cachedErr += "GetProcAddress(ntdll!NtResumeProcess) failed. ";
        }
    }
    if (!s || !r) { err = cachedErr; return false; }
    *suspend = s;
    *resume = r;
    return true;
}

bool NtThreadProc(PFN_NtSuspendThread* suspend, PFN_NtResumeThread* resume, std::string& err)
{
    static PFN_NtSuspendThread s = nullptr;
    static PFN_NtResumeThread r = nullptr;
    static std::string cachedErr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) ntdll = LoadLibraryW(L"ntdll.dll");
        if (!ntdll) {
            cachedErr = "LoadLibrary(ntdll.dll) failed, win32=" + std::to_string((unsigned long long)GetLastError());
        } else {
            s = (PFN_NtSuspendThread)(void*)GetProcAddress(ntdll, "NtSuspendThread");
            r = (PFN_NtResumeThread)(void*)GetProcAddress(ntdll, "NtResumeThread");
            if (!s) cachedErr += "GetProcAddress(ntdll!NtSuspendThread) failed. ";
            if (!r) cachedErr += "GetProcAddress(ntdll!NtResumeThread) failed. ";
        }
    }
    if (!s || !r) { err = cachedErr; return false; }
    *suspend = s;
    *resume = r;
    return true;
}

NTSTATUS DoResume(HANDLE h)
{
    PFN_NtSuspendProcess s = nullptr;
    PFN_NtResumeProcess r = nullptr;
    std::string err;
    if (!NtProc(&s, &r, err)) return (NTSTATUS)0xC0000002L;
    return r(h);
}

NTSTATUS ResumeExtraThread(DWORD tid)
{
    PFN_NtSuspendThread s = nullptr;
    PFN_NtResumeThread r = nullptr;
    std::string err;
    if (!NtThreadProc(&s, &r, err)) return (NTSTATUS)0xC0000002L;
    (void)s;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h) return (NTSTATUS)0xC0000002L;
    ULONG prev = 0;
    NTSTATUS st = r(h, &prev);
    CloseHandle(h);
    return st;
}

bool DoSuspend(HANDLE h, DWORD pid, SuspendResult& res)
{
    res = SuspendResult();
    PFN_NtSuspendProcess s = nullptr;
    PFN_NtResumeProcess r = nullptr;
    std::string err;
    if (!NtProc(&s, &r, err)) {
        Log("NtProc resolve failed: %s", err.c_str());
        return false;
    }

    res.before = EnumThreads(pid, false);
    res.threadsSeen = (unsigned)res.before.size();

    res.status = s(h);
    res.ok = (res.status == kStatusSuccess);
    Log("NtSuspendProcess(pid=%lu) -> 0x%08lX", (unsigned long)pid, (unsigned long)res.status);
    if (!res.ok) return false;

    Sleep(60);
    res.after = EnumThreads(pid, false);

    for (size_t i = 0; i < res.after.size(); ++i) {
        const ThreadInfo& a = res.after[i];
        const ThreadInfo* b = nullptr;
        for (size_t j = 0; j < res.before.size(); ++j)
            if (res.before[j].tid == a.tid) { b = &res.before[j]; break; }
        if (!b) {
            if (a.suspendCount == 0) res.escaped.push_back(a.tid);
            continue;
        }
        if (b->suspendCount >= 0 && a.suspendCount >= 0 && a.suspendCount != b->suspendCount + 1)
            res.escaped.push_back(a.tid);
    }

    if (!res.escaped.empty()) {
        PFN_NtSuspendThread st = nullptr;
        PFN_NtResumeThread rt = nullptr;
        if (NtThreadProc(&st, &rt, err)) {
            for (size_t i = 0; i < res.escaped.size(); ++i) {
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                       FALSE, res.escaped[i]);
                if (!ht) continue;
                ULONG prev = 0;
                NTSTATUS s2 = st(ht, &prev);
                CloseHandle(ht);
                if (s2 == kStatusSuccess) res.extraSuspended.push_back(res.escaped[i]);
                Log("DoSuspend: escaped thread %lu -> NtSuspendThread 0x%08lX (prev=%lu)",
                    (unsigned long)res.escaped[i], (unsigned long)s2, (unsigned long)prev);
            }
            Sleep(30);
            res.after = EnumThreads(pid, false);
        }
    }

    res.threadsIncremented = 0;
    res.threadsUnqueryable = 0;
    for (size_t i = 0; i < res.before.size(); ++i) {
        const ThreadInfo& b = res.before[i];
        if (b.suspendCount < 0) { res.threadsUnqueryable++; continue; }
        for (size_t j = 0; j < res.after.size(); ++j) {
            if (res.after[j].tid != b.tid) continue;
            if (res.after[j].suspendCount >= b.suspendCount + 1) res.threadsIncremented++;
            break;
        }
    }
    return true;
}

std::string NtStatusText(NTSTATUS st)
{
    if (st == kStatusSuccess) return "STATUS_SUCCESS";
    char buf[64];
    sprintf_s(buf, sizeof(buf), "0x%08lX", (unsigned long)st);
    return std::string(buf);
}

static long long D(long long before, long long after) { return after - before; }

double MeasureWakeMs(HANDLE h, DWORD pid, ULONGLONG cpuBefore, double timeoutMs,
                     double& cpuLatencyMs, bool& msgOk, bool& cpuOk)
{
    HWND w = FindTargetWindow(pid, false);
    ULONGLONG t0 = QpcMs();
    ULONGLONG tMsg = 0, tCpu = 0;
    msgOk = false;
    cpuOk = false;
    unsigned noWinSpins = 0;
    for (;;) {
        if (!msgOk && w) {
            DWORD_PTR r = 0;
            if (SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 30, &r)) {
                tMsg = QpcMs();
                msgOk = true;
            }
        }
        if (!cpuOk) {
            ULONGLONG k = 0, u = 0, c = 0;
            if (GetProcCpu(h, k, u, c) && (k + u) > cpuBefore) {
                tCpu = QpcMs();
                cpuOk = true;
            }
        }
        if (msgOk && cpuOk) break;
        if ((double)(QpcMs() - t0) > timeoutMs) break;
        if (!msgOk && !w) {
            if (++noWinSpins > 400) break;
        }
        Sleep(1);
    }
    cpuLatencyMs = cpuOk ? (double)(tCpu - t0) : -1.0;
    return msgOk ? (double)(tMsg - t0) : -1.0;
}

static bool CommitWarning(const SysPerf& sp, const PageFileNumbers& pf)
{
    if (!sp.ok || sp.commitLimit == 0) return false;
    bool low = (sp.freeCommit() * 100ULL) < (sp.commitLimit * 15ULL);
    if (!low) return false;
    Out("\n  WARNING: free commit is %s of %s (%llu%%) - below the 15%% headroom mark.\n",
        Bytes(sp.freeCommit()).c_str(), Bytes(sp.commitLimit).c_str(),
        (unsigned long long)((sp.freeCommit() * 100ULL) / sp.commitLimit));
    Out("           Freezing returns RAM but NOT commit. The pagefile is the real ceiling:\n"
        "           a new large launch can still fail with 'out of memory' while gigabytes\n"
        "           of RAM sit free.\n");
    if (pf.ok)
        Out("           Pagefile in use: %s of %s.\n",
            Bytes(pf.totalInUseBytes).c_str(), Bytes(pf.totalSizeBytes).c_str());
    return true;
}

static void PrintGuardRefusal(const std::vector<AcHit>& hits, DWORD pid, const std::wstring& exe)
{
    Out("\n=== REFUSED BY ANTI-CHEAT GUARD ===\n");
    Out("  target               : pid %lu  %s\n", (unsigned long)pid, W2U(exe).c_str());
    Out("  components found     : %u\n\n", (unsigned)hits.size());
    PrintGuardHits(hits);
    Out("\n  NOTHING WAS DONE: no freeze handle was opened on the target (the guard's own\n"
        "                    scan is read-only and closes every handle immediately),\n"
        "                    no window was touched, no working set was trimmed,\n"
        "                    no state file was written.\n"
        "  Why                  : suspending a process that kernel anti-cheat supervises gets\n"
        "                         the account banned. The guard exists so this cannot happen\n"
        "                         by accident.\n"
        "  If you own the machine and accept the ban risk: RamStasis.exe freeze %lu --force\n\n",
        (unsigned long)pid);
}

static void PrintTreeTable(const char* title, const std::vector<ProcSnapshot>& v,
                           const std::vector<MemNumbers>* mem)
{
    Out("  %s\n", title);
    Out("  %-8s %-6s %-26s %14s %14s %6s\n", "PID", "DEPTH", "EXE", "WORKING_SET", "COMMIT", "THR");
    for (size_t i = 0; i < v.size(); ++i) {
        std::string name = W2U(v[i].exeName);
        if (name.size() > 26) name = name.substr(0, 25) + "~";
        int thr = CountThreads(v[i].pid);
        if (mem && i < mem->size() && (*mem)[i].ok) {
            Out("  %-8lu %-6d %-26s %14llu %14llu %6d\n",
                (unsigned long)v[i].pid, v[i].depth, name.c_str(),
                (*mem)[i].workingSet, (*mem)[i].privateUsage, thr);
        } else {
            Out("  %-8lu %-6d %-26s %14s %14s %6d\n",
                (unsigned long)v[i].pid, v[i].depth, name.c_str(), "-", "-", thr);
        }
    }
}

namespace {

struct FreezeItem {
    ProcSnapshot p;
    HANDLE h = nullptr;
    bool opened = false;
    bool skipped = false;
    DWORD openErr = 0;
    MemNumbers mb, ma;
    SuspendResult sr;
    bool trimOk = false;
    BOOL ewsRet = FALSE;
    DWORD ewsErr = 0;
    bool late = false;
};

bool InItems(const std::vector<FreezeItem>& v, DWORD pid)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].p.pid == pid) return true;
    return false;
}

void CloseItems(std::vector<FreezeItem>& v)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].h) { CloseHandle(v[i].h); v[i].h = nullptr; }
}

}

int CmdFreeze(const std::wstring& spec)
{
    ProcSnapshot rootOnly;
    std::string err;
    if (!ResolveTargetNoHandle(spec, rootOnly, err)) {
        Out("ERROR: %s\n", err.c_str());
        Log("freeze: resolve failed: %s", err.c_str());
        return 2;
    }

    std::vector<AcHit> hits;
    unsigned gp = 0, gm = 0, gd = 0;
    GuardCheckTree(rootOnly.pid, hits, gp, gm);
    GuardCheckKernel(hits, gd);

    if (!hits.empty() && !g_ctx.force) {
        PrintGuardRefusal(hits, rootOnly.pid, rootOnly.exeName);
        Log("freeze: BLOCKED by anti-cheat guard: %u hit(s)", (unsigned)hits.size());
        return 8;
    }
    if (!hits.empty() && g_ctx.force) {
        Out("\n  WARNING: --force given. The anti-cheat guard found %u component(s):\n", (unsigned)hits.size());
        PrintGuardHits(hits);
        Out("  WARNING: PROCEEDING ANYWAY. Suspending this process can get your account PERMANENTLY BANNED.\n"
            "           You asked for this with --force; the risk is yours.\n\n");
        Log("freeze: --force override of anti-cheat guard, %u hit(s)", (unsigned)hits.size());
    } else if (!g_ctx.quiet) {
        Out("anti-cheat guard     : clean (%u processes, %u modules, %u kernel drivers scanned)\n",
            gp, gm, gd);
    }

    ProcSnapshot p;
    if (!PickOne(spec, p, err)) {
        Out("ERROR: %s\n", err.c_str());
        return 2;
    }

    std::vector<ProcSnapshot> procs;
    if (!g_ctx.noTree) procs = BuildTree(p.pid, true);
    if (procs.empty()) procs.push_back(p);
    OrderChildrenFirst(procs);

    std::vector<DWORD> allPids;
    for (size_t i = 0; i < procs.size(); ++i) allPids.push_back(procs[i].pid);
    DWORD hitPid = 0;
    std::string hitPath;
    if (StateFindTreeOverlap(allPids, hitPid, hitPath)) {
        Out("ERROR: pid %lu already has a freeze state file (%s).\n"
            "       That process (or its tree) is probably still suspended from an earlier invocation.\n"
            "       Suspends must be balanced 1:1 with resumes, so frz refuses to stack another one.\n"
            "       Wake it first:  RamStasis.exe thaw %lu\n",
            (unsigned long)hitPid, hitPath.c_str(), (unsigned long)p.pid);
        return 5;
    }

    if (!g_ctx.quiet) {
        Out("=== FREEZE  root pid %lu  %s%s ===\n", (unsigned long)p.pid, W2U(p.exeName).c_str(),
            g_ctx.noTree ? "  (single process, --no-tree)" : "");
        Out("  image                : %s\n", W2U(p.imagePath).c_str());
        Out("  tree                 : %u process(es)%s\n",
            (unsigned)procs.size(), g_ctx.noTree ? " (--no-tree)" : "");
        Out("  freeze order         : children first (deepest first), then root\n");
    }

    std::vector<FreezeItem> items;
    bool rootOpen = false;
    for (size_t i = 0; i < procs.size(); ++i) {
        FreezeItem it;
        it.p = procs[i];
        DWORD oe = OpenTarget(it.p.pid, kFreezeAccess, it.h, err);
        if (oe != 0) {
            it.opened = false;
            it.skipped = true;
            it.openErr = oe;
            it.p.alive = false;
            if (it.p.pid == p.pid) {
                Out("ERROR: cannot open the root process: %s\n", err.c_str());
                CloseItems(items);
                return 4;
            }
            Log("freeze: skip pid %lu: %s", (unsigned long)it.p.pid, err.c_str());
        } else {
            it.opened = true;
            rootOpen = true;
            GetMem(it.h, it.mb);
        }
        items.push_back(it);
    }
    if (!rootOpen) {
        Out("ERROR: the root process could not be opened; nothing was frozen.\n");
        CloseItems(items);
        return 4;
    }

    SysPerf sp0, sp1;
    PageFileNumbers pf0, pf1;
    GetSysPerf(sp0);
    GetPageFile(pf0);
    ULONGLONG treeCommit = 0, treeWs = 0;
    unsigned opened = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].opened) continue;
        opened++;
        if (items[i].mb.ok) {
            treeCommit += items[i].mb.privateUsage;
            treeWs += items[i].mb.workingSet;
        }
    }

    if (!g_ctx.quiet) {
        std::vector<MemNumbers> mems;
        std::vector<ProcSnapshot> snap;
        for (size_t i = 0; i < items.size(); ++i) {
            mems.push_back(items[i].opened ? items[i].mb : MemNumbers());
            snap.push_back(items[i].p);
        }
        Out("\n-- TREE (freeze order) --\n");
        PrintTreeTable("children first, root last", snap, &mems);

        Out("\n-- PRE-FLIGHT (D1: commit is the ceiling, not RAM) --\n");
        PrintSysTable("system before", sp0, pf0);
        Out("  tree commit          : %s   (%u of %u process(es) open)\n",
            Bytes(treeCommit).c_str(), opened, (unsigned)items.size());
        Out("  tree working set     : %s\n", Bytes(treeWs).c_str());
        if (!sp0.ok) Out("  NOTE: GetPerformanceInfo failed, free commit unknown\n");
    }

    std::vector<DWORD> livePids;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].opened) livePids.push_back(items[i].p.pid);

    std::vector<WinRec> wins;
    unsigned nMin = MinimizeWindowsForPids(livePids, wins);
    bool allIconic = VerifyMinimized(wins, 500);
    Sleep(150);

    if (!g_ctx.quiet) {
        Out("\n-- WINDOWS (ghost suppression via minimize, no injection) --\n");
        PrintWindowTable(wins);
        Out("  ShowWindowAsync(SW_MINIMIZE) : %u post(s) returned nonzero for %u window(s); %s\n",
            nMin, (unsigned)wins.size(),
            allIconic ? "all targeted windows reported IsIconic before the suspend"
                      : "WARNING: at least one window never became iconic within 500 ms");
    }

    unsigned suspOk = 0, suspFail = 0;
    bool rootSusp = false;
    for (size_t i = 0; i < items.size(); ++i) {
        FreezeItem& it = items[i];
        if (!it.opened) continue;
        bool ok = DoSuspend(it.h, it.p.pid, it.sr);
        if (!ok) {
            suspFail++;
            Log("freeze: NtSuspendProcess pid %lu failed 0x%08lX",
                (unsigned long)it.p.pid, (unsigned long)it.sr.status);
            continue;
        }
        suspOk++;
        if (it.p.pid == p.pid) rootSusp = true;
        if (!g_ctx.quiet) {
            Out("  suspend pid %-7lu %-22s threads %3u, +1 on %3u, escaped %u%s\n",
                (unsigned long)it.p.pid, NtStatusText(it.sr.status).c_str(),
                it.sr.threadsSeen, it.sr.threadsIncremented, (unsigned)it.sr.escaped.size(),
                it.sr.escaped.empty() ? "" : " (individually suspended)");
        }
    }

    if (!rootSusp) {
        Out("\nERROR: the root process did not suspend. Rolling back what did.\n");
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].opened) continue;
            if (items[i].sr.ok) {
                DoResume(items[i].h);
                for (size_t k = 0; k < items[i].sr.extraSuspended.size(); ++k)
                    ResumeExtraThread(items[i].sr.extraSuspended[k]);
            }
        }
        std::vector<std::string> notes;
        RestoreWindows(wins, notes);
        CloseItems(items);
        return 6;
    }

    for (int pass = 0; pass < 3; ++pass) {
        std::vector<ProcSnapshot> now = BuildTree(p.pid, true);
        std::vector<ProcSnapshot> fresh;
        for (size_t i = 0; i < now.size(); ++i)
            if (!InItems(items, now[i].pid)) fresh.push_back(now[i]);
        if (fresh.empty()) break;
        OrderChildrenFirst(fresh);
        for (size_t i = 0; i < fresh.size(); ++i) {
            FreezeItem it;
            it.p = fresh[i];
            it.late = true;
            if (OpenTarget(it.p.pid, kFreezeAccess, it.h, err) != 0) {
                it.opened = false;
                it.skipped = true;
                it.openErr = GetLastError();
                items.push_back(it);
                continue;
            }
            it.opened = true;
            GetMem(it.h, it.mb);
            std::vector<DWORD> one;
            one.push_back(it.p.pid);
            std::vector<WinRec> w2;
            MinimizeWindowsForPids(one, w2);
            VerifyMinimized(w2, 500);
            Sleep(150);
            wins.insert(wins.end(), w2.begin(), w2.end());
            bool ok = DoSuspend(it.h, it.p.pid, it.sr);
            if (!g_ctx.quiet)
                Out("  late child pid %-7lu %s (appeared during the walk)%s\n",
                    (unsigned long)it.p.pid, W2U(it.p.exeName).c_str(),
                    ok ? "" : " - SUSPEND FAILED");
            items.push_back(it);
        }
    }

    for (size_t i = 0; i < items.size(); ++i) {
        FreezeItem& it = items[i];
        if (!it.opened) continue;
        for (int attempt = 0; attempt < 2; ++attempt) {
            SetLastError(0);
            it.ewsRet = EmptyWorkingSet(it.h);
            it.ewsErr = GetLastError();
            Sleep(80);
            GetMem(it.h, it.ma);
            if (it.ma.ok && it.mb.ok && (it.ma.workingSet < it.mb.workingSet || (it.ewsRet && it.ma.workingSet == 0))) {
                it.trimOk = true;
                break;
            }
        }
    }

    GetSysPerf(sp1);
    GetPageFile(pf1);

    for (size_t i = 0; i < wins.size(); ++i) {
        WinRec& r = wins[i];
        if (!IsWindow(r.hwnd)) continue;
        r.iconicAfter = IsIconic(r.hwnd) != FALSE;
        r.visibleAfter = IsWindowVisible(r.hwnd) != FALSE;
        r.hungAfter = IsHungAppWindow(r.hwnd) != FALSE;
    }
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fgPid);

    if (!g_ctx.quiet) {
        Out("\n-- TRIM (psapi!EmptyWorkingSet, verified per pid) --\n");
        Out("  %-8s %-10s %14s %14s %8s %s\n", "PID", "DEPTH", "WS_BEFORE", "WS_AFTER", "DROPPED", "NOTE");
        for (size_t i = 0; i < items.size(); ++i) {
            FreezeItem& it = items[i];
            if (!it.opened) {
                Out("  %-8lu %-10d %14s %14s %8s %s\n", (unsigned long)it.p.pid, it.p.depth,
                    "-", "-", "-", "skipped (gone at open time)");
                continue;
            }
            char note[128];
            note[0] = 0;
            if (!it.trimOk)
                sprintf_s(note, sizeof(note), "WS DID NOT DROP (EmptyWorkingSet=%d win32=%lu)",
                          (int)it.ewsRet, (unsigned long)it.ewsErr);
            Out("  %-8lu %-10d %14llu %14llu %8s %s\n",
                (unsigned long)it.p.pid, it.p.depth,
                it.mb.workingSet, it.ma.workingSet,
                it.trimOk ? "yes" : "NO", note);
        }

        Out("\n-- AFTER --\n");
        PrintSysTable("system after trim", sp1, pf1);
        Out("  commit returned      : %+lld B (expected ~0: commit is a promise, not residency)\n",
            D((long long)sp0.commitTotal, (long long)sp1.commitTotal));

        Out("\n-- GHOST CHECK (what the OS reports right now) --\n");
        Out("  GetForegroundWindow  : hwnd 0x%08llX (pid %lu)%s\n",
            (unsigned long long)(ULONG_PTR)fg, (unsigned long)fgPid,
            (fg && InItems(items, fgPid)) ? "  <- belongs to the frozen set" : "");
        for (size_t i = 0; i < wins.size(); ++i) {
            const WinRec& r = wins[i];
            const char* note;
            if (!r.visibleBefore)
                note = "  (was hidden before the freeze - we never touched it, not a click target)";
            else if (r.iconicBefore)
                note = "  (was already minimized by the user - left minimized, not ours to undo)";
            else if (r.iconicAfter)
                note = "  (iconic: parked off the interaction path - not a ghost exemption)";
            else
                note = "  (NOT iconic: the owner thread never pumped the minimize - this window is a stale visible click target)";
            Out("  hwnd 0x%08llX pid %-7lu IsIconic=%d IsWindowVisible=%d IsHungAppWindow=%d%s\n",
                (unsigned long long)(ULONG_PTR)r.hwnd, (unsigned long)r.pid,
                r.iconicAfter ? 1 : 0, r.visibleAfter ? 1 : 0, r.hungAfter ? 1 : 0, note);
        }
    }

    bool warned = CommitWarning(sp1, pf1);
    (void)warned;

    StateFile sf;
    sf.version = 2;
    sf.rootPid = p.pid;
    sf.image = p.imagePath;
    sf.suspendedAt = NowUnix();
    for (size_t i = 0; i < items.size(); ++i) {
        StateProc sp;
        sp.pid = items[i].p.pid;
        sp.createTime = items[i].p.createTime;
        sp.depth = items[i].p.depth;
        sp.suspendCalls = 1;
        sp.wsBefore = items[i].mb.workingSet;
        sp.wsAfter = items[i].ma.workingSet;
        sp.commit = items[i].mb.privateUsage;
        sp.exe = items[i].p.exeName;
        sp.alive = items[i].opened && items[i].sr.ok;
        if (items[i].opened) {
            for (size_t k = 0; k < items[i].sr.before.size(); ++k) {
                LONG base = items[i].sr.before[k].suspendCount;
                sp.threads.push_back(std::make_pair(items[i].sr.before[k].tid, base < 0 ? 0 : base));
            }
            sp.extraThreads = items[i].sr.extraSuspended;
        }
        sf.procs.push_back(sp);
    }
    for (size_t i = 0; i < wins.size(); ++i) {
        StateWin w;
        w.pid = wins[i].pid;
        w.hwnd = (unsigned long long)(ULONG_PTR)wins[i].hwnd;
        w.visible = wins[i].visibleBefore;
        w.iconic = wins[i].iconicBefore;
        w.minimizedByUs = wins[i].minimizedByUs;
        w.left = wins[i].rect.left;
        w.top = wins[i].rect.top;
        w.right = wins[i].rect.right;
        w.bottom = wins[i].rect.bottom;
        sf.wins.push_back(w);
    }

    std::string serr;
    bool stateOk = StateWrite(sf, serr);
    if (!stateOk) Log("freeze: state write failed: %s", serr.c_str());

    ULONGLONG totWsBefore = 0, totWsAfter = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].opened) continue;
        totWsBefore += items[i].mb.workingSet;
        totWsAfter += items[i].ma.workingSet;
    }

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "freeze");
        j.un("root_pid", p.pid);
        j.sw("exe", p.exeName);
        j.sw("image", p.imagePath);
        j.b("tree", !g_ctx.noTree);
        j.un("process_count", (unsigned long long)items.size());
        j.member("system_before", sp0);
        j.member("pagefile_before", pf0);
        j.un("tree_commit_before", treeCommit);
        j.obj("windows");
        j.un("count", (unsigned long long)wins.size());
        j.un("minimized", nMin);
        j.arr("list");
        for (size_t i = 0; i < wins.size(); ++i) {
            j.objElem();
            j.un("pid", wins[i].pid);
            j.un("hwnd", (unsigned long long)(ULONG_PTR)wins[i].hwnd);
            j.b("visible_before", wins[i].visibleBefore);
            j.b("iconic_before", wins[i].iconicBefore);
            j.b("minimized_by_us", wins[i].minimizedByUs);
            j.b("iconic_after", wins[i].iconicAfter);
            j.b("hung_after", wins[i].hungAfter);
            j.n("left", wins[i].rect.left);
            j.n("top", wins[i].rect.top);
            j.n("right", wins[i].rect.right);
            j.n("bottom", wins[i].rect.bottom);
            j.s("title", W2U(wins[i].title).c_str());
            j.endObj();
        }
        j.endArr();
        j.endObj();
        j.arr("processes");
        for (size_t i = 0; i < items.size(); ++i) {
            FreezeItem& it = items[i];
            j.objElem();
            j.un("pid", it.p.pid);
            j.n("depth", (long long)it.p.depth);
            j.sw("exe", it.p.exeName);
            j.b("opened", it.opened);
            j.b("late_child", it.late);
            if (!it.opened) j.un("open_error", it.openErr);
            j.member("before", it.mb);
            j.member("after", it.ma);
            j.b("trim_verified", it.trimOk);
            j.un("empty_working_set_ret", (unsigned long long)it.ewsRet);
            j.un("empty_working_set_win32", it.ewsErr);
            j.obj("suspend");
            j.s("nt_suspend_process", NtStatusText(it.sr.status).c_str());
            j.n("ntstatus", (long long)it.sr.status);
            j.un("threads_seen", it.sr.threadsSeen);
            j.un("threads_incremented", it.sr.threadsIncremented);
            j.un("threads_escaped", (unsigned long long)it.sr.escaped.size());
            j.un("threads_individually_suspended", (unsigned long long)it.sr.extraSuspended.size());
            j.endObj();
            j.endObj();
        }
        j.endArr();
        j.member("system_after", sp1);
        j.member("pagefile_after", pf1);
        j.obj("delta");
        j.n("working_set", D((long long)totWsBefore, (long long)totWsAfter));
        j.n("commit_total", D((long long)sp0.commitTotal, (long long)sp1.commitTotal));
        j.un("free_commit_after", sp1.freeCommit());
        j.endObj();
        j.obj("state");
        j.b("written", stateOk);
        j.s("path", W2U(StatePathFor(p.pid)).c_str());
        j.un("processes", (unsigned long long)sf.procs.size());
        j.un("windows", (unsigned long long)sf.wins.size());
        j.endObj();
        j.b("ok", suspFail == 0 && stateOk);
        j.endObj();
        Out("\n");
        Out("\n");
    } else {
        if (!g_ctx.quiet) {
            Out("\n-- STATE --\n");
            Out("  state file           : %s (%s)\n", W2U(StatePathFor(p.pid)).c_str(),
                stateOk ? "written" : "FAILED");
            Out("  recorded             : %u process(es), %u window(s)\n",
                (unsigned)sf.procs.size(), (unsigned)sf.wins.size());
        }
        Out("FREEZE: root pid %lu, %u/%u process(es) suspended, working set %s -> %s%s, free commit %s, exit %s\n",
            (unsigned long)p.pid, suspOk, (unsigned)suspOk + suspFail,
            Bytes(totWsBefore).c_str(), Bytes(totWsAfter).c_str(),
            (nMin ? "" : ""), Bytes(sp1.freeCommit()).c_str(),
            (suspFail || !stateOk) ? "INCOMPLETE" : "ok");
        if (!g_ctx.quiet) Out("  next                 : RamStasis.exe thaw %lu\n", (unsigned long)p.pid);
    }

    CloseItems(items);
    return (suspFail == 0 && stateOk) ? 0 : 6;
}

struct OrphanHit {
    DWORD pid = 0;
    std::wstring exe;
    unsigned threads = 0;
    unsigned long long resumed = 0;
    bool systemCritical = false;
    bool coveredByState = false;
    bool skipped = false;
    std::string skipWhy;
};

static bool IsSystemCriticalExe(const std::wstring& exe)
{
    static const wchar_t* kCrit[] = {
        L"explorer.exe", L"dwm.exe", L"csrss.exe", L"wininit.exe", L"lsass.exe",
        L"services.exe", L"svchost.exe", L"LockApp.exe", L"ShellExperienceHost.exe",
        L"RuntimeBroker.exe", L"backgroundTaskHost.exe", L"SearchHost.exe",
        L"StartMenuExperienceHost.exe", L"TextInputHost.exe", L"ApplicationFrameHost.exe",
        L"System", L"Registry", L"Memory Compression"
    };
    for (size_t i = 0; i < sizeof(kCrit) / sizeof(kCrit[0]); ++i) {
        size_t n = wcslen(kCrit[i]);
        if (exe.size() == n && _wcsnicmp(exe.c_str(), kCrit[i], n) == 0) return true;
    }
    return false;
}

int CmdThawOrphans()
{
    std::vector<DWORD> covered;
    {
        std::wstring pat = g_ctx.stateDir + L"\\frz-*.state";
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                std::wstring path = g_ctx.stateDir + L"\\" + fd.cFileName;
                FILE* f = nullptr;
                if (_wfopen_s(&f, path.c_str(), L"r") == 0 && f) {
                    char line[4096];
                    while (fgets(line, sizeof(line), f)) {
                        if (strncmp(line, "root ", 5) == 0)
                            covered.push_back((DWORD)strtoul(line + 5, nullptr, 10));
                        else if (strncmp(line, "proc ", 5) == 0)
                            covered.push_back((DWORD)strtoul(line + 5, nullptr, 10));
                    }
                    fclose(f);
                }
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
    }

    std::vector<OrphanHit> hits;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (pid == 0 || pid == 4) continue;
            std::vector<ThreadInfo> th = EnumThreads(pid, false);
            std::vector<std::pair<DWORD, LONG> > sus;
            for (size_t i = 0; i < th.size(); ++i)
                if (th[i].suspendCount > 0) sus.push_back(std::make_pair(th[i].tid, th[i].suspendCount));
            if (sus.empty()) continue;

            OrphanHit hit;
            hit.pid = pid;
            hit.exe = pe.szExeFile;
            hit.threads = (unsigned)sus.size();
            hit.systemCritical = IsSystemCriticalExe(hit.exe);
            for (size_t i = 0; i < covered.size(); ++i)
                if (covered[i] == pid) { hit.coveredByState = true; break; }

            if (hit.coveredByState) {
                hit.skipped = true;
                hit.skipWhy = "covered by a state file - use: RamStasis.exe thaw <pid>";
            } else if (hit.systemCritical && !g_ctx.force) {
                hit.skipped = true;
                hit.skipWhy = "system-critical - Windows may have suspended it itself (use --force to override)";
            } else {
                for (size_t i = 0; i < sus.size(); ++i) {
                    for (LONG k = 0; k < sus[i].second; ++k) {
                        NTSTATUS st = ResumeExtraThread(sus[i].first);
                        if (st == (NTSTATUS)0xC0000002L) break;
                        hit.resumed++;
                    }
                }
            }
            hits.push_back(hit);
        } while (Process32NextW(snap, &pe));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);

    unsigned long long totalResumed = 0;
    unsigned orphans = 0, skipped = 0;
    for (size_t i = 0; i < hits.size(); ++i) {
        totalResumed += hits[i].resumed;
        if (hits[i].skipped) skipped++; else orphans++;
    }

    if (!g_ctx.quiet) {
        Out("=== THAW ORPHANS (processes suspended with no state file) ===\n");
        Out("  state dir           : %s\n", W2U(g_ctx.stateDir).c_str());
        Out("  pids covered by state files: %u\n", (unsigned)covered.size());
        Out("  candidate processes : %u\n", (unsigned)hits.size());
        if (hits.empty()) {
            Out("\n  RESULT: nothing is suspended outside frz's own state files.\n");
        } else {
            Out("\n  PID      EXE                             THR  RESUMED  NOTE\n");
            for (size_t i = 0; i < hits.size(); ++i) {
                const OrphanHit& h = hits[i];
                Out("  %-7lu %-30s %-4u %-8llu %s\n", (unsigned long)h.pid,
                    W2U(h.exe).c_str(), h.threads, h.resumed,
                    h.skipped ? h.skipWhy.c_str() : "");
            }
            Out("\n  RESULT: %u orphan process(es) resumed, %llu thread-suspend(s) released, %u skipped.\n",
                orphans, totalResumed, skipped);
        }
    }

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "thaw_orphans");
        j.un("state_dir_covered_pids", (unsigned long long)covered.size());
        j.un("candidate_processes", (unsigned long long)hits.size());
        j.un("orphans_resumed", (unsigned long long)orphans);
        j.un("thread_suspends_released", totalResumed);
        j.un("skipped", (unsigned long long)skipped);
        j.arr("processes");
        for (size_t i = 0; i < hits.size(); ++i) {
            const OrphanHit& h = hits[i];
            j.objElem();
            j.un("pid", (unsigned long long)h.pid);
            j.sw("exe", h.exe);
            j.un("suspended_threads", (unsigned long long)h.threads);
            j.un("resumed", h.resumed);
            j.b("system_critical", h.systemCritical);
            j.b("skipped", h.skipped);
            j.s("note", h.skipWhy.c_str());
            j.endObj();
        }
        j.endArr();
        j.b("ok", true);
        j.end();
    }
    return 0;
}

int CmdThaw(const std::wstring& spec)
{
    ProcSnapshot named;
    std::string err;
    if (!ResolveTargetNoHandle(spec, named, err)) {
        Out("ERROR: %s\n", err.c_str());
        return 2;
    }

    StateFile st;
    if (!StateRead(named.pid, st, err)) {
        Out("REFUSING TO THAW: %s\n"
            "  frz will not guess how many times a process was suspended.\n"
            "  A missing state file means frz did not freeze it (or the file was deleted).\n",
            err.c_str());
        Log("thaw refused: %s", err.c_str());
        return 3;
    }

    if (named.pid == st.rootPid) {
        const StateProc* rp = nullptr;
        for (size_t i = 0; i < st.procs.size(); ++i)
            if (st.procs[i].pid == st.rootPid) { rp = &st.procs[i]; break; }
        if (rp && rp->createTime) {
            ULONGLONG ct = 0;
            bool have = GetProcCreateTime(st.rootPid, ct);
            if (have && ct != rp->createTime) {
                Out("REFUSING TO THAW: pid %lu creation time %llu does not match state file %llu "
                    "(the pid was reused by a different process).\n",
                    (unsigned long)st.rootPid, ct, rp->createTime);
                Log("thaw refused: pid reuse detected on root pid");
                return 3;
            }
        }
    }

    std::vector<StateProc> order = st.procs;
    std::stable_sort(order.begin(), order.end(),
                     [](const StateProc& a, const StateProc& b) { return a.depth < b.depth; });

    if (!g_ctx.quiet) {
        Out("=== THAW  root pid %lu ===\n", (unsigned long)st.rootPid);
        Out("  state file           : %s (format %d)\n", st.rawPath.c_str(), st.version);
        Out("  image                : %s\n", W2U(st.image).c_str());
        Out("  suspended_at         : %llu (unix)\n", (unsigned long long)st.suspendedAt);
        Out("  recorded procs/wins  : %u / %u\n",
            (unsigned)st.procs.size(), (unsigned)st.wins.size());
        Out("  thaw order           : root first, then children\n");
    }

    unsigned resumed = 0, skipped = 0, failedOpen = 0;
    unsigned mismatches = 0, verified = 0;
    ULONGLONG cpuBeforeRoot = 0;
    HANDLE rootHandle = nullptr;
    double wakeMs = -1.0, cpuLat = -1.0;
    bool msgOk = false, cpuOk = false;
    std::vector<std::string> winNotes;

    for (size_t i = 0; i < order.size(); ++i) {
        const StateProc& sp = order[i];
        if (!sp.alive) {
            skipped++;
            if (!g_ctx.quiet)
                Out("  pid %-7lu skipped: it had already exited when the freeze ran\n",
                    (unsigned long)sp.pid);
            continue;
        }
        ULONGLONG ct = 0;
        if (!GetProcCreateTime(sp.pid, ct)) {
            skipped++;
            if (!g_ctx.quiet)
                Out("  pid %-7lu skipped: process exited\n", (unsigned long)sp.pid);
            continue;
        }
        if (sp.createTime && ct != sp.createTime) {
            skipped++;
            if (!g_ctx.quiet)
                Out("  pid %-7lu skipped: pid reused by a different process (state create %llu, now %llu)\n",
                    (unsigned long)sp.pid, (unsigned long long)sp.createTime, ct);
            continue;
        }

        HANDLE h = nullptr;
        DWORD oe = OpenTarget(sp.pid, kFreezeAccess, h, err);
        if (oe != 0) {
            failedOpen++;
            Out("  pid %-7lu cannot open (%lu): %s\n", (unsigned long)sp.pid,
                (unsigned long)oe, err.c_str());
            continue;
        }

        std::vector<ThreadInfo> now = EnumThreads(sp.pid, false);
        for (size_t k = 0; k < sp.threads.size(); ++k) {
            DWORD tid = sp.threads[k].first;
            LONG base = sp.threads[k].second;
            for (size_t m = 0; m < now.size(); ++m) {
                if (now[m].tid != tid) continue;
                if (now[m].suspendCount < 0) break;
                if (now[m].suspendCount >= base + 1) verified++;
                else mismatches++;
                break;
            }
        }

        if (sp.pid == st.rootPid) {
            ULONGLONG k = 0, u = 0, c = 0;
            if (GetProcCpu(h, k, u, c)) cpuBeforeRoot = k + u;
            rootHandle = h;
        }

        NTSTATUS rs = DoResume(h);
        for (size_t k = 0; k < sp.extraThreads.size(); ++k) {
            NTSTATUS es = ResumeExtraThread(sp.extraThreads[k]);
            if (es != kStatusSuccess)
                Log("thaw: ResumeExtraThread(tid=%lu) -> 0x%08lX",
                    (unsigned long)sp.extraThreads[k], (unsigned long)es);
        }

        std::vector<WinRec> mine;
        for (size_t k = 0; k < st.wins.size(); ++k) {
            if (st.wins[k].pid != sp.pid) continue;
            WinRec r;
            r.pid = st.wins[k].pid;
            r.hwnd = (HWND)(ULONG_PTR)st.wins[k].hwnd;
            r.visibleBefore = st.wins[k].visible;
            r.iconicBefore = st.wins[k].iconic;
            r.minimizedByUs = st.wins[k].minimizedByUs;
            r.rect.left = st.wins[k].left;
            r.rect.top = st.wins[k].top;
            r.rect.right = st.wins[k].right;
            r.rect.bottom = st.wins[k].bottom;
            mine.push_back(r);
        }
        RestoreWindows(mine, winNotes);

        Sleep(60);
        std::vector<ThreadInfo> after = EnumThreads(sp.pid, false);
        unsigned still = 0;
        for (size_t k = 0; k < sp.threads.size(); ++k) {
            for (size_t m = 0; m < after.size(); ++m) {
                if (after[m].tid != sp.threads[k].first) continue;
                if (after[m].suspendCount > sp.threads[k].second) still++;
                break;
            }
        }

        if (!g_ctx.quiet) {
            Out("  resume pid %-7lu %-22s threads %3u verified %3u, still suspended above baseline: %u%s\n",
                (unsigned long)sp.pid, NtStatusText(rs).c_str(),
                (unsigned)sp.threads.size(), verified, still,
                still ? "  <- NOT BALANCED" : "");
        }
        if (rs != kStatusSuccess || still != 0) {
            CloseHandle(h);
            Out("\nERROR: thaw incomplete for pid %lu (%s, %u thread(s) still suspended above baseline).\n"
                "       State file kept so you can retry.\n",
                (unsigned long)sp.pid, NtStatusText(rs).c_str(), still);
            return 6;
        }
        resumed++;
        if (h != rootHandle) CloseHandle(h);
    }

    if (rootHandle) {
        wakeMs = MeasureWakeMs(rootHandle, st.rootPid, cpuBeforeRoot, 5000.0, cpuLat, msgOk, cpuOk);
        CloseHandle(rootHandle);
        rootHandle = nullptr;
    }

    if (!g_ctx.quiet) {
        Out("\n-- WAKE --\n");
        if (msgOk) Out("  message round-trip    : %.1f ms  (SendMessageTimeout WM_NULL to the root)\n", wakeMs);
        else       Out("  message round-trip    : no window answered within 5000 ms\n");
        if (cpuOk) Out("  CPU time advanced at  : %.1f ms\n", cpuLat);
        Out("\n-- WINDOWS RESTORED --\n");
        if (winNotes.empty()) Out("  (no windows were recorded)\n");
        for (size_t i = 0; i < winNotes.size(); ++i) Out("  %s\n", winNotes[i].c_str());
        Out("\n-- VERIFY --\n");
        Out("  suspend counts balanced : %u thread(s) verified baseline+1 -> baseline\n", verified);
        Out("  count mismatches        : %u\n", mismatches);
        Out("  procs resumed/skipped   : %u / %u%s\n", resumed, skipped,
            failedOpen ? "  (plus open failures)" : "");
    }

    bool ok = (resumed > 0);
    if (ok) {
        std::string derr;
        if (!StateDelete(st.rootPid, derr)) {
            Out("NOTE: thaw succeeded but the state file could not be deleted: %s\n", derr.c_str());
        }
    }

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "thaw");
        j.un("root_pid", st.rootPid);
        j.b("state_file_found", true);
        j.s("state_file", st.rawPath.c_str());
        j.n("state_format", (long long)st.version);
        j.un("processes_resumed", resumed);
        j.un("processes_skipped", skipped);
        j.un("threads_verified", verified);
        j.un("thread_count_mismatches", mismatches);
        j.b("message_ok", msgOk);
        j.f("wake_latency_ms", wakeMs, 1);
        j.f("cpu_latency_ms", cpuLat, 1);
        j.arr("window_notes");
        for (size_t i = 0; i < winNotes.size(); ++i) j.s(nullptr, winNotes[i].c_str());
        j.endArr();
        j.b("ok", ok);
        j.endObj();
        Out("\n");
        Out("\n");
    } else {
        Out("THAW: %u process(es) resumed, %u skipped, exit %s\n", resumed, skipped,
            ok ? "ok" : "FAILED");
    }

    return ok ? 0 : 6;
}

int CmdStats(const std::wstring& spec)
{
    ProcSnapshot p;
    std::string err;
    if (!PickOne(spec, p, err)) { Out("ERROR: %s\n", err.c_str()); return 2; }

    HANDLE h = nullptr;
    if (OpenTarget(p.pid, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, h, err) != 0) {
        Out("ERROR: %s\n", err.c_str());
        return 4;
    }

    MemNumbers m;
    SysPerf sp;
    PageFileNumbers pf;
    GetMem(h, m);
    GetSysPerf(sp);
    GetPageFile(pf);
    std::vector<ThreadInfo> th = EnumThreads(p.pid, true);
    std::vector<HWND> wins;
    unsigned nwin = CountTargetWindows(p.pid, &wins);

    unsigned susp = 0, unq = 0;
    for (size_t i = 0; i < th.size(); ++i) {
        if (th[i].suspendCount < 0) unq++;
        else if (th[i].suspendCount > 0) susp++;
    }

    ULONGLONG ct = 0;
    GetProcCreateTime(p.pid, ct);

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "stats");
        j.un("pid", p.pid);
        j.sw("exe", p.exeName);
        j.sw("image", p.imagePath);
        j.un("create_time", ct);
        j.un("thread_count", (unsigned long long)th.size());
        j.un("threads_with_suspend_count_gt0", susp);
        j.un("threads_unqueryable", unq);
        j.un("window_count", nwin);
        j.member("process", m);
        j.member("system", sp);
        j.member("pagefile", pf);
        j.un("free_commit", sp.freeCommit());
        j.arr("threads");
        for (size_t i = 0; i < th.size(); ++i) {
            j.objElem();
            j.un("tid", th[i].tid);
            j.n("suspend_count", (long long)th[i].suspendCount);
            j.n("priority", (long long)th[i].priority);
            j.un("kernel_100ns", th[i].kernelTime);
            j.un("user_100ns", th[i].userTime);
            j.b("start_address_ok", th[i].startOk);
            if (th[i].startOk) j.s("start_address", Hex(th[i].startAddress).c_str());
            if (!th[i].opened) j.n("open_error", (long long)th[i].openErr);
            j.endObj();
        }
        j.endArr();
        j.endObj();
        Out("\n");
    } else {
        Out("=== STATS  pid %lu  %s ===\n", (unsigned long)p.pid, W2U(p.exeName).c_str());
        Out("  image                : %s\n", W2U(p.imagePath).c_str());
        Out("  created (FILETIME)   : %llu\n", ct);
        Out("  top-level windows    : %u\n", nwin);
        Out("  threads              : %u  (%u with suspend count > 0, %u unqueryable)\n",
            (unsigned)th.size(), susp, unq);
        Out("  state                : %s\n",
            (th.size() && susp == th.size()) ? "SUSPENDED (all threads)" :
            (susp ? "PARTIALLY SUSPENDED" : "running"));
        PrintMemTable("process", m);
        PrintSysTable("system", sp, pf);
        if (!g_ctx.quiet) {
            Out("\n  %-8s %-6s %-10s %-12s %-12s %s\n", "TID", "SUSP", "PRIO", "CPU(k+u) ms", "START", "NOTE");
            for (size_t i = 0; i < th.size(); ++i) {
                char cpu[64];
                double ms = (double)(th[i].kernelTime + th[i].userTime) / 10000.0;
                sprintf_s(cpu, sizeof(cpu), "%.1f", ms);
                Out("  %-8lu %-6ld %-10ld %-12s %-12s %s\n",
                    (unsigned long)th[i].tid, (long)th[i].suspendCount, (long)th[i].priority, cpu,
                    th[i].startOk ? Hex(th[i].startAddress).c_str() : "-",
                    th[i].opened ? "" : "open failed");
            }
        }
    }

    CloseHandle(h);
    return 0;
}

struct ListRow {
    DWORD pid;
    std::wstring exe;
    MemNumbers mem;
    int threads;
    int suspendedThreads;
    int unq;
    bool opened;
    DWORD openErr;
    std::string suspState;
};

static bool CmpWsDesc(const ListRow& x, const ListRow& y)
{
    return x.mem.workingSet > y.mem.workingSet;
}

int CmdList()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Out("ERROR: CreateToolhelp32Snapshot failed, win32=%lu\n", (unsigned long)GetLastError());
        return 2;
    }

    std::vector<ListRow> rows;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ListRow r;
            r.pid = pe.th32ProcessID;
            r.exe = pe.szExeFile;
            r.threads = (int)pe.cntThreads;
            r.suspendedThreads = 0;
            r.unq = 0;
            r.opened = false;
            r.openErr = 0;
            r.suspState = "?";

            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, r.pid);
            if (!h) {
                r.openErr = GetLastError();
                h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid);
            }
            if (h) {
                r.opened = true;
                if (!GetMem(h, r.mem)) r.mem.ok = false;

                std::vector<ThreadInfo> th = EnumThreads(r.pid, false);
                for (size_t i = 0; i < th.size(); ++i) {
                    if (th[i].suspendCount < 0) r.unq++;
                    else if (th[i].suspendCount > 0) r.suspendedThreads++;
                }
                if (th.empty()) r.suspState = "?";
                else if (r.suspendedThreads == (int)th.size()) r.suspState = "SUSPENDED";
                else if (r.suspendedThreads > 0) r.suspState = "partial";
                else r.suspState = "running";
                CloseHandle(h);
            } else {
                r.suspState = "denied";
            }
            rows.push_back(r);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (!rows.empty())
        std::sort(rows.begin(), rows.end(), CmpWsDesc);

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "list");
        j.un("process_count", (unsigned long long)rows.size());
        j.arr("processes");
        for (size_t i = 0; i < rows.size(); ++i) {
            j.objElem();
            j.un("pid", rows[i].pid);
            j.sw("exe", rows[i].exe);
            j.un("threads", (unsigned long long)rows[i].threads);
            j.un("threads_suspended", (unsigned long long)rows[i].suspendedThreads);
            j.s("suspend_state", rows[i].suspState.c_str());
            j.member("memory", rows[i].mem);
            if (!rows[i].opened) j.n("open_error", (long long)rows[i].openErr);
            j.endObj();
        }
        j.endArr();
        j.endObj();
        Out("\n");
    } else {
        Out("%-7s %-28s %14s %14s %14s %6s %-10s\n",
            "PID", "EXE", "WORKING_SET", "PRIVATE", "PAGEFILE", "THR", "SUSPEND");
        Out("%-7s %-28s %14s %14s %14s %6s %-10s\n",
            "-------", "----------------------------", "--------------",
            "--------------", "--------------", "------", "----------");
        unsigned long long totWs = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            std::string name = W2U(rows[i].exe);
            if (name.size() > 28) name = name.substr(0, 27) + "~";
            if (rows[i].mem.ok) {
                Out("%-7lu %-28s %14llu %14llu %14llu %6d %-10s\n",
                    (unsigned long)rows[i].pid, name.c_str(),
                    rows[i].mem.workingSet, rows[i].mem.privateUsage, rows[i].mem.pagefileUsage,
                    rows[i].threads, rows[i].suspState.c_str());
                totWs += rows[i].mem.workingSet;
            } else {
                Out("%-7lu %-28s %14s %14s %14s %6d %-10s\n",
                    (unsigned long)rows[i].pid, name.c_str(), "-", "-", "-",
                    rows[i].threads, rows[i].suspState.c_str());
            }
        }
        Out("\n%llu processes, total working set %s\n", (unsigned long long)rows.size(),
            Bytes(totWs).c_str());
        Out("PRIVATE is the process commit charge (PROCESS_MEMORY_COUNTERS_EX.PrivateUsage);\n"
            "PAGEFILE is PagefileUsage. Commit is expected to stay flat while suspended.\n");
    }
    return 0;
}
