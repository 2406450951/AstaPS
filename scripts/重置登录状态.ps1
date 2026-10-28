# 清理客户端本地登录痕迹（否则会拿旧 token 反复自检，界面直接报"登录状态失效"且不给输入框）
#   powershell -ExecutionPolicy Bypass -File .\重置登录状态.ps1
# 国服和国际服的注册表键、缓存目录不同，这里两种都处理。

$ErrorActionPreference = 'SilentlyContinue'

# 客户端目录由 paths.ps1 解析（环境变量 GS71_GAME 或常见安装位置）
$clients = @()
if ($GameDir) {
    $dataDir = if ($GameExe -eq 'GenshinImpact.exe') { 'GenshinImpact_Data' } else { 'YuanShen_Data' }
    $regKey  = if ($GameExe -eq 'GenshinImpact.exe') { 'HKCU:\SOFTWARE\miHoYo\Genshin Impact' }
               else { 'HKCU:\SOFTWARE\miHoYo\原神' }
    $clients += @{ Name = '当前客户端'; Exe = (Join-Path $GameDir $GameExe); Data = $dataDir; RegKey = $regKey }
} else {
    Write-Host '  没找到客户端目录：设环境变量 GS71_GAME 指向它' -ForegroundColor Yellow
}
$done = @()

foreach ($c in $clients) {
    if (-not (Test-Path $c.Exe)) { continue }

    $item = Get-Item $c.RegKey -ErrorAction SilentlyContinue
    if ($item) {
        foreach ($name in @('__LastUid___h2153286551', 'unity.player_sessionid_h1351336811')) {
            if ($item.GetValueNames() -contains $name) {
                Remove-ItemProperty -Path $c.RegKey -Name $name -ErrorAction SilentlyContinue
                $done += "$($c.Name) 注册表 $name"
            }
        }
    }

    $cache = Join-Path (Split-Path $c.Exe) "$($c.Data)\SDKCaches\HoYoPass"
    if (Test-Path $cache) {
        $files = @(Get-ChildItem $cache -Force -File -ErrorAction SilentlyContinue)
        if ($files.Count -gt 0) {
            foreach ($f in $files) { Remove-Item $f.FullName -Force -ErrorAction SilentlyContinue }
            $done += "$($c.Name) SDKCaches\HoYoPass 下 $($files.Count) 个文件"
        }
    }
}

if ($done.Count -eq 0) {
    Write-Host '  本地登录痕迹本来就是干净的' -ForegroundColor DarkGray
} else {
    foreach ($d in $done) { Write-Host "  已清理: $d" -ForegroundColor DarkGray }
}
exit 0