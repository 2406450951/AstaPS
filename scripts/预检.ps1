# 预检 —— 一条命令看清这台机器还缺什么
#   powershell -ExecutionPolicy Bypass -File .\预检.ps1
# 输出 PASS / FAIL，FAIL 的那行下面直接给修法。

$ErrorActionPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(936)
$results = New-Object System.Collections.ArrayList

$entry = $PSScriptRoot                                   # ...\genshi\GS71
$gs    = Join-Path (Split-Path -Parent $entry) 'gcsrv'   # ...\genshi\gcsrv
$rt    = 'D:\Programs\gs71-runtime'
$game  = 'D:\Program Files\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game'

function Add-Result($name, $ok, $detail, $fix) {
    [void]$results.Add([pscustomobject]@{ Name = $name; Ok = [bool]$ok; Detail = $detail; Fix = $fix })
}

# ---------------------------------------------------------------- 运行时
$java = Join-Path $rt 'jdk21\bin\java.exe'
if (Test-Path $java) {
    # java -version 写 stderr，在 PS 5.1 里不好抓，直接读文件版本更稳
    $ver = (Get-Item $java).VersionInfo.ProductVersion
    Add-Result 'JDK 21' ($ver -match '^21') "$java  $ver" '重新解压 JDK21 到 D:\Programs\gs71-runtime\jdk21'
} else {
    Add-Result 'JDK 21' $false 'missing' "把 OpenJDK21 解压到 $rt\jdk21"
}

$mongo = Join-Path $rt 'mongodb\bin\mongod.exe'
Add-Result 'MongoDB' (Test-Path $mongo) $(if (Test-Path $mongo) { $mongo } else { 'missing' }) "把 MongoDB 7.x zip 解压到 $rt\mongodb"

$mongoPort = Get-NetTCPConnection -LocalPort 27017 -State Listen -ErrorAction SilentlyContinue
Add-Result 'MongoDB 27017' ([bool]$mongoPort) $(if ($mongoPort) { 'LISTEN' } else { 'down' }) '双击 启动.bat 会自动拉起 mongod'

$py = Join-Path $rt 'python311\python.exe'
if (Test-Path $py) {
    $tk = (& $py -c "import tkinter;print(tkinter.TkVersion)" 2>&1) -join ''
    Add-Result 'Python 3.11' $true "$py" ''
    Add-Result 'tkinter' ($tk -match '^\d') "TkVersion=$tk" '重装 Python 时勾上 tcl/tk'
} else {
    Add-Result 'Python 3.11' $false 'missing' "装到 $rt\python311（仅旧版外部面板需要）"
}

# ---------------------------------------------------------------- 服务端
foreach ($p in @((Join-Path $gs 'grasscutter.jar'), (Join-Path $gs 'resources'),
                 (Join-Path $gs 'plugins\CheatMenu.jar'), (Join-Path $gs 'start-server.ps1'))) {
    Add-Result ("存在 " + (Split-Path $p -Leaf)) (Test-Path $p) $(if (Test-Path $p) { 'ok' } else { 'missing' }) 'gcsrv 目录不完整'
}

$res = Join-Path $gs 'resources'
if (Test-Path $res) {
    $n = (Get-ChildItem $res -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
    Add-Result 'resources 文件数' ($n -gt 30000) "$n files" '资源应约 40,079 个文件 / 1.63 GB'
}

# ---------------------------------------------------------------- 端口 / 插件
$tcp = Get-NetTCPConnection -LocalPort 8088 -State Listen -ErrorAction SilentlyContinue
$udp = Get-NetUDPEndpoint -LocalPort 22101 -ErrorAction SilentlyContinue
Add-Result '8088/TCP dispatch' ([bool]$tcp) $(if ($tcp) { 'LISTEN' } else { 'down' }) '双击 启动.bat'
Add-Result '22101/UDP 游戏' ([bool]$udp) $(if ($udp) { 'LISTEN' } else { 'down' }) '这端口是 UDP，别用 TCP 查'

try {
    $st = (Invoke-WebRequest 'http://127.0.0.1:8088/status/server' -TimeoutSec 3 -UseBasicParsing).Content
    Add-Result 'HTTP /status/server' $true $st ''
} catch {
    Add-Result 'HTTP /status/server' $false 'unreachable' '服务端没起'
}

try {
    $ping = (Invoke-WebRequest 'http://127.0.0.1:8890/ping' -TimeoutSec 3 -UseBasicParsing).Content
    Add-Result '覆盖层 API 8890' ($ping -eq 'ok') $ping '插件没加载或 8890 被占'
} catch {
    Add-Result '覆盖层 API 8890' $false 'unreachable' '看服务端日志有没有 “overlay API on http://127.0.0.1:8890/snap”'
}

# ---------------------------------------------------------------- 客户端
Add-Result '客户端 YuanShen.exe' (Test-Path (Join-Path $game 'YuanShen.exe')) $game '国服客户端没装到预期位置'
Add-Result '覆盖层 Astrolabe.dll' (Test-Path (Join-Path $game 'Astrolabe.dll')) `
    $(if (Test-Path (Join-Path $game 'Astrolabe.dll')) { '已部署' } else { 'missing' }) `
    'powershell -ExecutionPolicy Bypass -File ..\clientpatch\deploy_overlay.ps1'
Add-Result '补丁 hk4e-patch.dll' (Test-Path (Join-Path $game 'hk4e-patch.dll')) `
    $(if (Test-Path (Join-Path $game 'hk4e-patch.dll')) { '已部署' } else { 'missing' }) '同上'

$info = Join-Path $game 'config.ini'
if (Test-Path $info) {
    $ver = (Get-Content $info | Select-String 'game_version' | Select-Object -First 1).Line
    Add-Result '客户端版本' ($ver -match '7\.1') $ver '私服只认 7.1.x'
}

# ---------------------------------------------------------------- 输出
Write-Host ''
Write-Host ('=' * 78)
Write-Host '  环境预检'
Write-Host ('=' * 78)
foreach ($r in $results) {
    $mark = if ($r.Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($r.Ok) { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1,-28} {2}" -f $mark, $r.Name, $r.Detail) -ForegroundColor $color
    if (-not $r.Ok -and $r.Fix) { Write-Host ("        -> " + $r.Fix) -ForegroundColor Yellow }
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ('=' * 78)
if ($fail -eq 0) {
    Write-Host '  全部通过。' -ForegroundColor Green
} else {
    Write-Host ("  还有 {0} 项不满足，按上面的提示逐条修。" -f $fail) -ForegroundColor Yellow
}
Write-Host ''
exit $(if ($fail -eq 0) { 0 } else { 2 })
