#Requires -Version 5.1
# 待机快捷键 Mod · 安装/卸载/状态/自检工具（PowerShell 版，零外部依赖）
#
#   双击 manage.cmd            交互菜单
#   .\installer.ps1 status     查看安装状态
#   .\installer.ps1 install    安装 / 升级
#   .\installer.ps1 uninstall  卸载（备份）
#   .\installer.ps1 doctor     环境自检
#   .\installer.ps1 log [-Follow]
#   .\installer.ps1 setgame [路径] [-Auto]
#
# 通用参数：-Game "游戏目录"（本次生效；长期用 setgame）
#
# 本工具支持两种摆放方式，d3d11.dll 会自动在这两处找：
#   1) 开发快照 / 完整包：installer.ps1 同级有 dist\ 目录，DLL 在 dist\d3d11.dll
#   2) 便携包 / 游戏 Mod 条目：installer.ps1 与 d3d11.dll 平铺在同一目录，例如
#      ...\LocalLow\<公司>\<产品>\MODS\<modId>\ —— 这个目录本身就是游戏内 Mod
#      条目，解压进去后直接双击 manage.cmd 选安装即可。
# 本文件必须保存为 UTF-8 with BOM，否则 Windows PowerShell 5.1 会把中文当 ANSI 乱码。
# 同目录的 manage.cmd 必须保存为「纯 ASCII + CRLF」——cmd.exe 按字节偏移解析批处理，
# LF 行尾 + 多字节中文会让它把命令行从中间劈开（'aller.ps1' 不是内部或外部命令）。
# 中文提示一律只写在本文件里；本工具会自动检测并修复被改坏的 manage.cmd。

$ErrorActionPreference = 'Stop'
$AppId      = '4030150'
$GameExe    = 'HSLR.exe'
$ModName    = '待机快捷键'
$ModVersion = '2.9.0'          # 新建条目时写进 mod.json 的版本（须与 entry\mod.json 一致）
$ToolVersion= '2.9.0'          # 本工具 / 打包版本号
$DefaultModId = 'hslr-wait-hotkey'
# 与 DLL（src/waitkey.c 的 wk_accepted_hashes）保持同步：
$AcceptedGa = @(
  '1fda3e3add16e97abfd8ced98c24de839f0b079db803ae6f5a7867800fe03f1e',
  '7333475e7176b1f7395c724470a80fffd08e706295ba58fbbf59f0105e6634f1',
  '6aea28ddb7951f5df240f2fecc8cb16a79a8ecd0eb1cdc2b1343798a02be198c',
  'd969e10b94b47d423c701c1dd5fa6986cc8c8c5774519a89b2605fe91285bc09',
  'b023545ec4bc3bc23bf6193e55bb61e8583173b9dc65b1dec2e452e580296b45'
)
# ---------------------------------------------------------------- v2.0.0 卫星
# 整合版：战斗倍速与属性加值作为「卫星 DLL」随包分发（代码 = 原独立 Mod），
# 与 d3d11.dll 同目录摆放，安装时一起复制进游戏根目录；运行期由主 DLL
# LoadLibrary 装载。ini 继续放游戏根目录（与旧独立版位置一致），唯一要
# 迁移的是把 ini 里的 mod_id 指到整合条目（安装时自动补）。
$Satellites = @(
  @{ file = 'mod-speedkey.dll'; label = '战斗倍速快捷键';     ini = 'hslr-speed-hotkey.ini' },
  @{ file = 'mod-pinattr.dll';  label = '属性道具稳定加值';   ini = 'hslr-pinattr.ini' },
  @{ file = 'mod-goldhand.dll'; label = '金之手强化';         ini = 'hslr-goldhand.ini' }
)
# 旧独立版 Mod 条目（整合后不再需要；安装时移出 MODS 备份，不删除）
$LegacyModIds = @('战斗倍速快捷键', '属性道具稳定加值')

$Here        = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dist        = Join-Path $Here 'dist'
$BackupRoot  = Join-Path $Here 'backup'
$SettingsFile= Join-Path $Here 'settings.ini'
$Utf8Bom     = New-Object System.Text.UTF8Encoding $true
$Utf8NoBom   = New-Object System.Text.UTF8Encoding $false

# ---------------------------------------------------------------- 参数解析
$script:Cmd = ''; $script:GameArg = ''; $script:Follow = $false
$script:TestConfig = $false; $script:Force = $false
$script:NoModEntry = $false; $script:KeepModEntry = $false; $script:Auto = $false
$rest = @()
for ($i = 0; $i -lt $args.Count; $i++) {
  $a = [string]$args[$i]
  switch ($a) {
    '-Game'          { $i++; if ($i -lt $args.Count) { $script:GameArg = [string]$args[$i] } }
    '-Follow'        { $script:Follow = $true }
    '-f'             { $script:Follow = $true }
    '-TestConfig'    { $script:TestConfig = $true }
    '-Force'         { $script:Force = $true }
    '-NoModEntry'    { $script:NoModEntry = $true }
    '-KeepModEntry'  { $script:KeepModEntry = $true }
    '-Auto'          { $script:Auto = $true }
    default          { $rest += $a }
  }
}
if ($rest.Count -gt 0) { $script:Cmd = [string]$rest[0] }
if ($rest.Count -gt 1) { $script:GameArg = [string]$rest[1] }   # setgame 路径 位置参数

# ---------------------------------------------------------------- 小工具
function Write-Info([string]$m) { Write-Host $m }
function Write-Warn2([string]$m) { Write-Host $m -ForegroundColor Yellow }

function Sha256([string]$path) {
  (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
}

function Looks-LikeOurs([string]$path) {
  try {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 16) { return $false }
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    return $ascii.Contains('HSLR Wait Hotkey')
  } catch { return $false }
}

# d3d11.dll 的两个可能位置（两种摆放方式都支持）：
#   1) 完整包 / 开发快照：$Here\dist\d3d11.dll
#   2) 便携包 / 游戏 Mod 条目：$Here\d3d11.dll（与 installer.ps1 平铺同级）
function Get-DllSource {
  foreach ($p in @((Join-Path $Dist 'd3d11.dll'), (Join-Path $Here 'd3d11.dll'))) {
    if (Test-Path -LiteralPath $p) { return $p }
  }
  return $null
}

# 卫星 DLL 的两个可能位置（与 d3d11.dll 同一套摆放规则）
function Get-SatelliteSource([string]$file) {
  foreach ($p in @((Join-Path $Dist $file), (Join-Path $Here $file))) {
    if (Test-Path -LiteralPath $p) { return $p }
  }
  return $null
}

# 封面来源（新建条目时用它）：优先 mod\preview.png（游戏列表封面规格 256x256），
# 便携包/条目平铺布局里则是同级的 preview.png。
function Get-PreviewSource {
  foreach ($p in @((Join-Path $Here 'mod\preview.png'), (Join-Path $Here 'preview.png'))) {
    if (Test-Path -LiteralPath $p) { return $p }
  }
  return $null
}

# 随包附带的 mod.json（名字/版本/说明由作者维护，比工具模板更合适）。
function Get-EntryManifestSource {
  $p = Join-Path $Here 'entry\mod.json'
  if (Test-Path -LiteralPath $p) { return $p }
  return $null
}

# ---------------------------------------------------------------- 启动器自检
# manage.cmd 必须是「纯 ASCII + CRLF」。cmd.exe 按字节偏移解析批处理文件，
# LF 行尾叠加多字节中文字符会让解析位置逐行漂移，把整行劈成两半当命令执行，
# 报错形如：'aller.ps1' 不是内部或外部命令 / 'thon' 不是内部或外部命令。
# 所以中文一律只放在本 ps1（UTF-8 BOM）里，启动器保持 ASCII。
$LauncherBody = @'
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
'@

function Get-LauncherPath { Join-Path $Here 'manage.cmd' }

function Test-LauncherBroken {
  # 返回 $true = 启动器坏了（非 ASCII / 缺 CRLF / 没指向 installer.ps1）
  $p = Get-LauncherPath
  if (!(Test-Path -LiteralPath $p)) { return $false }
  try { $bytes = [IO.File]::ReadAllBytes($p) } catch { return $false }
  foreach ($b in $bytes) { if ($b -gt 127) { return $true } }
  $txt = [Text.Encoding]::ASCII.GetString($bytes)
  if ($txt -notmatch "`r`n") { return $true }
  if ($txt -notmatch 'installer\.ps1') { return $true }
  return $false
}

