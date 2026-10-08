# Build the PinAttr proxy DLL with Zig.
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File pinattr/build.ps1 -Zig <zig.exe> [-OutDir <dir>]
param(
    [string]$Zig = 'zig',
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)
$ErrorActionPreference = 'Stop'
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

& $Zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -shared `
    -I "$src" -I "$shared" -I "$mh/include" `
    "$src/pinattr.c" "$shared/proxy.S" "$src/proxy.def" `
    "$mh/src/buffer.c" "$mh/src/hook.c" "$mh/src/trampoline.c" "$mh/src/hde/hde64.c" `
    -ladvapi32 -luser32 -o "$OutDir/d3d11.dll"
if ($LASTEXITCODE -ne 0) { throw 'PinAttr native build failed.' }
# Linker by-products; dist/ ships the DLL and the default config only.
Remove-Item -Force -ErrorAction SilentlyContinue `
    (Join-Path $OutDir 'd3d11.pdb'), (Join-Path $OutDir 'pinattr.lib')
Get-FileHash -Algorithm SHA256 (Join-Path $OutDir 'd3d11.dll')
