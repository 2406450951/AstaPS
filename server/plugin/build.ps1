# CheatMenu build — javac straight against grasscutter.jar, no gradle/maven needed.
#   .\build.ps1
# Output: E:\dsh_code\gcsrv\plugins\CheatMenu.jar
$ErrorActionPreference = 'Stop'

$jdk    = $null
foreach ($c in @(
        $env:JAVA_HOME + '\bin\javac.exe',
        'C:\Program Files\Eclipse Adoptium\jdk-21*\bin\java.exe',
        'C:\Program Files\Java\jdk-21*\bin\java.exe',
        'C:\Program Files\Microsoft\jdk-21*\bin\java.exe')) {
    $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { $jdk = Split-Path -Parent $hit.FullName; break }   # -> ...\bin
}
if (-not $jdk) { throw 'JDK 21 not found' }
$root   = Split-Path -Parent $PSScriptRoot   # cheatmenu 的上一级就是 gcsrv
$proj   = Join-Path $root 'cheatmenu'
$src    = Join-Path $proj 'src'
$build  = Join-Path $proj 'build'
$jar    = Join-Path $root 'grasscutter.jar'
$plugin = Join-Path $root 'plugins'

Write-Host "[1/4] clean"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
New-Item -ItemType Directory -Path $build -Force | Out-Null

Write-Host "[2/4] javac"
$files = Get-ChildItem $src -Recurse -Filter *.java | ForEach-Object { $_.FullName }
& "$jdk\javac.exe" -encoding UTF-8 -nowarn -Xlint:none -cp $jar -d $build @files
if ($LASTEXITCODE -ne 0) { throw "javac failed ($LASTEXITCODE)" }

Write-Host "[3/4] package"
Copy-Item (Join-Path $src 'cheatmenu\plugin.json') (Join-Path $build 'plugin.json') -Force
if (-not (Test-Path $plugin)) { New-Item -ItemType Directory -Path $plugin -Force | Out-Null }
$out = Join-Path $plugin 'CheatMenu.jar'
if (Test-Path $out) { Remove-Item $out -Force }
& "$jdk\jar.exe" --create --file $out -C $build .

Write-Host "[4/4] verify"
& "$jdk\jar.exe" tf $out
Write-Host ("OK -> {0} ({1:N0} bytes)" -f $out, (Get-Item $out).Length)
