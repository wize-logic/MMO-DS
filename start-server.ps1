# Start the databases, the login server and the game server for local play.
# Safe to run twice: anything already listening is left alone.
#
#   .\start-server.ps1
#   .\start-server.ps1 -GamePort 7777
#
[CmdletBinding()]
param(
    [int] $LoginPort = 2106,
    [int] $GamePort  = 0,
    [int] $WaitSecs  = 120
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Say  { param($m) Write-Host "[openmmo] $m" -ForegroundColor White }
function Warn { param($m) Write-Host "[openmmo] $m" -ForegroundColor Yellow }
function Die  { param($m) Write-Host "[openmmo] $m" -ForegroundColor Red; exit 1 }

function Test-Listening {
    param([int] $Port)
    $null -ne (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

# Default the game port to whatever the launcher is set to dial, so that
# starting a server and playing agree without being told twice.
if ($GamePort -eq 0) {
    $GamePort = 7777
    $cfg = Join-Path $env:APPDATA 'openmmo\launcher.cfg'
    if (Test-Path $cfg) {
        $line = Select-String -Path $cfg -Pattern '^\s*gameport\s+(\d+)' | Select-Object -Last 1
        if ($line) { $GamePort = [int] $line.Matches[0].Groups[1].Value }
    }
}

$logDir   = if ($env:OPENMMO_LOG_DIR) { $env:OPENMMO_LOG_DIR } else { $env:TEMP }
$loginLog = Join-Path $logDir 'openmmo-login-server.log'
$gameLog  = Join-Path $logDir 'openmmo-game-server.log'

if (-not (Test-Path '.env')) { Die ".env not found. It holds the local database and key settings." }
if (-not $env:JAVA_HOME)     { Die "JAVA_HOME is not set. Point it at a JDK 25." }
if (-not (Test-Path (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
    Die "No java.exe under JAVA_HOME ($env:JAVA_HOME)."
}
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Die "docker not found; the databases run in containers (Docker Desktop)."
}

Say 'databases'
& docker compose up -d login-db game-db | Out-Null
if ($LASTEXITCODE -ne 0) { Die 'the database containers would not start' }

if (Test-Listening $LoginPort) {
    Say "login server already listening on $LoginPort"
} else {
    Say "starting login server on $LoginPort (log: $loginLog)"
    Start-Process -FilePath '.\gradlew.bat' -ArgumentList ':server.login:run' `
        -RedirectStandardOutput $loginLog -RedirectStandardError "$loginLog.err" `
        -WindowStyle Hidden
}

# The game server runs from its installed distribution rather than the gradle
# run task, because that is the only one of the two that takes a port: the port
# lives in application.conf with no environment override, and the run task
# forwards a fixed list of variables that does not include it. The generated
# start script passes JAVA_OPTS through to the server itself, where
# -Dserver.port wins over the packaged default.
if (Test-Listening $GamePort) {
    Say "game server already listening on $GamePort"
} else {
    Say 'building the game server distribution'
    & .\gradlew.bat ':server.game:installDist' *> $gameLog
    if ($LASTEXITCODE -ne 0) { Get-Content $gameLog -Tail 20; Die 'the game server would not build' }

    # The database settings and the session secret come from .env; the signing
    # key is left blank there, so point at the one in the tree.
    Get-Content '.env' | ForEach-Object {
        if ($_ -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$') {
            $name  = $Matches[1]
            $value = $Matches[2].Trim().Trim('"')
            if ($value -ne '') { Set-Item -Path "Env:$name" -Value $value }
        }
    }
    if (-not $env:OPENMMO_GAME_PRIVATE_KEY_FILE) {
        $env:OPENMMO_GAME_PRIVATE_KEY_FILE =
            Join-Path $PSScriptRoot 'server.game\src\main\resources\game.private.pem'
    }
    $env:JAVA_OPTS = "-Dserver.port=$GamePort"

    Say "starting game server on $GamePort (log: $gameLog)"
    Start-Process -FilePath '.\server.game\build\install\server.game\bin\server.game.bat' `
        -RedirectStandardOutput "$gameLog.out" -RedirectStandardError "$gameLog.err" `
        -WindowStyle Hidden
}

Say 'waiting for both to accept connections'
$deadline = (Get-Date).AddSeconds($WaitSecs)
while ((Get-Date) -lt $deadline) {
    if ((Test-Listening $LoginPort) -and (Test-Listening $GamePort)) {
        Say "login on $LoginPort, game on $GamePort - ready"
        Say 'play with .\play.ps1, stop with .\stop-server.ps1'
        exit 0
    }
    Start-Sleep -Seconds 2
}

Warn "still not up after ${WaitSecs}s. Last lines of each log:"
foreach ($l in @($loginLog, "$gameLog.out", "$gameLog.err")) {
    Write-Host ""
    Write-Host "--- $l"
    if (Test-Path $l) { Get-Content $l -Tail 15 } else { Write-Host '(no log)' }
}
exit 1
