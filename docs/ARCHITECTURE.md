# RamStasis engine - architecture

Process freezer for Windows x64. Freezes a **process tree**, pages its resident
memory out, and wakes it later without losing a single increment of application
state. The engine is compiled into `RamStasis.exe` and runs in CLI mode when the
first argument is an engine command.

- Toolchain: MSVC v145, `/W4 /WX`, x64, C++17, no third-party dependencies.
- Toolset and SDK are resolved by `build.cmd`; the SDK headers cited below are
  from **Windows SDK 10.0.26100.0**.
- Build: `build.cmd` -> `bin\RamStasis.exe` (engine + GUI in one exe).

Nothing here injects code into a target process. No DLL is loaded into a target,
no thread is created in a target, no byte of a target's memory is written.

---

## 1. Module map

| File | Responsibility |
|---|---|
| `src/frz_main.cpp` | CLI parse, flag/command dispatch, exit codes |
| `src/frz_util.cpp` | target resolution, process/thread enumeration, JSON writer, formatting |
| `src/frz_measure.cpp` | `GetPerformanceInfo` + `EnumPageFilesW` stats, all number formatting |
| `src/frz_tree.cpp` | process-tree walk (transitive `th32ParentProcessID`), `tree` verb |
| `src/frz_guard.cpp` | anti-cheat guard: name matcher, module scan, kernel driver scan, `guard` verb |
| `src/frz_window.cpp` | window enumeration, minimize, verify, restore (ghost suppression) |
| `src/frz_state.cpp` | on-disk freeze bookkeeping, format 1 + 2 |
| `src/frz_ops.cpp` | `freeze` / `thaw` / `stats` / `list`, suspend/resume primitives |
| `src/frz_watch.cpp` | freeze-until-clicked mode (low-level mouse hook) |
| `src/frz_selftest.cpp` | 36 end-to-end checks against scratch children |

---

## 2. Every API used, with its SDK header and line

### 2.1 Statistics (requirement: no undocumented class-2 blob)

| Symbol | Header | Line | Use |
|---|---|---|---|
| `PERFORMANCE_INFORMATION` | `um\psapi.h` | 534-549 | struct; `CommitTotal`/`CommitLimit`/`CommitPeak` are **`SIZE_T`**, not `DWORD` |
| `GetPerformanceInfo` | `um\psapi.h` | 551-556 | system commit / physical / kernel / handle counts |
| `ENUM_PAGE_FILE_INFORMATION` | `um\psapi.h` | 564-570 | `TotalSize`, `TotalInUse`, `PeakUsage` - **in pages** |
| `PENUM_PAGE_FILE_CALLBACKW` | `um\psapi.h` | 572 | callback signature |
| `EnumPageFilesW` | `um\psapi.h` | 576-581 | per-pagefile stats (e.g. `pagefile.sys`) |
| `GetProcessMemoryInfo` | `um\psapi.h` | 120 (K32 alias 520) | `WorkingSetSize`, `PrivateUsage` (commit), `PagefileUsage` |
| `EmptyWorkingSet` | `um\psapi.h` | 98 (K32 alias 247) | page-out |
| `EnumProcessModulesEx` | `um\psapi.h` | 63 (K32 alias 156) | module-list fallback for cross-bitness targets |
| `GetModuleBaseNameW` | `um\psapi.h` | 95 | module basename in the fallback path |

**Why `SIZE_T`:** passing `sizeof(DWORD)`-based assumptions to `GetPerformanceInfo`
is the classic failure - the call returns `FALSE` with `ERROR_INVALID_PARAMETER`
(24) because `cb` must equal `sizeof(PERFORMANCE_INFORMATION)`, which contains
five `SIZE_T` fields and is therefore 4 bytes larger than the naive DWORD layout.
`frz` sets `pi.cb = sizeof(pi)` and never a literal.

**`free commit` is always printed:** `CommitLimit - CommitTotal`. It is not a
field, it is the subtraction that answers "can I still start something".

### 2.2 Process / thread control

