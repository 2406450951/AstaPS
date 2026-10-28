# 自检：一次性验证 服务端 / 插件 / 覆盖层 / 数据通道 是否全部活着
#   powershell -ExecutionPolicy Bypass -File .\自检.ps1
# 每项打印 PASS / FAIL，最后给总结。退出码 0 = 全过。

$ErrorActionPreference = 'SilentlyContinue'
$results = New-Object System.Collections.ArrayList
function Add-R($name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Name = $name; Ok = [bool]$ok; Detail = $detail })
}

$gs71 = $PSScriptRoot
$CP = Join-Path (Split-Path -Parent $gs71) 'clientpatch'
$gcsrv = Join-Path (Split-Path -Parent $gs71) 'gcsrv'
$game = 'D:\Program Files\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game'

# ---------------------------------------------------------------- 1. 服务端
try {
    $st = (Invoke-WebRequest 'http://127.0.0.1:8088/status/server' -TimeoutSec 3 -UseBasicParsing).Content
    Add-R 'dispatch 8088 /status/server' ($st -match '"version":"7\.1') $st
} catch {
    Add-R 'dispatch 8088 /status/server' $false 'unreachable —— 先双击 启动.bat'
}

$udp = Get-NetUDPEndpoint -LocalPort 22101 -ErrorAction SilentlyContinue
Add-R '游戏端口 22101/UDP' ([bool]$udp) $(if ($udp) { 'LISTEN' } else { 'down' })

# ---------------------------------------------------------------- 2. 插件 / 覆盖层 API
try {
    $ping = (Invoke-WebRequest 'http://127.0.0.1:8890/ping' -TimeoutSec 3 -UseBasicParsing).Content
    Add-R '覆盖层 API 8890 /ping' ($ping -eq 'ok') $ping
} catch {
    Add-R '覆盖层 API 8890 /ping' $false 'unreachable —— 插件没加载？'
}

# ---------------------------------------------------------------- 3. 数据通道（写入 → 回读）
$before = ''
try {
    Invoke-WebRequest 'http://127.0.0.1:8890/set?god=1&mult=100&aura=1&aurar=20' -TimeoutSec 3 -UseBasicParsing | Out-Null
    Start-Sleep -Milliseconds 700
    $snap = (Invoke-WebRequest 'http://127.0.0.1:8890/snap' -TimeoutSec 3 -UseBasicParsing).Content
    $stLine = ($snap -split "`n" | Where-Object { $_ -match '^st ' }) -join ''
    $before = $stLine
    $ok = ($stLine -match 'god=1') -and ($stLine -match 'mult=100\.0') -and ($stLine -match 'aurar=20\.0')
    Add-R '状态写入 /set → /snap 回读' $ok $(if ($ok) { 'god=1 mult=100 aurar=20 全部生效' } else { $stLine })
    # 复位
    Invoke-WebRequest 'http://127.0.0.1:8890/set?god=0&mult=1&aura=0' -TimeoutSec 3 -UseBasicParsing | Out-Null
} catch {
    Add-R '状态写入 /set → /snap 回读' $false $_.Exception.Message
}

# ---------------------------------------------------------------- 4. 动作通道
try {
    Invoke-WebRequest 'http://127.0.0.1:8890/cmd?c=killall' -TimeoutSec 3 -UseBasicParsing | Out-Null
    Start-Sleep -Milliseconds 800
    $snap2 = (Invoke-WebRequest 'http://127.0.0.1:8890/snap' -TimeoutSec 3 -UseBasicParsing).Content
    $act = ($snap2 -split "`n" | Where-Object { $_ -match '^act ' }) -join ''
    # 没人在线时插件会正确回 no_player_online；有角色在线时会回 killall 结果
    $ok = ($act -match 'no_player_online') -or ($act -match 'killall')
    Add-R '动作通道 /cmd killall' $ok $act
} catch {
    Add-R '动作通道 /cmd killall' $false $_.Exception.Message
}

# ---------------------------------------------------------------- 6. 国服登录链（服务端这半条）
try {
    $lc = (Invoke-WebRequest 'http://127.0.0.1:8088/gs71/sdk/loadConfig?client=3&game_key=hk4e_cn' -TimeoutSec 3 -UseBasicParsing).Content
    $hasUrl = $lc -match '"account_url":"http://127\.0\.0\.1:8088"'
    Add-R 'loadConfig 带 account_url' $hasUrl $(if ($hasUrl) { '指向 127.0.0.1:8088' } else { '缺少 account_url —— SDK 会回落到官服地址' })
} catch {
    Add-R 'loadConfig 带 account_url' $false $_.Exception.Message
}

