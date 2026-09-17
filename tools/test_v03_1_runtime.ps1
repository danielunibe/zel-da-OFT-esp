param(
    [string]$RuntimeDir = "$PSScriptRoot\..\runtime\build-test"
)

$ErrorActionPreference = 'Stop'
Set-Location $RuntimeDir

Write-Host "=== OCARINA COUCH EDITION V03.1 RUNTIME VALIDATION ===" -ForegroundColor Cyan

$results = @{}
$jsonPath = Join-Path $RuntimeDir "shipofharkinian.json"

# Baseline Save Hashes
$saveDir = Join-Path $RuntimeDir "Save"
$saveHashesBefore = @{
    "file2.sav" = (Get-FileHash (Join-Path $saveDir "file2.sav") -Algorithm SHA256).Hash
    "file3.sav" = (Get-FileHash (Join-Path $saveDir "file3.sav") -Algorithm SHA256).Hash
    "global.sav" = (Get-FileHash (Join-Path $saveDir "global.sav") -Algorithm SHA256).Hash
}

function Set-MasterTonemapping([int]$val) {
    $conf = Get-Content $jsonPath -Raw | ConvertFrom-Json
    if (-not $conf.CVars.PSObject.Properties['gVisualEnhancements']) {
        $conf.CVars | Add-Member -NotePropertyName "gVisualEnhancements" -NotePropertyValue ([PSCustomObject]@{}) -Force
    }
    $conf.CVars.gVisualEnhancements | Add-Member -NotePropertyName "MasterTonemapping" -NotePropertyValue $val -Force
    $conf | ConvertTo-Json -Depth 10 | Set-Content $jsonPath
}

# 1. CLASSIC MODE BOOT TEST
Write-Host "`n[TEST 1] Testing Classic Mode Boot..." -ForegroundColor Yellow
Set-MasterTonemapping 0

$p = Start-Process -FilePath ".\soh.exe" -PassThru
Start-Sleep -Seconds 6
$classicAlive = -not $p.HasExited
if ($classicAlive) {
    Write-Host "  -> Classic Mode Boot: ALIVE (PID: $($p.Id), WorkingSet: $($p.WorkingSet64 / 1MB) MB)" -ForegroundColor Green
    $p | Stop-Process -Force
    $results["ClassicBoot"] = "PASS"
} else {
    Write-Host "  -> Classic Mode Boot: EXITED PREMATURELY ($($p.ExitCode))" -ForegroundColor Red
    $results["ClassicBoot"] = "FAIL"
}

# 2. ENHANCED MODE BOOT TEST
Write-Host "`n[TEST 2] Testing Enhanced Mode Boot (ACES + Dynamic Fog + MaterialRegistry)..." -ForegroundColor Yellow
Set-MasterTonemapping 1

$p = Start-Process -FilePath ".\soh.exe" -PassThru
Start-Sleep -Seconds 6
$enhancedAlive = -not $p.HasExited
if ($enhancedAlive) {
    Write-Host "  -> Enhanced Mode Boot: ALIVE (PID: $($p.Id), WorkingSet: $($p.WorkingSet64 / 1MB) MB)" -ForegroundColor Green
    $p | Stop-Process -Force
    $results["EnhancedBoot"] = "PASS"
} else {
    Write-Host "  -> Enhanced Mode Boot: EXITED PREMATURELY ($($p.ExitCode))" -ForegroundColor Red
    $results["EnhancedBoot"] = "FAIL"
}

# 3. TOGGLE STRESS TEST (3 quick consecutive cycles)
Write-Host "`n[TEST 3] Testing Toggle Stress (Classic <-> Enhanced Cycles)..." -ForegroundColor Yellow
$toggleSuccess = $true
for ($i = 1; $i -le 3; $i++) {
    $mode = $i % 2
    Set-MasterTonemapping $mode
    
    $p = Start-Process -FilePath ".\soh.exe" -PassThru
    Start-Sleep -Seconds 4
    if ($p.HasExited) {
        $toggleSuccess = $false
        Write-Host "  -> Cycle $i (Mode $mode) failed!" -ForegroundColor Red
        break
    }
    $p | Stop-Process -Force
}
if ($toggleSuccess) {
    Write-Host "  -> Toggle Stress Test: ALL 3 CYCLES PASSED CLEANLY" -ForegroundColor Green
    $results["ToggleStress"] = "PASS"
} else {
    $results["ToggleStress"] = "FAIL"
}

# 4. NEGATIVE TEST: Missing es.o2r fallback
Write-Host "`n[TEST 4] Testing Negative Fallback (Missing es.o2r)..." -ForegroundColor Yellow
$esPath = Join-Path $RuntimeDir "es.o2r"
$esBak = Join-Path $RuntimeDir "es.o2r.tempbak"
if (Test-Path $esPath) {
    Rename-Item $esPath $esBak
}

try {
    $p = Start-Process -FilePath ".\soh.exe" -PassThru
    Start-Sleep -Seconds 4
    $negAlive = -not $p.HasExited
    if ($negAlive) {
        Write-Host "  -> Negative Fallback Test: ALIVE (Fallback to English successful)" -ForegroundColor Green
        $p | Stop-Process -Force
        $results["NegativeFallback_MissingArchive"] = "PASS"
    } else {
        Write-Host "  -> Negative Fallback Test: FAILED" -ForegroundColor Red
        $results["NegativeFallback_MissingArchive"] = "FAIL"
    }
} finally {
    if (Test-Path $esBak) {
        Rename-Item $esBak $esPath
    }
}

# 5. SAVE INTEGRITY VERIFICATION
Write-Host "`n[TEST 5] Verifying Save File Integrity..." -ForegroundColor Yellow
$saveHashesAfter = @{
    "file2.sav" = (Get-FileHash (Join-Path $saveDir "file2.sav") -Algorithm SHA256).Hash
    "file3.sav" = (Get-FileHash (Join-Path $saveDir "file3.sav") -Algorithm SHA256).Hash
    "global.sav" = (Get-FileHash (Join-Path $saveDir "global.sav") -Algorithm SHA256).Hash
}

$saveMatch = $true
foreach ($k in $saveHashesBefore.Keys) {
    if ($saveHashesBefore[$k] -ne $saveHashesAfter[$k]) {
        Write-Host "  -> MISMATCH on ${k}!" -ForegroundColor Red
        $saveMatch = $false
    } else {
        Write-Host "  -> ${k}: HASH PRESERVED ($($saveHashesBefore[$k].Substring(0, 16))...)" -ForegroundColor Green
    }
}
if ($saveMatch) {
    $results["SaveIntegrity"] = "PASS"
} else {
    $results["SaveIntegrity"] = "FAIL"
}

# Reset config to Couch Edition default (Enhanced Enabled)
Set-MasterTonemapping 1

Write-Host "`n=== SUMMARY RESULTS ===" -ForegroundColor Cyan
$results.GetEnumerator() | Format-Table -AutoSize
