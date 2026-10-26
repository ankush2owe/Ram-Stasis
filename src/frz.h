// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef FRZ_NTSTATUS_DEFINED
#define FRZ_NTSTATUS_DEFINED
typedef LONG NTSTATUS;
typedef NTSTATUS* PNTSTATUS;
#endif

extern "C" NTSTATUS NTAPI NtQueryInformationThread(HANDLE ThreadHandle,
                                                  ULONG ThreadInformationClass,
                                                  PVOID ThreadInformation,
                                                  ULONG ThreadInformationLength,
                                                  PULONG ReturnLength);
extern "C" NTSTATUS NTAPI NtQuerySystemInformation(ULONG SystemInformationClass,
                                                  PVOID SystemInformation,
                                                  ULONG SystemInformationLength,
                                                  PULONG ReturnLength);
extern "C" ULONG NTAPI RtlNtStatusToDosError(NTSTATUS Status);

constexpr ULONG kThreadBasicInformation          = 0;
constexpr ULONG kThreadQuerySetWin32StartAddress = 9;
constexpr ULONG kThreadSuspendCount              = 35;

constexpr ULONG kSystemModuleInformation         = 11;

constexpr NTSTATUS kStatusSuccess          = 0x00000000L;
constexpr NTSTATUS kStatusInfoLengthMismatch = 0xC0000004L;

struct FrzClientId {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
};

struct FrzThreadBasicInformation {
    NTSTATUS ExitStatus;
    PVOID    TebBaseAddress;
    FrzClientId ClientId;
    ULONG_PTR AffinityMask;
    LONG     Priority;
    LONG     BasePriority;
};

struct MemNumbers {
    bool  ok = false;
    DWORD err = 0;
    ULONGLONG workingSet = 0;
    ULONGLONG peakWorkingSet = 0;
    ULONGLONG privateUsage = 0;
    ULONGLONG pagefileUsage = 0;
    ULONGLONG peakPagefileUsage = 0;
    ULONGLONG quotaPaged = 0;
    ULONGLONG quotaNonPaged = 0;
    ULONGLONG pageFaultCount = 0;
};

struct SysPerf {
    bool ok = false;
    DWORD err = 0;
    ULONGLONG commitTotal = 0;
    ULONGLONG commitLimit = 0;
    ULONGLONG commitPeak = 0;
    ULONGLONG physicalTotal = 0;
    ULONGLONG physicalAvailable = 0;
    ULONGLONG systemCache = 0;
    ULONGLONG kernelTotal = 0;
    ULONGLONG kernelPaged = 0;
    ULONGLONG kernelNonpaged = 0;
    ULONGLONG pageSize = 0;
    DWORD handleCount = 0;
    DWORD processCount = 0;
    DWORD threadCount = 0;

    ULONGLONG freeCommit() const { return commitLimit > commitTotal ? commitLimit - commitTotal : 0; }
};

struct PageFileNumbers {
    bool ok = false;
    DWORD win32err = 0;
    ULONGLONG totalSizeBytes = 0;
    ULONGLONG totalInUseBytes = 0;
    ULONGLONG peakUsageBytes = 0;
    ULONGLONG totalSizePages = 0;
    ULONGLONG totalInUsePages = 0;
    unsigned fileCount = 0;
    std::vector<std::wstring> names;
};

struct ThreadInfo {
    DWORD tid = 0;
    bool  opened = false;
    DWORD openErr = 0;
    LONG  suspendCount = -1;
    NTSTATUS suspendStatus = 0;
    ULONGLONG kernelTime = 0;
    ULONGLONG userTime = 0;
    LONG  priority = 0;
    ULONG_PTR startAddress = 0;
    bool  startOk = false;
    DWORD waitReason = 0xFFFFFFFF;
};

struct ProcSnapshot {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring exeName;
    std::wstring imagePath;
    ULONGLONG createTime = 0;
    int depth = 0;
    bool alive = true;
    DWORD openErr = 0;
};

struct WinRec {
    DWORD  pid = 0;
    HWND   hwnd = nullptr;
    bool   visibleBefore = false;
    bool   iconicBefore = false;
    bool   minimizedByUs = false;
    bool   iconicAfter = false;
    bool   visibleAfter = false;
    bool   hungAfter = false;
    int    showAsyncRet = -1;
    DWORD  showAsyncErr = 0;
    RECT   rect = { 0, 0, 0, 0 };
    std::wstring title;
};

