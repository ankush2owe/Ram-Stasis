// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frz.h"

namespace {

std::string LowerAscii(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c - 'A' + 'a');
    }
    return s;
}

std::string BaseNameOf(const std::string& p)
{
    size_t k = p.find_last_of("\\/");
    return (k == std::string::npos) ? p : p.substr(k + 1);
}

}

bool AcMatchName(const std::string& name, bool kernelMedia,
                 std::string& component, std::string& family)
{
    std::string base = LowerAscii(BaseNameOf(name));
    if (base.empty()) return false;

    struct Rule { const char* n; const char* fam; };
    static const Rule kExact[] = {
        { "easyanticheat.exe", "Easy Anti-Cheat" },
        { "easyanticheat.sys", "Easy Anti-Cheat" },
        { "easyanticheat.eac", "Easy Anti-Cheat" },
        { "eacclient.dll",     "Easy Anti-Cheat" },
        { "easyhook.dll",      "Easy Anti-Cheat" },
        { "beservice.exe",     "BattlEye" },
        { "beservice.dll",     "BattlEye" },
        { "beservice.sys",     "BattlEye" },
        { "bedaisy.sys",       "BattlEye" },
        { "vgk.sys",           "Vanguard" },
        { "vgc.sys",           "Vanguard" },
    };
    for (size_t i = 0; i < sizeof(kExact) / sizeof(kExact[0]); ++i) {
        if (base == kExact[i].n) {
            component = base;
            family = kExact[i].fam;
            return true;
        }
    }

    struct Prefix { const char* p; const char* fam; };
    static const Prefix kPrefix[] = {
        { "eacup",     "Easy Anti-Cheat" },
        { "beservice", "BattlEye" },
    };
    for (size_t i = 0; i < sizeof(kPrefix) / sizeof(kPrefix[0]); ++i) {
        size_t n = strlen(kPrefix[i].p);
        if (base.size() >= n && base.compare(0, n, kPrefix[i].p) == 0) {
            component = base;
            family = kPrefix[i].fam;
            return true;
        }
    }

    if (kernelMedia) {
        static const char* kGeneric[] = {
            "anticheat", "easyanticheat", "battleye", "vgk", "vgc",
            "bedaisy", "nprotect", "xigncode",
        };
        for (size_t i = 0; i < sizeof(kGeneric) / sizeof(kGeneric[0]); ++i) {
            if (base.find(kGeneric[i]) != std::string::npos) {
                component = base;
                family = "generic kernel anti-cheat";
                return true;
            }
        }
    }
    return false;
}

namespace {

std::string ProcLabel(DWORD pid, const std::wstring& exe)
{
    char buf[64];
    sprintf_s(buf, sizeof(buf), "pid %lu", (unsigned long)pid);
    std::string s(buf);
    if (!exe.empty()) s += " (" + W2U(exe) + ")";
    return s;
}

bool ScanModulesToolhelp(DWORD pid, const std::wstring& exe,
                         std::vector<AcHit>& hits, unsigned& scanned)
{
    HANDLE snap = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 4; ++attempt) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_BAD_LENGTH) return false;
        Sleep(10);
    }
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            scanned++;
            std::string name = W2U(me.szModule);
            std::string comp, fam;
            if (AcMatchName(name, false, comp, fam)) {
                AcHit hit;
                hit.component = comp;
                hit.family = fam;
                hit.source = "module";
                hit.where = name + " loaded in " + ProcLabel(pid, exe);
                hits.push_back(hit);
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return true;
}

bool ScanModulesPsapi(DWORD pid, const std::wstring& exe,
                      std::vector<AcHit>& hits, unsigned& scanned)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return false;
    HMODULE mods[1024];
    DWORD need = 0;
    bool any = false;
    if (EnumProcessModulesEx(h, mods, (DWORD)sizeof(mods), &need, LIST_MODULES_ALL)) {
        DWORD n = need / (DWORD)sizeof(HMODULE);
        if (n > 1024) n = 1024;
        for (DWORD i = 0; i < n; ++i) {
            wchar_t base[MAX_PATH];
            if (!GetModuleBaseNameW(h, mods[i], base, MAX_PATH)) continue;
            scanned++;
            any = true;
            std::string name = W2U(std::wstring(base));
            std::string comp, fam;
            if (AcMatchName(name, false, comp, fam)) {
                AcHit hit;
                hit.component = comp;
                hit.family = fam;
                hit.source = "module";
                hit.where = name + " loaded in " + ProcLabel(pid, exe);
                hits.push_back(hit);
            }
        }
    }
    CloseHandle(h);
    return any;
}

