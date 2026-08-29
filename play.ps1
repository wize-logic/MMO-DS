# Open the game on Windows.
#
#   .\play.ps1                        # straight into the game
#   .\play.ps1 -Menu                  # open the front door instead
#   .\play.ps1 -Plan                  # print what Play would run, and stop
#
# The client is built by mmo/Makefile, which has no Windows target: it produces
# 32-bit Linux binaries and there is no .exe to run here yet. Until there is,
# this hands the job to the working copy under WSL, which is the same machine
# and the same tree. The servers do run natively on Windows, see
# start-server.ps1, so the only part that needs WSL is the game itself.
[CmdletBinding()]
param(
    [switch] $Menu,
    [switch] $Plan
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Say  { param($m) Write-Host "[openmmo] $m" -ForegroundColor White }
function Warn { param($m) Write-Host "[openmmo] $m" -ForegroundColor Yellow }
function Die  { param($m) Write-Host "[openmmo] $m" -ForegroundColor Red; exit 1 }

$launcherExe = 'mmo\build\openmmo-launch.exe'

$flag = ''
if ($Menu) { $flag = '--menu' }
if ($Plan) { $flag = '--plan' }

if (Test-Path $launcherExe) {
    # A Windows build exists after all; use it rather than crossing over.
    # Not $args: that is an automatic variable in PowerShell.
    Say 'starting the front door'
    $launchArgs = @('--play')
    if ($Menu) { $launchArgs = @() } elseif ($Plan) { $launchArgs = @('--print-plan') }
    & ".\$launcherExe" @launchArgs
    exit $LASTEXITCODE
}

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
    Die @"
There is no Windows build of the game ($launcherExe is missing) and WSL is
not available to run the Linux one. Build the client under Linux and play it
there with ./play.sh.
"@
}

Say 'no Windows build of the game; running the Linux one through WSL'
if ($flag) {
    & wsl -e ./play.sh $flag
} else {
    & wsl -e ./play.sh
}
exit $LASTEXITCODE
