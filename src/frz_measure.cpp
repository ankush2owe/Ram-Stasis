// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

bool GetMem(HANDLE h, MemNumbers& out)
{
    out = MemNumbers();
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        out.err = GetLastError();
        return false;
    }
    out.ok = true;
    out.workingSet = (ULONGLONG)pmc.WorkingSetSize;
    out.peakWorkingSet = (ULONGLONG)pmc.PeakWorkingSetSize;
    out.privateUsage = (ULONGLONG)pmc.PrivateUsage;
    out.pagefileUsage = (ULONGLONG)pmc.PagefileUsage;
    out.peakPagefileUsage = (ULONGLONG)pmc.PeakPagefileUsage;
    out.quotaPaged = (ULONGLONG)pmc.QuotaPagedPoolUsage;
    out.quotaNonPaged = (ULONGLONG)pmc.QuotaNonPagedPoolUsage;
    out.pageFaultCount = (ULONGLONG)pmc.PageFaultCount;
    return true;
}

bool GetSysPerf(SysPerf& out)
{
    out = SysPerf();
    PERFORMANCE_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    pi.cb = sizeof(pi);
    SetLastError(0);
    if (!GetPerformanceInfo(&pi, sizeof(pi))) {
        out.err = GetLastError();
        return false;
    }
    out.pageSize = (ULONGLONG)pi.PageSize;
    if (out.pageSize == 0) out.pageSize = 4096;
    out.commitTotal = (ULONGLONG)pi.CommitTotal * out.pageSize;
    out.commitLimit = (ULONGLONG)pi.CommitLimit * out.pageSize;
    out.commitPeak = (ULONGLONG)pi.CommitPeak * out.pageSize;
    out.physicalTotal = (ULONGLONG)pi.PhysicalTotal * out.pageSize;
    out.physicalAvailable = (ULONGLONG)pi.PhysicalAvailable * out.pageSize;
    out.systemCache = (ULONGLONG)pi.SystemCache * out.pageSize;
    out.kernelTotal = (ULONGLONG)pi.KernelTotal * out.pageSize;
    out.kernelPaged = (ULONGLONG)pi.KernelPaged * out.pageSize;
    out.kernelNonpaged = (ULONGLONG)pi.KernelNonpaged * out.pageSize;
    out.handleCount = pi.HandleCount;
    out.processCount = pi.ProcessCount;
    out.threadCount = pi.ThreadCount;
    out.ok = true;
    return true;
}

namespace {
struct PfCtx {
    PageFileNumbers* out;
};

BOOL CALLBACK PfCallback(LPVOID ctx, PENUM_PAGE_FILE_INFORMATION info, LPCWSTR name)
{
    PfCtx* c = (PfCtx*)ctx;
    if (!c || !c->out || !info) return TRUE;
    ULONGLONG page = 4096;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwPageSize) page = si.dwPageSize;

    c->out->totalSizePages += (ULONGLONG)info->TotalSize;
    c->out->totalInUsePages += (ULONGLONG)info->TotalInUse;
    c->out->totalSizeBytes += (ULONGLONG)info->TotalSize * page;
    c->out->totalInUseBytes += (ULONGLONG)info->TotalInUse * page;
    if ((ULONGLONG)info->PeakUsage * page > c->out->peakUsageBytes)
        c->out->peakUsageBytes = (ULONGLONG)info->PeakUsage * page;
    if (name && *name) c->out->names.push_back(name);
    c->out->fileCount++;
    return TRUE;
}
}

bool GetPageFile(PageFileNumbers& out)
{
    out = PageFileNumbers();
    PfCtx c;
    c.out = &out;
    SetLastError(0);
    if (!EnumPageFilesW(PfCallback, &c)) {
        out.win32err = GetLastError();
        return false;
    }
    out.ok = (out.fileCount > 0);
    return out.ok;
}

bool QueryThreadSuspendCount(HANDLE hThread, LONG& count, NTSTATUS& st)
{
    ULONG v = 0;
    st = NtQueryInformationThread(hThread, kThreadSuspendCount, &v, sizeof(v), nullptr);
    if (st != kStatusSuccess) { count = -1; return false; }
    count = (LONG)v;
    return true;
}

HANDLE OpenThreadForQuery(DWORD tid, DWORD& err)
{
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (h) { err = 0; return h; }
    DWORD e1 = GetLastError();
    h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (h) { err = 0; return h; }
    err = (e1 != 0) ? e1 : GetLastError();
    return nullptr;
}

std::vector<ThreadInfo> EnumThreads(DWORD pid, bool withTimes)
{
    std::vector<ThreadInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            ThreadInfo ti;
            ti.tid = te.th32ThreadID;
            DWORD err = 0;
            HANDLE h = OpenThreadForQuery(ti.tid, err);
            if (!h) {
                ti.openErr = err;
            } else {
                ti.opened = true;
                NTSTATUS st = 0;
                QueryThreadSuspendCount(h, ti.suspendCount, st);
                ti.suspendStatus = st;
                ULONG_PTR start = 0;
                ULONG rl = 0;
                if (NtQueryInformationThread(h, kThreadQuerySetWin32StartAddress,
                                             &start, sizeof(start), &rl) == kStatusSuccess) {
                    ti.startAddress = start;
                    ti.startOk = true;
                }
                if (withTimes) {
                    FILETIME c, e, k, u;
                    if (GetThreadTimes(h, &c, &e, &k, &u)) {
                        ti.kernelTime = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime);
                        ti.userTime = ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
                    }
                    ti.priority = GetThreadPriority(h);
                }
                CloseHandle(h);
            }
            out.push_back(ti);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return out;
}

