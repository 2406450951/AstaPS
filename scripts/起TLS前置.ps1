. "$PSScriptRoot\paths.ps1"
# 起本地 TLS 前置（截获 SDK 发往 passport-api.mihoyo.com 的 HTTPS 登录，转成明文给私服）
#   powershell -ExecutionPolicy Bypass -File .\起TLS前置.ps1
# 已在监听则直接返回；启动后等最多 8 秒确认端口真的起来了。

$ErrorActionPreference = 'SilentlyContinue'
$cp = Join-Path (Split-Path -Parent $PSScriptRoot) 'clientpatch'
$py = $PythonExe
$script = Join-Path $cp 'mitm_sdk.py'

function Port443() {
    @(Get-NetTCPConnection -LocalPort 443 -State Listen -ErrorAction SilentlyContinue).Count -gt 0
}

if (Port443) {
    Write-Host '  TLS 前置已在运行（443 已监听）' -ForegroundColor DarkGray
    exit 0
}

if (-not (Test-Path $script)) { Write-Host "  找不到 $script" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $py))     { Write-Host "  找不到 $py" -ForegroundColor Red; exit 1 }

Start-Process -FilePath $py -ArgumentList @($script) `
    -RedirectStandardError  (Join-Path $cp '_mitm.log') `
    -RedirectStandardOutput (Join-Path $cp '_mitm.out') `
    -WindowStyle Hidden

for ($i = 0; $i -lt 8; $i++) {
    Start-Sleep -Seconds 1
    if (Port443) {
        Write-Host '  TLS 前置已启动（443 监听中）' -ForegroundColor DarkGray
        exit 0
    }
}
Write-Host '  TLS 前置启动失败，443 没起来；看 clientpatch\_mitm.log' -ForegroundColor Red
exit 1
