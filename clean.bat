@echo off
setlocal

echo Cleaning build artifacts...

REM Femboy intermediate (now at root level)
if exist "Win32" rmdir /s /q "Win32"
if exist "x64" rmdir /s /q "x64"

REM Proxy intermediate
if exist "proxy_version\Win32" rmdir /s /q "proxy_version\Win32"
if exist "proxy_version\x64" rmdir /s /q "proxy_version\x64"

REM Output directory
if exist "bin" rmdir /s /q "bin"

echo Clean complete.
