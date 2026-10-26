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
#include <shellapi.h>
#include <psapi.h>

#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

struct ID3D11ShaderResourceView;

#define IDI_RAMSTASIS 101
#define IDR_RAMSTASIS_LOGO 102

HICON LoadRamStasisAppIcon(HINSTANCE hInst, int sizePx);
HICON CreateRamStasisIcon(int sizePx, bool hasFrozenApps);

bool IsElevatedUi();
bool TryEnableDebugPrivilege();

struct AppConfig {
    bool defaultFreezeTree = true;
    bool defaultAutoThawOnFocus = true;
    bool watchForegroundWake = true;
    bool warnOnLowCommit = true;
    int  lowCommitWarnPct = 15;
    bool guardAntiCheat = true;
    bool minimizeToTrayOnClose = false;
    UINT swapHotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT swapHotkeyVk = 'S';
};

extern AppConfig g_cfg;
void LoadAppConfig();
void SaveAppConfig();

struct EngineResult {
    long exitCode = -1;
    long spawnErr = 0;   // win32 GetLastError() when the engine process could not be created at all
    std::string out;
    std::string err;
    bool ran = false;
    std::string merged() const { return out.empty() ? err : (out + "\n" + err); }
};

EngineResult RunEngine(const wchar_t* args);
std::wstring EngineExePath();
std::wstring EngineStateDir();
void         EngineCapabilityProbe();
std::wstring UiExeDir();
std::wstring UiRootDir();

// engine CLI baked into this exe (see src\frz.h)
int  FrzEngineMain();
bool FrzEngineCliRequest();

struct Caps { bool probed = false; bool hasTree = false; bool hasGuard = false; };
extern Caps g_caps;

struct FreezeInfo {
    bool ok = false;
    std::wstring exe;
    std::wstring image;
    unsigned long long freedWs = 0;
    unsigned long long commitHeld = 0;
    unsigned long long privateWsBefore = 0;
    int windowCount = 0;
    int procCount = 0;
    bool haveEngineRect = false;
    RECT engineRect = { 0, 0, 0, 0 };
    bool engineIconicBefore = false;
    std::string error;
    long exitCode = -1;
};
bool ParseFreezeOut(const std::string& json, FreezeInfo& out);
struct ThawInfo {
    bool ok = false;
    bool msgOk = false;
    bool stateFileFound = false;
    int  processesResumed = 0;
    int  processesSkipped = 0;
    int  threadsVerified = 0;
    double wakeMs = -1;
    std::string error;
};
bool ParseThawOut(const std::string& json, ThawInfo& out);
struct ListRowUi {
    unsigned long long pid = 0;
    std::wstring exe;
    unsigned long long workingSet = 0;
    unsigned long long commit = 0;
    int threads = 0;
    int threadsSuspended = 0;
    bool running = false;
    bool suspended = false;
};
bool ParseListOut(const std::string& json, std::vector<ListRowUi>& out);

struct GuardComp {
    std::wstring component;
    std::wstring family;
    std::wstring source;
    std::wstring where;
    unsigned long long pid = 0;
};
bool ParseGuardOut(const std::string& json, std::vector<GuardComp>& out);

struct StateWinRec { unsigned long long pid; RECT rect; bool visible; bool iconic; };

struct StateProcRec {
    unsigned long long pid = 0;
    unsigned long long createTime = 0;
    int depth = 0;
    unsigned suspendCalls = 0;
    unsigned long long wsBefore = 0;
    unsigned long long wsAfter = 0;
    unsigned long long commit = 0;
    int alive = 1;
    std::wstring exe;
};

struct StateFileInfo {
    int version = 0;
    unsigned long long rootPid = 0;
    unsigned long long suspendedAt = 0;
    std::wstring image;
    std::vector<StateProcRec> procs;
    std::vector<StateWinRec> wins;
    unsigned long long wsBeforeSum = 0;
    unsigned long long commitSum = 0;
    std::wstring path;
};

bool ReadStateFile(unsigned long long rootPid, StateFileInfo& out);
bool ReadStateDirFrozen(std::vector<unsigned long long>& pids, std::vector<StateWinRec>& wins,
                        std::vector<StateFileInfo>* infos = nullptr);
unsigned long long StateCommitSum(unsigned long long rootPid);

int SweepThawStateFilesInProcess(const std::vector<unsigned long long>& rootPids);

bool LiveTreeSummary(unsigned long long rootPid, int& procCount,
                     unsigned long long& wsNow, unsigned long long& commitNow);
bool IsTreeMember(unsigned long long rootPid, unsigned long long pid);

struct ChildProcEntry {
    unsigned long long pid = 0;
    std::wstring exe;
    unsigned long long ws = 0;
    unsigned long long commit = 0;
};

