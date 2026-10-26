// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frzui.h"

#include <cstdlib>
#include <cstring>

namespace {

struct JVal {
    enum Type { TNull, TBool, TNum, TStr, TArr, TObj } type = TNull;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal> > obj;

    const JVal* find(const char* key) const
    {
        if (type != TObj) return nullptr;
        for (size_t i = 0; i < obj.size(); ++i)
            if (obj[i].first == key) return &obj[i].second;
        return nullptr;
    }
    double numOr(double dflt) const { return type == TNum ? num : dflt; }
    bool truthy() const { return type == TBool ? b : (type == TNum ? num != 0 : false); }
};

struct Parser {
    const char* p;
    const char* end;
    bool fail = false;

    void ws()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    }
    bool lit(const char* s)
    {
        size_t n = strlen(s);
        if ((size_t)(end - p) < n || memcmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }
    bool parseString(std::string& out)
    {
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < end) {
                char e = *p++;
                switch (e) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                    if (end - p >= 4) { p += 4; out += '?'; }
                    break;
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool parseValue(JVal& v)
    {
        ws();
        if (p >= end) return false;
        char c = *p;
        if (c == '{') {
            v.type = JVal::TObj;
            ++p;
            ws();
            if (p < end && *p == '}') { ++p; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!parseString(key)) return false;
                ws();
                if (p >= end || *p != ':') return false;
                ++p;
                JVal child;
                if (!parseValue(child)) return false;
                v.obj.push_back(std::make_pair(key, child));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.type = JVal::TArr;
            ++p;
            ws();
            if (p < end && *p == ']') { ++p; return true; }
            for (;;) {
                JVal child;
                if (!parseValue(child)) return false;
                v.arr.push_back(child);
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (c == '"') {
            v.type = JVal::TStr;
            return parseString(v.str);
        }
        if (lit("true"))  { v.type = JVal::TBool; v.b = true;  return true; }
        if (lit("false")) { v.type = JVal::TBool; v.b = false; return true; }
        if (lit("null"))  { v.type = JVal::TNull; return true; }
        char* numEnd = nullptr;
        v.num = strtod(p, &numEnd);
        if (numEnd == p || numEnd > end) return false;
        v.type = JVal::TNum;
        p = numEnd;
        return true;
    }
};

bool Parse(const std::string& s, JVal& root)
{
    for (size_t start = 0; start < s.size(); ++start) {
        if (s[start] != '{' && s[start] != '[') continue;
        Parser ps;
        ps.p = s.c_str() + start;
        ps.end = ps.p + (s.size() - start);
        JVal v;
        if (!ps.parseValue(v)) continue;
        if (v.type != JVal::TObj && v.type != JVal::TArr) continue;
        root = v;
        return true;
    }
    return false;
}

std::wstring Utf8ToW(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring w((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], need);
    return w;
}

std::string BaseName(const std::string& p)
{
    size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? p : p.substr(k + 1);
}

unsigned long long PidFromWhere(const std::string& where)
{
    size_t k = where.find("pid ");
    if (k == std::string::npos) return 0;
    return strtoull(where.c_str() + k + 4, nullptr, 10);
}

}

bool ParseFreezeOut(const std::string& json, FreezeInfo& out)
{
    JVal root;
    if (!Parse(json, root)) return false;
    if (root.type != JVal::TObj) return false;

    const JVal* e = root.find("error");
    if (e && e->type == JVal::TStr) {
        out.error = e->str;
        out.ok = false;
        return true;
    }

    const JVal* rootPid = root.find("root_pid");
    if (!rootPid) return false;

    const JVal* okv = root.find("ok");
    out.ok = okv ? okv->truthy() : (out.exitCode == 0);

    const JVal* exe = root.find("exe");
    if (exe && exe->type == JVal::TStr) out.exe = Utf8ToW(exe->str);
    const JVal* img = root.find("image");
    if (img && img->type == JVal::TStr) out.image = Utf8ToW(img->str);

    const JVal* pc = root.find("process_count");
    if (pc) out.procCount = (int)pc->numOr(0);

    const JVal* wins = root.find("windows");
    if (wins) {
        const JVal* count = wins->find("count");
        if (count) out.windowCount = (int)count->numOr(0);
        const JVal* list = wins->find("list");
        if (list && list->type == JVal::TArr) {
            long long bestArea = -1;
            int visibleCount = 0;
            for (size_t i = 0; i < list->arr.size(); ++i) {
                const JVal& w = list->arr[i];
                const JVal* l = w.find("left");   const JVal* t = w.find("top");
                const JVal* r = w.find("right");  const JVal* b = w.find("bottom");
                if (!l || !t || !r || !b) continue;
                RECT rc = { (LONG)l->numOr(0), (LONG)t->numOr(0), (LONG)r->numOr(0), (LONG)b->numOr(0) };
                bool vis = w.find("visible_before") ? w.find("visible_before")->truthy() : true;
                if (vis && rc.right > rc.left && rc.bottom > rc.top) visibleCount++;
                if (rc.right <= rc.left || rc.bottom <= rc.top) continue;
                long long area = (long long)(rc.right - rc.left) * (rc.bottom - rc.top);
                long long score = vis ? (area + (1LL << 40)) : area;
                if (score > bestArea) {
                    bestArea = score;
                    out.engineRect = rc;
                    out.haveEngineRect = true;
                    const JVal* ic = w.find("iconic_before");
                    out.engineIconicBefore = ic ? ic->truthy() : false;
                }
            }
            if (visibleCount > 0) out.windowCount = visibleCount;
        }
    }

    const JVal* tcb = root.find("tree_commit_before");
    if (tcb) out.commitHeld = (unsigned long long)tcb->numOr(0);

    unsigned long long freed = 0;
    unsigned long long commitBefore = 0;
    bool anyProc = false;
    const JVal* procs = root.find("processes");
    if (procs && procs->type == JVal::TArr) {
        for (size_t i = 0; i < procs->arr.size(); ++i) {
            const JVal& p = procs->arr[i];
            const JVal* before = p.find("before");
            const JVal* after  = p.find("after");
            if (!before || !after) continue;
            const JVal* bws = before->find("working_set");
            const JVal* aws = after->find("working_set");
            const JVal* bpr = before->find("private_usage");
            if (bws && aws) {
                double b = bws->numOr(0), a = aws->numOr(0);
                if (b > a) freed += (unsigned long long)(b - a);
                anyProc = true;
            }
            if (bpr) commitBefore += (unsigned long long)bpr->numOr(0);
        }
    }
    if (anyProc) {
        out.freedWs = freed;
        if (out.commitHeld == 0) out.commitHeld = commitBefore;
    } else {
        const JVal* delta = root.find("delta");
        if (delta) {
            const JVal* d = delta->find("working_set");
            if (d && d->numOr(0) < 0) out.freedWs = (unsigned long long)(-d->numOr(0));
        }
    }
    if (out.procCount == 0 && procs && procs->type == JVal::TArr)
        out.procCount = (int)procs->arr.size();
    if (out.windowCount == 0) out.windowCount = out.haveEngineRect ? 1 : 0;
    return true;
}

bool ParseThawOut(const std::string& json, ThawInfo& out)
{
    JVal root;
    if (!Parse(json, root)) return false;
    if (root.type != JVal::TObj) return false;

    const JVal* e = root.find("error");
    if (e && e->type == JVal::TStr) {
        out.error = e->str;
        out.ok = false;
        return true;
    }

    const JVal* ok = root.find("ok");
    if (ok) out.ok = ok->truthy();
    else out.ok = (out.error.empty());

    const JVal* sff = root.find("state_file_found");
    if (sff) out.stateFileFound = sff->truthy();
    const JVal* pr = root.find("processes_resumed");
    if (pr) out.processesResumed = (int)pr->numOr(0);
    const JVal* ps = root.find("processes_skipped");
    if (ps) out.processesSkipped = (int)ps->numOr(0);
    const JVal* tv = root.find("threads_verified");
    if (tv) out.threadsVerified = (int)tv->numOr(0);
    const JVal* msg = root.find("message_ok");
    if (msg) out.msgOk = msg->truthy();
    const JVal* wl = root.find("wake_latency_ms");
    if (wl) out.wakeMs = wl->numOr(0);
    return true;
}

bool ParseListOut(const std::string& json, std::vector<ListRowUi>& out)
{
    JVal root;
    if (!Parse(json, root)) return false;
    if (root.type != JVal::TObj) return false;
    const JVal* procs = root.find("processes");
    if (!procs || procs->type != JVal::TArr) return false;
    for (size_t i = 0; i < procs->arr.size(); ++i) {
        const JVal& p = procs->arr[i];
        ListRowUi r;
        const JVal* pid = p.find("pid");
        const JVal* exe = p.find("exe");
        const JVal* st  = p.find("suspend_state");
        const JVal* mem = p.find("memory");
        if (!pid || !exe) continue;
        r.pid = (unsigned long long)pid->numOr(0);
        r.exe = Utf8ToW(exe->str);
        r.running = st && st->type == JVal::TStr && st->str == "running";
        r.suspended = st && st->type == JVal::TStr &&
                      (st->str == "SUSPENDED" || st->str == "partial" ||
                       st->str == "suspended");
        const JVal* th = p.find("threads");
        if (th) r.threads = (int)th->numOr(0);
        const JVal* ts = p.find("threads_suspended");
        if (ts) r.threadsSuspended = (int)ts->numOr(0);
        if (mem) {
            const JVal* ws = mem->find("working_set");
            const JVal* pr = mem->find("private_usage");
            if (ws) r.workingSet = (unsigned long long)ws->numOr(0);
            if (pr) r.commit = (unsigned long long)pr->numOr(0);
        }
        out.push_back(r);
    }
    return true;
}

bool ParseGuardOut(const std::string& json, std::vector<GuardComp>& out)
{
    JVal root;
    if (!Parse(json, root)) return false;
    if (root.type != JVal::TObj) return false;
    const JVal* comps = root.find("components");
    if (!comps || comps->type != JVal::TArr) return false;
    for (size_t i = 0; i < comps->arr.size(); ++i) {
        const JVal& c = comps->arr[i];
        GuardComp g;
        const JVal* comp = c.find("component");
        const JVal* fam  = c.find("family");
        const JVal* src  = c.find("source");
        const JVal* wh   = c.find("where");
        if (comp && comp->type == JVal::TStr) g.component = Utf8ToW(comp->str);
        if (fam  && fam->type  == JVal::TStr) g.family = Utf8ToW(fam->str);
        if (src  && src->type  == JVal::TStr) g.source = Utf8ToW(src->str);
        if (wh   && wh->type   == JVal::TStr) {
            g.where = Utf8ToW(wh->str);
            g.pid = PidFromWhere(wh->str);
        }
        out.push_back(g);
    }
    return true;
}

std::wstring Utf8ToWPublic(const std::string& s) { return Utf8ToW(s); }
std::string BaseNamePublic(const std::string& p) { return BaseName(p); }
