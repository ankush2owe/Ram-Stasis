// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

bool SnapshotProcesses(std::vector<ProcRow>& out)
{
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcRow r;
            r.pid = pe.th32ProcessID;
            r.parentPid = pe.th32ParentProcessID;
            r.exe = pe.szExeFile;
            r.threads = pe.cntThreads;
            out.push_back(r);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return !out.empty();
}

std::vector<ProcSnapshot> BuildTree(DWORD rootPid, bool includeRoot)
{
    std::vector<ProcSnapshot> result;
    std::vector<ProcRow> all;
    if (!SnapshotProcesses(all)) return result;

    ProcSnapshot root;
    root.pid = rootPid;
    root.depth = 0;
    root.parentPid = 0;
    bool haveRoot = false;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].pid != rootPid) continue;
        root.parentPid = all[i].parentPid;
        root.exeName = all[i].exe;
        haveRoot = true;
        break;
    }
    if (!haveRoot) return result;

    root.imagePath = std::wstring();
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, rootPid);
        if (h) {
            wchar_t buf[1024];
            DWORD len = 1024;
            std::wstring img;
            if (QueryFullProcessImageNameW(h, 0, buf, &len)) root.imagePath.assign(buf, len);
            CloseHandle(h);
            if (!root.imagePath.empty()) {
                size_t slash = root.imagePath.find_last_of(L"\\/");
                if (slash != std::wstring::npos) root.exeName = root.imagePath.substr(slash + 1);
            }
        }
        GetProcCreateTime(rootPid, root.createTime);
    }
    if (includeRoot) result.push_back(root);

    std::vector<std::pair<DWORD, int> > frontier;
    std::vector<DWORD> seen;
    std::vector<std::pair<DWORD, ULONGLONG> > created;
    frontier.push_back(std::make_pair(rootPid, 0));
    seen.push_back(rootPid);
    created.push_back(std::make_pair(rootPid, root.createTime));

    while (!frontier.empty()) {
        DWORD cur = frontier.back().first;
        int depth = frontier.back().second;
        frontier.pop_back();

        ULONGLONG parentCreateTime = 0;
        for (size_t k = 0; k < created.size(); ++k)
            if (created[k].first == cur) { parentCreateTime = created[k].second; break; }

        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].parentPid != cur) continue;
            if (all[i].pid == 0 || all[i].pid == cur) continue;
            bool dup = false;
            for (size_t k = 0; k < seen.size(); ++k)
                if (seen[k] == all[i].pid) { dup = true; break; }
            if (dup) continue;
            seen.push_back(all[i].pid);

            ProcSnapshot c;
            c.pid = all[i].pid;
            c.parentPid = all[i].parentPid;
            c.exeName = all[i].exe;
            c.depth = depth + 1;
            {
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, c.pid);
                if (h) {
                    wchar_t buf[1024];
                    DWORD len = 1024;
                    std::wstring img;
                    if (QueryFullProcessImageNameW(h, 0, buf, &len)) c.imagePath.assign(buf, len);
                    CloseHandle(h);
                }
                GetProcCreateTime(c.pid, c.createTime);
            }

            if (c.createTime != 0 && parentCreateTime != 0 && c.createTime < parentCreateTime) {
                Log("tree: pid %lu (%s) rejected as a child of pid %lu - child created before parent "
                    "(orphan pointing at a recycled pid)",
                    (unsigned long)c.pid, W2U(c.exeName).c_str(), (unsigned long)cur);
                continue;
            }

            result.push_back(c);
            created.push_back(std::make_pair(c.pid, c.createTime));
            frontier.push_back(std::make_pair(c.pid, c.depth));
        }
    }
    return result;
}

static bool DepthGreater(const ProcSnapshot& a, const ProcSnapshot& b)
{
    return a.depth > b.depth;
}

static bool DepthLess(const ProcSnapshot& a, const ProcSnapshot& b)
{
    return a.depth < b.depth;
}

void OrderChildrenFirst(std::vector<ProcSnapshot>& v)
{
    std::stable_sort(v.begin(), v.end(), DepthGreater);
}

void OrderRootFirst(std::vector<ProcSnapshot>& v)
{
    std::stable_sort(v.begin(), v.end(), DepthLess);
}

