// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

#include <share.h>

Ctx g_ctx;

static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

static void EnsureLogLock()
{
    if (!g_logLockInit) {
        InitializeCriticalSection(&g_logLock);
        g_logLockInit = true;
    }
}

std::string W2U(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string s((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], need, nullptr, nullptr);
    return s;
}

std::wstring U2W(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring w((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], need);
    return w;
}

std::wstring ExeDirOf(const wchar_t* path)
{
    std::wstring p(path ? path : L"");
    size_t slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return p.substr(0, slash);
}

static std::wstring RootDirOf(const std::wstring& exeDir)
{

    size_t k = exeDir.find_last_of(L"\\/");
    if (k != std::wstring::npos && k > 2 && _wcsicmp(exeDir.c_str() + k + 1, L"bin") == 0)
        return exeDir.substr(0, k);
    return exeDir;
}

bool IsElevated()
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

bool FrzTryEnableDebugPrivilege()
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

std::string Comma(unsigned long long v)
{
    char buf[32];
    sprintf_s(buf, sizeof(buf), "%llu", v);
    std::string digits(buf);
    std::string out;
    int count = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        out.insert(out.begin(), digits[i]);
        if (++count % 3 == 0 && i != 0) out.insert(out.begin(), ',');
    }
    return out;
}

std::string Bytes(unsigned long long v)
{
    char buf[64];
    double mib = (double)v / (1024.0 * 1024.0);
    sprintf_s(buf, sizeof(buf), "%s B (%.2f MiB)", Comma(v).c_str(), mib);
    return std::string(buf);
}

std::string Hex(unsigned long long v)
{
    char buf[32];
    sprintf_s(buf, sizeof(buf), "0x%llX", v);
    return std::string(buf);
}

ULONGLONG NowUnix()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) / 10000000ULL;
}

ULONGLONG QpcMs()
{
    static LARGE_INTEGER freq = { 0, 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0) return 0;
    return (ULONGLONG)((now.QuadPart * 1000) / freq.QuadPart);
}

ULONGLONG FileTimeToU64(const FILETIME& ft)
{
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

std::string TimeStampNow()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    sprintf_s(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

void LogInit(const wchar_t* exePath)
{
    EnsureLogLock();
    g_ctx.exeDir = ExeDirOf(exePath);
    std::wstring rootDir = RootDirOf(g_ctx.exeDir);
    if (g_ctx.stateDir.empty()) g_ctx.stateDir = rootDir + L"\\state";
    if (g_ctx.logPath.empty()) {
        std::wstring dir = rootDir + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t name[64];
        swprintf_s(name, L"\\frz-%04u%02u%02u.log", st.wYear, st.wMonth, st.wDay);
        g_ctx.logPath = dir + name;
    }
    CreateDirectoryW(g_ctx.stateDir.c_str(), nullptr);
    if (g_log) { fclose(g_log); g_log = nullptr; }
    _wfopen_s(&g_log, g_ctx.logPath.c_str(), L"ab");
}

void Log(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    EnterCriticalSection(&g_logLock);
    if (g_log) {
        fprintf(g_log, "[%s] %s\n", TimeStampNow().c_str(), buf);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_logLock);
}

static const char* g_cmdName = "";

void SetCmd(const char* name) { g_cmdName = name; }

void Err(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s\n", buf);
    fflush(stderr);

    if (g_ctx.json) {
        std::string esc;
        for (const char* p = buf; *p; ++p) {
            switch (*p) {
            case '"':  esc += "\\\""; break;
            case '\\': esc += "\\\\"; break;
            case '\n': esc += "\\n"; break;
            case '\r': esc += "\\r"; break;
            case '\t': esc += "\\t"; break;
            default: esc += *p; break;
            }
        }
        printf("{\"ok\": false, \"command\": \"%s\", \"error\": \"%s\"}\n", g_cmdName, esc.c_str());
        fflush(stdout);
    }
    if (g_logLockInit) Log("%s", buf);
}

void Out(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE* dest = g_ctx.json ? stderr : stdout;
    fputs(buf, dest);
    fflush(dest);
    if (g_logLockInit) Log("%s", buf);
}

static std::string JsonEsc(const char* s)
{
    std::string o;
    if (!s) return o;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        switch (*p) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (*p < 0x20) {
                char b[8];
                sprintf_s(b, sizeof(b), "\\u%04x", (unsigned)*p);
                o += b;
            } else {
                o += (char)*p;
            }
        }
    }
    return o;
}

