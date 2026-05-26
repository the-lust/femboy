@echo off
setlocal enabledelayedexpansion
set SOLUTION_DIR=%~dp0
set SOLUTION=%SOLUTION_DIR%femboy.sln

echo Building femboy.dll and proxy_version.dll
echo.

REM Find MSBuild
set MSBUILD=
for %%p in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    "D:\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
) do (
    if exist "%%~p" set MSBUILD=%%~p
)

if not defined MSBUILD (
    echo MSBuild not found. Ensure Visual Studio 2022 is installed.
    exit /b 1
)

echo Using MSBuild: %MSBUILD%
echo.

REM Build x64 Release
echo === Building x64 Release ===
"%MSBUILD%" "%SOLUTION%" /p:Configuration=Release /p:Platform=x64 /t:Clean,Build /m /nologo
if %ERRORLEVEL% NEQ 0 (
    echo x64 build failed!
    exit /b %ERRORLEVEL%
)

REM Build x86 Release
echo.
echo === Building x86 Release ===
"%MSBUILD%" "%SOLUTION%" /p:Configuration=Release /p:Platform=Win32 /t:Clean,Build /m /nologo
if %ERRORLEVEL% NEQ 0 (
    echo x86 build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo === Build Complete ===
echo Files:
echo   bin\x64\Release\femboy.dll
echo   bin\Win32\Release\femboy.dll
echo   bin\x64\Release\version.dll  (proxy)
echo   bin\Win32\Release\version.dll  (proxy)

exit /b 0