function Repair-Launcher {
  # 修复被改坏/被错误编码的 manage.cmd。返回 $true 表示本次真的修了
  if (!(Test-Path -LiteralPath (Get-LauncherPath))) { return $false }
  if (!(Test-LauncherBroken)) { return $false }
  $body = $LauncherBody -replace "`r`n", "`n"
  $body = $body -replace "`n", "`r`n"
  try {
    [IO.File]::WriteAllText((Get-LauncherPath), $body, $Utf8NoBom)
    Write-Warn2 '!! 启动器 manage.cmd 已损坏（行尾/编码不对），已自动重写成正确版本'
    return $true
  } catch {
    Write-Warn2 "!! manage.cmd 损坏，且自动修复失败：$_"
    return $false
  }
}

# ---------------------------------------------------------------- settings.ini
function Read-Settings {
  $out = @{}
  if (!(Test-Path -LiteralPath $SettingsFile)) { return $out }
  $section = ''
  foreach ($line in [IO.File]::ReadAllLines($SettingsFile)) {
    $t = $line.Trim()
    if (!$t -or $t.StartsWith('#') -or $t.StartsWith(';')) { continue }
    if ($t.StartsWith('[') -and $t.EndsWith(']')) {
      $section = $t.Trim('[', ']').Trim().ToLower(); continue
    }
    $i = $t.IndexOf('=')
    if ($i -lt 0 -or !$section) { continue }
    $out[$section + '.' + $t.Substring(0, $i).Trim().ToLower()] = $t.Substring($i + 1).Trim()
  }
  return $out
}

function Save-Settings([hashtable]$updates) {
  # 就地补丁：只改匹配到的 key = value 行，注释与排版原样保留。
  # settings.ini 是给玩家手改的配置文件（带成段中文说明），整体重写会把注释抹掉。
  foreach ($k in @($updates.Keys)) { $updates[$k.ToLower()] = [string]$updates[$k] }
  $lines = @()
  if (Test-Path -LiteralPath $SettingsFile) {
    $lines = @([IO.File]::ReadAllLines($SettingsFile))
  }
  $done = @{}
  $section = ''
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $t = $lines[$i].Trim()
    if (!$t -or $t.StartsWith('#') -or $t.StartsWith(';')) { continue }
    if ($t.StartsWith('[') -and $t.EndsWith(']')) {
      $section = $t.Trim('[', ']').Trim().ToLower(); continue
    }
    $eq = $t.IndexOf('=')
    if ($eq -lt 0) { continue }
    $full = $section + '.' + $t.Substring(0, $eq).Trim().ToLower()
    if ($updates.ContainsKey($full)) {
      $lines[$i] = $t.Substring(0, $eq).Trim() + ' = ' + $updates[$full]
      $done[$full] = $true
    }
  }
  foreach ($key in ($updates.Keys | Where-Object { !$done.ContainsKey($_) } | Sort-Object)) {
    $p = $key.Split('.', 2)
    $sec = if ($p.Count -gt 1) { $p[0] } else { 'paths' }
    $name = if ($p.Count -gt 1) { $p[1] } else { $key }
    $secIdx = $null; $endIdx = $null; $cur = ''
    for ($i = 0; $i -lt $lines.Count; $i++) {
      $t = $lines[$i].Trim()
      if ($t.StartsWith('[') -and $t.EndsWith(']')) {
        if ($cur -eq $sec) { $endIdx = $i; break }
        $cur = $t.Trim('[', ']').Trim().ToLower()
        if ($cur -eq $sec) { $secIdx = $i }
      } elseif ($null -ne $secIdx -and $t -and !$t.StartsWith('#') -and !$t.StartsWith(';')) {
        $endIdx = $i + 1
      }
    }
    if ($null -eq $secIdx) {
      if ($lines.Count -gt 0 -and $lines[-1].Trim()) { $lines += '' }
      $lines += "[$sec]"; $lines += "$name = " + $updates[$key]
    } else {
      $at = if ($null -ne $endIdx) { $endIdx } else { $lines.Count }
      $lines = @($lines[0..($at - 1)]) + @("$name = " + $updates[$key]) +
               @($lines[$at..($lines.Count - 1)])
    }
  }
  [IO.File]::WriteAllText($SettingsFile, ($lines -join "`r`n") + "`r`n", $Utf8Bom)
}

function Get-ConfiguredModId {
  $s = Read-Settings
  $id = $null
  if ($s.ContainsKey('mod.id')) { $id = $s['mod.id'] }
  if (!$id) { $id = $DefaultModId }
  return $id
}