bool GetProcCpu(HANDLE h, ULONGLONG& kernel, ULONGLONG& user, ULONGLONG& create)
{
    FILETIME c, e, k, u;
    if (!GetProcessTimes(h, &c, &e, &k, &u)) return false;
    create = FileTimeToU64(c);
    kernel = ((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime;
    user = ((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime;
    return true;
}

int CountThreads(DWORD pid)
{
    return (int)EnumThreads(pid, false).size();
}

struct EnumCtx {
    DWORD pid;
    std::vector<HWND>* out;
};

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    EnumCtx* c = (EnumCtx*)lp;
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == c->pid) c->out->push_back(hwnd);
    return TRUE;
}

unsigned CountTargetWindows(DWORD pid, std::vector<HWND>* out)
{
    std::vector<HWND> found;
    EnumCtx c;
    c.pid = pid;
    c.out = &found;
    EnumWindows(EnumProc, (LPARAM)&c);
    if (out) *out = found;
    return (unsigned)found.size();
}

HWND FindTargetWindow(DWORD pid, bool requireVisible)
{
    std::vector<HWND> all;
    CountTargetWindows(pid, &all);
    HWND fallback = nullptr;
    for (size_t i = 0; i < all.size(); ++i) {
        if (!IsWindow(all[i])) continue;
        if (!fallback) fallback = all[i];
        bool vis = IsWindowVisible(all[i]) != FALSE;
        if (requireVisible && !vis) continue;
        if (vis) return all[i];
    }
    return requireVisible ? nullptr : fallback;
}

void PrintMemTable(const char* label, const MemNumbers& m)
{
    if (g_ctx.quiet) return;
    Out("  %s\n", label);
    if (!m.ok) {
        Out("    GetProcessMemoryInfo FAILED, win32=%lu (%s)\n",
            (unsigned long)m.err, "GetLastError");
        return;
    }
    Out("    WorkingSetSize       : %s\n", Bytes(m.workingSet).c_str());
    Out("    PeakWorkingSetSize   : %s\n", Bytes(m.peakWorkingSet).c_str());
    Out("    PrivateUsage (commit): %s\n", Bytes(m.privateUsage).c_str());
    Out("    PagefileUsage        : %s\n", Bytes(m.pagefileUsage).c_str());
    Out("    PeakPagefileUsage    : %s\n", Bytes(m.peakPagefileUsage).c_str());
    Out("    QuotaPaged/NonPaged  : %s / %s\n", Bytes(m.quotaPaged).c_str(), Bytes(m.quotaNonPaged).c_str());
    Out("    PageFaultCount       : %s\n", Comma(m.pageFaultCount).c_str());
}

void PrintSysTable(const char* label, const SysPerf& p, const PageFileNumbers& pf)
{
    if (g_ctx.quiet) return;
    Out("  %s\n", label);
    if (!p.ok) {
        Out("    GetPerformanceInfo FAILED, win32=%lu (psapi.h:551, cb=%llu)\n",
            (unsigned long)p.err, (unsigned long long)sizeof(PERFORMANCE_INFORMATION));
    } else {
        Out("    CommitTotal           : %s\n", Bytes(p.commitTotal).c_str());
        Out("    CommitLimit           : %s\n", Bytes(p.commitLimit).c_str());
        Out("    Free commit           : %s   (CommitLimit - CommitTotal)\n", Bytes(p.freeCommit()).c_str());
        Out("    CommitPeak            : %s\n", Bytes(p.commitPeak).c_str());
        Out("    PhysicalTotal         : %s\n", Bytes(p.physicalTotal).c_str());
        Out("    PhysicalAvailable     : %s\n", Bytes(p.physicalAvailable).c_str());
        Out("    SystemCache           : %s\n", Bytes(p.systemCache).c_str());
        Out("    KernelTotal           : %s\n", Bytes(p.kernelTotal).c_str());
        Out("    PageSize              : %s\n", Comma(p.pageSize).c_str());
        Out("    Handles/Procs/Threads : %lu / %lu / %lu\n",
            (unsigned long)p.handleCount, (unsigned long)p.processCount, (unsigned long)p.threadCount);
    }
    if (pf.ok) {
        Out("    Pagefile total size   : %s  (%u file(s))\n", Bytes(pf.totalSizeBytes).c_str(), pf.fileCount);
        Out("    Pagefile IN USE       : %s\n", Bytes(pf.totalInUseBytes).c_str());
        Out("    Pagefile peak usage   : %s\n", Bytes(pf.peakUsageBytes).c_str());
        for (size_t i = 0; i < pf.names.size(); ++i)
            Out("    Pagefile file         : %s\n", W2U(pf.names[i]).c_str());
    } else {
        Out("    Pagefile (psapi!EnumPageFilesW) : FAILED win32=%lu\n", (unsigned long)pf.win32err);
    }
}
