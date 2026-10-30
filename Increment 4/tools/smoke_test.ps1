# REII Increment 4 smoke test wrapper (Windows PowerShell)
#   .\smoke_test.ps1                 ICD profile (Normal Crane Control Mode), 200 PINGs, PA LOW
#   .\smoke_test.ps1 -Profile both   STATIC bring-up profile first, then ICD
#   .\smoke_test.ps1 -Sweep          repeat at PA MIN/LOW/HIGH/MAX
#   .\smoke_test.ps1 -DiagOnly       only "which radio is deaf / mute?"
# Close every serial monitor on COM10 / COM12 first.
param(
    [string]$GsPort = "COM10",
    [string]$RigPort = "COM12",
    [ValidateSet("both", "icd", "static")][string]$Profile = "icd",
    [ValidateSet("MIN", "LOW", "HIGH", "MAX")][string]$Pa = "LOW",
    [int]$N = 200,
    [switch]$Sweep,
    [switch]$DiagOnly,
    [switch]$Echo
)
$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot
python -m pip install -q -r requirements.txt
$argsList = @("smoke_test.py", "--gs-port", $GsPort, "--rig-port", $RigPort, "--profile", $Profile, "--pa", $Pa, "--n", $N)
if ($Sweep) { $argsList += "--sweep" }
if ($DiagOnly) { $argsList += "--diag-only" }
if ($Echo) { $argsList += "--echo" }
python @argsList
exit $LASTEXITCODE
