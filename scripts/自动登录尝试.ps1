# 尝试用"扫描码注入"在登录页自动输入账号密码（之前 VK 码 / Unicode / 剪贴板都无效）
#   powershell -ExecutionPolicy Bypass -File .\自动登录尝试.ps1
# 结果写进 clientpatch\_autologin.log，并检查服务端是否收到登录请求。

param([string]$Account = 'admin', [string]$Password = 'admin')

$ErrorActionPreference = 'SilentlyContinue'
$log = (Join-Path $LocalDir 'autologin.log')
function L($m) { $m | Tee-Object -FilePath $log -Append | Out-Null; Write-Host $m }

Remove-Item $log -Force
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SI {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern int MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT {
        public uint type; public MOUSEINPUT mi; public KEYBDINPUT ki; }
}
"@
[SI]::SetProcessDPIAware() | Out-Null

function ClickP([IntPtr]$h, [int]$x, [int]$y) {
    $pt = New-Object SI+POINT; $pt.X = $x; $pt.Y = $y
    [SI]::ScreenToClient($h, [ref]$pt) | Out-Null
    $lp = [IntPtr](($pt.Y -shl 16) -bor ($pt.X -band 0xFFFF))
    [SI]::PostMessageW($h, 0x0200, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 60
    [SI]::PostMessageW($h, 0x0201, [IntPtr]1, $lp) | Out-Null
    Start-Sleep -Milliseconds 50
    [SI]::PostMessageW($h, 0x0202, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 250
}

# 扫描码注入：KEYEVENTF_SCANCODE(0x0008) | KEYEVENTF_KEYUP(0x0002)
function ScanType([string]$text) {
    $sz = [System.Runtime.InteropServices.Marshal]::SizeOf([type]'SI+INPUT')
    foreach ($ch in $text.ToCharArray()) {
        $vk = [uint16][char]::ToUpper($ch)
        $sc = [uint16][SI]::MapVirtualKeyW([uint32]$vk, 0)   # MAPVK_VK_TO_VSC
        $d = New-Object SI+INPUT; $d.type = 1; $d.ki.wScan = $sc; $d.ki.dwFlags = 0x0008
        [SI]::SendInput(1, @($d), $sz) | Out-Null
        Start-Sleep -Milliseconds 25
        $u = New-Object SI+INPUT; $u.type = 1; $u.ki.wScan = $sc; $u.ki.dwFlags = 0x000A
        [SI]::SendInput(1, @($u), $sz) | Out-Null
        Start-Sleep -Milliseconds 35
    }
}

$p = Get-Process YuanShen
if (-not $p) { L 'NO_GAME'; exit 1 }
$h = $p.MainWindowHandle
if ([SI]::IsIconic($h)) { [SI]::ShowWindow($h, 9) | Out-Null; Start-Sleep -Milliseconds 900 }
[SI]::BringWindowToTop($h) | Out-Null
[SI]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 800
L "window=$h"

# 1) 先点掉可能存在的错误弹窗
foreach ($w in @([IntPtr]0)) { }
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
    public static int CloseDialogs() {
        int n = 0;
        EnumWindows((hw, l) => {
            if (!IsWindowVisible(hw)) return true;
            RECT r; GetWindowRect(hw, out r);
            int w = r.R-r.L, hh = r.B-r.T;
            if (w < 200 || w > 600 || hh < 100 || hh > 350) return true;
            var cn = new StringBuilder(64); GetClassNameW(hw, cn, 64);
            if (cn.ToString().StartsWith("#")) { PostMessageW(hw, 0x0010, IntPtr.Zero, IntPtr.Zero); n++; }
            return true;
        }, IntPtr.Zero);
        return n;
    }
}
"@
L ("dialogs closed = " + [W]::CloseDialogs())
Start-Sleep -Milliseconds 300

# 2) 账号 / 密码 / 勾选协议 / 进入游戏
ClickP $h 963 385; ScanType $Account
Start-Sleep -Milliseconds 200
ClickP $h 963 460; ScanType $Password
Start-Sleep -Milliseconds 200
ClickP $h 613 565      # 同意协议
Start-Sleep -Milliseconds 300
ClickP $h 958 650      # 进入游戏
L 'submitted'

Start-Sleep -Seconds 6
$b=[System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height
$gr=[System.Drawing.Graphics]::FromImage($bmp); $gr.CopyFromScreen($b.Left,$b.Top,0,0,$bmp.Size); $gr.Dispose()
$bmp.Save((Join-Path $LocalDir 'autologin.png'),[System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

# 3) 服务端是否收到登录
$hit = @(Select-String -Path (Join-Path $ServerDir 'server.log') -Pattern 'ma-passport|logged in|已连接' | Select-Object -Last 3)
if ($hit.Count -gt 0) { foreach ($h2 in $hit) { L ('SRV ' + $h2.Line.Substring(0, [Math]::Min(140, $h2.Line.Length))) } }
else { L 'SRV no login seen' }
