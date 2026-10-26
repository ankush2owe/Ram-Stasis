# RamStasis

**Zero-CPU pagefile process stasis for Windows.**

RamStasis parks a running application tree by suspending every thread and paging its
working set out to disk. The app keeps its exact in-memory state, uses no CPU, and
returns instantly when thawed. A native Win32 + Direct3D 11 + Dear ImGui front end
drives the engine embedded in the same single `RamStasis.exe`.

![RamStasis](assets/logo.svg)

## What it does

- **Stasis freeze** — suspend a process tree, verify every thread is suspended, trim
  the working set to the pagefile, and record the exact state on disk.
- **Instant thaw** — resume only the processes and threads that were suspended and
  restore only the windows RamStasis minimized.
- **Real memory accounting** — private working set is sampled with
  `NtQueryInformationProcess(ProcessVmCounters)` and locked at freeze time, so the
  freed-RAM figure reflects what actually left physical memory.
- **Process & window explorer** — live application list with icons, tree size,
  private RAM, commit charge and owned windows.
- **Swap and auto-thaw** — one hotkey swaps a running app with a parked one; parked
  apps can thaw automatically when they receive focus.
- **Pagefile manager** — inspect fixed drives, choose a pagefile target, and expand
  the commit limit without leaving the app.
- **Safe by default** — refuses to freeze system/shell processes and refuses trees
  protected by known anti-cheat drivers unless explicitly forced.
- **Tray resident** — left-click toggles the window, double-click restores, right-click
  opens the menu. Settings and frozen state survive a restart.
- **Single instance** — launching `RamStasis.exe` again wakes the existing window.

## Layout

```text
build.cmd              one-command build
assets\                RamStasis icon, in-app logo, SVG mark
src\                   CLI engine (frz_*.cpp)
src\ui\                Win32/DX11/ImGui front end
third_party\imgui\     vendored Dear ImGui
res\app.manifest       DPI awareness and Windows compatibility
bin\                   build output: RamStasis.exe (GUI + engine + CLI in one file)
state\                 runtime state and ramstasis-settings.json
logs\                  runtime logs
```

## Build

Requirements: Windows 10/11 x64, Visual Studio 2022 Build Tools with the
**Desktop development with C++** workload, and the Windows SDK.

```bat
build.cmd
build.cmd clean
```

`build.cmd` regenerates `assets\ramstasis.ico`, compiles the engine and the UI
together with `/W4 /WX`, embeds the icon and logo resources, and writes one
binary:

```text
bin\RamStasis.exe    GUI + engine + CLI + self-test
```

The build is statically linked (`/MT`) and needs no redistributable.

## Run

```bat
bin\RamStasis.exe
```

Everything is in that one file: the GUI spawns the engine from inside itself, and
there is no helper executable to lose or have quarantined. Runtime data lives in
`state\` and `logs\` beside the exe, or in the folder above `bin\` when the exe
sits in a `bin\` folder (those folders are created automatically). The same exe
also serves the CLI; a command as the first argument runs the engine instead of
the GUI:

```bat
RamStasis.exe list
RamStasis.exe freeze <pid|exe>
RamStasis.exe thaw   <pid|exe>
RamStasis.exe selftest
RamStasis.exe --help
```

`RamStasis.exe selftest` exercises suspend counts, zero-CPU verification, page-out
accounting, tree freeze/thaw, window restoration, and anti-cheat refusal.

## Hotkeys and tray

- **Ctrl + Alt + S** — swap the running slot with the parked slot (configurable).
- **Tray left-click** — show or hide RamStasis.
- **Tray double-click** — restore the window.
- **Tray right-click** — show/hide, swap, thaw all, settings, exit.
- **Esc** — cancel window picking or hotkey capture.

## Data

```text
state\ramstasis-settings.json    persisted UI settings
state\frz-<pid>.state            per-tree freeze state written by the engine
logs\ramstasis.log               GUI log
logs\frz-YYYYMMDD.log            CLI log
```

Old `frzui-settings.json` files are migrated automatically on first launch.

## Safety

RamStasis never injects code. Freeze uses `NtSuspendProcess` plus per-thread
verification, and thaw resumes exactly the recorded threads. Core Windows
processes and anti-cheat-protected trees are refused. For an intentional
override, `RamStasis.exe` requires `--force` and prints a ban-risk warning.

## License

MIT. See `LICENSE` if present in the distribution.
