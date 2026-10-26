@echo off
setlocal
set "ROOT=%~dp0"
set "VCVARS="

if /I "%~1"=="clean" goto :clean

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH if exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS goto :novc
call "%VCVARS%" >nul 2>&1
if errorlevel 1 goto :novc

cd /d "%ROOT%"
if not exist bin mkdir bin
if not exist build mkdir build

echo [0/2] assets\ramstasis.ico
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1 -Source assets\iconforRamStasis.png -Destination assets\ramstasis.ico
if errorlevel 1 goto :fail

echo [1/2] build\ramstasis.res
rc /nologo /fo build\ramstasis.res ramstasis.rc
if errorlevel 1 goto :fail

echo [2/2] bin\RamStasis.exe  engine + ImGui/DX11 UI in one exe
cl /nologo /W4 /WX /EHsc /O2 /MT /utf-8 /DNDEBUG ^
   /I"%ROOT%src" /I"%ROOT%src\ui" /I"%ROOT%third_party\imgui" ^
   /Fobuild\ /Fdbuild\ramstasis.pdb ^
   src\frz_main.cpp src\frz_util.cpp src\frz_measure.cpp src\frz_state.cpp ^
   src\frz_tree.cpp src\frz_guard.cpp src\frz_window.cpp src\frz_ops.cpp ^
   src\frz_watch.cpp src\frz_selftest.cpp ^
   src\ui\frzui_imgui.cpp src\ui\frzui_engine.cpp src\ui\frzui_json.cpp ^
   src\ui\frzui_settings.cpp src\ui\frzui_win.cpp ^
   third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp ^
   third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp ^
   third_party\imgui\imgui_impl_win32.cpp ^
   third_party\imgui\imgui_impl_dx11.cpp ^
   /Fe:bin\RamStasis.exe ^
   /link /SUBSYSTEM:WINDOWS /manifest:embed /manifestinput:res\app.manifest ^
   build\ramstasis.res ^
   d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib psapi.lib comctl32.lib ole32.lib advapi32.lib ^
   version.lib dwmapi.lib windowscodecs.lib shlwapi.lib ntdll.lib
if errorlevel 1 goto :fail

echo.
echo BUILD OK
echo   %ROOT%bin\RamStasis.exe
exit /b 0

:novc
echo BUILD FAILED: vcvars64.bat not found or failed
echo   install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload
echo   (or set VCVARS to your own vcvars64.bat path)
exit /b 1

:fail
echo.
echo BUILD FAILED
exit /b 1

:clean
if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
if exist "%ROOT%bin" rmdir /s /q "%ROOT%bin"
if exist "%ROOT%state" rmdir /s /q "%ROOT%state"
if exist "%ROOT%logs" rmdir /s /q "%ROOT%logs"
echo CLEAN OK
exit /b 0
