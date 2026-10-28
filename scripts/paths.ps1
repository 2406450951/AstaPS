# 各脚本共用的路径解析。要改路径只改这一个文件。

$RepoRoot = Split-Path -Parent $PSScriptRoot      # 仓库根
$ServerDir = Join-Path $RepoRoot 'server'
$LocalDir = Join-Path $RepoRoot '_local'          # 日志、截图、临时文件
if (-not (Test-Path $LocalDir)) { New-Item -ItemType Directory -Force -Path $LocalDir | Out-Null }

# ---- 客户端目录 ----
# 优先读环境变量 GS71_GAME；否则在下面几个候选里找存在的那一个。
# 想固定用某个客户端，就把它的路径填到第一行。
$GameCandidates = @(
    $env:GS71_GAME,
    (Join-Path $env:ProgramFiles 'HoYoPlay\games\Genshin Impact game'),
    'D:\Program Files\HoYoPlay\games\Genshin Impact game',
    (Join-Path $env:ProgramFiles 'miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game'),
    'D:\Program Files\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game'
) | Where-Object { $_ }

$GameDir = $null
foreach ($c in $GameCandidates) {
    if ((Test-Path (Join-Path $c 'GenshinImpact.exe')) -or (Test-Path (Join-Path $c 'YuanShen.exe'))) {
        $GameDir = $c
        break
    }
}
$GameExe = if (-not $GameDir) { $null }
           elseif (Test-Path (Join-Path $GameDir 'GenshinImpact.exe')) { 'GenshinImpact.exe' }
           else { 'YuanShen.exe' }

# ---- 解释器 ----
# PATH 里有就用 PATH 里的；装在别处就设环境变量 GS71_PYTHON / JAVA_HOME。
$PythonExe = if ($env:GS71_PYTHON) { $env:GS71_PYTHON } else { 'python' }
$PythonWExe = if ($env:GS71_PYTHONW) { $env:GS71_PYTHONW }
              elseif ($env:GS71_PYTHON) { Join-Path (Split-Path -Parent $env:GS71_PYTHON) 'pythonw.exe' }
              else { 'pythonw' }
$JavaExe = if ($env:JAVA_HOME) { Join-Path $env:JAVA_HOME 'bin\java.exe' } else { 'java' }

# ---- 可选：外置帧率解锁工具 ----
$UnlockerExe = if ($env:GS71_UNLOCKER) { $env:GS71_UNLOCKER }
               else { 'unlockfps_nc.exe' }   # 放在 PATH 里，或设 GS71_UNLOCKER 指向完整路径

# ---- 端口 ----
$HttpPort = 8088
$ApiPort = 8890
$TlsPort = 443