# ---------------------------------------------------------------- 游戏目录发现
function Get-SteamRoots {
  $roots = New-Object System.Collections.Generic.List[string]
  function Add-Root([string]$p) {
    if ($p -and (Test-Path -LiteralPath $p -PathType Container)) {
      $full = (Resolve-Path -LiteralPath $p).Path
      if (!$roots.Contains($full)) { $roots.Add($full) }
    }
  }
  # settings.ini [paths] steam：用户手动指的 Steam 库根，永远排最前
  $s = Read-Settings
  if ($s.ContainsKey('paths.steam') -and $s['paths.steam']) { Add-Root $s['paths.steam'] }
  $suffixes = @('Program Files (x86)\Steam', 'Program Files\Steam', 'Steam',
                'SteamLibrary', 'Games\Steam', 'Steam\SteamLibrary')
  foreach ($d in [char[]](67..90)) {   # C..Z
    $drive = "$d`:\"
    if (!(Test-Path $drive)) { continue }
    foreach ($sfx in $suffixes) { Add-Root (Join-Path $drive $sfx) }
  }
  foreach ($base in @($roots)) {
    $vdf = Join-Path $base 'steamapps\libraryfolders.vdf'
    if (!(Test-Path -LiteralPath $vdf)) { continue }
    try {
      foreach ($m in ([regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"'))) {
        Add-Root ($m.Groups[1].Value -replace '\\\\', '\')
      }
    } catch { }
  }
  return $roots
}

function Find-Game {
  foreach ($root in (Get-SteamRoots)) {
    $apps = Join-Path $root 'steamapps'
    if (!(Test-Path -LiteralPath $apps -PathType Container)) { continue }
    $manifest = Join-Path $apps "appmanifest_$AppId.acf"
    if (!(Test-Path -LiteralPath $manifest)) { continue }
    $text = [IO.File]::ReadAllText($manifest)
    $m = [regex]::Match($text, '"installdir"\s+"([^"]+)"')
    if (!$m.Success) { continue }
    $game = Join-Path $apps ('common\' + $m.Groups[1].Value)
    if (Test-Path -LiteralPath $game -PathType Container) { return (Resolve-Path -LiteralPath $game).Path }
  }
  # 兜底：每个盘往下一层扫 steamapps
  foreach ($d in [char[]](67..90)) {
    $drive = "$d`:\"
    if (!(Test-Path $drive)) { continue }
    $children = @(Get-ChildItem -LiteralPath $drive -Directory -ErrorAction SilentlyContinue)
    foreach ($c in $children) {
      foreach ($cand in @((Join-Path $c.FullName 'steamapps'),
                          (Join-Path $c.FullName 'SteamLibrary\steamapps'))) {
        $manifest = Join-Path $cand "appmanifest_$AppId.acf"
        if (!(Test-Path -LiteralPath $manifest)) { continue }
        $m = [regex]::Match([IO.File]::ReadAllText($manifest), '"installdir"\s+"([^"]+)"')
        if (!$m.Success) { continue }
        $game = Join-Path $cand ('common\' + $m.Groups[1].Value)
        if (Test-Path -LiteralPath $game -PathType Container) { return (Resolve-Path -LiteralPath $game).Path }
      }
    }
  }
  return $null
}

function Resolve-Game {
  if ($GameArg) {
    $p = $GameArg
    if (!(Test-Path -LiteralPath $p -PathType Container)) {
      Write-Warn2 "!! 目录不存在：$p"; return $null
    }
    return (Resolve-Path -LiteralPath $p).Path
  }
  $s = Read-Settings
  if ($s.ContainsKey('paths.game') -and $s['paths.game']) {
    $p = $s['paths.game']
    if ((Test-Path -LiteralPath $p -PathType Container) -and
        (Test-Path -LiteralPath (Join-Path $p $GameExe))) { return $p }
    Write-Warn2 "!! settings.ini 里记的游戏目录当前不可用：$p"
    Write-Warn2 "   改用自动发现；要修掉这条记录请运行：installer.ps1 setgame -Auto"
  }
  $g = Find-Game
  if (!$g) {
    Write-Warn2 '!! 没能从 Steam 清单里找到游戏目录。'
    Write-Warn2 '   请手动指定一次（之后不用再填）：'
    Write-Warn2 '     installer.ps1 setgame "游戏目录"'
    return $null
  }
  return $g
}

function Test-GameRunning {
  return [bool](Get-Process -Name 'HSLR' -ErrorAction SilentlyContinue)
}

function Assert-GameClosed {
  if (Test-GameRunning) {
    Write-Warn2 "!! $GameExe 正在运行。请先完全退出游戏（含启动器）再操作。"
    return $false
  }
  return $true
}

# 【v2.3.1 长路径】Windows 传统路径上限是 MAX_PATH(260) 字符。游戏装在很深的
# 目录、或路径里套了多层中文文件夹时，Mod 的 ini / 日志可能写不进去，游戏也
# 可能读不到旁边的 d3d11.dll —— 用户看到的就是「装了，但功能不生效，连日志
# 都没有」，极难排查。新版 DLL 内部已经按长路径处理（超长自动补 \\?\ 前缀），
# 但本安装工具的 .ps1 受 PowerShell 5.1 / .NET Framework 限制，在超长路径下
# 仍可能失败，所以这里提前提醒，让用户有机会把游戏挪到短路径。
function Test-PathLength([string]$path) {
  if (!$path) { return $true }
  if ($path.Length -ge 260) {
    Write-Warn2 "!! 游戏目录路径长 $($path.Length) 字符，已达到/超过 Windows 的 260 上限。"
    Write-Warn2 '   新版 Mod 的 DLL 已支持长路径，但安装/游戏本体在这么深的路径下仍可能出问题。'
    Write-Warn2 '   强烈建议把游戏挪到短一些的路径（例如 D:\Games\HSLR）后再装一次。'
    Write-Warn2 "   当前路径：$path"
    return $false
  }
  if ($path.Length -ge 200) {
    Write-Warn2 "提示：游戏目录路径较长（$($path.Length) 字符），已接近 Windows 的 260 上限，建议留意。"
  }
  return $true
}

# ---------------------------------------------------------------- 游戏自己的 Mod 系统
# 【v2.5.3 环境适配】LocalLow 的定位不再硬拼 %USERPROFILE%\AppData\LocalLow。
# 用户配置文件目录被组策略 / 无人值守部署脚本重定向到其他盘时，硬拼出来的路径并
# 不存在，症状是「游戏目录能自动找到，但 Mod 条目写不进去 / 状态永远读不到」。
# 这里改成「候选根列表 + 逐个存在性验证」：
#   ① SHGetKnownFolderPath(FOLDERID_LocalAppDataLow) —— Windows 的权威答案，尊重重定向
#   ② [Environment]::GetFolderPath(LocalApplicationData) 的父目录 + LocalLow
#      （同样经由 Shell API 取得；LocalLow 本身没有对应的 SpecialFolder 枚举值）
#   ③ %USERPROFILE%\AppData\LocalLow —— 旧行为兜底
if (-not ('HSLR.KnownFolder' -as [type]) -and !$script:HslrKfTried) {
  $script:HslrKfTried = $true
  try {
    Add-Type -ErrorAction Stop -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HSLR {
  public static class KnownFolder {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern int SHGetKnownFolderPath(ref Guid rfid, uint dwFlags,
                                                   IntPtr hToken, out IntPtr ppszPath);

    // FOLDERID_LocalAppDataLow = {A520A1A4-1780-4FF6-BD18-167343C5AF16}
    public static string LocalAppDataLow() {
      Guid g = new Guid("A520A1A4-1780-4FF6-BD18-167343C5AF16");
      IntPtr p;
      if (SHGetKnownFolderPath(ref g, 0, IntPtr.Zero, out p) != 0 || p == IntPtr.Zero) return null;
      try { return Marshal.PtrToStringUni(p); }
      finally { Marshal.FreeCoTaskMem(p); }
    }
  }
}
'@
  } catch { }   # 取不到就走后面的候选，绝不让环境探测把安装流程打断
}

function Get-LocalLowRoots {
  $out = New-Object System.Collections.Generic.List[string]
  function Add-R([string]$p) {
    if (!$p) { return }
    $p = $p.TrimEnd('\')
    if ($p -and !$out.Contains($p)) { $out.Add($p) }
  }
  if ('HSLR.KnownFolder' -as [type]) {
    try { Add-R ([HSLR.KnownFolder]::LocalAppDataLow()) } catch { }
  }
  try {
    $local = [Environment]::GetFolderPath('LocalApplicationData')
    if ($local) { Add-R (Join-Path (Split-Path -Parent $local) 'LocalLow') }
  } catch { }
  if ($env:USERPROFILE) { Add-R (Join-Path $env:USERPROFILE 'AppData\LocalLow') }
  return $out
}

function Get-PersistentDir([string]$game) {
  $info = Join-Path $game 'HSLR_Data\app.info'
  if (!(Test-Path -LiteralPath $info)) { return $null }
  $lines = @([IO.File]::ReadAllLines($info) | Where-Object { $_.Trim() } |
             ForEach-Object { $_.Trim() })
  if ($lines.Count -lt 2) { return $null }
  $rel = Join-Path $lines[0] $lines[1]
  $roots = Get-LocalLowRoots
  $first = $null
  foreach ($r in $roots) {
    $cand = Join-Path $r $rel
    if ($null -eq $first) { $first = $cand }
    if (Test-Path -LiteralPath $cand -PathType Container -ErrorAction SilentlyContinue) {
      return $cand
    }
  }
  # 三个候选都不存在（游戏还没启动过）：用最权威的那个；多个候选时说明都试了哪些
  if ($roots.Count -gt 1) {
    Write-Warn2 "提示：LocalLow 下暂未找到 $rel，已按权威路径使用。尝试过：$($roots -join ' ; ')"
  }
  return $first
}

function Get-LocalModsRoot([string]$game) {
  # settings.ini [paths] mods：用户手动指的 MODS 根，优先于自动推导
  $s = Read-Settings
  if ($s.ContainsKey('paths.mods') -and $s['paths.mods']) { return $s['paths.mods'] }
  $p = Get-PersistentDir $game
  if (!$p) { return $null }
  return (Join-Path $p 'MODS')
}

function Safe-DirName([string]$id) {
  $bad = '[\\/:\*\?"<>\|]'
  $clean = ([regex]::Replace($id, $bad, '_')).Trim(' ', '.')
  if (!$clean) { $clean = $DefaultModId }
  return $clean
}

function Scan-ModEntries([string]$game) {
  $root = Get-LocalModsRoot $game
  $out = @()
  if (!$root -or !(Test-Path -LiteralPath $root -PathType Container)) { return $out }
  foreach ($child in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
                       Sort-Object Name)) {
    if ($child.Name.StartsWith('.')) { continue }
    $manifest = Join-Path $child.FullName 'mod.json'
    if (!(Test-Path -LiteralPath $manifest)) { continue }
    $data = $null
    try { $data = ConvertFrom-Json ([IO.File]::ReadAllText($manifest)) } catch { }
    $entry = [ordered]@{
      dir = $child.FullName; name = $child.Name
      modId = ''; displayName = ''; version = ''
    }
    if ($data -and $data.PSObject.Properties['modId']) { $entry.modId = [string]$data.modId }
    if ($data -and $data.PSObject.Properties['name']) { $entry.displayName = [string]$data.name }
    if ($data -and $data.PSObject.Properties['version']) { $entry.version = [string]$data.version }
    $out += New-Object PSObject -Property $entry
  }
  return ,$out        # , 防止单条目时被 PS 展开成标量、.Count 失效
}

function Find-ModEntry([string]$game, [string]$modId) {
  foreach ($e in (Scan-ModEntries $game)) {
    if ($e.modId -eq $modId) { return $e.dir }
  }
  return $null
}

function Get-ModEntryState([string]$game, [string]$modId) {
  $entry = Find-ModEntry $game $modId
  if (!$entry) {
    $root = Get-LocalModsRoot $game
    if (!$root) { return "未创建（找不到游戏的本地 Mod 目录；期望 modId=$modId）" }
    $configured = Join-Path $root (Safe-DirName $modId)
    if (Test-Path -LiteralPath $configured) {
      if (!(Test-Path -LiteralPath (Join-Path $configured 'mod.json'))) {
        return "目录 $(Split-Path -Leaf $configured) 存在，但缺少 mod.json —— 游戏会忽略整个目录（Mod 列表里看不到本 Mod，快捷键不会生效）。重跑一次安装即可重建。"
      }
      return "目录 $(Split-Path -Leaf $configured) 存在，但里面的 modId 不是 $modId（DLL 不会认）"
    }
    return "未创建（游戏内看不到本 Mod；期望 modId=$modId）"
  }
  $root = Get-LocalModsRoot $game
  $profilePath = Join-Path $root 'mod_profile.json'
  if (Test-Path -LiteralPath $profilePath) {
    try {
      $profile = ConvertFrom-Json ([IO.File]::ReadAllText($profilePath))
      foreach ($m in $profile.mods) {
        if ($m.modId -eq $modId) {
          $on = '已停用'; if ($m.enabled) { $on = '已启用' }
          return "已创建于 $(Split-Path -Leaf $entry)\，游戏内$on（priority=$($m.priority)）"
        }
      }
    } catch { }
  }
  return "已创建于 $(Split-Path -Leaf $entry)\，游戏里还没开关过（游戏里默认是停用）"
}

function New-ModJson([string]$author, [string]$modId) {
  $desc = (@(
    '【用途】'
    '在角色行动菜单里按 空格 直接执行「待机」，等同于用鼠标点那个按钮。'
    '空格走的是游戏自己的点击管线（UIButtonEx.InvokeClick），只在菜单真正可操作时触发；'
    '菜单之外完全无反应，不会抢对话、跳过、确认等原有按键。长按只触发一次。'
    ''
    '【改键】'
    '编辑游戏根目录下的 hslr-wait-hotkey.ini，保存即生效，不用重启游戏。'
    '可选动作：move / attack / skill / magic / item / status / wait'
    ''
    '【安装】'
    '退出游戏及启动器，双击 manage.cmd 选安装。'
    '它会把 d3d11.dll 放进游戏根目录，并把本条目放进游戏的本地 Mod 目录。'
    '启动游戏后在 Mod 管理里启用本 Mod 并应用变更。'
    ''
    '【卸载】'
    'manage.cmd 选卸载会把 DLL、配置和本条目一起移走（先备份到工具目录 backup\）。'
    ''
    '【版本】'
    "Mod：$ModVersion｜游戏：2.2.4｜Windows x64"
  ) -join "`n")
  $payload = [ordered]@{
    modId = $modId; name = $ModName; version = $ModVersion
    minGameVersion = '2.2.4'; author = $author; description = $desc
    content = [ordered]@{}
  }
  return (ConvertTo-Json $payload -Depth 4)
}

function Install-ModEntry([string]$game, [string]$author, [string]$modId) {
  $root = Get-LocalModsRoot $game
  if (!$root) {
    Write-Warn2 '!! 找不到游戏的本地 Mod 目录（HSLR_Data/app.info 缺失或格式不对）。'
    return $false
  }
  $entry = Join-Path $root (Safe-DirName $modId)
  New-Item -ItemType Directory -Force -Path $entry | Out-Null
  $manifest = Join-Path $entry 'mod.json'

  # mod.json 是这条 Mod 的身份卡，游戏只认它。玩家/作者自己维护过的名字、版本、
  # 说明优先级高于工具模板 —— modId 一致就原样保留，不覆盖（要重生成加 -Force）。
  $hasManifest = Test-Path -LiteralPath $manifest
  $existingId = ''
  if ($hasManifest) {
    try { $existingId = [string](ConvertFrom-Json ([IO.File]::ReadAllText($manifest))).modId }
    catch { $existingId = '' }
  }

  if ($hasManifest -and $existingId -eq $modId -and !$Force) {
    Write-Info "  本条目已有 mod.json（modId=$modId），按现有版本原样保留"
  } else {
    $src = Get-EntryManifestSource
    if ($src) {
      $fresh = [IO.File]::ReadAllText($src)
      Write-Info "  采用随包附带的 mod.json"
    } else {
      $fresh = New-ModJson $author $modId
    }
    if ($hasManifest) {
      $backupText = [IO.File]::ReadAllText($manifest)
      if ($backupText.Trim() -ne $fresh.Trim()) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $target = Join-Path $BackupRoot "$stamp-modjson\mod.json"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $manifest -Destination $target -Force
        Write-Info "  原有 mod.json 已备份  ->  $target"
      }
    }
    [IO.File]::WriteAllText($manifest, $fresh, $Utf8Bom)
  }

  # 封面只补缺，不覆盖已有那张（要刷新加 -Force）
  $cover = Join-Path $entry 'preview.png'
  $coverSrc = Get-PreviewSource
  if ($coverSrc) {
    if (!(Test-Path -LiteralPath $cover)) {
      Copy-Item -LiteralPath $coverSrc -Destination $cover -Force
      Write-Info '  已放入封面 preview.png'
    } elseif ($Force -and ($Here -ne $entry)) {
      Copy-Item -LiteralPath $coverSrc -Destination $cover -Force
      Write-Info '  已按 -Force 刷新封面 preview.png'
    }
  }
  Write-Info "游戏内 Mod 条目已就绪  ->  $entry"
  return $true
}

function Remove-ModEntry([string]$game, [string]$modId) {
  $entry = Find-ModEntry $game $modId
  if (!$entry) {
    $root = Get-LocalModsRoot $game
    if (!$root) { return $false }
    $entry = Join-Path $root (Safe-DirName $modId)
    if (!(Test-Path -LiteralPath (Join-Path $entry 'mod.json'))) { return $false }
  }
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $target = Join-Path $BackupRoot "$stamp\mod-entry"
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Move-Item -LiteralPath $entry -Destination $target -Force
  Write-Info "已移出游戏内 Mod 条目（备份在 $target）"
  return $true
}

# ---------------------------------------------------------------- .ini 读写
function Find-IniOption([System.Collections.Generic.List[string]]$lines, [string]$key) {
  $section = ''; $target = -1; $optionsAt = -1
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $t = $lines[$i].Trim()
    if ($t.StartsWith('[') -and $t.EndsWith(']')) {
      $section = $t.Trim('[', ']').Trim().ToLower()
      if ($section -eq 'options' -and $optionsAt -lt 0) { $optionsAt = $i }
      continue
    }
    if ($section -ne 'options') { continue }
    $eq = $t.IndexOf('=')
    if ($eq -gt 0 -and $t.Substring(0, $eq).Trim().ToLower() -eq $key) { $target = $i; break }
  }
  return @($target, $optionsAt)
}

function Read-IniOption([string]$path, [string]$key) {
  if (!(Test-Path -LiteralPath $path)) { return $null }
  $lines = New-Object 'System.Collections.Generic.List[string]'
  $lines.AddRange([IO.File]::ReadAllLines($path))
  $r = Find-IniOption $lines $key
  if ($r[0] -lt 0) { return $null }
  $v = $lines[$r[0]].Substring($lines[$r[0]].IndexOf('=') + 1).Trim()
  if (!$v) { return $null }
  return $v
}

function Patch-IniOption([string]$path, [string]$key, [string]$value) {
  if (!(Test-Path -LiteralPath $path)) { return $false }
  $lines = New-Object 'System.Collections.Generic.List[string]'
  $lines.AddRange([IO.File]::ReadAllLines($path))
  $target, $optionsAt = Find-IniOption $lines $key
  $newline = "$key = $value"
  if ($target -ge 0) {
    if ($lines[$target].Trim() -eq $newline) { return $true }
    $lines[$target] = $newline
  } elseif ($optionsAt -ge 0) {
    $lines.Insert($optionsAt + 1, $newline)
  } else {
    if ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Trim()) { $lines.Add('') }
    $lines.Add('[options]'); $lines.Add($newline)
  }
  [IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), $Utf8Bom)
  return $true
}

function New-DefaultConfig([bool]$testing, [string]$modId, [hashtable]$hotkeys) {
  $ve = '0'; $fe = '0'
  if ($testing) { $ve = '1'; $fe = '1' }
  if ($hotkeys -and $hotkeys.Count -gt 0) {
    $hk = "# 键位来自工具包 settings.ini 的 [hotkeys] 段`r`n" +
          (($hotkeys.GetEnumerator() | Sort-Object Name |
             ForEach-Object { "$($_.Name) = $($_.Value)" }) -join "`r`n")
  } else {
    $hk = "# 空格 → 待机`r`nspace = wait`r`n`r`n# 想扩展时取消下面的注释（改完保存即生效）：`r`n# f = attack`r`n# c = magic`r`n# numpad0 = wait"
  }
  return (@"
# 幻世录重置版 · 待机快捷键 配置文件
#
# 键位在 [hotkeys] 段里改，保存后游戏内立刻生效，不用重启。
# 可选动作：move / attack / skill / magic / item / status / wait
# 想关掉某个键就把它那一行删掉或注释掉。

[hotkeys]
$hk

[options]
# 按住 Shift/Ctrl/Alt 时不触发快捷键
require_no_modifier = 1
# 往日志里写每一次按键请求（排查用，日常可以关掉）
verbose_log = $ve
# 1 = 忽略游戏内的 Mod 开关，强制生效
# 本地实测用 1；正式从创意工坊安装后应为 0，由游戏内的 Mod 开关控制
force_enable = $fe
# 游戏 Mod 管理里那条记录的 ID（MODS 下那个目录里 mod.json 的 modId）。
# 安装时会自动和它保持一致；手动改了游戏里的包名，这里也要跟着改。
mod_id = $modId

# ── 免版本跟进（v2.9.0）──
# 官方小更新（只挪动/新增别的功能、没改我们用的那些函数）后，四个 DLL 不再
# 直接失效：启动时会在锚点附近的窗口里按「长掩码指纹」把真实地址找回来。
# 构建哈希命中白名单时走恒等映射（零开销、行为与旧版完全一致），这时下面三项
# 都不用管。只有在排查时才需要手工设置；设成注释行 = 使用默认值。
#
# rva_mode   auto(默认) / native / adaptive / selftest
#              auto     = 哈希命中就用原值，未命中自动切换窗口搜索
#              native   = 强制用原值（调试基准用）
#              adaptive = 强制窗口搜索
#              selftest = 强制窗口搜索，并把锚点人为偏移 rva_shot 字节（自测用）
# rva_window 窗口半宽，十六进制，默认 0x40000（256 KB）。仅排查用。
# rva_shot   selftest 模式下锚点的人为偏移量（十六进制，可为负）。默认 0。
# rva_mode   = auto
# rva_window = 0x40000
# rva_shot   = 0x2A00

[features]
# 分功能总控开关（1=开 0=关），保存即生效，不用重启游戏。
# 这里是各功能的总闸；关掉后该功能整体停用，游戏内 Mod 开关和
# 卫星自己的 ini 都不再单独影响它。
wait = 1
approach = 1
speed = 1
pinattr = 1
goldhand = 1
"@ -join "`n") + "`n"
}

function Read-SettingsHotkeys {
  # settings.ini 的 [hotkeys] 段 = 键位的「源头」，安装时同步进游戏 .ini
  $s = Read-Settings
  $hk = @{}
  foreach ($k in $s.Keys) {
    if ($k.StartsWith('hotkeys.') -and $s[$k]) { $hk[$k.Substring(8)] = $s[$k] }
  }
  return $hk
}

function Set-IniSection([string]$path, [string]$section, [hashtable]$pairs) {
  # 整段替换 INI 里的一个节（其余字节不动）。文件没变化时返回 $false。
  if (!(Test-Path -LiteralPath $path)) { return $false }
  $lines = @([IO.File]::ReadAllLines($path))
  $out = [System.Collections.Generic.List[string]]@()
  $replaced = $false
  $i = 0
  while ($i -lt $lines.Count) {
    $t = $lines[$i].Trim()
    if ($t.ToLower() -eq "[$($section.ToLower())]") {
      $i++
      while ($i -lt $lines.Count) {
        $s2 = $lines[$i].Trim()
        if ($s2.StartsWith('[') -and $s2.EndsWith(']')) { break }
        $i++
      }
      $out.Add("[$section]")
      foreach ($k in ($pairs.Keys | Sort-Object)) { $out.Add("$k = " + $pairs[$k]) }
      $out.Add('')
      $replaced = $true
      continue
    }
    $out.Add($lines[$i]); $i++
  }
  if (!$replaced) {
    if ($out.Count -gt 0 -and $out[$out.Count - 1].Trim()) { $out.Add('') }
    $out.Add("[$section]")
    foreach ($k in ($pairs.Keys | Sort-Object)) { $out.Add("$k = " + $pairs[$k]) }
  }
  $text = ($out -join "`n") + "`n"
  $old = [IO.File]::ReadAllText($path)
  if ($text -eq $old) { return $false }
  [IO.File]::WriteAllText($path, $text, $Utf8Bom)
  return $true
}

# ---------------------------------------------------------------- status
function Invoke-Status {
  $null = Repair-Launcher        # 坏掉的启动器先悄悄修好，其他命令也顺带受益
  $modId = Get-ConfiguredModId
  $game = Resolve-Game
  if (!$game) { return 2 }
  Write-Info "游戏目录 : $game"
  Write-Info "游戏进程 : $(if (Test-GameRunning) { '运行中' } else { '未运行' })"
  $assembly = Join-Path $game 'GameAssembly.dll'
  $gaTxt = '缺失'; if (Test-Path -LiteralPath $assembly) { $gaTxt = '存在' }
  Write-Info "GameAssembly.dll : $gaTxt"

  $persistent = Get-PersistentDir $game
  if ($persistent) {
    $modsRoot = Get-LocalModsRoot $game
    $rootTxt = '（尚未创建）'
    if (Test-Path -LiteralPath $modsRoot -PathType Container) { $rootTxt = '（存在）' }
    Write-Info "游戏数据目录     : $persistent"
    Write-Info "本地 Mod 目录    : $modsRoot  $rootTxt"
    $src = '（默认值）'; if ((Read-Settings).ContainsKey('mod.id')) { $src = '（来自 settings.ini）' }
    Write-Info "本 Mod 的 modId  : $modId  $src"
    $hk = Read-SettingsHotkeys
    if ($hk.Count -gt 0) {
      $desc = ($hk.GetEnumerator() | Sort-Object Name |
               ForEach-Object { "$($_.Name)→$($_.Value)" }) -join '、'
      Write-Info "键位（settings.ini）: $desc"
    }
    Write-Info "游戏内 Mod 条目  : $(Get-ModEntryState $game $modId)"
    $entries = Scan-ModEntries $game
    if ($entries.Count -gt 0) {
      Write-Info ''
      Write-Info '游戏本地 Mod 目录里的条目（游戏 Mod 列表里看到的就是这些）：'
      foreach ($e in $entries) {
        $mark = ''; if ($e.modId -eq $modId) { $mark = '  <- 本 Mod' }
        $mid = $e.modId; if (!$mid) { $mid = '(空)' }
        $vn = $e.version; if (!$vn) { $vn = '?' }
        Write-Info ("  {0}  modId={1}  v{2}  {3}{4}" -f $e.name, $mid, $vn, $e.displayName, $mark)
      }
    } else {
      Write-Info ''
      Write-Info '游戏本地 Mod 目录里目前没有任何有效条目（游戏内 Mod 列表是空的）。'
    }
  }

  $local = Get-DllSource
  $localHash = $null
  if ($local) { $localHash = Sha256 $local }
  $lh = '（工具包里找不到 d3d11.dll，跳过一致性比对）'
  if ($localHash) { $lh = $localHash.Substring(0, 16) + '…' }
  Write-Info ''
  Write-Info "工具包里的 DLL  : $lh"
  if ($local) { Write-Info "  （取自 $local）" }
  Write-Info ''
  Write-Info '游戏目录内的 Mod 文件：'
  foreach ($name in @('d3d11.dll', 'hslr-wait-hotkey.ini', 'hslr-wait-hotkey.log')) {
    $path = Join-Path $game $name
    if (!(Test-Path -LiteralPath $path)) {
      Write-Info ("  {0}  -- 未安装" -f $name); continue
    }
    $size = (Get-Item -LiteralPath $path).Length
    if ($name -eq 'd3d11.dll' -and $localHash) {
      $digest = Sha256 $path
      $marker = '与工具一致'
      if ($digest -ne $localHash) { $marker = '与工具不一致（需重新安装）' }
      Write-Info ("  {0}  {1,10:N0} B  {2}…  {3}" -f $name, $size, $digest.Substring(0, 16), $marker)
    } else {
      Write-Info ("  {0}  {1,10:N0} B" -f $name, $size)
    }
  }
  # v2.0.0 卫星功能（战斗倍速 / 属性加值）
  foreach ($sat in $Satellites) {
    $dest = Join-Path $game $sat.file
    $src = Get-SatelliteSource $sat.file
    if (!(Test-Path -LiteralPath $dest)) {
      if ($src) { Write-Info ("  {0}  -- 未安装（{1}，重跑安装即可）" -f $sat.file, $sat.label) }
      else      { Write-Info ("  {0}  -- 随包未携带（{1}）" -f $sat.file, $sat.label) }
      continue
    }
    $digest = Sha256 $dest
    $marker = ''
    if ($src -and $digest -ne (Sha256 $src)) { $marker = '  与工具不一致（需重新安装）' }
    Write-Info ("  {0}  {1,10:N0} B  {2}…{3}  （{4}）" -f $sat.file, (Get-Item -LiteralPath $dest).Length,
                $digest.Substring(0, 16), $marker, $sat.label)
  }
  return 0
}

# ---------------------------------------------------------------- doctor
function Invoke-Doctor {
  Write-Info '待机快捷键 Mod · 环境自检'
  Write-Info ('=' * 62)
  $issues = @()

  Write-Info ("[1] PowerShell {0}  OK（需 5.1+，Windows 内置）" -f $PSVersionTable.PSVersion)

  $game = Resolve-Game
  Write-Info ("[2] 游戏目录：{0}" -f $(if ($game) { $game } else { '没找到' }))
  if (!$game) {
    $issues += '没找到游戏目录。解决：installer.ps1 setgame "游戏目录"（填含 HSLR.exe 的那个文件夹）'
  } else {
    # 【v2.3.1】路径长度体检：超 260 时 DLL 侧的 ini/日志早已写好但游戏可能读不到
    if ($game.Length -ge 260) {
      Write-Info ("[2.5] 路径长度：{0} 字符  !! 达到/超过 Windows 的 260 上限" -f $game.Length)
      $issues += "游戏目录路径长 $($game.Length) 字符，超过 Windows 传统上限 260。" +
                 '新版 DLL 已适配长路径，但游戏本体与安装工具在这么深的路径下仍可能出问题，' +
                 '建议把游戏挪到 D:\Games\HSLR 这类短路径'
    } elseif ($game.Length -ge 200) {
      Write-Info ("[2.5] 路径长度：{0} 字符  接近 260 上限，留意" -f $game.Length)
    } else {
      Write-Info ("[2.5] 路径长度：{0} 字符  OK" -f $game.Length)
    }
    $gaDll = Join-Path $game 'GameAssembly.dll'
    if (Test-Path -LiteralPath $gaDll) {
      $digest = Sha256 $gaDll
      if ($AcceptedGa -contains $digest) {
        Write-Info '[3] 游戏版本  OK —— 是本 Mod 支持的 2.2.4（build 25767968）'
      } else {
        Write-Info '[3] 游戏版本  !! 不是本 Mod 支持的 2.2.4（build 25767968）'
        Write-Info "    你机器的 GameAssembly.dll SHA256 = $digest"
        $issues += '游戏版本不受支持（游戏更新过/版本不同）。DLL 会安静地不生效，这是刻意的安全设计。' +
                   '把上面那行 SHA256 发给作者，确认能不能为新版本适配；或先回退游戏版本'
      }
    } else {
      Write-Info '[3] GameAssembly.dll 缺失  !!'
      $issues += "$game 里没有 GameAssembly.dll —— 这不像游戏目录"
    }

    $probe = Join-Path $game '~waitkey-doctor.tmp'
    $wrote = $false
    try {
      [IO.File]::WriteAllText($probe, '', $Utf8NoBom)
      $wrote = $true
    } catch { }
    if (!$wrote) {
      Write-Info "[4] 游戏目录不可写  !!（创建探针文件失败）"
      $issues += '游戏目录没有写权限（如装在 Program Files）。DLL 写不了日志/配置，install 也会失败；' +
                 '用管理员身份运行，或把游戏挪出受保护目录'
    } else {
      try { Remove-Item -LiteralPath $probe -Force -ErrorAction Stop } catch { }
      if (Test-Path -LiteralPath $probe) {
        try { Remove-Item -LiteralPath $probe -Force -ErrorAction Stop } catch { }
      }
      Write-Info '[4] 游戏目录可写  OK（DLL 能写日志和默认配置）'
    }

    $dll = Join-Path $game 'd3d11.dll'
    $distDll = Get-DllSource
    if (!(Test-Path -LiteralPath $dll)) {
      Write-Info '[5] d3d11.dll 未安装  !!'
      $issues += '游戏目录里没有 d3d11.dll —— Mod 没装。注意：只订阅创意工坊条目不会自动安装 DLL，必须运行安装'
    } elseif (!(Looks-LikeOurs $dll)) {
      Write-Info '[5] 游戏根目录已有别的 d3d11.dll  !!'
      $issues += '游戏根目录的 d3d11.dll 不是本 Mod（可能是其它同类 Mod），两者冲突；确认后加 -Force 覆盖'
    } elseif ($distDll -and ((Sha256 $dll) -ne (Sha256 $distDll))) {
      Write-Info '[5] d3d11.dll 已安装，但与本工具的版本不同（建议重跑安装升级）'
    } else {
      Write-Info '[5] d3d11.dll 已安装且与本工具一致  OK'
    }

    $modId = Get-ConfiguredModId
    $config = Join-Path $game 'hslr-wait-hotkey.ini'
    if (Test-Path -LiteralPath $config) {
      $iniId = Read-IniOption $config 'mod_id'
      $fe = Read-IniOption $config 'force_enable'
      $fid = $iniId; if (!$fid) { $fid = '（缺）' }
      $ffe = $fe; if (!$ffe) { $ffe = '（缺）' }
      Write-Info "[6] 配置文件：mod_id=$fid  force_enable=$ffe"
      if ($iniId -ne $modId) {
        $issues += ".ini 里的 mod_id（$iniId）和工具的 modId（$modId）不一致，" +
                   "游戏内开关会「拨了没反应」。这个工具安装时会自动同步"
      }
    } else {
      Write-Info '[6] hslr-wait-hotkey.ini 不存在（DLL 首次启动会自动生成默认配置）'
    }

    Write-Info "[7] 游戏内 Mod 条目：$(Get-ModEntryState $game $modId)"
    if (!(Find-ModEntry $game $modId)) {
      $issues += "游戏本地 Mod 目录里没有 modId=$modId 的条目 —— 游戏内看不到开关。运行安装即可创建"
    }

    # settings.ini 里手填的路径：填了就必须有效，否则宁可让它报错
    $s = Read-Settings
    foreach ($opt in @(@('paths.mods', 'Mod 目录（[paths] mods）'),
                      @('paths.steam', 'Steam 库根（[paths] steam）'))) {
      $key = $opt[0]; $label = $opt[1]
      if ($s.ContainsKey($key) -and $s[$key]) {
        if (Test-Path -LiteralPath $s[$key] -PathType Container) {
          Write-Info "[$key] $label = $($s[$key])  OK"
        } else {
          Write-Info "[$key] $label = $($s[$key])  !!（目录不存在）"
          $issues += "settings.ini 里 $label 填的目录不存在：$($s[$key])。修掉或清空（清空=回到自动发现）"
        }
      }
    }
    $hk = Read-SettingsHotkeys
    if ($hk.Count -gt 0) {
      $desc = ($hk.GetEnumerator() | Sort-Object Name |
               ForEach-Object { "$($_.Name)→$($_.Value)" }) -join '、'
      Write-Info "[hotkeys] 当前键位：$desc"
    }

    $log = Join-Path $game 'hslr-wait-hotkey.log'
    if (Test-Path -LiteralPath $log) {
      $text = ''
      try { $text = [IO.File]::ReadAllText($log) } catch { }
      if ($text -match 'Unsupported GameAssembly' -or $text -match 'Fingerprint mismatch') {
        Write-Info '[8] 日志确认：DLL 判定游戏版本不受支持，什么都没挂钩  !!'
        $issues += '日志确认游戏版本不受支持（见第 [3] 项）'
      } elseif ($text -match 'Hooks ready') {
        Write-Info '[8] 日志确认：DLL 已正常挂钩，就等游戏内开关打开  OK'
        if ($issues.Count -eq 0) {
          $issues += 'DLL 一切正常但仍不生效的话：游戏内 Mod 管理没勾选启用，或勾了但没点「套用變更」（重启游戏前必须点）'
        }
      } else {
        Write-Info '[8] 日志存在，但没有看到安装成功/失败的标志行（可能是旧日志）'
      }
    } else {
      Write-Info '[8] 还没有 hslr-wait-hotkey.log —— DLL 没装，或游戏装完 DLL 后没启动过'
    }
  }

  # 启动器（manage.cmd）自检：坏掉的包双击就是「不是内部或外部命令」
  $launcher = Get-LauncherPath
  if (!(Test-Path -LiteralPath $launcher)) {
    Write-Info '[9] 启动器 manage.cmd 不存在（用 installer.ps1 直连也算正常；双击入口会缺失）'
  } elseif (Test-LauncherBroken) {
    $fixedNow = Repair-Launcher
    if ($fixedNow) {
      Write-Info '[9] 启动器 manage.cmd 曾损坏（行尾/编码不对）—— 已自动修复  OK'
      $issues += 'manage.cmd 曾被改坏（UTF-8 中文 / LF 行尾），双击会报「不是内部或外部命令」。本次已自动修好'
    } else {
      Write-Info '[9] 启动器 manage.cmd 损坏且修复失败  !!'
      $issues += "manage.cmd 损坏，自动修复失败。请重新下载完整包，或直接用 installer.ps1 运行命令"
    }
  } else {
    Write-Info '[9] 启动器 manage.cmd 正常（ASCII + CRLF）  OK'
  }

  # v2.0.0 卫星功能自检
  if ($game) {
    $satNotes = @()
    foreach ($sat in $Satellites) {
      $dest = Join-Path $game $sat.file
      if (!(Test-Path -LiteralPath $dest)) {
        $satNotes += "$($sat.label)：$($sat.file) 未安装"
        continue
      }
      $src = Get-SatelliteSource $sat.file
      if ($src -and ((Sha256 $dest) -ne (Sha256 $src))) {
        Write-Info "[10] $($sat.label) DLL 与本工具版本不同（建议重跑安装升级）"
      } else {
        Write-Info "[10] $($sat.label) DLL 已安装  OK"
      }
      $satIni = Join-Path $game $sat.ini
      if (Test-Path -LiteralPath $satIni) {
        $iniId = Read-IniOption $satIni 'mod_id'
        if ($iniId -and $iniId -ne $modId) {
          Write-Info ("[10] {0} 里的 mod_id（{1}）与条目开关（{2}）不一致  !!（重跑安装自动同步）" -f $sat.ini, $iniId, $modId)
          $satNotes += "$($sat.ini) 的 mod_id 未同步"
        }
      }
    }
    if ($satNotes.Count -gt 0) {
      foreach ($n in $satNotes) {
        Write-Info "[10] $n（不影响待机快捷键本身；重跑安装即可补齐/同步）"
      }
    }
  }

  Write-Info ''
  if ($issues.Count -gt 0) {
    Write-Info '发现的问题：'
    for ($i = 0; $i -lt $issues.Count; $i++) { Write-Info "  $($i + 1). $($issues[$i])" }
  } else {
    Write-Info '全部检查通过。如果游戏里仍无反应：'
    Write-Info '  1. 游戏内 Mod 管理 -> Mod List -> 勾选「待机快捷键」-> 点「套用變更」'
    Write-Info '  2. 行动菜单出现后再按空格（菜单外无反应是设计如此）'
    Write-Info '  3. 还不行就把本页截图 + 日志发给作者'
  }
  return 0
}

# ---------------------------------------------------------------- install / uninstall
function Invoke-Install {
  $null = Repair-Launcher        # 顺手修好被改坏的启动器
  $local = Get-DllSource
  if (!$local) {
    Write-Warn2 '!! 找不到 d3d11.dll。安装包不完整，请重新下载。'
    Write-Warn2 "   本工具会在下面两处找：$(Join-Path $Dist 'd3d11.dll') 或 $(Join-Path $Here 'd3d11.dll')"
    return 2
  }
  $game = Resolve-Game
  if (!$game) { return 2 }
  $null = Test-PathLength $game   # 【v2.3.1】路径过长只提醒、不阻断安装
  if (!(Assert-GameClosed)) { return 2 }
  if (!(Test-Path -LiteralPath (Join-Path $game 'GameAssembly.dll'))) {
    Write-Warn2 "!! $game 里没有 GameAssembly.dll，这看起来不是游戏目录。"
    return 2
  }

  $modId = Get-ConfiguredModId

  # modId 一致性闸门
  $configPath = Join-Path $game 'hslr-wait-hotkey.ini'
  if (!$TestConfig -and !$Force) {
    $existing = Read-IniOption $configPath 'mod_id'
    if ($existing -and $existing -ne $modId) {
      Write-Warn2 "!! hslr-wait-hotkey.ini 里的 mod_id 是「$existing」，本工具的是「$modId」，两边对不上。"
      Write-Warn2 "   继续装会把 .ini 改成「$modId」，游戏 Mod 列表里「$existing」那个开关就失效了。"
      Write-Warn2 "   确认要换的话：加 -Force 重跑。"
      return 2
    }
  }

  $target = Join-Path $game 'd3d11.dll'
  if ((Test-Path -LiteralPath $target) -and ((Sha256 $target) -ne (Sha256 $local)) -and !$Force) {
    if (Looks-LikeOurs $target) {
      $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
      $keep = Join-Path $BackupRoot "$stamp-upgrade\d3d11.dll"
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $keep) | Out-Null
      Copy-Item -LiteralPath $target -Destination $keep -Force
      Write-Info "检测到本 Mod 的旧版本 DLL（$((Sha256 $target).Substring(0,16))…），先备份再覆盖"
      Write-Info "  旧版本已备份  ->  $keep"
    } else {
      Write-Warn2 "!! 游戏目录已存在一个不同的 d3d11.dll（$((Sha256 $target).Substring(0,16))…），而且不像本 Mod 的版本。"
      Write-Warn2 '   如果你装过别的同类 Mod，它们不能共存。确认要覆盖请加 -Force。'
      return 2
    }
  }

  Copy-Item -LiteralPath $local -Destination $target -Force
  Write-Info "已安装 d3d11.dll  ->  $target"
  Write-Info "  SHA256 $(Sha256 $target)"

  # ---------------- v2.0.0 卫星安装：战斗倍速 / 属性道具稳定加值 ----------------
  # 卫星 DLL 由主 DLL 在运行期按「自己的所在目录」加载，所以必须与 d3d11.dll
  # 同放游戏根目录；ini 也在游戏根目录（与旧独立版位置一致，玩家旧配置直接沿用）。
  foreach ($sat in $Satellites) {
    $src = Get-SatelliteSource $sat.file
    if (!$src) {
      Write-Info "未随包携带「$($sat.label)」（$($sat.file)），跳过该功能"
      continue
    }
    $dest = Join-Path $game $sat.file
    if ((Test-Path -LiteralPath $dest) -and ((Sha256 $dest) -ne (Sha256 $src))) {
      $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
      $keep = Join-Path $BackupRoot "$stamp-satellite\$($sat.file)"
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $keep) | Out-Null
      Copy-Item -LiteralPath $dest -Destination $keep -Force
      Write-Info "检测到旧版 $($sat.file)（$((Sha256 $dest).Substring(0,16))…），已备份后覆盖  ->  $keep"
    }
    Copy-Item -LiteralPath $src -Destination $dest -Force
    Write-Info "已安装「$($sat.label)」  ->  $dest"
    Write-Info "  SHA256 $(Sha256 $dest)"
    # 旧独立版迁移：配置原样保留，只把 mod_id 指向整合条目（游戏内一个开关管全部）
    $satIni = Join-Path $game $sat.ini
    if (Test-Path -LiteralPath $satIni) {
      if (Patch-IniOption $satIni 'mod_id' $modId) {
        Write-Info "  已把 $($sat.ini) 的 mod_id 同步为 $modId（原有键位/参数原样保留）"
      }
    }
  }

  # ---------------- v2.0.0 旧独立版条目清理：移出 MODS（备份，不删除）----------------
  # 整合后三个功能共用「待机快捷键」一个条目开关；旧的两个条目留着会占用
  # 游戏内 Mod 列表（且其 DLL 已不在 d3d11 槽位，纯粹是摆设），统一移到
  # MODS 旁边的 disabled-mods-<时间戳>\ 文件夹，随时可以手动搬回来。
  if (!$NoModEntry) {
    $modsRoot = Get-LocalModsRoot $game
    if ($modsRoot) {
      foreach ($legacy in $LegacyModIds) {
        # 同一个 modId 可能对应多个目录（改名重建/工坊+手装并存），全部移走。
        # 注意：Scan-ModEntries 用 , 包装返回（防单条目被展开成标量），直接把
        # 它接进管道会把整个数组当成一个条目 —— 必须先赋值再过滤。
        $all = Scan-ModEntries $game
        $found = @($all | Where-Object { $_.modId -eq $legacy })
        foreach ($e in $found) {
          $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
          $destName = [string]$e.name      # 显式收敛为 string（PS 5.1 Join-Path 不收数组）
          $disabled = Join-Path ([string](Split-Path -Parent $modsRoot)) "disabled-mods-$stamp"
          New-Item -ItemType Directory -Force -Path $disabled | Out-Null
          Move-Item -LiteralPath ([string]$e.dir) -Destination (Join-Path $disabled $destName) -Force
          Write-Info "旧独立版条目「$destName」（modId=$legacy）已移出 MODS（未删除，备份在 $disabled）"
        }
      }
    }
  }

  $config = $configPath
  $hotkeys = Read-SettingsHotkeys
  if ($TestConfig) {
    [IO.File]::WriteAllText($config, (New-DefaultConfig $true $modId $null), $Utf8Bom)
    Write-Info "已写入测试配置（force_enable=1, verbose_log=1）  ->  $config"
  } elseif (Test-Path -LiteralPath $config) {
    Write-Info "配置文件已存在  ->  $config"
    if (Patch-IniOption $config 'mod_id' $modId) { Write-Info "  已同步 mod_id = $modId" }
    $feText = [IO.File]::ReadAllText($config)
    if (!$feText.Contains('force_enable = 0')) {
      if (Patch-IniOption $config 'force_enable' '0') {
        Write-Info '  已把 force_enable 改为 0（改由游戏内的 Mod 开关控制）'
      }
    }
    # settings.ini 的 [hotkeys] 是键位的源头，安装时同步进游戏 .ini。
    # 想临时改键也可以直接改游戏里的 .ini（保存即生效），但下次安装会被
    # settings.ini 覆盖 —— 记得两边保持一致。
    if ($hotkeys.Count -gt 0 -and (Set-IniSection $config 'hotkeys' $hotkeys)) {
      $desc = ($hotkeys.GetEnumerator() | Sort-Object Name |
               ForEach-Object { "$($_.Name)→$($_.Value)" }) -join '、'
      Write-Info "  已按 settings.ini 的 [hotkeys] 同步键位：$desc"
    }
  } else {
    [IO.File]::WriteAllText($config, (New-DefaultConfig $false $modId $hotkeys), $Utf8Bom)
    Write-Info "已生成默认配置（force_enable=0，听游戏内的开关）  ->  $config"
    if ($hotkeys.Count -gt 0) {
      $desc = ($hotkeys.GetEnumerator() | Sort-Object Name |
               ForEach-Object { "$($_.Name)→$($_.Value)" }) -join '、'
      Write-Info "  键位来自 settings.ini 的 [hotkeys]：$desc"
    }
  }

  if (!$NoModEntry) {
    $s = Read-Settings
    $author = 'unknown'
    if ($s.ContainsKey('mod.author') -and $s['mod.author']) { $author = $s['mod.author'] }
    elseif ($env:USERNAME) { $author = $env:USERNAME }
    if (!(Install-ModEntry $game $author $modId) -and !$Force) {
      Write-Warn2 '   （Mod 条目没建起来。DLL 已装好，只是游戏内看不到开关。加 -Force 可跳过。）'
      return 2
    }
  } else {
    Write-Info '按要求跳过游戏内 Mod 条目。'
  }

  if (!$TestConfig) {
    Write-Info ''
    Write-Info "启动游戏 -> 主菜单的 Mod 管理 -> Mod List 分页，就能看到「$ModName」了。"
    Write-Info '  在里面勾选启用，务必点「套用變更」，否则重启后不生效。'
    Write-Info "  条目 ID：modId=$modId（DLL 靠它判断开关状态）"
  }
  Write-Info ''
  Write-Info '完成。启动游戏即可；游戏启动后会在同目录生成 hslr-wait-hotkey.log。'
  return 0
}