| Symbol | Header / origin | Line | Use |
|---|---|---|---|
| `CreateToolhelp32Snapshot` | `um\tlhelp32.h` | 34 | process list, module lists |
| `PROCESSENTRY32W` | `um\tlhelp32.h` | 70-91 | `th32ProcessID`, `th32ParentProcessID`, `cntThreads` |
| `Process32FirstW` / `Process32NextW` | `um\tlhelp32.h` | 160 / 167 | process walk |
| `MODULEENTRY32W` | `um\tlhelp32.h` | 196-225 | module walk |
| `Module32FirstW` / `Module32NextW` | `um\tlhelp32.h` | 259 / 266 | loaded modules per process (`TH32CS_SNAPMODULE`) |
| `OpenProcess` | `um\processthreadsapi.h` | 888 | freeze handle |
| `OpenThread` | `um\processthreadsapi.h` | 246 | per-thread fixup / verification |
| `GetProcessTimes` | `um\processthreadsapi.h` | 137 | creation time (pid-reuse guard), CPU proof |
| `GetExitCodeProcess` | `um\processthreadsapi.h` | 178 | liveness |
| `TerminateProcess` | `um\processthreadsapi.h` | 170 | selftest teardown only |
| `CreateProcessW` | `um\processthreadsapi.h` | 386 | selftest children |
| `QueryFullProcessImageNameW` | `um\WinBase.h` | 3481 | full image path |
| `NtSuspendProcess` | `ntdll.dll` export | n/a | the freeze |
| `NtResumeProcess` | `ntdll.dll` export | n/a | the thaw |
| `NtSuspendThread` | `ntdll.dll` export | n/a | escapee fixup |
| `NtResumeThread` | `ntdll.dll` export | n/a | escapee fixup, reversed |
| `NtQueryInformationThread` | `ntdll.dll` export | n/a | `ThreadSuspendCount` (class **35**, undocumented) |
| `NtQuerySystemInformation` | `ntdll.dll` export | n/a | `SystemModuleInformation` (class **11**, undocumented) |

`NtSuspendProcess` / `NtResumeProcess` are exported by `ntdll.dll` but are not in
`ntdll.lib`, so they are resolved with `GetProcAddress` and typed by hand. The
documented `winternl.h` enum only reaches `SystemPerformanceInformation = 2`
(`um\winternl.h:393`); class 11 is out of that range by design and is declared
locally from the `ntddk.h`/phnt layout.

### 2.3 Windows (ghost suppression)

| Symbol | Header | Line | Use |
|---|---|---|---|
| `EnumWindows` | `um\winuser.h` | 10189 | top-level window enumeration |
| `GetWindowThreadProcessId` | `um\winuser.h` | 10273 | window -> pid |
| `GetWindowTextW` | `um\winuser.h` | 9007 | window title (reporting) |
| `IsWindowVisible` | `um\winuser.h` | 4845 | prior visibility |
| `IsIconic` | `um\winuser.h` | 4857 | prior minimised state, and post-freeze verification |
| `GetWindowRect` | `um\winuser.h` | 9055 | last-known rect, handed to the overlay |
| `ShowWindowAsync` | `um\winuser.h` | 4674 | `SW_MINIMIZE` before suspend, `SW_RESTORE` after resume |
| `IsHungAppWindow` | `um\winuser.h` | 1852 | honest ghost report |
| `GetForegroundWindow` | `um\winuser.h` | 8514 | which process owns the screen |
| `SendMessageTimeoutW` | `um\winuser.h` | 3762 | hung proof / wake latency |

### 2.4 IPC / bookkeeping

| Symbol | Header | Line | Use |
|---|---|---|---|
| `CreateFileMappingW` | `um\memoryapi.h` | 176 | selftest shared counters |
| `MapViewOfFile` | `um\memoryapi.h` | 207 | selftest shared counters |

---

## 3. Freeze - exact ordered sequence

Root = the process named on the command line. The target set is the whole
transitive tree unless `--no-tree` is passed.