void Jw::begin(char k)
{
    fputs(k == 'o' ? "{" : "[", stdout);
    kind.push_back(k);
    first.push_back(true);
}

void Jw::end()
{
    char k = kind.empty() ? 'o' : kind.back();
    if (!kind.empty()) {
        bool hadAny = !first.back();
        kind.pop_back();
        first.pop_back();
        if (hadAny) {
            fputs("\n", stdout);
            for (size_t i = 0; i < kind.size(); ++i) fputs("  ", stdout);
        }
    }
    fputs(k == 'o' ? "}" : "]", stdout);
    fflush(stdout);
}

void Jw::sep()
{
    if (kind.empty()) return;
    if (!first.back()) fputs(",", stdout);
    first.back() = false;
    fputs("\n", stdout);
    for (size_t i = 0; i < kind.size(); ++i) fputs("  ", stdout);
}

void Jw::key(const char* k)
{
    if (!kind.empty() && kind.back() == 'o') {
        printf("\"%s\": ", JsonEsc(k).c_str());
    }
}

void Jw::obj(const char* k) { sep(); key(k); begin('o'); }
void Jw::arr(const char* k) { sep(); key(k); begin('a'); }
void Jw::objElem()          { sep(); begin('o'); }
void Jw::endObj()           { end(); }
void Jw::endArr()           { end(); }

void Jw::n(const char* k, long long v)   { sep(); key(k); printf("%lld", v); }
void Jw::un(const char* k, unsigned long long v) { sep(); key(k); printf("%llu", v); }
void Jw::f(const char* k, double v, int prec) { sep(); key(k); printf("%.*f", prec, v); }
void Jw::s(const char* k, const char* v) { sep(); key(k); printf("\"%s\"", JsonEsc(v).c_str()); }
void Jw::sw(const char* k, const std::wstring& v) { s(k, W2U(v).c_str()); }
void Jw::b(const char* k, bool v)        { sep(); key(k); fputs(v ? "true" : "false", stdout); }
void Jw::nullv(const char* k)            { sep(); key(k); fputs("null", stdout); }

void Jw::member(const char* k, const MemNumbers& m)
{
    obj(k);
    b("ok", m.ok);
    if (!m.ok) { n("win32_error", (long long)m.err); endObj(); return; }
    un("working_set", m.workingSet);
    un("peak_working_set", m.peakWorkingSet);
    un("private_usage", m.privateUsage);
    un("pagefile_usage", m.pagefileUsage);
    un("peak_pagefile_usage", m.peakPagefileUsage);
    un("quota_paged", m.quotaPaged);
    un("quota_nonpaged", m.quotaNonPaged);
    un("page_faults", m.pageFaultCount);
    endObj();
}

void Jw::member(const char* k, const SysPerf& p)
{
    obj(k);
    b("ok", p.ok);
    if (!p.ok) { n("win32_error", (long long)p.err); endObj(); return; }
    un("commit_total", p.commitTotal);
    un("commit_limit", p.commitLimit);
    un("free_commit", p.freeCommit());
    un("commit_peak", p.commitPeak);
    un("physical_total", p.physicalTotal);
    un("physical_available", p.physicalAvailable);
    un("system_cache", p.systemCache);
    un("kernel_total", p.kernelTotal);
    un("kernel_paged", p.kernelPaged);
    un("kernel_nonpaged", p.kernelNonpaged);
    un("page_size", p.pageSize);
    un("handle_count", p.handleCount);
    un("process_count", p.processCount);
    un("thread_count", p.threadCount);
    endObj();
}

void Jw::member(const char* k, const PageFileNumbers& p)
{
    obj(k);
    b("ok", p.ok);
    n("win32_error", (long long)p.win32err);
    un("file_count", p.fileCount);
    un("total_size_pages", p.totalSizePages);
    un("total_in_use_pages", p.totalInUsePages);
    un("total_size_bytes", p.totalSizeBytes);
    un("total_in_use_bytes", p.totalInUseBytes);
    un("peak_usage_bytes", p.peakUsageBytes);
    endObj();
}

bool GetProcCreateTime(DWORD pid, ULONGLONG& out)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    FILETIME c, e, k, u;
    bool ok = GetProcessTimes(h, &c, &e, &k, &u) != FALSE;
    if (ok) out = FileTimeToU64(c);
    CloseHandle(h);
    return ok;
}

