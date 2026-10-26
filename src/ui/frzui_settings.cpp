// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frzui.h"

#include <winioctl.h>
#include <winternl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

AppConfig g_cfg;

namespace {

constexpr unsigned long long kMB = 1024ULL * 1024ULL;
constexpr unsigned long long kGB = 1024ULL * 1024ULL * 1024ULL;

std::wstring ConfigPath()
{
    return EngineStateDir() + L"\\ramstasis-settings.json";
}

std::wstring LegacyConfigPath()
{
    return EngineStateDir() + L"\\frzui-settings.json";
}

bool ReadSmallFile(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(f);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(f, &out[0], (DWORD)out.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok) {
        out.clear();
        return false;
    }
    out.resize(got);
    return true;
}

bool WriteSmallFileAtomic(const std::wstring& path, const std::string& text)
{
    std::wstring dir = EngineStateDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, text.data(), (DWORD)text.size(), &wrote, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || wrote != (DWORD)text.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

const char* FindJsonValue(const std::string& text, const char* key)
{
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t at = text.find(needle);
    if (at == std::string::npos) return nullptr;
    at += needle.size();
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at;
    if (at >= text.size() || text[at] != ':') return nullptr;
    ++at;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at;
    return text.c_str() + at;
}

bool JsonBool(const std::string& text, const char* key, bool dflt)
{
    const char* v = FindJsonValue(text, key);
    if (!v) return dflt;
    if (strncmp(v, "true", 4) == 0) return true;
    if (strncmp(v, "false", 5) == 0) return false;
    return dflt;
}

long long JsonInt(const std::string& text, const char* key, long long dflt)
{
    const char* v = FindJsonValue(text, key);
    if (!v) return dflt;
    return strtoll(v, nullptr, 10);
}

std::wstring TrimW(std::wstring s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return std::wstring();
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring NormalizePagefilePath(const std::wstring& path)
{
    std::wstring p = TrimW(path);
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    if (p.rfind(L"\\??\\", 0) == 0) p = p.substr(4);
    else if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    return p;
}

wchar_t DriveLetterOfPath(const std::wstring& path)
{
    std::wstring p = NormalizePagefilePath(path);
    if (p.size() >= 2 && p[1] == L':') {
        wchar_t c = p[0];
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
        if (c >= L'A' && c <= L'Z') return c;
    }
    return 0;
}

bool ReadMultiString(const wchar_t* valueName, std::vector<std::wstring>& out)
{
    out.clear();
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LONG rc = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || bytes == 0 || (type != REG_MULTI_SZ && type != REG_SZ)) {
        RegCloseKey(key);
        return false;
    }
    std::vector<BYTE> buf((size_t)bytes);
    rc = RegQueryValueExW(key, valueName, nullptr, &type, buf.data(), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;

    const wchar_t* p = reinterpret_cast<const wchar_t*>(buf.data());
    size_t chars = (size_t)bytes / sizeof(wchar_t);
    size_t i = 0;
    while (i < chars && p[i] != 0) {
        std::wstring one(p + i);
        i += one.size() + 1;
        if (!one.empty()) out.push_back(one);
    }
    return true;
}

struct PagingEntry {
    std::wstring path;
    unsigned long long initialMb = 0;
    unsigned long long maxMb = 0;
    bool systemManaged = false;
};

PagingEntry ParsePagingEntry(const std::wstring& raw)
{
    PagingEntry e;
    std::wstring s = TrimW(raw);
    size_t space = s.find(L' ');
    if (space == std::wstring::npos) {
        e.path = NormalizePagefilePath(s);
        return e;
    }
    e.path = NormalizePagefilePath(s.substr(0, space));
    std::wstring rest = TrimW(s.substr(space + 1));
    unsigned long long initial = 0, maximum = 0;
    int n = swscanf_s(rest.c_str(), L"%llu %llu", &initial, &maximum);
    if (n >= 1) e.initialMb = initial;
    if (n >= 2) e.maxMb = maximum;
    e.systemManaged = (e.initialMb == 0 && e.maxMb == 0);
    return e;
}

std::wstring BuildPagingEntry(const std::wstring& path, unsigned long long initialMb,
                              unsigned long long maximumMb, bool systemManaged)
{
    wchar_t buf[512];
    if (systemManaged) swprintf_s(buf, L"%s 0 0", path.c_str());
    else swprintf_s(buf, L"%s %llu %llu", path.c_str(), initialMb, maximumMb);
    return std::wstring(buf);
}

std::wstring DriveRootForLetter(wchar_t letter)
{
    wchar_t root[4] = { letter, L':', L'\\', 0 };
    return std::wstring(root);
}

bool QueryDriveIsSsd(const std::wstring& root, bool& known)
{
    known = false;
    std::wstring dev = L"\\\\.\\";
    dev += root.substr(0, 2);
    HANDLE h = CreateFileW(dev.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return true;

    STORAGE_PROPERTY_QUERY query;
    ZeroMemory(&query, sizeof(query));
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR desc;
    ZeroMemory(&desc, sizeof(desc));
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                              &desc, sizeof(desc), &returned, nullptr);
    CloseHandle(h);
    if (!ok) return true;
    known = true;
    return desc.IncursSeekPenalty == FALSE;
}

struct PagefileEnumCtx {
    PagefileStatus* status = nullptr;
    unsigned long long pageSize = 4096;
};

BOOL CALLBACK PagefileEnumCallback(LPVOID context, PENUM_PAGE_FILE_INFORMATION info,
                                   LPCWSTR filename)
{
    PagefileEnumCtx* ctx = static_cast<PagefileEnumCtx*>(context);
    if (!ctx || !ctx->status || !info || !filename) return TRUE;
    std::wstring path = NormalizePagefilePath(filename);
    PagefileStatus& st = *ctx->status;
    st.activePaths.push_back(path);
    unsigned long long bytes = (unsigned long long)info->TotalSize * ctx->pageSize;
    st.currentPagefileBytes += bytes;
    wchar_t letter = DriveLetterOfPath(path);
    if (letter) {
        for (size_t i = 0; i < st.drives.size(); ++i) {
            if (st.drives[i].letter == letter) {
                st.drives[i].activeBytes += bytes;
                st.drives[i].hasPagefile = true;
                if (st.drives[i].pagefilePath.empty()) st.drives[i].pagefilePath = path;
                break;
            }
        }
    }
    return TRUE;
}

void EnablePrivilegesForPagefile()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    const wchar_t* names[] = { L"SeCreatePagefilePrivilege", L"SeManageVolumePrivilege" };
    for (int i = 0; i < 2; ++i) {
        LUID luid;
        if (!LookupPrivilegeValueW(nullptr, names[i], &luid)) continue;
        TOKEN_PRIVILEGES tp;
        ZeroMemory(&tp, sizeof(tp));
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(token);
}

typedef NTSTATUS(NTAPI* PFN_NtCreatePagingFile)(PUNICODE_STRING, PLARGE_INTEGER,
                                                PLARGE_INTEGER, ULONG);

NTSTATUS CreateLivePagingFile(const std::wstring& path, unsigned long long initialMb,
                              unsigned long long maximumMb)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return (NTSTATUS)0xC0000001L;
    PFN_NtCreatePagingFile create =
        reinterpret_cast<PFN_NtCreatePagingFile>(GetProcAddress(ntdll, "NtCreatePagingFile"));
    if (!create) return (NTSTATUS)0xC0000002L;

    std::wstring ntPath = L"\\??\\" + path;
    UNICODE_STRING us;
    ZeroMemory(&us, sizeof(us));
    us.Buffer = const_cast<PWSTR>(ntPath.c_str());
    us.Length = (USHORT)(ntPath.size() * sizeof(wchar_t));
    us.MaximumLength = (USHORT)(us.Length + sizeof(wchar_t));
    LARGE_INTEGER initial;
    initial.QuadPart = (LONGLONG)(initialMb * kMB);
    LARGE_INTEGER maximum;
    maximum.QuadPart = (LONGLONG)(maximumMb * kMB);
    return create(&us, &initial, &maximum, 0);
}

}

void LoadAppConfig()
{
    std::string text;
    if (!ReadSmallFile(ConfigPath(), text)) {
        if (!ReadSmallFile(LegacyConfigPath(), text)) return;
        if (MoveFileExW(LegacyConfigPath().c_str(), ConfigPath().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            LogUi("settings migrated to ramstasis-settings.json");
        } else {
            LogUi("legacy settings read; migration will complete on next save");
        }
    }

    g_cfg.defaultFreezeTree = JsonBool(text, "defaultFreezeTree", g_cfg.defaultFreezeTree);
    g_cfg.defaultAutoThawOnFocus = JsonBool(text, "defaultAutoThawOnFocus", g_cfg.defaultAutoThawOnFocus);
    g_cfg.watchForegroundWake = JsonBool(text, "watchForegroundWake", g_cfg.watchForegroundWake);
    g_cfg.warnOnLowCommit = JsonBool(text, "warnOnLowCommit", g_cfg.warnOnLowCommit);
    g_cfg.guardAntiCheat = JsonBool(text, "guardAntiCheat", g_cfg.guardAntiCheat);
    g_cfg.minimizeToTrayOnClose = JsonBool(text, "minimizeToTrayOnClose", g_cfg.minimizeToTrayOnClose);

    long long pct = JsonInt(text, "lowCommitWarnPct", g_cfg.lowCommitWarnPct);
    if (pct < 5) pct = 5;
    if (pct > 30) pct = 30;
    g_cfg.lowCommitWarnPct = (int)pct;

    long long mods = JsonInt(text, "swapHotkeyModifiers", (long long)g_cfg.swapHotkeyModifiers);
    long long vk = JsonInt(text, "swapHotkeyVk", (long long)g_cfg.swapHotkeyVk);
    const long long allowedMods = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    if ((mods & allowedMods) == 0) mods = MOD_CONTROL | MOD_ALT;
    g_cfg.swapHotkeyModifiers = (UINT)(mods & allowedMods);
    if (vk < 1 || vk > 255) vk = 'S';
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
        vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 ||
        vk == VK_XBUTTON2) {
        vk = 'S';
    }
    g_cfg.swapHotkeyVk = (UINT)vk;
    LogUi("settings loaded: tree=%d autoThaw=%d watch=%d warn=%d(%d%%) guard=%d trayClose=%d hotkey=%u/%u",
          (int)g_cfg.defaultFreezeTree, (int)g_cfg.defaultAutoThawOnFocus,
          (int)g_cfg.watchForegroundWake, (int)g_cfg.warnOnLowCommit,
          g_cfg.lowCommitWarnPct, (int)g_cfg.guardAntiCheat,
          (int)g_cfg.minimizeToTrayOnClose, g_cfg.swapHotkeyModifiers, g_cfg.swapHotkeyVk);
}

void SaveAppConfig()
{
    std::string text;
    text += "{\n";
    text += std::string("  \"defaultFreezeTree\": ") + (g_cfg.defaultFreezeTree ? "true" : "false") + ",\n";
    text += std::string("  \"defaultAutoThawOnFocus\": ") + (g_cfg.defaultAutoThawOnFocus ? "true" : "false") + ",\n";
    text += std::string("  \"watchForegroundWake\": ") + (g_cfg.watchForegroundWake ? "true" : "false") + ",\n";
    text += std::string("  \"warnOnLowCommit\": ") + (g_cfg.warnOnLowCommit ? "true" : "false") + ",\n";
    char buf[256];
    sprintf_s(buf, sizeof(buf), "  \"lowCommitWarnPct\": %d,\n", g_cfg.lowCommitWarnPct);
    text += buf;
    text += std::string("  \"guardAntiCheat\": ") + (g_cfg.guardAntiCheat ? "true" : "false") + ",\n";
    text += std::string("  \"minimizeToTrayOnClose\": ") + (g_cfg.minimizeToTrayOnClose ? "true" : "false") + ",\n";
    sprintf_s(buf, sizeof(buf), "  \"swapHotkeyModifiers\": %u,\n", g_cfg.swapHotkeyModifiers);
    text += buf;
    sprintf_s(buf, sizeof(buf), "  \"swapHotkeyVk\": %u\n", g_cfg.swapHotkeyVk);
    text += buf;
    text += "}\n";
    if (!WriteSmallFileAtomic(ConfigPath(), text))
        LogUi("settings save failed: could not write state\\ramstasis-settings.json");
}

bool ScanPagefileDrives(PagefileStatus& out)
{
    out = PagefileStatus();
    out.elevated = IsElevatedUi();

    Telemetry tel;
    PollTelemetry(tel);
    out.ramTotal = tel.ramTotal;
    out.commitUsed = tel.commitUsed;
    out.commitLimit = tel.commitLimit;

    std::vector<std::wstring> pagingFiles;
    ReadMultiString(L"PagingFiles", pagingFiles);
    std::vector<PagingEntry> entries;
    for (size_t i = 0; i < pagingFiles.size(); ++i) {
        PagingEntry e = ParsePagingEntry(pagingFiles[i]);
        if (!e.path.empty()) entries.push_back(e);
    }

    wchar_t driveStrings[512];
    ZeroMemory(driveStrings, sizeof(driveStrings));
    DWORD len = GetLogicalDriveStringsW((DWORD)(sizeof(driveStrings) / sizeof(driveStrings[0])),
                                        driveStrings);
    if (len == 0) {
        out.error = L"GetLogicalDriveStringsW failed";
        return false;
    }

    for (DWORD i = 0; i < len;) {
        const wchar_t* one = driveStrings + i;
        size_t n = wcslen(one);
        if (n == 0) break;
        i += (DWORD)n + 1;
        if (GetDriveTypeW(one) != DRIVE_FIXED) continue;
        if (n < 2 || one[1] != L':') continue;

        PagefileDriveInfo info;
        info.letter = one[0];
        if (info.letter >= L'a' && info.letter <= L'z')
            info.letter = (wchar_t)(info.letter - L'a' + L'A');
        info.root = DriveRootForLetter(info.letter);

        ULARGE_INTEGER freeBytes, totalBytes, totalFree;
        ZeroMemory(&freeBytes, sizeof(freeBytes));
        ZeroMemory(&totalBytes, sizeof(totalBytes));
        ZeroMemory(&totalFree, sizeof(totalFree));
        if (GetDiskFreeSpaceExW(info.root.c_str(), &freeBytes, &totalBytes, &totalFree)) {
            info.totalBytes = totalBytes.QuadPart;
            info.freeBytes = totalFree.QuadPart;
        }
        info.isSsd = QueryDriveIsSsd(info.root, info.driveTypeKnown);

        for (size_t k = 0; k < entries.size(); ++k) {
            if (DriveLetterOfPath(entries[k].path) != info.letter) continue;
            info.hasPagefile = true;
            info.pagefilePath = entries[k].path;
            info.initialMb = entries[k].initialMb;
            info.maxMb = entries[k].maxMb;
            info.systemManaged = entries[k].systemManaged;
            break;
        }
        out.drives.push_back(info);
    }

    if (out.drives.empty()) {
        out.error = L"No fixed drives were found";
        return false;
    }

    SYSTEM_INFO si;
    ZeroMemory(&si, sizeof(si));
    GetNativeSystemInfo(&si);
    PagefileEnumCtx enumCtx;
    enumCtx.status = &out;
    enumCtx.pageSize = si.dwPageSize ? (unsigned long long)si.dwPageSize : 4096ULL;
    EnumPageFilesW(PagefileEnumCallback, &enumCtx);

    if (out.currentPagefileBytes == 0) {
        for (size_t i = 0; i < out.drives.size(); ++i) {
            if (out.drives[i].initialMb > 0 && !out.drives[i].systemManaged)
                out.currentPagefileBytes += out.drives[i].initialMb * kMB;
        }
    }

    unsigned long long ramMb = out.ramTotal / kMB;
    unsigned long long recommended = ((ramMb * 3) / 2 + 8192) & ~1023ULL;
    if (recommended < 16384ULL) recommended = 16384ULL;
    out.recInitialMb = recommended;

    int best = -1;
    unsigned long long bestFree = 0;
    for (size_t i = 0; i < out.drives.size(); ++i) {
        if (!out.drives[i].isSsd) continue;
        if (out.drives[i].freeBytes > bestFree) {
            bestFree = out.drives[i].freeBytes;
            best = (int)i;
        }
    }
    if (best < 0) {
        for (size_t i = 0; i < out.drives.size(); ++i) {
            if (out.drives[i].freeBytes > bestFree) {
                bestFree = out.drives[i].freeBytes;
                best = (int)i;
            }
        }
    }
    out.recDrive = best;
    if (best >= 0) {
        unsigned long long freeMb = out.drives[(size_t)best].freeBytes / kMB;
        unsigned long long maxByFree = (freeMb * 50) / 100;
        unsigned long long recMax = recommended + 8192ULL;
        if (recMax > maxByFree) recMax = maxByFree;
        if (recMax < 1024ULL) recMax = 1024ULL;
        out.recMaxMb = recMax;
    }

    out.scanned = true;
    return true;
}

unsigned long long PagefileMaxAllowedMb(const PagefileStatus& st, int driveIndex)
{
    if (driveIndex < 0 || (size_t)driveIndex >= st.drives.size()) return 0;
    unsigned long long freeMb = st.drives[(size_t)driveIndex].freeBytes / kMB;
    return (freeMb * 70) / 100;
}

PagefileApplyResult ApplyPagefileConfig(const PagefileStatus& st, int driveIndex,
                                        unsigned long long initialMb,
                                        unsigned long long maxMb, bool systemManaged)
{
    PagefileApplyResult res;
    res.elevated = IsElevatedUi();
    if (driveIndex < 0 || (size_t)driveIndex >= st.drives.size()) {
        res.message = L"Select a fixed drive first.";
        return res;
    }
    if (!res.elevated) {
        res.message = L"Administrator access is required to change the Windows pagefile.";
        return res;
    }

    const PagefileDriveInfo& drive = st.drives[(size_t)driveIndex];
    unsigned long long allowedMb = PagefileMaxAllowedMb(st, driveIndex);
    if (systemManaged) {
        initialMb = 0;
        maxMb = 0;
    } else {
        if (initialMb < 1024ULL) initialMb = 1024ULL;
        if (maxMb < initialMb) maxMb = initialMb;
        if (allowedMb > 0 && maxMb > allowedMb) maxMb = allowedMb;
        if (maxMb < initialMb) {
            res.message = L"The selected drive does not have enough free space for that pagefile.";
            return res;
        }
    }

    std::wstring targetPath = drive.root + L"pagefile.sys";
    std::vector<std::wstring> existing;
    ReadMultiString(L"PagingFiles", existing);

    bool shrinkOrRemove = false;
    std::vector<std::wstring> rewritten;
    for (size_t i = 0; i < existing.size(); ++i) {
        PagingEntry e = ParsePagingEntry(existing[i]);
        if (e.path.empty()) continue;
        if (DriveLetterOfPath(e.path) == drive.letter) {
            if (systemManaged) {
                shrinkOrRemove = true;
            } else if (e.initialMb > initialMb || e.maxMb > maxMb) {
                shrinkOrRemove = true;
            }
            continue;
        }
        rewritten.push_back(existing[i]);
    }
    rewritten.push_back(BuildPagingEntry(targetPath, initialMb, maxMb, systemManaged));

    std::vector<wchar_t> multi;
    for (size_t i = 0; i < rewritten.size(); ++i) {
        multi.insert(multi.end(), rewritten[i].begin(), rewritten[i].end());
        multi.push_back(0);
    }
    multi.push_back(0);

    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                            0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
        wchar_t buf[160];
        swprintf_s(buf, L"Could not open the Memory Management registry key (win32 %ld).", rc);
        res.message = buf;
        return res;
    }
    rc = RegSetValueExW(key, L"PagingFiles", 0, REG_MULTI_SZ,
                        reinterpret_cast<const BYTE*>(multi.data()),
                        (DWORD)(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        wchar_t buf[160];
        swprintf_s(buf, L"Writing PagingFiles failed (win32 %ld).", rc);
        res.message = buf;
        return res;
    }

    res.ok = true;
    res.rebootRequired = shrinkOrRemove;

    if (!systemManaged && initialMb > 0) {
        EnablePrivilegesForPagefile();
        NTSTATUS status = CreateLivePagingFile(targetPath, initialMb, maxMb);
        if (status == 0) {
            res.liveExpanded = true;
            res.rebootRequired = shrinkOrRemove;
            res.message = L"Saved to Windows Registry and expanded the live commit limit through NtCreatePagingFile.";
            LogUi("pagefile: live expansion ok %ls initial=%llu max=%llu",
                  targetPath.c_str(), initialMb, maxMb);
        } else {
            res.rebootRequired = true;
            wchar_t buf[256];
            swprintf_s(buf, L"Saved to Windows Registry. NtCreatePagingFile returned 0x%08lX; "
                            L"restart Windows to apply the new pagefile.",
                       (unsigned long)status);
            res.message = buf;
            LogUi("pagefile: live expansion failed status=0x%08lX path=%ls",
                  (unsigned long)status, targetPath.c_str());
        }
    } else {
        res.rebootRequired = true;
        res.message = L"Saved to Windows Registry. Restart Windows to finish removing or "
                      L"recreating the system-managed pagefile.";
    }

    if (shrinkOrRemove) {
        res.message = L"Saved to Windows Registry. Restart Windows to finish shrinking/removing "
                      L"the previous pagefile.";
    }

    LogUi("pagefile: registry updated %ls %llu %llu systemManaged=%d live=%d reboot=%d",
          targetPath.c_str(), initialMb, maxMb, (int)systemManaged,
          (int)res.liveExpanded, (int)res.rebootRequired);
    return res;
}

bool RelaunchSelfElevated(std::wstring& err)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0 || n >= MAX_PATH) {
        err = L"Could not resolve the UI executable path.";
        return false;
    }
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        wchar_t buf[160];
        swprintf_s(buf, L"Relaunch as Administrator failed (win32 %lu).",
                   (unsigned long)GetLastError());
        err = buf;
        return false;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    err.clear();
    return true;
}
