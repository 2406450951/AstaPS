# 7.1 私服起停脚本 — 幂等，可反复调用，路径与盘符无关
#   .\start-server.ps1           启动（已在跑就只报告）
#   .\start-server.ps1 -Status   只看状态
#   .\start-server.ps1 -Stop     停服（连 Mongo 一起停）
#   .\start-server.ps1 -Restart  重启（改完插件 jar 后用这个）
param(
    [switch]$Stop,
    [switch]$Status,
    [switch]$Restart,
    [switch]$KeepMongo,
    [int]$HeapMb = 4096,
    [int]$BootWaitSec = 60
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- 路径
# 全部相对本脚本 -> gcsrv 目录搬到哪都能跑
$root = $PSScriptRoot

# 运行时目录（JDK / Mongo / Python 都在这儿）。可用环境变量覆盖。
$RuntimeRoot = if ($env:GS71_RUNTIME) { $env:GS71_RUNTIME } else { '' }

$jar = Join-Path $root 'grasscutter.jar'
$log = Join-Path $root 'server.log'
$err = Join-Path $root 'server.err'
$var = Join-Path $root '.run'
$mongoBin = Join-Path $RuntimeRoot 'mongodb\bin\mongod.exe'
$mongoDb = Join-Path $var 'mongo'
$mongoLog = Join-Path $var 'mongod.log'

function Find-Java {
    $cands = @(
        (Join-Path $RuntimeRoot 'jdk21\bin\java.exe'),
        (Join-Path $RuntimeRoot 'jdk-21*\bin\java.exe')
    )
    foreach ($c in $cands) {
        $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    foreach ($c in @('C:\Program Files\Eclipse Adoptium\jdk-21*\bin\java.exe',
                     'C:\Program Files\Java\jdk-21*\bin\java.exe',
                     'C:\Program Files\Microsoft\jdk-21*\bin\java.exe')) {
        $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    $cmd = Get-Command java.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$jdk = Find-Java
if (-not $jdk) { throw "java.exe not found - 先把 JDK 21 解压到 $RuntimeRoot\jdk21" }

New-Item -ItemType Directory -Force -Path $var | Out-Null

# ---------------------------------------------------------------- 进程探测
function Get-ServerProcess {
    Get-Process java -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $jdk }
}

function Get-MongoProcess {
    if (-not (Test-Path $mongoBin)) { return $null }
    Get-Process mongod -ErrorAction SilentlyContinue
}

function Test-Port([int]$port, [string]$proto = 'TCP') {
    if ($proto -eq 'UDP') {
        return [bool](Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue)
    }
    return [bool](Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)
}

function Wait-Port([int]$port, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        if (Test-Port $port) { return $true }
        Start-Sleep -Milliseconds 400
    } while ((Get-Date) -lt $deadline)
    return (Test-Port $port)
}

# ---------------------------------------------------------------- Mongo
function Start-Mongo {
    if (Test-Port 27017) { Write-Host "mongo: already listening on 27017"; return $true }
    if (-not (Test-Path $mongoBin)) {
        Write-Host "mongo: mongod.exe 不存在 ($mongoBin) —— 但 27017 也没在听" -ForegroundColor Red
        return $false
    }
    New-Item -ItemType Directory -Force -Path $mongoDb | Out-Null
    Write-Host "mongo: starting $mongoBin"
    Start-Process -FilePath $mongoBin `
        -ArgumentList @('--dbpath', "`"$mongoDb`"", '--port', '27017', '--bind_ip', '127.0.0.1',
                        '--logpath', "`"$mongoLog`"", '--logappend') `
        -WindowStyle Hidden | Out-Null
    return (Wait-Port 27017 30)
}

function Stop-Mongo {
    $p = Get-MongoProcess
    if ($p) {
        Write-Host "mongo: stopping pid $($p.Id -join ',')"
        $p | Stop-Process -Force
        Start-Sleep -Seconds 2
    } else {
        Write-Host "mongo: not running"
    }
}

# ---------------------------------------------------------------- 状态
function Show-State {
    $proc = Get-ServerProcess
    if (-not $proc) {
        Write-Host "server: STOPPED"
        Write-Host ("mongo : {0}" -f $(if (Test-Port 27017) { 'UP' } else { 'DOWN' }))
        return $false
    }
    $tcp = Test-Port 8088 'TCP'
    $udp = Test-Port 22101 'UDP'
    $stat = try { (Invoke-WebRequest 'http://127.0.0.1:8088/status/server' -TimeoutSec 3 -UseBasicParsing).Content } catch { $null }
    Write-Host ("server: RUNNING  pid={0}  rss={1}MB  started={2:HH:mm:ss}" -f `
        $proc.Id, [int]($proc.WorkingSet64 / 1MB), $proc.StartTime)
    Write-Host ("mongo : {0}" -f $(if (Test-Port 27017) { 'UP' } else { 'DOWN' }))
    Write-Host ("ports : 8088/TCP {0}   22101/UDP {1}" -f `
        $(if ($tcp) { 'LISTEN' } else { 'down' }), $(if ($udp) { 'LISTEN' } else { 'down' }))
    Write-Host ("/status/server: {0}" -f $(if ($stat) { $stat } else { 'unreachable' }))
    return [bool]($tcp -and $udp)
}

# ---------------------------------------------------------------- 起停
function Stop-Server {
    $proc = Get-ServerProcess
    if ($proc) {
        Write-Host "stopping server pid $($proc.Id -join ',') ..."
        $proc | Stop-Process -Force
        Start-Sleep -Seconds 3
    } else {
        Write-Host "server: already stopped"
    }
    if (-not $KeepMongo) { Stop-Mongo }
}

function Start-Server {
    if (Get-ServerProcess) {
        Write-Host "server: already running — 用 -Restart 才会重新加载插件"
        return
    }
    if (-not (Test-Path $jar)) { throw "jar missing: $jar" }
    if (-not (Start-Mongo)) { throw 'MongoDB 起不来，服务端连不上库' }
    if (Test-Path $log) { Move-Item $log "$log.prev" -Force }
    if (Test-Path $err) { Move-Item $err "$err.prev" -Force }

    Write-Host "starting: $jdk -Xms1G -Xmx${HeapMb}M -jar grasscutter.jar"
    Start-Process -FilePath $jdk `
        -ArgumentList "-Xms1G", "-Xmx${HeapMb}M", "-jar", "grasscutter.jar" `
        -WorkingDirectory $root `
        -RedirectStandardOutput $log -RedirectStandardError $err `
        -WindowStyle Hidden | Out-Null

    Write-Host "waiting up to ${BootWaitSec}s for dispatch ..."
    $deadline = (Get-Date).AddSeconds($BootWaitSec)
    $ok = $false
    do {
        Start-Sleep -Milliseconds 800
        try { $ok = (Invoke-WebRequest 'http://127.0.0.1:8088/status' -TimeoutSec 2 -UseBasicParsing).StatusCode -eq 200 }
        catch { $ok = $false }
    } while (-not $ok -and (Get-Date) -lt $deadline)

    if ($ok) {
        Write-Host "dispatch is up."
        Write-Host "--- plugin lines ---"
        Select-String -Path $log -Pattern 'PluginManager|CheatMenu|CheatBridge' -Encoding utf8 -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Line }
    } else {
        Write-Host "dispatch did not answer in ${BootWaitSec}s — tail of $err :" -ForegroundColor Yellow
        if (Test-Path $err) { Get-Content $err -Tail 20 }
    }
}

if ($Status) { Show-State | Out-Null; return }
if ($Stop) { Stop-Server; return }
if ($Restart) { Stop-Server; Start-Server; Show-State | Out-Null; return }

Start-Server
Show-State | Out-Null