static std::wstring QueryImagePath(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::wstring();
    wchar_t buf[1024];
    DWORD len = 1024;
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) out.assign(buf, len);
    CloseHandle(h);
    return out;
}

bool ResolveTarget(const std::wstring& spec, std::vector<ProcSnapshot>& out, std::string& err)
{
    out.clear();
    if (spec.empty()) { err = "empty target"; return false; }

    bool numeric = true;
    for (size_t i = 0; i < spec.size(); ++i) {
        if (spec[i] < L'0' || spec[i] > L'9') { numeric = false; break; }
    }

    if (numeric) {
        DWORD pid = (DWORD)_wtoi(spec.c_str());
        ProcSnapshot p;
        p.pid = pid;
        p.imagePath = QueryImagePath(pid);
        if (p.imagePath.empty()) {
            err = "no such process or access denied: pid " + std::to_string((unsigned long long)pid);
            return false;
        }
        size_t slash = p.imagePath.find_last_of(L"\\/");
        p.exeName = (slash == std::wstring::npos) ? p.imagePath : p.imagePath.substr(slash + 1);
        GetProcCreateTime(pid, p.createTime);
        out.push_back(p);
        return true;
    }

    std::wstring want = spec;
    if (want.size() < 4 || _wcsicmp(want.c_str() + want.size() - 4, L".exe") != 0) want += L".exe";

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        err = "CreateToolhelp32Snapshot failed, win32=" + std::to_string(GetLastError());
        return false;
    }
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, want.c_str()) == 0) {
                ProcSnapshot p;
                p.pid = pe.th32ProcessID;
                p.exeName = pe.szExeFile;
                p.imagePath = QueryImagePath(p.pid);
                GetProcCreateTime(p.pid, p.createTime);
                out.push_back(p);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (out.empty()) {
        err = "no process named " + W2U(want);
        return false;
    }
    return true;
}

bool PickOne(const std::wstring& spec, ProcSnapshot& out, std::string& err)
{
    std::vector<ProcSnapshot> v;
    if (!ResolveTarget(spec, v, err)) return false;
    if (v.size() > 1 && !g_ctx.all) {
        std::string list;
        for (size_t i = 0; i < v.size(); ++i) {
            list += "\n    pid " + std::to_string((unsigned long long)v[i].pid) + "  " + W2U(v[i].exeName);
        }
        err = "ambiguous target '" + W2U(spec) + "': " + std::to_string((unsigned long long)v.size()) +
              " processes match." + list + "\n  pass an explicit pid, or --all";
        return false;
    }
    out = v[0];
    return true;
}

bool ResolveTargetNoHandle(const std::wstring& spec, ProcSnapshot& out, std::string& err)
{
    out = ProcSnapshot();
    if (spec.empty()) { err = "empty target"; return false; }

    bool numeric = true;
    for (size_t i = 0; i < spec.size(); ++i) {
        if (spec[i] < L'0' || spec[i] > L'9') { numeric = false; break; }
    }

    std::vector<ProcRow> rows;
    if (!SnapshotProcesses(rows)) {
        err = "CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS) failed";
        return false;
    }

    if (numeric) {
        DWORD pid = (DWORD)_wtoi(spec.c_str());
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].pid != pid) continue;
            out.pid = pid;
            out.parentPid = rows[i].parentPid;
            out.exeName = rows[i].exe;
            return true;
        }
        err = "no such process: pid " + std::to_string((unsigned long long)pid);
        return false;
    }

    std::wstring want = spec;
    if (want.size() < 4 || _wcsicmp(want.c_str() + want.size() - 4, L".exe") != 0) want += L".exe";

    std::vector<ProcSnapshot> matches;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (_wcsicmp(rows[i].exe.c_str(), want.c_str()) != 0) continue;
        ProcSnapshot p;
        p.pid = rows[i].pid;
        p.parentPid = rows[i].parentPid;
        p.exeName = rows[i].exe;
        matches.push_back(p);
    }
    if (matches.empty()) {
        err = "no process named " + W2U(want);
        return false;
    }
    if (matches.size() > 1 && !g_ctx.all) {
        std::string list;
        for (size_t i = 0; i < matches.size(); ++i)
            list += "\n    pid " + std::to_string((unsigned long long)matches[i].pid) + "  " + W2U(matches[i].exeName);
        err = "ambiguous target '" + W2U(spec) + "': " + std::to_string((unsigned long long)matches.size()) +
              " processes match." + list + "\n  pass an explicit pid, or --all";
        return false;
    }
    out = matches[0];
    return true;
}