struct Jw {
    std::vector<char> kind;
    std::vector<bool> first;

    void begin(char k);
    void end();
    void sep();
    void key(const char* k);
    void obj(const char* k);
    void arr(const char* k);
    void objElem();
    void endObj();
    void endArr();
    void n(const char* k, long long v);
    void un(const char* k, unsigned long long v);
    void f(const char* k, double v, int prec = 3);
    void s(const char* k, const char* v);
    void sw(const char* k, const std::wstring& v);
    void b(const char* k, bool v);
    void nullv(const char* k);
    void member(const char* k, const MemNumbers& m);
    void member(const char* k, const SysPerf& p);
    void member(const char* k, const PageFileNumbers& p);
};

struct Ctx {
    bool json = false;
    bool verbose = false;
    bool all = false;
    bool force = false;
    bool noTree = false;
    bool quiet = false;
    std::wstring exeDir;
    std::wstring stateDir;
    std::wstring logPath;
    int  timeoutMs = 120000;
    bool orphans = false;
};

extern Ctx g_ctx;

void LogInit(const wchar_t* exePath);
void Log(const char* fmt, ...);
void Out(const char* fmt, ...);
void Err(const char* fmt, ...);
void SetCmd(const char* name);
std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);
std::string Comma(unsigned long long v);
std::string Bytes(unsigned long long v);
std::string Hex(unsigned long long v);
std::wstring ExeDirOf(const wchar_t* path);
ULONGLONG NowUnix();
ULONGLONG QpcMs();
ULONGLONG FileTimeToU64(const FILETIME& ft);
std::string TimeStampNow();
bool IsElevated();
bool FrzTryEnableDebugPrivilege();

bool GetMem(HANDLE h, MemNumbers& out);
bool GetSysPerf(SysPerf& out);
bool GetPageFile(PageFileNumbers& out);
std::vector<ThreadInfo> EnumThreads(DWORD pid, bool withTimes);
bool QueryThreadSuspendCount(HANDLE hThread, LONG& count, NTSTATUS& st);
HANDLE OpenThreadForQuery(DWORD tid, DWORD& err);
bool GetProcCpu(HANDLE h, ULONGLONG& kernel, ULONGLONG& user, ULONGLONG& create);
double MeasureWakeMs(HANDLE h, DWORD pid, ULONGLONG cpuBefore, double timeoutMs,
                     double& cpuLatencyMs, bool& msgOk, bool& cpuOk);
int  CountThreads(DWORD pid);
bool GetProcCreateTime(DWORD pid, ULONGLONG& out);
HWND FindTargetWindow(DWORD pid, bool requireVisible);
unsigned CountTargetWindows(DWORD pid, std::vector<HWND>* out);
void PrintMemTable(const char* label, const MemNumbers& m);
void PrintSysTable(const char* label, const SysPerf& p, const PageFileNumbers& pf);

struct ProcRow {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring exe;
    DWORD threads = 0;
};

bool SnapshotProcesses(std::vector<ProcRow>& out);
std::vector<ProcSnapshot> BuildTree(DWORD rootPid, bool includeRoot);
void OrderChildrenFirst(std::vector<ProcSnapshot>& v);
void OrderRootFirst(std::vector<ProcSnapshot>& v);
bool ProcMatchesCreateTime(DWORD pid, ULONGLONG createTime);
int  CmdTree(const std::wstring& spec);
int  CmdThawOrphans();

struct AcHit {
    std::string component;
    std::string family;
    std::string source;
    std::string where;
};

bool AcMatchName(const std::string& name, bool kernelMedia, std::string& component, std::string& family);
bool GuardCheckTree(DWORD rootPid, std::vector<AcHit>& hits,
                    unsigned& procsScanned, unsigned& modsScanned);
bool GuardCheckKernel(std::vector<AcHit>& hits, unsigned& driversScanned);
bool GuardCheckSystem(std::vector<AcHit>& hits, unsigned& procsScanned, unsigned& modsScanned,
                      unsigned& driversScanned);
bool GuardBlocks(const std::vector<AcHit>& hits, bool force);
void PrintGuardHits(const std::vector<AcHit>& hits);
int  CmdGuard();

