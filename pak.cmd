setlocal

rem Resolve PowerShell: prefer pwsh (PowerShell 7+), fall back to Windows PowerShell if needed
set "PS_CMD="
for %%G in (pwsh.exe) do (
  if exist "%%~$PATH:G" set "PS_CMD=%%~$PATH:G"
)
if not defined PS_CMD if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "PS_CMD=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not defined PS_CMD set "PS_CMD=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

"%PS_CMD%" -noprofile -executionpolicy Bypass -file D:/vcpkg/scripts/buildsystems/msbuild/applocal.ps1 -targetBinary E:/work/personal/vulkanwork/build/vs26/Debug/vulkanwork.exe -installedDir E:/work/personal/vulkanwork/build/vs26/vcpkg_installed/x64-windows/debug/bin -OutVariable out
if %errorlevel% neq 0 goto :cmEnd
:cmEnd
endlocal & call :cmErrorLevel %errorlevel% & goto :cmDone
:cmErrorLevel
exit /b %1
:cmDone
if %errorlevel% neq 0 goto :VCEnd