std::vector<std::pair<DWORD, std::wstring> > GuardSet(DWORD rootPid, bool wholeSystem)
{
    std::vector<std::pair<DWORD, std::wstring> > out;
    std::vector<ProcRow> all;
    if (!SnapshotProcesses(all)) return out;

    if (wholeSystem) {
        for (size_t i = 0; i < all.size(); ++i)
            out.push_back(std::make_pair(all[i].pid, all[i].exe));
        return out;
    }

    std::vector<DWORD> pids;
    pids.push_back(rootPid);
    DWORD parent = 0;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].pid == rootPid) { parent = all[i].parentPid; break; }

    std::vector<DWORD> frontier;
    frontier.push_back(rootPid);
    for (;;) {
        if (frontier.empty()) break;
        DWORD cur = frontier.back();
        frontier.pop_back();
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].parentPid != cur || all[i].pid == 0) continue;
            bool dup = false;
            for (size_t k = 0; k < pids.size(); ++k)
                if (pids[k] == all[i].pid) { dup = true; break; }
            if (dup) continue;
            pids.push_back(all[i].pid);
            frontier.push_back(all[i].pid);
        }
    }
    if (parent != 0) {
        bool dup = false;
        for (size_t k = 0; k < pids.size(); ++k) if (pids[k] == parent) dup = true;
        if (!dup) pids.push_back(parent);
    }

    for (size_t i = 0; i < pids.size(); ++i) {
        std::wstring exe;
        for (size_t k = 0; k < all.size(); ++k)
            if (all[k].pid == pids[i]) { exe = all[k].exe; break; }
        out.push_back(std::make_pair(pids[i], exe));
    }
    return out;
}

}

bool GuardCheckTree(DWORD rootPid, std::vector<AcHit>& hits,
                    unsigned& procsScanned, unsigned& modsScanned)
{
    procsScanned = 0;
    modsScanned = 0;
    std::vector<std::pair<DWORD, std::wstring> > set = GuardSet(rootPid, false);
    if (set.empty()) return false;

    bool nameHit = false;
    for (size_t i = 0; i < set.size(); ++i) {
        procsScanned++;
        std::string comp, fam;
        if (AcMatchName(W2U(set[i].second), false, comp, fam)) {
            AcHit hit;
            hit.component = comp;
            hit.family = fam;
            hit.source = "process";
            hit.where = ProcLabel(set[i].first, set[i].second);
            hits.push_back(hit);
            nameHit = true;
        }
    }

    if (nameHit) return true;

    for (size_t i = 0; i < set.size(); ++i) {
        if (!ScanModulesPsapi(set[i].first, set[i].second, hits, modsScanned))
            ScanModulesToolhelp(set[i].first, set[i].second, hits, modsScanned);
    }
    return true;
}

bool GuardCheckKernel(std::vector<AcHit>& hits, unsigned& driversScanned)
{
    driversScanned = 0;

    DWORD need = 0;
    if (!EnumDeviceDrivers(nullptr, 0, &need)) {
        DWORD e = GetLastError();
        if (e != ERROR_INSUFFICIENT_BUFFER || need == 0) {
            Log("guard: EnumDeviceDrivers(size probe) failed win32=%lu need=%lu",
                (unsigned long)e, (unsigned long)need);
            return false;
        }
    }
    if (need == 0) need = 8192 * (DWORD)sizeof(LPVOID);

    std::vector<LPVOID> bases((size_t)need / sizeof(LPVOID) + 16, nullptr);
    DWORD bytes = 0;
    if (!EnumDeviceDrivers(bases.data(), (DWORD)(bases.size() * sizeof(LPVOID)), &bytes)) {
        Log("guard: EnumDeviceDrivers failed win32=%lu need=%lu",
            (unsigned long)GetLastError(), (unsigned long)bytes);
        return false;
    }
    unsigned n = (unsigned)(bytes / sizeof(LPVOID));

    for (unsigned i = 0; i < n; ++i) {
        wchar_t baseName[MAX_PATH];
        baseName[0] = 0;
        if (GetDeviceDriverBaseNameW(bases[i], baseName, MAX_PATH) == 0) continue;
        std::string name = W2U(std::wstring(baseName));
        if (name.empty()) continue;
        driversScanned++;

        std::string comp, fam;
        if (!AcMatchName(name, true, comp, fam)) continue;

        wchar_t fileName[MAX_PATH];
        fileName[0] = 0;
        GetDeviceDriverFileNameW(bases[i], fileName, MAX_PATH);

        AcHit hit;
        hit.component = comp;
        hit.family = fam;
        hit.source = "driver";
        hit.where = W2U(std::wstring(fileName));
        if (hit.where.empty()) hit.where = name;
        hits.push_back(hit);
    }
    return true;
}

