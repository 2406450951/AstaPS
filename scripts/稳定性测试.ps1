# 稳定性浸泡测试：起全套 → 等进世界 → 挂着观察 N 分钟 → 报告是否闪退
#   powershell -ExecutionPolicy Bypass -File .\稳定性测试.ps1 -Minutes 5
# 输出：进世界用时、在线时长、是否崩溃（含 Astrolabe.crash.dmp 与最后一帧日志）

param([int]$Minutes = 5)

. "$PSScriptRoot\paths.ps1"
$ErrorActionPreference = 'SilentlyContinue'
$gs71   = $PSScriptRoot
$game   = $GameDir
$astLog = Join-Path $game 'Astrolabe.log'
$dmp    = Join-Path $game 'Astrolabe.crash.dmp'
$outLog = 'C:\Users\Administrator\AppData\LocalLow\miHoYo\原神\output_log.txt'

function Show($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }

Show ''
Show ('=' * 78)
Show "  GS71 稳定性浸泡测试（$Minutes 分钟）"
Show ('=' * 78)

# 0) 前置检查
if (-not (Test-Path (Join-Path $game 'YuanShen.exe'))) { Show '  找不到客户端，退出。' 'Red'; exit 1 }

# 锁屏检测：锁屏状态下游戏会丢 DXGI 设备、收不到键盘，测出来的结论没有意义
if (@(Get-Process LogonUI -ErrorAction SilentlyContinue).Count -gt 0) {
    Show ''
    Show '  ✗ 检测到当前会话处于锁屏状态（LogonUI 在运行）。' 'Red'
    Show '    锁屏会让游戏丢 DXGI 设备、收不到键盘，闪退/登录失败都会变得不可信。' 'Yellow'
    Show '    请先解锁（或按 Ctrl+Alt+Del 回到桌面），再重新运行本测试。' 'Yellow'
    Show ''
    exit 4
}

# 测试期间阻止息屏/休眠（脚本自己持锁，退出时自动释放）
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AW2 {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SetThreadExecutionState(uint flags);
}
"@ -ErrorAction SilentlyContinue
$ES_CONTINUOUS = 0x80000000; $ES_SYSTEM_REQUIRED = 0x00000001; $ES_DISPLAY_REQUIRED = 0x00000002
$awFlags = $ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED -bor $ES_DISPLAY_REQUIRED
[void][AW2]::SetThreadExecutionState($awFlags)
Show '  已阻止息屏/休眠（本次测试期间有效）' 'DarkGray'

Get-Process YuanShen | Stop-Process -Force
Start-Sleep -Seconds 2
Remove-Item $dmp -Force
Remove-Item $astLog -Force

# 1) 起全套
Show '  [1] 启动 启动.bat ...' 'Cyan'
Start-Process -FilePath (Join-Path $gs71 '启动.bat') -WorkingDirectory $gs71 -WindowStyle Minimized

# 2) 等进世界
Show '  [2] 等待进入大世界（最长 4 分钟）' 'Cyan'
Show '      >>> 客户端出现在登录页后，请立刻输入任意账号密码并勾选协议 <<<' 'Yellow'
Show '      （登录每秒都在计时；未登录时客户端约 2 分钟会自己退出，脚本会提示）' 'DarkGray'
$entered = $false
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt 48; $i++) {
    Start-Sleep -Seconds 5
    $pc = (Invoke-WebRequest 'http://127.0.0.1:8088/status/server' -UseBasicParsing -TimeoutSec 4).Content
    if ($pc -match '"playerCount":(\d+)' -and [int]$matches[1] -ge 1) { $entered = $true; break }
    if (-not (Get-Process YuanShen)) { break }
}
if (-not $entered) {
    Show '  没有进入世界（超时或客户端退出）。' 'Yellow'
    Show ('  Astrolabe.log 最后几行：')
    Get-Content $astLog -Tail 6 | ForEach-Object { Show ("    " + $_) 'DarkGray' }
    exit 2
}
Show ("  [2] 已进世界，用时 {0:N0} 秒" -f $sw.Elapsed.TotalSeconds) 'Green'

# 3) 浸泡
$minutes = $Minutes
Show "  [3] 保持在线观察 $minutes 分钟（请不要关窗口）..." 'Cyan'
$deadline = (Get-Date).AddMinutes($minutes)
$died = $false
$lastAlive = Get-Date
$lastPc = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    if (-not (Get-Process YuanShen)) { $died = $true; break }
    $pc = (Invoke-WebRequest 'http://127.0.0.1:8088/status/server' -UseBasicParsing -TimeoutSec 4).Content
    if ($pc -match '"playerCount":(\d+)') { $lastPc = [int]$matches[1] }
    $lastAlive = Get-Date
    $left = [int]($deadline - (Get-Date)).TotalMinutes
    Write-Host ("    ... 在线 {0:N1} 分钟，playerCount={1}" -f ($minutes - $left), $lastPc) -ForegroundColor DarkGray
}

# 4) 结论
Show ''
Show ('=' * 78)
if ($died) {
    Show "  ✗ 闪退：进世界后约 $([int](((Get-Date) - $sw.StartTime).TotalMinutes)) 分钟内客户端退出" 'Red'
    if (Test-Path $dmp) {
        Show "  已生成崩溃转储：$dmp" 'Yellow'
        Show '  用这条命令定位：' 'Yellow'
        Show "    cdb -z `"$dmp`" -c `"!analyze -v; kb`"" 'DarkGray'
    } else {
        Show '  没有生成 minidump（可能是被外部终止，而不是异常崩溃）' 'Yellow'
    }
    $crash = Select-String -Path $astLog -Pattern 'CRASH exception' | Select-Object -Last 1
    if ($crash) { Show ("  日志：" + $crash.Line) 'Yellow' }
    Show ('  崩溃前 Astrolabe.log 最后 8 行：')
    Get-Content $astLog -Tail 8 | ForEach-Object { Show ("    " + $_) 'DarkGray' }
    $trace = @(Select-String -Path $outLog -Pattern 'OUTPUTING STACK TRACE').Count
    Show ("  客户端 output_log 里的栈记录数：$trace")
    exit 3
} else {
    Show "  ✓ 稳定：进世界后连续在线 $minutes 分钟没有闪退，playerCount=$lastPc" 'Green'
    Show '  可以认为覆盖层的堆损坏问题已解决。' 'Green'
}
Show ''
Show '  提示：服务端仍在跑，按 INSERT 可以验菜单；不玩了双击 停止.bat。' 'DarkGray'
Show ''
exit 0
