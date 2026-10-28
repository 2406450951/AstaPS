# 把私服补丁部署到"国际服"客户端目录
#   powershell -ExecutionPolicy Bypass -File .\部署到国际服.ps1 -GameDir "D:\Program Files\HoYoPlay\games\Genshin Impact game"
# 做的事：复制 Astrolabe.dll（菜单）+ hk4e-patch.dll（dispatch/登录补丁），并检查版本与 hosts。

param(
    [Parameter(Mandatory = $true)][string]$GameDir
)

$ErrorActionPreference = 'Stop'
$cp = Join-Path (Split-Path -Parent $PSScriptRoot) 'clientpatch'

function Ok($m)   { Write-Host "  [OK]   $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  [警告] $m" -ForegroundColor Yellow }
function Bad($m)  { Write-Host "  [失败] $m" -ForegroundColor Red }

Write-Host ''
Write-Host ('=' * 74)
Write-Host "  部署到国际服：$GameDir"
Write-Host ('=' * 74)

# 1) 目录与可执行文件
$exe = Join-Path $GameDir 'YuanShen.exe'
if (-not (Test-Path $exe)) { Bad "找不到 $exe"; exit 1 }
Ok "找到客户端 YuanShen.exe"

# 2) 版本检查（服务端是 7.1.0，客户端必须同版本）
$cfg = Join-Path $GameDir 'config.ini'
if (Test-Path $cfg) {
    $ver = (Select-String -Path $cfg -Pattern '^game_version=(.+)$' | Select-Object -First 1)
    if ($ver) {
        $v = $ver.Matches[0].Groups[1].Value.Trim()
        if ($v -like '7.1*') { Ok "客户端版本 $v" }
        else { Warn "客户端版本是 $v，服务端是 7.1.0 —— 版本不一致会连不上" }
    } else { Warn 'config.ini 里没有 game_version' }
} else { Warn '没有 config.ini（可能不是标准安装）' }

# 3) 部署两个 DLL
$pair = @(
    @{ Src = Join-Path $cp 'overlay\build\Gs71Menu.dll';                      Dst = 'Astrolabe.dll'   },
    @{ Src = Join-Path $cp 'build\hk4e-patch\target\release\ext.dll';         Dst = 'hk4e-patch.dll'  }
)
foreach ($p in $pair) {
    if (-not (Test-Path $p.Src)) { Bad "缺少构建产物 $($p.Src)"; continue }
    Copy-Item $p.Src (Join-Path $GameDir $p.Dst) -Force
    Ok ("{0}  {1:N0} 字节" -f $p.Dst, (Get-Item (Join-Path $GameDir $p.Dst)).Length)
}

# 4) hosts 检查（国际服需要这些域名指向本机）
$need = @(
    'passport-api.hoyoverse.com', 'hk4e-sdk-os.hoyoverse.com', 'sdk-os-static.hoyoverse.com',
    'api-os-takumi.hoyoverse.com', 'hk4e-api-os.hoyoverse.com', 'dispatchosglobal.yuanshen.com'
)
$hosts = Get-Content 'C:\Windows\System32\drivers\etc\hosts'
$missing = $need | Where-Object { -not ($hosts -match [regex]::Escape($_)) }
if ($missing) { Warn ("hosts 缺少：{0} —— 跑 GS71\起TLS前置.ps1 前先补上" -f ($missing -join ', ')) }
else { Ok 'hosts 里的国际服域名齐全' }

# 5) 证书检查
$ca = Get-ChildItem Cert:\LocalMachine\Root -ErrorAction SilentlyContinue | Where-Object { $_.Subject -match 'GS71' }
if ($ca) { Ok 'GS71 根证书已信任' } else { Warn '根证书未安装：certutil -addstore -f Root clientpatch\gs71_ca.pem' }

Write-Host ''
Write-Host '  完成后启动顺序：' -ForegroundColor Cyan
Write-Host '    1. GS71\起TLS前置.ps1   （443 本地前置，截获登录）' -ForegroundColor DarkGray
Write-Host '    2. 服务端 start-server.ps1' -ForegroundColor DarkGray
Write-Host '    3. 双击该客户端的 YuanShen.exe' -ForegroundColor DarkGray
Write-Host ''
