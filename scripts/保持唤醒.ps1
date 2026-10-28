# 保持系统唤醒（禁止息屏/休眠），避免锁屏导致游戏丢设备、失去输入或退出
#   powershell -ExecutionPolicy Bypass -File .\保持唤醒.ps1            # 一直保持，直到本脚本退出
#   powershell -ExecutionPolicy Bypass -File .\保持唤醒.ps1 -Minutes 30 # 只保持 30 分钟
# 原理：SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)

param([int]$Minutes = 0)

$ErrorActionPreference = 'SilentlyContinue'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AW {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SetThreadExecutionState(uint flags);
}
"@

$ES_CONTINUOUS       = 0x80000000
$ES_SYSTEM_REQUIRED  = 0x00000001
$ES_DISPLAY_REQUIRED = 0x00000002

$flags = $ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED -bor $ES_DISPLAY_REQUIRED
$prev = [AW]::SetThreadExecutionState($flags)
Write-Host "  已请求保持唤醒（息屏/休眠将被阻止）。按 Ctrl+C 结束。" -ForegroundColor Green
Write-Host "  另外建议：Ctrl+Alt+Del 里确认没有停留在锁屏界面（锁屏会让游戏丢设备）。" -ForegroundColor DarkGray

if ($Minutes -gt 0) {
    Write-Host ("  将在 {0} 分钟后自动结束。" -f $Minutes) -ForegroundColor DarkGray
    $end = (Get-Date).AddMinutes($Minutes)
    while ((Get-Date) -lt $end) {
        # 定时刷新，防止被系统重置
        [void][AW]::SetThreadExecutionState($flags)
        Start-Sleep -Seconds 30
    }
} else {
    while ($true) {
        [void][AW]::SetThreadExecutionState($flags)
        Start-Sleep -Seconds 30
    }
}

# 恢复默认
[void][AW]::SetThreadExecutionState($ES_CONTINUOUS)
Write-Host '  已恢复默认电源行为。' -ForegroundColor DarkGray