```
 0. Resolve the target WITHOUT opening it.
      CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS) -> pid, exe name, parent pid.
      No OpenProcess call has happened yet, on purpose.

 1. Anti-cheat guard (D3). Read-only. Runs BEFORE any freeze handle exists.
      a. process names: the target, every transitive child, and the target's
         PARENT (a launcher such as BEService.exe is the parent of the game).
         If any image name matches -> refuse, do not even enumerate modules.
      b. loaded modules: CreateToolhelp32Snapshot(TH32CS_SNAPMODULE) +
         Module32FirstW/NextW per process; EnumProcessModulesEx as the fallback
         for cross-bitness targets.
      c. kernel drivers: NtQuerySystemInformation(SystemModuleInformation = 11),
         walk NumberOfModules, match each driver's basename.
      Any hit without --force -> print the components, exit code 8, and stop.
      With --force -> print the same list plus a ban-risk warning, continue.

 2. Refuse to stack suspends.
      One directory pass over state\frz-*.state; if any pid of the target tree is
      already recorded, refuse (exit 5). Suspend and resume must stay 1:1.

 3. Build the tree and order it.
      Transitive walk on th32ParentProcessID, depth recorded.
      Freeze order = children first (deepest depth first), so no parent can spawn
      a new child into a subtree already swept.

 4. Open every handle in that order.
      OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION |
                   PROCESS_VM_READ | PROCESS_SET_QUOTA | PROCESS_VM_OPERATION)
      A child that is already gone (ERROR_INVALID_PARAMETER 87) is skipped and
      reported, not an error. A root that cannot be opened aborts (exit 4).

 5. PRE-FLIGHT numbers (D1), before anything is touched.
      GetPerformanceInfo -> CommitTotal, CommitLimit, free commit.
      EnumPageFilesW    -> pagefile total/in-use/peak per file.
      Sum the tree's PROCS: working set and private (commit).
      Print both. This is the baseline the after-numbers are compared against.

 6. MINIMISE the windows worth hiding. NO INJECTION (D2).
      EnumWindows once, filtered by pid set.
      Per window record: IsWindowVisible, IsIconic, GetWindowRect, title.
      Post SW_MINIMIZE only for visible && !iconic. An already-iconic window is
      left alone and an invisible one is never touched, so restore can be exact
      and nothing is undone that we did not do.

 7. Poll IsIconic until the minimise actually lands (<= 500 ms, 10 ms granularity).
      ShowWindowAsync only POSTS to the owner thread's queue; it returns before
      the window has moved. The poll therefore has to happen BEFORE the suspend:
      a suspended owner thread never pumps its queue, so a post that has not been
      consumed is never consumed. A window that never turns iconic is reported as
      such - the freeze does not claim a suppression it did not get.

 8. Sleep(150) - the mandated settle, taken after the minimise has landed.

 9. Suspend, children first.
      ntdll!NtSuspendProcess(handle).
      Then re-read every thread's suspend count (NtQueryInformationThread 35).
      NtSuspendProcess is an ITERATIVE walk inside PsSuspendProcess, not atomic:
      a thread created during the walk escapes. Any thread that did not receive
      exactly +1 is suspended individually with NtSuspendThread and remembered in
      the `xthr` list, to be resumed exactly once on thaw.

10. Late-child rescan (max 3 passes).
      Rebuild the tree; any pid not yet handled is opened, its windows minimised,
      polled iconic (<= 500 ms), then Sleep(150), suspended, marked `late_child`.
      Stops when a pass finds nothing new.

11. Trim, verified per pid.
      EmptyWorkingSet per pid, then re-read WorkingSetSize. The kernel can
      silently no-op this, so the ONLY accepted proof is
      workingSet_after < workingSet_before. One retry. Result recorded per pid.

12. AFTER numbers.
      GetPerformanceInfo + EnumPageFilesW again.
      Commit is printed next to working set on purpose: freeing RAM does not free
      commit, and the pagefile is the real ceiling.

13. Commit warning (D1).
      If free commit < 15% of CommitLimit, print the warning. `--quiet` does not
      suppress it, ever.

14. Ghost report.
      GetForegroundWindow, and per window IsIconic / IsWindowVisible /
      IsHungAppWindow, all read after the suspend.

15. Write the state file (format 2), fsync-by-close.
      Every pid in the tree: creation time, depth, suspend call count, ws before,
      ws after, commit, alive flag, per-thread (tid, baseline suspend count),
      escaped-thread list.
      Every window: pid, hwnd, visible_before, iconic_before, minimized_by_us,
      last-known rect. The overlay positions its card from that rect.
```

Exit codes: `0` ok, `2` usage/no such process, `3` refused (no state file or pid
reused), `4` `OpenProcess` denied, `5` already frozen, `6` incomplete,
`7` hook install failed, `8` blocked by the anti-cheat guard.

---

## 4. Thaw - exact ordered sequence

```
 0. Resolve the named pid without opening it.
 1. Load the state file. Missing -> REFUSE (exit 3). frz never guesses how many
    times something was suspended.
 2. Pid-reuse guard on the NAMED pid: its current creation time must equal the
    recorded one. Mismatch -> REFUSE (exit 3): the pid now belongs to a stranger.
 3. Order root first, then children by depth.
    Per pid:
      a. not alive at freeze time      -> skip + report
      b. process gone                  -> skip + report (normal, not an error)
      c. creation time mismatch        -> skip + report (pid recycled)
      d. OpenProcess denied            -> report, continue with the rest
      e. verify every recorded thread still shows baseline+1
      f. capture CPU time (root only), then NtResumeProcess
      g. NtResumeThread once for each thread in the `xthr` list
      h. restore windows for this pid (see below)
      i. Sleep(60), re-enumerate: every recorded thread must be back at baseline
         (not 0 - an app that had its own suspended threads keeps them)
 4. Wake measurement, after the whole tree is awake.
    SendMessageTimeoutW(WM_NULL) to the root's window, and CPU-time advance.
    A console app's window is pumped by conhost.exe, so measuring before the
    children are resumed would time out and report a false 5 s wake.
 5. Delete the state file only if every pid resumed and every count balanced.
    Otherwise keep it and exit 6, so the operation can be retried.
```

