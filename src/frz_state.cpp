// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

std::wstring StatePathFor(DWORD pid)
{
    wchar_t buf[64];
    swprintf_s(buf, L"\\frz-%lu.state", (unsigned long)pid);
    return g_ctx.stateDir + buf;
}

bool StateWrite(const StateFile& sf, std::string& err)
{
    std::wstring path = StatePathFor(sf.rootPid);
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) { err = "cannot create state file: " + W2U(path); return false; }

    fprintf(f, "frz-state 2\n");
    fprintf(f, "root %lu\n", (unsigned long)sf.rootPid);
    fprintf(f, "image %s\n", W2U(sf.image).c_str());
    fprintf(f, "suspended_at %llu\n", (unsigned long long)sf.suspendedAt);
    for (size_t i = 0; i < sf.procs.size(); ++i) {
        const StateProc& p = sf.procs[i];
        fprintf(f, "proc %lu %llu %d %u %llu %llu %llu %d\n",
                (unsigned long)p.pid, (unsigned long long)p.createTime, p.depth,
                p.suspendCalls, (unsigned long long)p.wsBefore,
                (unsigned long long)p.wsAfter, (unsigned long long)p.commit,
                p.alive ? 1 : 0);
        fprintf(f, "exe %lu %s\n", (unsigned long)p.pid, W2U(p.exe).c_str());
        for (size_t k = 0; k < p.threads.size(); ++k)
            fprintf(f, "thr %lu %lu %ld\n", (unsigned long)p.pid,
                    (unsigned long)p.threads[k].first, (long)p.threads[k].second);
        for (size_t k = 0; k < p.extraThreads.size(); ++k)
            fprintf(f, "xthr %lu %lu\n", (unsigned long)p.pid, (unsigned long)p.extraThreads[k]);
    }
    for (size_t i = 0; i < sf.wins.size(); ++i) {
        const StateWin& w = sf.wins[i];
        fprintf(f, "win %lu %llu %d %d %d %ld %ld %ld %ld\n",
                (unsigned long)w.pid, w.hwnd, w.visible ? 1 : 0, w.iconic ? 1 : 0,
                w.minimizedByUs ? 1 : 0, w.left, w.top, w.right, w.bottom);
    }
    fclose(f);
    return true;
}

namespace {

StateProc* FindProc(StateFile& sf, DWORD pid)
{
    for (size_t i = 0; i < sf.procs.size(); ++i)
        if (sf.procs[i].pid == pid) return &sf.procs[i];
    return nullptr;
}

bool ParseStateFile(const std::wstring& path, StateFile& out, std::string& err)
{
    out = StateFile();
    out.rawPath = W2U(path);
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) {
        err = "no state file at " + out.rawPath;
        return false;
    }

