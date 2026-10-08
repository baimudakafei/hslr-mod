@echo off
rem ---------------------------------------------------------------------
rem  HSLR Wait Hotkey - launcher shim (double-click entry point)
rem
rem  IMPORTANT: this file MUST stay ASCII-only and use CRLF line endings.
rem  cmd.exe parses .cmd files by byte offset; with LF-only endings plus
rem  multi-byte (UTF-8 Chinese) characters the parser drifts and slices
rem  lines in half, producing errors like:
rem      'aller.ps1' is not recognized as an internal or external command
rem  All Chinese messages live in installer.ps1 (UTF-8 with BOM) instead.
rem ---------------------------------------------------------------------
setlocal
set "SCRIPT=%~dp0installer.ps1"

rem Prefer the built-in Windows PowerShell 5.1 absolute path (never rely on PATH).
set "PSEXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PSEXE%" set "PSEXE=powershell.exe"

if not exist "%SCRIPT%" goto :missing

"%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
set "RC=%ERRORLEVEL%"
if "%~1"=="" (
  echo.
  pause
)
exit /b %RC%

:missing
echo.
echo  [ERROR] installer.ps1 was not found next to this launcher.
echo  [ERROR] Path checked: %SCRIPT%
echo  [ERROR] The download package is incomplete - please download it again.
echo.
echo  (If the file IS there, run it directly instead:
echo     right-click installer.ps1 - "Run with PowerShell")
echo.
if "%~1"=="" pause
exit /b 1