Window restore rules:

The freeze-time predicate is `wasVisible && !wasIconic` - NOT `wasVisible`
alone. `user32!IsWindowVisible` returns TRUE for a **minimized** window (the
iconic bit is never consulted), so `wasVisible` by itself would wrongly
minimize a window the user had already hidden, and would wrongly restore it
later. The three-way restore predicate is:

- window gone -> report, nothing to do.
- `wasVisible == false` -> leave it alone. The window was hidden before the
  freeze and must stay hidden; restoring it would make it appear out of
  nowhere.
- `wasVisible && wasIconic` (it was already iconic before the freeze) ->
  `ShowWindowAsync(hwnd, SW_SHOWMINNOACTIVE)`. It was minimized by the user,
  so it goes back to minimized - we only clear the iconic bit we set.
- `wasVisible && !wasIconic` (we minimized it) -> `ShowWindowAsync(hwnd,
  SW_RESTORE)`.
- if the window was already restored by the app itself -> report, nothing to
  do.

`ShowWindow` (synchronous) is never used on another process's window.
`ShowWindowAsync` posts to the owning thread's queue
(`win32kfull!NtUserShowWindowAsync` -> `PostEventMessageEx`), so it only
takes effect once that thread pumps - hence the freeze-side 500 ms
`IsIconic` poll before the process is suspended, and hence the thaw-side
resume happens BEFORE the restore.

---

## 5. State file format

`<exedir>\state\frz-<rootpid>.state`, plain text, one record per line, `%lu` /
`%llu` numerics so there is no locale or float in the round trip.

```
frz-state 2
root <pid>
image <utf8 path>
suspended_at <unix seconds>
proc <pid> <createTime> <depth> <suspendCalls> <wsBefore> <wsAfter> <commit> <alive>
exe  <pid> <utf8 name>
thr  <pid> <tid> <baseline suspend count>
xthr <pid> <tid>
win  <pid> <hwnd> <visible> <iconic> <minimizedByUs> <left> <top> <right> <bottom>
```

Format **1** (single pid, no tree, no windows) is still read, so a process frozen
by an earlier build can be thawed instead of stranded. Format 2 is written from
now on.

Thaw also accepts a child pid: the state directory is scanned for any file whose
pid set contains it.

---

## 6. Design rationale - the locked decisions

### D1 - pagefile is the ceiling, not RAM  [APPLIED]
The pagefile is pre-sized to 24 GB initial / 32 GB max on the system drive
(`PagingFiles = <system drive>:\pagefile.sys 24576 32768`, `AutomaticManagedPagefile = False`)
and was confirmed active after the 2026-10-21 16:20 reboot
(`Win32_PageFileUsage AllocatedBaseSize = 24576 MB`).

Why pre-size instead of system-managed: a system-managed pagefile **grows
reactively**, i.e. under memory pressure - the worst possible moment to stall
while the OS extends a file. Pre-sizing removes that stall.

Consequence for the product: trimming returns RAM but **never returns commit**.
Commit is a promise the OS has already made; only exit or a commit-lowering
allocation releases it. So the pre-flight prints free commit
(`CommitLimit - CommitTotal`) and the tree's total commit, and if free commit is
below 15% of `CommitLimit` the freeze prints a warning that the pagefile, not
RAM, is now the binding constraint - a new large launch can still fail with
"out of memory" while gigabytes of RAM sit free.

### D2 - ghost dialog: minimise, never inject
Injecting `DisableProcessWindowsGhosting` (or any DLL) into a target trips AV
heuristics and anti-cheat bans, which would make the tool unusable for its
purpose. So the mechanism is `ShowWindowAsync(hwnd, SW_MINIMIZE)` on visible,
non-iconic top-level windows, a poll of `IsIconic` until the minimise lands
(<= 500 ms), `Sleep(150)`, then `NtSuspendProcess`.

What this does NOT do, stated plainly because the earlier draft of this file got
both wrong:

