# Stop the login and game servers. The databases keep running unless asked,
# because starting them again is the slow part and they hold the accounts.
#
#   .\stop-server.ps1
#   .\stop-server.ps1 -Db
#
[CmdletBinding()]
param(
    [int]    $LoginPort = 2106,
    [int]    $GamePort  = 0,
    [switch] $Db
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Say  { param($m) Write-Host "[openmmo] $m" -ForegroundColor White }
function Warn { param($m) Write-Host "[openmmo] $m" -ForegroundColor Yellow }

function Test-Listening {
    param([int] $Port)
    $null -ne (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

if ($GamePort -eq 0) {
    $GamePort = 7777
    $cfg = Join-Path $env:APPDATA 'openmmo\launcher.cfg'
    if (Test-Path $cfg) {
        $line = Select-String -Path $cfg -Pattern '^\s*gameport\s+(\d+)' | Select-Object -Last 1
        if ($line) { $GamePort = [int] $line.Matches[0].Groups[1].Value }
    }
}

# Matched on the exact task and main class so a compiling gradle daemon, and
# anything else on this machine, are left alone.
$patterns = @(
    ':server.login:run',
    ':server.game:run',
    'de.fiereu.openmmo.server.login.MainKt',
    'de.fiereu.openmmo.server.game.MainKt'
)

function Get-ServerProcesses {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $cmd = $_.CommandLine
        $cmd -and ($patterns | Where-Object { $cmd.Contains($_) })
    }
}

Say 'stopping the servers'
$procs = @(Get-ServerProcesses)
if ($procs.Count -eq 0) {
    Say 'nothing was running'
} else {
    foreach ($p in $procs) {
        Stop-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
    }
}

for ($i = 0; $i -lt 15; $i++) {
    if (-not (Test-Listening $LoginPort) -and -not (Test-Listening $GamePort)) { break }
    Start-Sleep -Seconds 1
}

$still = @()
if (Test-Listening $LoginPort) { $still += $LoginPort }
if (Test-Listening $GamePort)  { $still += $GamePort }

if ($still.Count -gt 0) {
    Warn "still listening on $($still -join ', '), forcing"
    foreach ($p in @(Get-ServerProcesses)) {
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}

if ((Test-Listening $LoginPort) -or (Test-Listening $GamePort)) {
    Warn 'something is still holding a port:'
    Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalPort -in @($LoginPort, $GamePort) } |
        Select-Object LocalAddress, LocalPort, OwningProcess |
        Format-Table -AutoSize
} else {
    Say 'both servers are down'
}

if ($Db) {
    Say 'stopping the databases'
    & docker compose stop login-db game-db | Out-Null
    if ($LASTEXITCODE -ne 0) { Warn 'the containers would not stop' }
}