function Invoke-Uninstall {
  $game = Resolve-Game
  if (!$game) { return 2 }
  if (!(Assert-GameClosed)) { return 2 }
  $modId = Get-ConfiguredModId
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $backup = Join-Path $BackupRoot $stamp
  New-Item -ItemType Directory -Force -Path $backup | Out-Null

  $removed = 0
  $names = @('d3d11.dll', 'hslr-wait-hotkey.ini', 'hslr-wait-hotkey.log')
  # v2.0.0：卫星 DLL 及其 ini/日志一起移出
  foreach ($sat in $Satellites) {
    $names += @($sat.file, $sat.ini, ($sat.ini -replace '\.ini$', '.log'))
  }
  foreach ($name in $names) {
    $path = Join-Path $game $name
    if (Test-Path -LiteralPath $path) {
      Move-Item -LiteralPath $path -Destination (Join-Path $backup $name) -Force
      Write-Info "已移出 $name"
      $removed++
    }
  }
  if (!$KeepModEntry) {
    if (Remove-ModEntry $game $modId) { $removed++ }
  }
  if ($removed -eq 0) {
    Write-Info '游戏目录里没有本 Mod 的文件，无需卸载。'
    return 0
  }
  Write-Info ''
  Write-Info "卸载完成。文件备份在：$backup"
  Write-Info '游戏目录已恢复原状。确认没问题后可以手动删掉上面的备份目录。'
  return 0
}