- Minimising does **not** exempt a window from ghosting. `win32kfull`
  `_ShouldGhostWindow` tests `WS_VISIBLE`, top-level-ness and foreground
  equality, and has no `IsIconic` check. What actually happens is that the
  suspended app cannot repaint, so leaving a stale visible window on screen
  advertises "alive but broken" and gives the user a dead click target that the
  ghost dialog is entitled to answer. Minimising removes that click target and
  frees the screen space for the overlay card. Measured: a suspended, minimised
  Notepad did not ghost over 6 s with a real click, while a deliberately
  deadlocked control did - so the difference is the app's own state, not an
  exemption the minimise buys.
- Minimising does **not** release VRAM. `DXGI_STATUS_OCCLUDED` stops
  presentation only, and `D3DKMTEvict` needs the owning device handle, so there
  is no user-mode path to evict another process's residency. Measured:
  `GPU Process Memory\Dedicated Usage` moved by +0.00 MB across a minimise.
  RAM is reclaimed by `EmptyWorkingSet`, not by the minimise.

Ownership is recorded per window (`visible_before`, `iconic_before`,
`minimized_by_us`), and thaw restores only the windows frz itself minimised. An
already-iconic window is restored with `SW_SHOWMINNOACTIVE`; an invisible one is
never touched; only a window we made iconic from visible gets `SW_RESTORE`.

`frz` does not draw an overlay. Each minimised window's last-known rect is
written to the state file so that the separate overlay component can place a
card where the window used to be.

### D3 - anti-cheat guard: refuse, do not attempt
Suspending a process that kernel anti-cheat supervises gets the account banned.
The guard therefore runs before any freeze handle exists and covers process
names (target + all children + the target's **parent**), loaded modules per
process, and loaded kernel drivers.

Detected families: **Easy Anti-Cheat** (`EasyAntiCheat.exe/.sys/.eac`,
`eacclient.dll`, `EasyHook.dll`, any `eacup*`), **BattlEye** (`BEService.exe`,
`BEService.dll`, `BEService.sys`, `BEDaisy.sys`, any `beservice*`),
**Vanguard** (`vgk.sys`, `vgc.sys`), and generic kernel drivers whose basename
contains `anticheat`, `easyanticheat`, `battleye`, `vgk`, `vgc`, `bedaisy`,
`nprotect` or `xigncode`.

On a hit: the component and family are named, nothing is touched, and the exit
code is **8**. `--force` overrides with a loud, specific ban-risk warning.

The generic substring rule applies to **kernel drivers only**. A user-mode module
called `anti-cheat-report.dll` is not swept up; a driver called
`myanticheat_x64.sys` is.

### D4 - process trees are the default
A game or IDE is one process only in the loosest sense: browser renderers,
Unreal shaders, JetBrains helpers and launchers are children. Freezing the named
pid alone leaves the children running, they touch the parent's shared memory,
and the freeze is pointless. `freeze` therefore walks the tree transitively by
default (`--no-tree` opts out).

Order matters: **freeze children first, thaw the root first**. Freezing deepest
first means no parent is still running to spawn a child into a subtree already
swept. Thawing the root first means the thing the user is looking at starts
breathing immediately, while the children are still coming back.

Already-exited pids are skipped and reported, never treated as errors.

### D5 - documented statistics only
The old code read `NtQuerySystemInformation(SystemPerformanceInformation = 2)`
and parsed a 312-byte `SYSTEM_PERFORMANCE_INFORMATION` blob from
`winternl.h:328-330`. That blob is `Reserved1[312]` in the public header - the
fields the code wanted are only known from reverse-engineered layout, so any
servicing update could silently change the meaning of every number. The class-2
path and everything it fed were **deleted**.

Replacement: `psapi!GetPerformanceInfo` (`psapi.h:534-549`, `SIZE_T` commit
fields, `cb = sizeof(PERFORMANCE_INFORMATION)`) and `psapi!EnumPageFilesW`
(`psapi.h:564-581`, page counts converted with the reported `PageSize`).
`free commit` is the subtraction, always printed.

### D6 - UI rules
Every feature is a toggle row over the socket, driven by the menu - never a
config file and never a hidden flag. The row set and mode ids are a contract
between the menu and this binary, so a feature without a row is not finished.

---

## 7. Deliberate non-goals

- **No injection.** No `CreateRemoteThread`, no `VirtualAllocEx`, no
  `SetWindowsHookEx` into a target. The only hooks in the product are global
  low-level mouse hooks installed in *our own* process (watch mode).
- **No `--force` stacking.** `--force` means exactly one thing: override the
  anti-cheat guard. It does not mean "suspend something that is already
  suspended", because that breaks the 1:1 suspend/resume invariant and leaks a
  permanently frozen process. Re-freezing a recorded pid is refused.
- **No registry, driver or service writes.** The only files written are the
  state files under `state\` and the log under `logs\`.