    char line[4096];
    bool haveHeader = false;
    int version = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n) continue;
        if (strncmp(line, "frz-state ", 10) == 0) {
            haveHeader = true;
            version = atoi(line + 10);
            continue;
        }
        if (version == 1) {
            if (strncmp(line, "pid ", 4) == 0) {
                out.rootPid = (DWORD)strtoul(line + 4, nullptr, 10);
                continue;
            }
            if (strncmp(line, "create ", 7) == 0) {
                StateProc p;
                p.pid = out.rootPid;
                p.createTime = _strtoui64(line + 7, nullptr, 10);
                out.procs.push_back(p);
                continue;
            }
            if (strncmp(line, "image ", 6) == 0) { out.image = U2W(std::string(line + 6)); continue; }
            if (strncmp(line, "suspended_at ", 13) == 0) {
                out.suspendedAt = _strtoui64(line + 13, nullptr, 10);
                continue;
            }
            if (strncmp(line, "thr ", 4) == 0) {
                unsigned long tid = 0;
                long cnt = 0;
                if (sscanf_s(line + 4, "%lu %ld", &tid, &cnt) == 2 && !out.procs.empty())
                    out.procs[0].threads.push_back(std::make_pair((DWORD)tid, (LONG)cnt));
                continue;
            }
            continue;
        }

        if (strncmp(line, "root ", 5) == 0) { out.rootPid = (DWORD)strtoul(line + 5, nullptr, 10); continue; }
        if (strncmp(line, "image ", 6) == 0) { out.image = U2W(std::string(line + 6)); continue; }
        if (strncmp(line, "suspended_at ", 13) == 0) {
            out.suspendedAt = _strtoui64(line + 13, nullptr, 10);
            continue;
        }
        if (strncmp(line, "proc ", 5) == 0) {
            unsigned long pid = 0;
            unsigned long long ct = 0, wsb = 0, wsa = 0, com = 0;
            int depth = 0, alive = 1;
            unsigned calls = 1;
            if (sscanf_s(line + 5, "%lu %llu %d %u %llu %llu %llu %d",
                         &pid, &ct, &depth, &calls, &wsb, &wsa, &com, &alive) >= 5) {
                StateProc p;
                p.pid = (DWORD)pid;
                p.createTime = ct;
                p.depth = depth;
                p.suspendCalls = calls;
                p.wsBefore = wsb;
                p.wsAfter = wsa;
                p.commit = com;
                p.alive = alive != 0;
                out.procs.push_back(p);
            }
            continue;
        }
        if (strncmp(line, "exe ", 4) == 0) {
            unsigned long pid = 0;
            char name[2048];
            name[0] = 0;
            if (sscanf_s(line + 4, "%lu %2047[^\n]", &pid, name, (unsigned)sizeof(name)) >= 1) {
                StateProc* p = FindProc(out, (DWORD)pid);
                if (p) p->exe = U2W(std::string(name));
            }
            continue;
        }
        if (strncmp(line, "thr ", 4) == 0) {
            unsigned long pid = 0, tid = 0;
            long cnt = 0;
            if (sscanf_s(line + 4, "%lu %lu %ld", &pid, &tid, &cnt) == 3) {
                StateProc* p = FindProc(out, (DWORD)pid);
                if (p) p->threads.push_back(std::make_pair((DWORD)tid, (LONG)cnt));
            }
            continue;
        }
        if (strncmp(line, "xthr ", 5) == 0) {
            unsigned long pid = 0, tid = 0;
            if (sscanf_s(line + 5, "%lu %lu", &pid, &tid) == 2) {
                StateProc* p = FindProc(out, (DWORD)pid);
                if (p) p->extraThreads.push_back((DWORD)tid);
            }
            continue;
        }
        if (strncmp(line, "win ", 4) == 0) {
            unsigned long pid = 0;
            unsigned long long hwnd = 0;
            int vis = 0, icon = 0, byus = 0;
            long l = 0, t = 0, rr = 0, b = 0;
            if (sscanf_s(line + 4, "%lu %llu %d %d %d %ld %ld %ld %ld",
                         &pid, &hwnd, &vis, &icon, &byus, &l, &t, &rr, &b) == 9) {
                StateWin w;
                w.pid = (DWORD)pid;
                w.hwnd = hwnd;
                w.visible = vis != 0;
                w.iconic = icon != 0;
                w.minimizedByUs = byus != 0;
                w.left = l; w.top = t; w.right = rr; w.bottom = b;
                out.wins.push_back(w);
            }
            continue;
        }
    }
    fclose(f);

    if (!haveHeader) { err = "state file has no header (corrupt): " + out.rawPath; return false; }
    if (version != 1 && version != 2) {
        err = "state file version " + std::to_string((long long)version) + " is not supported: " + out.rawPath;
        return false;
    }
    out.version = version;
    if (version == 1 && out.procs.empty()) {
        StateProc p;
        p.pid = out.rootPid;
        out.procs.push_back(p);
    }
    if (out.rootPid == 0) { err = "state file has no root pid: " + out.rawPath; return false; }
    out.exists = true;
    return true;
}

bool ContainsPid(const StateFile& sf, DWORD pid)
{
    if (sf.rootPid == pid) return true;
    for (size_t i = 0; i < sf.procs.size(); ++i)
        if (sf.procs[i].pid == pid) return true;
    return false;
}

}

bool StateRead(DWORD pid, StateFile& out, std::string& err)
{
    StateFile sf;
    std::string perr;
    if (ParseStateFile(StatePathFor(pid), sf, perr) && ContainsPid(sf, pid)) {
        out = sf;
        return true;
    }

    std::wstring pat = g_ctx.stateDir + L"\\frz-*.state";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring path = g_ctx.stateDir + L"\\" + fd.cFileName;
            StateFile cand;
            std::string ce;
            if (!ParseStateFile(path, cand, ce)) continue;
            if (!ContainsPid(cand, pid)) continue;
            FindClose(h);
            out = cand;
            return true;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    out = StateFile();
    out.rawPath = W2U(StatePathFor(pid));
    err = "no state file for pid " + std::to_string((unsigned long long)pid) +
          " at " + out.rawPath + " and no other state file lists it";
    return false;
}

bool StateDelete(DWORD pid, std::string& err)
{
    std::vector<std::wstring> victims;
    StateFile sf;
    std::string perr;
    if (StateRead(pid, sf, perr) && !sf.rawPath.empty()) {
        victims.push_back(U2W(sf.rawPath));
    } else {
        victims.push_back(StatePathFor(pid));
    }

    bool ok = true;
    for (size_t i = 0; i < victims.size(); ++i) {
        if (DeleteFileW(victims[i].c_str())) continue;
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) continue;
        err = "cannot delete state file " + W2U(victims[i]) + " win32=" +
              std::to_string((unsigned long long)e);
        ok = false;
    }
    return ok;
}

bool StateFindTreeOverlap(const std::vector<DWORD>& pids, DWORD& pidHit, std::string& path)
{
    pidHit = 0;
    std::wstring pat = g_ctx.stateDir + L"\\frz-*.state";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        std::wstring full = g_ctx.stateDir + L"\\" + fd.cFileName;
        StateFile cand;
        std::string ce;
        if (!ParseStateFile(full, cand, ce)) continue;
        for (size_t i = 0; i < pids.size(); ++i) {
            if (!ContainsPid(cand, pids[i])) continue;
            pidHit = pids[i];
            path = cand.rawPath;
            FindClose(h);
            return true;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}