# ---------------------------------------------------------------- log
function Invoke-Log {
  $game = Resolve-Game
  if (!$game) { return 2 }
  $path = Join-Path $game 'hslr-wait-hotkey.log'
  if (!(Test-Path -LiteralPath $path)) {
    Write-Warn2 "还没有日志：$path"
    Write-Warn2 '游戏至少启动过一次之后才会有。'
    return 2
  }
  Write-Info "===== $path ====="
  Get-Content -LiteralPath $path -Encoding UTF8 | ForEach-Object { Write-Info $_ }
  if ($Follow) {
    Write-Info '---- 跟踪中，Ctrl+C 退出 ----'
    Get-Content -LiteralPath $path -Encoding UTF8 -Wait -Tail 0 | ForEach-Object { Write-Info $_ }
  }
  return 0
}

# ---------------------------------------------------------------- setgame
function Invoke-SetGame {
  if ($Auto) {
    Save-Settings @{ 'paths.game' = '' }
    Write-Info '已清除 settings.ini 里的游戏目录，回到 Steam 清单自动发现。'
    return 0
  }
  if ($GameArg) {
    $game = $GameArg
    if (!(Test-Path -LiteralPath $game -PathType Container)) {
      Write-Warn2 "!! 目录不存在：$game"; return 2
    }
    if (!(Test-Path -LiteralPath (Join-Path $game $GameExe))) {
      Write-Warn2 "!! $game 里没有 $GameExe，这看起来不是游戏目录。"
      return 2
    }
  } else {
    $game = Find-Game
    if (!$game) {
      Write-Warn2 '!! 没能自动找到游戏目录。请手动指定：'
      Write-Warn2 '   installer.ps1 setgame "游戏目录"'
      return 2
    }
  }
  Save-Settings @{ 'paths.game' = $game }
  Write-Info "已记住游戏目录（写入 settings.ini）："
  Write-Info "  $game"
  Write-Info ''
  Write-Info '之后 status / install / uninstall / log 都会用它，不用再传 -Game。'
  Write-Info '想改回自动发现：installer.ps1 setgame -Auto'
  return 0
}

