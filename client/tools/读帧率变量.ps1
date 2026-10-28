# 只读导出候选帧率变量的当前值，用于 before/after 对比定位真正的帧率上限
#   powershell -ExecutionPolicy Bypass -File .\读帧率变量.ps1 -Out fps_before.txt

param([string]$Out = 'fps_before.txt')

$ErrorActionPreference = 'Stop'
$cand = 'D:\dsh_workspace\dsh_code\genshi\clientpatch\fps_candidates.txt'
if (-not (Test-Path $cand)) { Write-Host "缺少 $cand，先跑 fpsfind.py scan" -ForegroundColor Red; exit 1 }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RP {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool inh, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@

$p = Get-Process GenshinImpact -ErrorAction SilentlyContinue
if (-not $p) { Write-Host '游戏没在运行' -ForegroundColor Red; exit 1 }
$base = $p.MainModule.BaseAddress.ToInt64()
Write-Host ("模块基址 0x{0:X}" -f $base) -ForegroundColor DarkGray

$h = [RP]::OpenProcess(0x0410, $false, $p.Id)   # QUERY_INFORMATION | VM_READ
if ($h -eq [IntPtr]::Zero) { Write-Host ('OpenProcess 失败: ' + [ComponentModel.Win32Exception]::new().Message) -ForegroundColor Red; exit 1 }

$rvas = Get-Content $cand | Where-Object { $_ -match '^0x' }
$buf = New-Object byte[] 4
$read = [IntPtr]::Zero
$lines = New-Object System.Collections.Generic.List[string]
$n = 0

foreach ($r in $rvas) {
    $rva = [Convert]::ToInt64($r.Substring(2), 16)
    $addr = [IntPtr]($base + $rva)
    if ([RP]::ReadProcessMemory($h, $addr, $buf, [IntPtr]4, [ref]$read)) {
        $v = [BitConverter]::ToInt32($buf, 0)
        $lines.Add(("{0} {1}" -f $r, $v))
        $n++
    }
}
[RP]::CloseHandle($h) | Out-Null
$lines | Set-Content -Path $Out -Encoding ASCII
Write-Host "已导出 $n / $($rvas.Count) 个值 -> $Out" -ForegroundColor Green

$interesting = $lines | Where-Object { $_ -match ' (30|60|120|144|165)$' }
Write-Host "其中值像帧率的有 $($interesting.Count) 个：" -ForegroundColor Cyan
$interesting | Select-Object -First 20 | ForEach-Object { "  $_" }
