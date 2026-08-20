@echo off
setlocal enabledelayedexpansion

rem Builds both architectures, stages output\ in the layout the installer
rem expects, and compiles the installer.
rem
rem   build_all.bat            build everything and make the installer
rem   build_all.bat noinstaller  build and stage only

echo Infovox 230 SAPI5 build
echo.

set "ROOT=%~dp0"
set "BUILD_X86=%ROOT%build_x86"
set "BUILD_X64=%ROOT%build_x64"
set "STAGE=%ROOT%output"

rem The worker keeps the engine dlls open, so it has to be gone before the
rem staging step can replace them.
taskkill /F /IM Infovox230Server.exe >nul 2>&1

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 or the Build Tools.
    exit /b 1
)
rem -products * so a Build Tools installation counts, not just a full Visual Studio.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo ERROR: no Visual Studio installation found.
    exit /b 1
)
echo Visual Studio: %VSDIR%
echo.

echo === 32-bit: SAPI5 engine, worker, diagnostics ===
cmake -S "%ROOT%." -B "%BUILD_X86%" -A Win32 || exit /b 1
cmake --build "%BUILD_X86%" --config Release || exit /b 1
echo.

echo === 64-bit: SAPI5 engine, diagnostics ===
cmake -S "%ROOT%." -B "%BUILD_X64%" -A x64 || exit /b 1
cmake --build "%BUILD_X64%" --config Release || exit /b 1
echo.

rem Documentation ships alongside the binaries.
copy /Y "%ROOT%docs\README.txt" "%STAGE%\" >nul
copy /Y "%ROOT%docs\voices.example.ini" "%STAGE%\" >nul

echo Staged in %STAGE%:
dir /b "%STAGE%"
echo.

if /i "%~1"=="noinstaller" (
    echo Skipping the installer.
    goto :done
)

echo === installer ===
set "ISCC="
for %%p in (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
) do if exist %%p set "ISCC=%%~p"

if not defined ISCC (
    echo Inno Setup 6 was not found, so the installer was not built.
    echo Install it with:  winget install JRSoftware.InnoSetup
    echo Then run:         build_all.bat
    goto :done
)

"%ISCC%" "%ROOT%installer\Infovox230SAPI.iss" || exit /b 1
echo.
echo Installer: %STAGE%\Infovox230SAPI_Setup.exe

:done
echo.
echo Build complete.
endlocal
