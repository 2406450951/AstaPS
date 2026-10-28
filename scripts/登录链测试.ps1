# 登录链端到端测试：按国服客户端真实的请求顺序和报文体打一遍私服
#   powershell -ExecutionPolicy Bypass -File .\登录链测试.ps1
# 全部 PASS 说明"登录状态失效"那条链已经通了（剩下只是客户端稳定性）

$ErrorActionPreference = 'Continue'
$base = 'http://127.0.0.1:8088'
$results = New-Object System.Collections.ArrayList
function Add-R($name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Name = $name; Ok = [bool]$ok; Detail = $detail })
}
function PostJ($path, $obj) {
    $body = if ($obj -is [string]) { $obj } else { $obj | ConvertTo-Json -Compress -Depth 6 }
    Invoke-WebRequest ($base + $path) -Method POST -Body $body -ContentType 'application/json' -UseBasicParsing -TimeoutSec 10
}

# 1) loadConfig
try {
    $r = (Invoke-WebRequest "$base/gs71/sdk/loadConfig?client=3&game_key=hk4e_cn" -UseBasicParsing -TimeoutSec 8).Content
    $ok = $r -match '"account_url"\s*:\s*"http://127\.0\.0\.1:8088"'
    Add-R '1 loadConfig → account_url' $ok $(if ($ok) { 'ok' } else { 'missing account_url' })
} catch { Add-R '1 loadConfig → account_url' $false $_.Exception.Message }

# 2) compareProtocolVersion（客户端会拿它决定要不要走新协议）
try {
    $r = (PostJ '/gs71/sdk/compareProtocolVersion?client=3&game_key=hk4e_cn' '{}').Content
    $ok = ($r -match '"protocol"\s*:\s*true') -or ($r -match '"modified"\s*:\s*true')
    Add-R '2 compareProtocolVersion' $ok $(if ($ok) { 'protocol=true' } else { $r })
} catch { Add-R '2 compareProtocolVersion' $false $_.Exception.Message }

# 3) loginByPassword（国服加密体，服务端回落建号）
$token = $null
try {
    $fake = 'A' * 344
    $r = (PostJ '/account/ma-cn-passport/app/loginByPassword' @{ account = $fake; password = $fake }).Content
    $ok = $r -match '"retcode"\s*:\s*0'
    if ($ok -and $r -match '"token"\s*:\s*"([^"]+)"') { $token = $matches[1] }
    Add-R '3 loginByPassword → token' ($ok -and $token) $(if ($token) { "token=$($token.Substring(0,[Math]::Min(12,$token.Length)))..." } else { $r.Substring(0,[Math]::Min(160,$r.Length)) })
} catch { Add-R '3 loginByPassword → token' $false $_.Exception.Message }
if (-not $token) { $token = 'v2_fallback' }

# 4) verify（客户端用旧 token 自检）
try {
    $r = (PostJ '/account/ma-cn-session/app/verify' @{ mid = '10002'; token = @{ token_type = 1; token = $token }; refresh = $true }).Content
    $ok = ($r -match '"retcode"\s*:\s*0') -and ($r -match 'user_info') -and ($r -match '"token"\s*:')
    Add-R '4 verify → user_info + token' $ok $(if ($ok) { "len=$($r.Length)" } else { $r.Substring(0,[Math]::Min(160,$r.Length)) })
} catch { Add-R '4 verify → user_info + token' $false $_.Exception.Message }

# 5) exchange（把 token 换成 dst_token_type=4）—— 今晚"登录状态失效"的死因
try {
    $r = (PostJ '/account/ma-cn-session/app/exchange' @{ mid = '10002'; dst_token_type = 4; src_token = @{ token_type = 1; token = $token } }).Content
    $ok = ($r -match '"retcode"\s*:\s*0') -and ($r -match '"token_type"\s*:\s*4') -and ($r.Length -gt 200)
    Add-R '5 exchange → token_type 4' $ok $(if ($ok) { "len=$($r.Length)" } else { "len=$($r.Length) —— 空包就是登录状态失效的原因" })
} catch { Add-R '5 exchange → token_type 4' $false $_.Exception.Message }

# 6) query_region_list（客户端拿服务器列表）
try {
    $r = (Invoke-WebRequest "$base/query_region_list?version=CNRELWin7.1.0&lang=2&platform=3&binary=1&time=1&channel_id=1&sub_channel_id=1" -UseBasicParsing -TimeoutSec 8).Content
    $ok = $r.Length -gt 100
    Add-R '6 query_region_list' $ok $(if ($ok) { "len=$($r.Length)" } else { 'too short' })
} catch { Add-R '6 query_region_list' $false $_.Exception.Message }

# 7) query_cur_region（客户端拿网关地址）
try {
    $r = (Invoke-WebRequest "$base/query_cur_region/os_usa?version=CNRELWin7.1.0&lang=2&platform=3&binary=1&time=1&channel_id=1&sub_channel_id=1" -UseBasicParsing -TimeoutSec 8).Content
    $ok = $r.Length -gt 50
    Add-R '7 query_cur_region/os_usa' $ok $(if ($ok) { "len=$($r.Length)" } else { 'too short' })
} catch { Add-R '7 query_cur_region/os_usa' $false $_.Exception.Message }

# 输出
Write-Host ''
Write-Host ('=' * 78)
Write-Host '  登录链端到端测试（HTTP 层，不需要启动游戏）'
Write-Host ('=' * 78)
foreach ($r in $results) {
    $mark = if ($r.Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($r.Ok) { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1,-30} {2}" -f $mark, $r.Name, $r.Detail) -ForegroundColor $color
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ('=' * 78)
if ($fail -eq 0) {
    Write-Host '  全链通过：客户端从登录到拿到网关，服务端每一环都能正确应答。' -ForegroundColor Green
} else {
    Write-Host ("  {0} 项不通过，上面 FAIL 就是断点。" -f $fail) -ForegroundColor Yellow
}
Write-Host ''
exit $(if ($fail -eq 0) { 0 } else { 2 })