bool ProcMatchesCreateTime(DWORD pid, ULONGLONG createTime)
{
    ULONGLONG ct = 0;
    if (!GetProcCreateTime(pid, ct)) return false;
    if (createTime == 0) return true;
    return ct == createTime;
}

int CmdTree(const std::wstring& spec)
{
    ProcSnapshot root;
    std::string err;
    if (!ResolveTargetNoHandle(spec, root, err)) {
        Out("ERROR: %s\n", err.c_str());
        return 2;
    }

    std::vector<ProcSnapshot> procs = BuildTree(root.pid, true);
    if (procs.empty()) procs.push_back(root);
    OrderRootFirst(procs);

    unsigned long long totWs = 0, totCommit = 0;
    unsigned long long totThreads = 0;
    struct Row {
        ProcSnapshot p;
        MemNumbers m;
        int threads = 0;
        std::string state = "?";
    };
    std::vector<Row> rows;
    for (size_t i = 0; i < procs.size(); ++i) {
        Row r;
        r.p = procs[i];
        HANDLE h = nullptr;
        if (OpenTarget(procs[i].pid,
                       PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, h, err) == 0) {
            GetMem(h, r.m);
            std::vector<ThreadInfo> th = EnumThreads(procs[i].pid, false);
            r.threads = (int)th.size();
            unsigned susp = 0;
            for (size_t k = 0; k < th.size(); ++k)
                if (th[k].suspendCount > 0) susp++;
            if (th.empty()) r.state = "?";
            else if (susp == th.size()) r.state = "SUSPENDED";
            else if (susp > 0) r.state = "partial";
            else r.state = "running";
            CloseHandle(h);
        } else {
            r.state = "gone/denied";
        }
        if (r.m.ok) { totWs += r.m.workingSet; totCommit += r.m.privateUsage; }
        totThreads += (unsigned long long)r.threads;
        rows.push_back(r);
    }

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "tree");
        j.un("root_pid", root.pid);
        j.sw("root_exe", root.exeName);
        j.un("process_count", (unsigned long long)rows.size());
        j.arr("processes");
        for (size_t i = 0; i < rows.size(); ++i) {
            j.objElem();
            j.un("pid", rows[i].p.pid);
            j.n("depth", (long long)rows[i].p.depth);
            j.sw("exe", rows[i].p.exeName);
            j.sw("image", rows[i].p.imagePath);
            j.un("create_time", rows[i].p.createTime);
            j.un("threads", (unsigned long long)rows[i].threads);
            j.s("state", rows[i].state.c_str());
            j.member("memory", rows[i].m);
            j.endObj();
        }
        j.endArr();
        j.obj("total");
        j.un("working_set", totWs);
        j.un("commit", totCommit);
        j.un("threads", totThreads);
        j.endObj();
        j.endObj();
        Out("\n");
        return 0;
    }

    Out("=== TREE  root %lu  %s ===\n", (unsigned long)root.pid, W2U(root.exeName).c_str());
    Out("  %-5s %-8s %-26s %14s %14s %6s %s\n",
        "DEPTH", "PID", "EXE", "WORKING_SET", "COMMIT", "THR", "STATE");
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string name = W2U(rows[i].p.exeName);
        if (name.size() > 26) name = name.substr(0, 25) + "~";
        std::string pad((size_t)rows[i].p.depth * 2, ' ');
        if (rows[i].m.ok) {
            Out("  %-5d %-8lu %-26s %14llu %14llu %6d %s\n",
                rows[i].p.depth, (unsigned long)rows[i].p.pid, (pad + name).c_str(),
                rows[i].m.workingSet, rows[i].m.privateUsage, rows[i].threads, rows[i].state.c_str());
        } else {
            Out("  %-5d %-8lu %-26s %14s %14s %6d %s\n",
                rows[i].p.depth, (unsigned long)rows[i].p.pid, (pad + name).c_str(),
                "-", "-", rows[i].threads, rows[i].state.c_str());
        }
    }
    Out("\n  %llu process(es), total working set %s, total commit %s, %llu threads\n",
        (unsigned long long)rows.size(), Bytes(totWs).c_str(), Bytes(totCommit).c_str(), totThreads);
    Out("  freeze order: children first (deepest first), then root\n");
    Out("  thaw order  : root first, then children\n");
    return 0;
}