bool GuardCheckSystem(std::vector<AcHit>& hits, unsigned& procsScanned, unsigned& modsScanned,
                      unsigned& driversScanned)
{
    procsScanned = 0;
    modsScanned = 0;
    driversScanned = 0;
    std::vector<std::pair<DWORD, std::wstring> > set = GuardSet(0, true);
    if (set.empty()) return false;
    for (size_t i = 0; i < set.size(); ++i) {
        procsScanned++;
        std::string comp, fam;
        bool flagged = false;
        if (AcMatchName(W2U(set[i].second), false, comp, fam)) {
            AcHit hit;
            hit.component = comp;
            hit.family = fam;
            hit.source = "process";
            hit.where = ProcLabel(set[i].first, set[i].second);
            hits.push_back(hit);
            flagged = true;
        }
        if (flagged) continue;
        if (!ScanModulesPsapi(set[i].first, set[i].second, hits, modsScanned))
            ScanModulesToolhelp(set[i].first, set[i].second, hits, modsScanned);
    }
    unsigned drivers = 0;
    GuardCheckKernel(hits, drivers);
    driversScanned = drivers;
    modsScanned += drivers;
    return true;
}

bool GuardBlocks(const std::vector<AcHit>& hits, bool force)
{
    return !hits.empty() && !force;
}

void PrintGuardHits(const std::vector<AcHit>& hits)
{
    if (hits.empty()) return;
    Out("  %-26s %-28s %-8s %s\n", "COMPONENT", "FAMILY", "SOURCE", "WHERE");
    for (size_t i = 0; i < hits.size(); ++i) {
        Out("  %-26s %-28s %-8s %s\n",
            hits[i].component.c_str(), hits[i].family.c_str(),
            hits[i].source.c_str(), hits[i].where.c_str());
    }
}

int CmdGuard()
{
    std::vector<AcHit> hits;
    unsigned procs = 0, mods = 0, drivers = 0;
    bool ok = GuardCheckSystem(hits, procs, mods, drivers);

    if (g_ctx.json) {
        Jw j;
        j.obj(nullptr);
        j.s("command", "guard");
        j.b("scan_ok", ok);
        j.un("processes_scanned", procs);
        j.un("kernel_drivers_scanned", drivers);
        j.un("modules_and_drivers_scanned", mods);
        j.un("hits", (unsigned long long)hits.size());
        j.arr("components");
        for (size_t i = 0; i < hits.size(); ++i) {
            j.objElem();
            j.s("component", hits[i].component.c_str());
            j.s("family", hits[i].family.c_str());
            j.s("source", hits[i].source.c_str());
            j.s("where", hits[i].where.c_str());
            j.endObj();
        }
        j.endArr();
        j.b("blocked", !hits.empty());
        j.endObj();
        Out("\n");
    } else {
        Out("=== ANTI-CHEAT GUARD  (system-wide, read-only) ===\n");
        Out("  processes scanned    : %u\n", procs);
        Out("  process modules      : %u\n", mods - drivers);
        Out("  kernel drivers       : %u\n", drivers);
        Out("  modules+drivers      : %u\n", mods);
        if (!ok) Out("  NOTE: some process module lists could not be read (protected / cross-bitness)\n");
        if (hits.empty()) {
            Out("\n  RESULT: clean - no anti-cheat component found.\n");
        } else {
            Out("\n  RESULT: %u anti-cheat component(s) found:\n\n", (unsigned)hits.size());
            PrintGuardHits(hits);
            Out("\n  Freezing a process supervised by any of these risks a permanent ban.\n"
                "  RamStasis.exe will refuse such a target unless --force is given.\n");
        }
    }
    return hits.empty() ? 0 : 8;
}