$sampleMid = '10002'
try {
    $vBody = '{"mid":"' + $sampleMid + '","token":{"token_type":1,"token":"v2_selftest"},"refresh":true}'
    $vr = (Invoke-WebRequest 'http://127.0.0.1:8088/account/ma-cn-session/app/verify' -Method POST -Body $vBody -ContentType 'application/json' -TimeoutSec 4 -UseBasicParsing).Content
    $vOk = ($vr -match '"retcode"\s*:\s*0') -and ($vr -match 'user_info')
    Add-R 'verify 返回 user_info' $vOk $(if ($vOk) { "len=$($vr.Length)" } else { '空应答或结构不对' })
} catch {
    Add-R 'verify 返回 user_info' $false $_.Exception.Message
}

try {
    $eBody = '{"mid":"' + $sampleMid + '","dst_token_type":4,"src_token":{"token_type":1,"token":"v2_selftest"}}'
    $er = (Invoke-WebRequest 'http://127.0.0.1:8088/account/ma-cn-session/app/exchange' -Method POST -Body $eBody -ContentType 'application/json' -TimeoutSec 4 -UseBasicParsing).Content
    $eOk = ($er -match '"retcode"\s*:\s*0') -and ($er -match '"token_type"\s*:\s*4')
    Add-R 'exchange 返回 dst token' $eOk $(if ($eOk) { "len=$($er.Length) token_type=4" } else { "空应答或没有 token_type:4  (len=$($er.Length))" })
} catch {
    Add-R 'exchange 返回 dst token' $false $_.Exception.Message
}

# ---------------------------------------------------------------- 7. 文件部署
foreach ($f in @('Astrolabe.dll', 'hk4e-patch.dll')) {
    $p = Join-Path $game $f
    $ok = Test-Path $p
    Add-R "客户端 $f" $ok $(if ($ok) { "{0:N0} bytes" -f (Get-Item $p).Length } else { 'missing —— 跑 clientpatch\deploy_overlay.ps1' })
}

$hasCfg = Test-Path (Join-Path $CP 'mitm_sdk.py')
Add-R '本地 TLS 前置脚本' $hasCfg $(if ($hasCfg) { 'clientpatch\mitm_sdk.py' } else { 'missing' })

$ca = Get-ChildItem Cert:\LocalMachine\Root -ErrorAction SilentlyContinue | Where-Object { $_.Subject -match 'GS71' }
Add-R 'GS71 根证书已信任' ([bool]$ca) $(if ($ca) { $ca.Subject } else { '没装 —— 客户端 HTTPS 登录会被拒' })

$hostsFile = 'C:\Windows\System32\drivers\etc\hosts'
$hostsOk = (Get-Content $hostsFile -ErrorAction SilentlyContinue | Where-Object { $_ -match 'passport-api\.mihoyo\.com' }).Count -gt 0
Add-R 'hosts 指向本地' $hostsOk $(if ($hostsOk) { '127.0.0.1 passport-api.mihoyo.com' } else { '缺这行 —— 登录会发去官服' })

# ---------------------------------------------------------------- 8. 覆盖层是否真的进了游戏
$ast = Join-Path $game 'Astrolabe.log'
if (Test-Path $ast) {
    $tail = Get-Content $ast -Tail 20
    $ready = @($tail | Where-Object { $_ -match 'imgui ready' }).Count -gt 0
    Add-R '覆盖层已加载进游戏' $ready $(if ($ready) { 'imgui ready 出现过' } else { '没有 imgui ready 记录' })
    $crash = @($tail | Where-Object { $_ -match 'CRASH exception' }).Count -gt 0
    if ($crash) {
        $line = ($tail | Where-Object { $_ -match 'CRASH exception' } | Select-Object -Last 1)
        Add-R '覆盖层崩溃记录' $false $line
    }
} else {
    Add-R '覆盖层已加载进游戏' $false '还没有 Astrolabe.log（客户端没启动过？）'
}

$pat = Join-Path $game 'hk4e-patch.log'
if (Test-Path $pat) {
    $ok = @(Get-Content $pat -ErrorAction SilentlyContinue | Where-Object { $_ -match 'Successfully initialized|Redirect:' }).Count -gt 0
    Add-R '补丁已生效' $ok $(if ($ok) { 'Successfully initialized' } else { '未见初始化成功记录' })
} else {
    Add-R '补丁已生效' $false '还没有 hk4e-patch.log'
}

# ---------------------------------------------------------------- 输出
Write-Host ''
Write-Host ('=' * 74)
Write-Host '  GS71 自检'
Write-Host ('=' * 74)
foreach ($r in $results) {
    $mark = if ($r.Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($r.Ok) { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1,-26} {2}" -f $mark, $r.Name, $r.Detail) -ForegroundColor $color
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ('=' * 74)
if ($fail -eq 0) {
    Write-Host '  全部通过：服务端 + 登录链(loadConfig/verify/exchange) + 插件 + 覆盖层 + 作弊通道' -ForegroundColor Green
    Write-Host '  客户端侧只剩稳定性验证：进世界后挂着别动 3-5 分钟（见 明天继续.md）' -ForegroundColor Yellow
} else {
    Write-Host ("  {0} 项不通过。" -f $fail) -ForegroundColor Yellow
}
Write-Host ''
exit $(if ($fail -eq 0) { 0 } else { 2 })