# ---------------------------------------------------------------- 交互菜单
$menu = @(
  @('status', '查看当前安装状态（含游戏内 Mod 列表）'),
  @('doctor', '环境自检：用不了 / 装不上时先跑这个'),
  @('install', '安装 / 升级（DLL + 配置 + 游戏内 Mod 条目）'),
  @('install -TestConfig', '安装并写入测试配置（force_enable=1，本地实测用）'),
  @('uninstall', '卸载（文件备份到工具目录 backup\）'),
  @('log', '打印游戏日志'),
  @('setgame', '设置游戏目录（自动检测；或 setgame 路径 手动指定）')
)

function Invoke-Menu {
  Write-Info "幻世录重置版 · 待机快捷键 管理工具 v$ToolVersion（PowerShell 版）"
  if (Test-Path -LiteralPath (Join-Path $Here 'mod.json')) {
    Write-Info '本目录就是游戏内 Mod 条目（随包自带 mod.json，游戏认它的身份卡）'
    Write-Info '首次使用：先把游戏完全退出，再选 3) 安装。'
  }
  while ($true) {
    Write-Info ''
    for ($i = 0; $i -lt $menu.Count; $i++) { Write-Info "  $($i + 1)) $($menu[$i][1])" }
    Write-Info "  0) 退出"
    Write-Info ''
    $raw = Read-Host "请选择 [0-$($menu.Count)]"
    $raw = "$raw".Trim()
    if ($raw -in @('', '0', 'q', 'Q')) { return 0 }
    $idx = 0
    if (![int]::TryParse($raw, [ref]$idx) -or $idx -lt 1 -or $idx -gt $menu.Count) {
      Write-Warn2 "无效输入：$raw"; continue
    }
    Write-Info ''
    $parts = $menu[$idx - 1][0].Split(' ')
    switch ($parts[0]) {
      'status'    { $null = Invoke-Status }
      'doctor'    { $null = Invoke-Doctor }
      'install'   {
        $script:TestConfig = ($parts -contains '-TestConfig')
        $null = Invoke-Install
      }
      'uninstall' { $null = Invoke-Uninstall }
      'log'       { $null = Invoke-Log }
      'setgame'   { $null = Invoke-SetGame }
    }
    Write-Info ''
    Read-Host '按回车继续' | Out-Null
  }
}

# ---------------------------------------------------------------- 入口
$rc = 0
try {
  switch ($Cmd) {
    ''            { $rc = Invoke-Menu }
    'status'      { $rc = Invoke-Status }
    'doctor'      { $rc = Invoke-Doctor }
    'install'     { $rc = Invoke-Install }
    'uninstall'   { $rc = Invoke-Uninstall }
    'log'         { $rc = Invoke-Log }
    'setgame'     { $rc = Invoke-SetGame }
    'version'     { Write-Info $ToolVersion; $rc = 0 }
    default       { Write-Warn2 "未知命令：$Cmd（可用：status/install/uninstall/doctor/log/setgame）"; $rc = 2 }
  }
} catch {
  Write-Warn2 "!! 出错了：$($_.Exception.Message)"
  Write-Warn2 "   位置：$($_.InvocationInfo.ScriptName) 第 $($_.InvocationInfo.ScriptLineNumber) 行附近"
  Write-Warn2 "   请把这段话截图发给作者。"
  $rc = 1
}
exit $rc