struct OwnedWindowEntry {
    HWND hwnd = nullptr;
    unsigned long long pid = 0;
    std::wstring title;
    bool isIconic = false;
};

struct PickRow {
    unsigned long long rootPid = 0;
    unsigned long long createTime = 0;
    std::wstring exe;
    std::wstring friendlyName;
    std::wstring primaryWindowTitle;
    std::wstring image;
    ID3D11ShaderResourceView* iconSrv = nullptr;
    unsigned long long treeWs = 0;
    unsigned long long treeCommit = 0;
    bool hasVisibleWindow = false;
    bool protectedHit = false;
    std::wstring protectedBy;
    std::vector<OwnedWindowEntry> windows;
    std::vector<ChildProcEntry> childProcs;
};

bool SnapshotApps(std::vector<PickRow>& out, const std::vector<GuardComp>& guards);
bool IsCoreSystemProcess(unsigned long long pid);
bool IsProcessInstanceAlive(unsigned long long pid, unsigned long long expectedCreateTime);
unsigned long long ProcessCreateTimeUi(unsigned long long pid);
bool ProcessPrivateWorkingSetUi(unsigned long long pid, unsigned long long& bytes);
bool TreePrivateWorkingSetUi(unsigned long long rootPid, bool tree, int& procCount,
                             unsigned long long& privateWs, unsigned long long& commit);
int  CleanupStaleFrozenStateFiles();
unsigned long long ResolveAppRootPid(unsigned long long pid);
std::wstring FriendlyExeName(const std::wstring& image);
std::wstring ExeBaseName(const std::wstring& path);
unsigned CountWindowsForPid(unsigned long long pid);
std::wstring PrimaryWindowTitleForPid(unsigned long long pid);

struct Telemetry {
    unsigned long long ramTotal = 0;
    unsigned long long ramUsed = 0;
    unsigned long long commitUsed = 0;
    unsigned long long commitLimit = 0;
    unsigned long long commitPeak = 0;
    unsigned long long freeCommit = 0;
    double commitFrac = 0.0;
};
bool PollTelemetry(Telemetry& out);
bool CommitHeadroomLow(unsigned long long& freeBytes, unsigned long long& limitBytes);

enum SlotState {
    SLOT_EMPTY = 0,
    SLOT_RUNNING,
    SLOT_FROZEN,
    SLOT_BUSY
};

struct Slot {
    SlotState state = SLOT_EMPTY;
    unsigned long long pid = 0;
    unsigned long long createTime = 0;
    std::wstring exe;
    std::wstring image;
    std::wstring friendlyName;
    std::wstring windowTitle;
    bool tree = true;
    bool autoThaw = true;
    bool treeOverride = false;
    bool autoThawOverride = false;
    int procCount = 0;
    unsigned long long wsNow = 0;
    unsigned long long commitNow = 0;
    unsigned long long baselineWs = 0;
    int windowCount = 0;
    bool fromRecovery = false;
    bool protectedHit = false;
    std::wstring protectedBy;
    std::string busyAction;
    std::vector<unsigned long long> treePids;
    std::string lastError;
};
extern std::vector<Slot> g_slots;

struct PagefileDriveInfo {
    wchar_t letter = 0;
    std::wstring root;
    unsigned long long totalBytes = 0;
    unsigned long long freeBytes = 0;
    bool isSsd = true;
    bool driveTypeKnown = false;
    bool hasPagefile = false;
    bool systemManaged = false;
    unsigned long long initialMb = 0;
    unsigned long long maxMb = 0;
    std::wstring pagefilePath;
    unsigned long long activeBytes = 0;
};

struct PagefileStatus {
    bool scanned = false;
    bool elevated = false;
    unsigned long long ramTotal = 0;
    unsigned long long commitUsed = 0;
    unsigned long long commitLimit = 0;
    unsigned long long currentPagefileBytes = 0;
    unsigned long long recInitialMb = 0;
    unsigned long long recMaxMb = 0;
    int recDrive = -1;
    std::vector<PagefileDriveInfo> drives;
    std::vector<std::wstring> activePaths;
    std::wstring error;
};

struct PagefileApplyResult {
    bool ok = false;
    bool liveExpanded = false;
    bool rebootRequired = false;
    bool elevated = false;
    std::wstring message;
};

bool ScanPagefileDrives(PagefileStatus& out);
PagefileApplyResult ApplyPagefileConfig(const PagefileStatus& st, int driveIndex,
                                        unsigned long long initialMb,
                                        unsigned long long maxMb, bool systemManaged);
bool RelaunchSelfElevated(std::wstring& err);
unsigned long long PagefileMaxAllowedMb(const PagefileStatus& st, int driveIndex);

void LogUi(const char* fmt, ...);
std::wstring Utf8ToWPublic(const std::string& s);
std::string BaseNamePublic(const std::string& p);
