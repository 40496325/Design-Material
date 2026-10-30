#!/usr/bin/env pwsh
# ============================================================================
# capture_evidence.ps1 — PowerShell wrapper for REII I3 bench evidence capture
#
# Runs capture_evidence.py against both serial ports with sensible defaults.
# Edit the -gs-port and -rig-port values to match your COM ports.
#
#   .\capture_evidence.ps1 -gs-port COM10 -rig-port COM12 -smoke-n 200
# ============================================================================

param(
    [string]$gsPort = "COM10",
    [string]$rigPort = "COM12",
    [int]$baud = 115200,
    [int]$smokeN = 200,
    [double]$smokeTimeout = 3.0,
    [switch]$NoEcho,
    [switch]$ListPorts
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pyScript = Join-Path $scriptDir "capture_evidence.py"

# Check Python and pyserial
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Error "python not found on PATH. Install Python from https://python.org"
    exit 1
}

& $python -c "import serial" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing pyserial…"
    & $python -m pip install pyserial
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to install pyserial"
        exit 1
    }
}

# Build argument list
$args = @("--gs-port", $gsPort, "--rig-port", $rigPort, "--baud", $baud, "--smoke-n", $smokeN, "--smoke-timeout", $smokeTimeout)
if ($NoEcho) { $args += @("--no-echo") }
if ($ListPorts) { $args += @("--list-ports") }

Write-Host "=== REII I3 Bench Evidence Capture ===" -ForegroundColor Cyan
Write-Host "Ground station port: $gsPort"
Write-Host "Rig port: $rigPort"
Write-Host "Smoke N: $smokeN"
Write-Host ""

& $python $pyScript @args
exit $LASTEXITCODE