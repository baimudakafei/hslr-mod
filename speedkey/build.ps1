# Build the Speed Hotkey proxy DLL with Zig.
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File speedkey/build.ps1 -Zig <zig.exe> [-OutDir <dir>]
param(
    [string]$Zig = '',
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)
$ErrorActionPreference = 'Stop'
# v1.1.7: resolve zig.exe explicitly. A bare 'zig' relies on PATH, which the
# sandboxed shell does not always provide - the build then dies before the
# compiler is even reached, and Tee-Object leaves no log behind to explain it.
if (-not $Zig) {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\tools\zig-x86_64-windows-0.14.1\zig.exe'),
        (Join-Path $PSScriptRoot 'tools\zig-x86_64-windows-0.14.1\zig.exe')
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $Zig = (Resolve-Path $c).Path; break }
    }
    if (-not $Zig) { $Zig = 'zig' }   # last resort: whatever PATH has
}
if (-not (Test-Path $Zig) -and $Zig -ne 'zig') {
    throw "zig not found at '$Zig'."
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$src = Join-Path $PSScriptRoot 'src'
$shared = Join-Path $PSScriptRoot '../shared'
$mh = Join-Path $shared 'minhook'

# Zig must be able to write its caches; point both at the workspace, otherwise
# it fails with AccessDenied on %LOCALAPPDATA%\zig in a locked-down shell.
# NOTE: 杀软实时扫描可能让 mingw CRT 子编译随机 AccessDenied——重跑即可。
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zgc'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zc'

# NOTE: 把 zig 的 stderr 合并进管道(2>&1)会把每条编译告警变成 PowerShell 的
# ErrorRecord；在 $ErrorActionPreference='Stop' 下这会被判为致命错误，导致
# 「有告警但编译成功」被静默当成失败。故此处临时降为 Continue，只认真实退出码。
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $Zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -shared `
    -I "$src" -I "$shared" -I "$mh/include" `
    "$src/speedkey.c" "$shared/proxy.S" "$src/proxy.def" `
    "$mh/src/buffer.c" "$mh/src/hook.c" "$mh/src/trampoline.c" "$mh/src/hde/hde64.c" `
    -ladvapi32 -luser32 -lgdi32 -o "$OutDir/d3d11.dll" 2>&1 |
    Tee-Object -FilePath (Join-Path $PSScriptRoot 'build/native-build.log')
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP
Write-Host "zig exit code: $code"
if ($code -ne 0) { throw 'Speed Hotkey native build failed.' }
# Linker by-products; dist/ ships the DLL and the default config only.
Remove-Item -Force -ErrorAction SilentlyContinue `
    (Join-Path $OutDir 'd3d11.pdb'), (Join-Path $OutDir 'speedkey.lib')
Get-FileHash -Algorithm SHA256 (Join-Path $OutDir 'd3d11.dll')
