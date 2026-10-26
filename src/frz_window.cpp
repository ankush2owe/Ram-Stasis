// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

namespace {

struct CollectCtx {
    const std::vector<DWORD>* pids;
    std::vector<WinRec>* out;
};

bool Contains(const std::vector<DWORD>& v, DWORD x)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == x) return true;
    return false;
}

BOOL CALLBACK CollectProc(HWND hwnd, LPARAM lp)
{
    CollectCtx* c = (CollectCtx*)lp;
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (!wpid || !Contains(*c->pids, wpid)) return TRUE;

    WinRec r;
    r.pid = wpid;
    r.hwnd = hwnd;
    r.visibleBefore = IsWindowVisible(hwnd) != FALSE;
    r.iconicBefore = IsIconic(hwnd) != FALSE;
    GetWindowRect(hwnd, &r.rect);
    wchar_t title[192];
    title[0] = 0;
    GetWindowTextW(hwnd, title, 192);
    r.title = title;
    c->out->push_back(r);
    return TRUE;
}

}

std::vector<HWND> TopLevelWindows(DWORD pid)
{
    std::vector<HWND> v;
    CountTargetWindows(pid, &v);
    return v;
}

unsigned MinimizeWindowsForPids(const std::vector<DWORD>& pids, std::vector<WinRec>& out)
{
    out.clear();
    if (pids.empty()) return 0;
    CollectCtx c;
    c.pids = &pids;
    c.out = &out;
    EnumWindows(CollectProc, (LPARAM)&c);

    unsigned posted = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        WinRec& r = out[i];

        r.minimizedByUs = r.visibleBefore && !r.iconicBefore;
        if (!r.minimizedByUs) continue;

        SetLastError(0);
        BOOL ok = ShowWindowAsync(r.hwnd, SW_MINIMIZE);
        r.showAsyncRet = ok ? 1 : 0;
        r.showAsyncErr = GetLastError();
        if (ok) posted++;
    }
    return posted;
}

bool VerifyMinimized(std::vector<WinRec>& recs, int timeoutMs)
{
    ULONGLONG start = QpcMs();
    for (;;) {
        bool pending = false;
        for (size_t i = 0; i < recs.size(); ++i) {
            WinRec& r = recs[i];
            if (!IsWindow(r.hwnd)) { r.iconicAfter = false; continue; }
            r.iconicAfter = IsIconic(r.hwnd) != FALSE;
            r.visibleAfter = IsWindowVisible(r.hwnd) != FALSE;
            if (r.minimizedByUs && !r.iconicAfter) pending = true;
        }
        if (!pending) return true;
        if ((int)(QpcMs() - start) >= timeoutMs) return false;
        Sleep(100);
    }
}

void RestoreWindows(const std::vector<WinRec>& recs, std::vector<std::string>& notes)
{
    for (size_t i = 0; i < recs.size(); ++i) {
        const WinRec& r = recs[i];
        char tag[96];
        sprintf_s(tag, sizeof(tag), "hwnd 0x%08llX", (unsigned long long)(ULONG_PTR)r.hwnd);

        if (!r.visibleBefore) {
            notes.push_back(std::string(tag) + ": not visible before the freeze - left untouched");
            continue;
        }
        if (!IsWindow(r.hwnd)) {
            notes.push_back(std::string(tag) + ": window no longer exists, nothing to restore");
            continue;
        }

        if (r.iconicBefore) {
            SetLastError(0);
            BOOL ok = ShowWindowAsync(r.hwnd, SW_SHOWMINNOACTIVE);
            char line[256];
            sprintf_s(line, sizeof(line),
                      "%s: ShowWindowAsync(SW_SHOWMINNOACTIVE) -> %d win32=%lu (was already minimized, kept minimized)",
                      tag, ok ? 1 : 0, (unsigned long)GetLastError());
            notes.push_back(line);
            continue;
        }

        SetLastError(0);
        BOOL ok = ShowWindowAsync(r.hwnd, SW_RESTORE);
        char line[256];
        sprintf_s(line, sizeof(line), "%s: ShowWindowAsync(SW_RESTORE) -> %d win32=%lu",
                  tag, ok ? 1 : 0, (unsigned long)GetLastError());
        notes.push_back(line);
    }
}

void PrintWindowTable(const std::vector<WinRec>& recs)
{
    if (g_ctx.quiet) return;
    if (recs.empty()) {
        Out("  (no top-level windows in the target set)\n");
        return;
    }
    Out("  %-8s %-12s %-4s %-4s %-4s %-5s %-22s %s\n",
        "PID", "HWND", "VIS", "ICON", "OURS", "HUNG", "RECT", "TITLE");
    for (size_t i = 0; i < recs.size(); ++i) {
        const WinRec& r = recs[i];
        char rect[48];
        sprintf_s(rect, sizeof(rect), "%ld,%ld %ldx%ld", (long)r.rect.left, (long)r.rect.top,
                  (long)(r.rect.right - r.rect.left), (long)(r.rect.bottom - r.rect.top));
        std::string title = W2U(r.title);
        if (title.size() > 40) title = title.substr(0, 39) + "~";
        Out("  %-8lu %-12llu %-4s %-4s %-4s %-5s %-22s %s\n",
            (unsigned long)r.pid, (unsigned long long)(ULONG_PTR)r.hwnd,
            r.visibleBefore ? "vis" : "-",
            r.iconicAfter ? "min" : (r.iconicBefore ? "was" : "-"),
            r.minimizedByUs ? "yes" : "no",
            r.hungAfter ? "yes" : "no",
            rect, title.c_str());
    }
}