std::vector<HWND> TopLevelWindows(DWORD pid);
unsigned MinimizeWindowsForPids(const std::vector<DWORD>& pids, std::vector<WinRec>& out);
bool VerifyMinimized(std::vector<WinRec>& recs, int timeoutMs);
void RestoreWindows(const std::vector<WinRec>& recs, std::vector<std::string>& notes);
void PrintWindowTable(const std::vector<WinRec>& recs);

struct StateProc {
    DWORD pid = 0;
    ULONGLONG createTime = 0;
    int depth = 0;
    unsigned suspendCalls = 1;
    ULONGLONG wsBefore = 0;
    ULONGLONG wsAfter = 0;
    ULONGLONG commit = 0;
    std::wstring exe;
    std::vector<std::pair<DWORD, LONG> > threads;
    std::vector<DWORD> extraThreads;
    bool alive = true;
};

struct StateWin {
    DWORD pid = 0;
    unsigned long long hwnd = 0;
    bool visible = false;
    bool iconic = false;
    bool minimizedByUs = false;
    long left = 0, top = 0, right = 0, bottom = 0;
};

struct StateFile {
    bool exists = false;
    int  version = 0;
    DWORD rootPid = 0;
    std::wstring image;
    ULONGLONG suspendedAt = 0;
    std::vector<StateProc> procs;
    std::vector<StateWin> wins;
    std::string rawPath;
};

std::wstring StatePathFor(DWORD pid);
bool StateWrite(const StateFile& sf, std::string& err);
bool StateRead(DWORD pid, StateFile& out, std::string& err);
bool StateDelete(DWORD pid, std::string& err);
bool StateFindTreeOverlap(const std::vector<DWORD>& pids, DWORD& pidHit, std::string& path);

struct SuspendResult {
    NTSTATUS status = (NTSTATUS)0x80000000L;
    bool ok = false;
    unsigned threadsSeen = 0;
    unsigned threadsIncremented = 0;
    unsigned threadsUnqueryable = 0;
    std::vector<ThreadInfo> before;
    std::vector<ThreadInfo> after;
    std::vector<DWORD> escaped;
    std::vector<DWORD> extraSuspended;
};

typedef NTSTATUS(NTAPI* PFN_NtSuspendProcess)(HANDLE);
typedef NTSTATUS(NTAPI* PFN_NtResumeProcess)(HANDLE);
typedef NTSTATUS(NTAPI* PFN_NtSuspendThread)(HANDLE, PULONG);
typedef NTSTATUS(NTAPI* PFN_NtResumeThread)(HANDLE, PULONG);

bool NtProc(PFN_NtSuspendProcess* suspend, PFN_NtResumeProcess* resume, std::string& err);
bool NtThreadProc(PFN_NtSuspendThread* suspend, PFN_NtResumeThread* resume, std::string& err);
std::string NtStatusText(NTSTATUS st);
bool DoSuspend(HANDLE h, DWORD pid, SuspendResult& r);
NTSTATUS DoResume(HANDLE h);
NTSTATUS ResumeExtraThread(DWORD tid);

DWORD OpenTarget(DWORD pid, DWORD access, HANDLE& h, std::string& err);

int CmdFreeze(const std::wstring& spec);
int CmdThaw(const std::wstring& spec);
int CmdStats(const std::wstring& spec);
int CmdList();
int CmdWatch(const std::wstring& spec);
int CmdSelftest();

// engine entry point inside the merged RamStasis.exe.
// FrzEngineCliRequest() is true when the process was started with an engine
// command as the first argument (list/tree/guard/freeze/thaw/watch/stats/
// selftest/help/__child/__tree/__win); wWinMain then runs FrzEngineMain()
// instead of the GUI.
int FrzEngineMain();
bool FrzEngineCliRequest();

bool ResolveTarget(const std::wstring& spec, std::vector<ProcSnapshot>& out, std::string& err);
bool PickOne(const std::wstring& spec, ProcSnapshot& out, std::string& err);
bool ResolveTargetNoHandle(const std::wstring& spec, ProcSnapshot& out, std::string& err);

int ChildMain(const std::wstring& shmName);
int ChildTreeMain(const std::wstring& shmName, int leafCount, int slot);
int ChildWinMain(const std::wstring& shmName);
