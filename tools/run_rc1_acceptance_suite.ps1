param(
    [string[]]$Scenarios = @(
        'TEMPLE_INTERIOR',
        'NAVI_TEMPLE_REFLECTIVE_FLOOR',
        'BASELINE_KOKIRI',
        'NAVI_LAKE_WATER_FEET',
        'NAVI_LAKE_NIGHT_WATER_FEET',
        'HYRULE_FIELD_DAY',
        'HYRULE_FIELD_NIGHT',
        'NAVI_KOKIRI_TWO_FAIRIES',
        'NAVI_KOKIRI_DISABLED'
    ),
    [string]$Executable = 'build\OCZ_RTX_15_GLOBAL_LIGHTING_RC1\soh.exe',
    [string]$RuntimeDir = 'build\OCZ_RTX_15_GLOBAL_LIGHTING_RC1',
    [int]$PollSeconds = 180
)

$ErrorActionPreference = 'Stop'
$harnessScript = 'source\shipwright\scripts\scene-harness\run-scene.ps1'
$acceptanceEvidenceDir = 'reports\OCZ_RTX_15_ACCEPTANCE'
if (-not (Test-Path $acceptanceEvidenceDir)) {
    New-Item -ItemType Directory -Path $acceptanceEvidenceDir -Force | Out-Null
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "OCZ-RTX-15 OFFICIAL ACCEPTANCE SUITE EXECUTION" -ForegroundColor Cyan
Write-Host "Executable: $Executable" -ForegroundColor Cyan
Write-Host "PollSeconds: $PollSeconds" -ForegroundColor Cyan
Write-Host "Evidence Dir: $acceptanceEvidenceDir" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

$results = [ordered]@{}

foreach ($sc in $Scenarios) {
    Write-Host ""
    Write-Host ">>> Executing Scenario: $sc" -ForegroundColor Yellow
    $startTime = Get-Date

    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $harnessScript `
            -RunScenario $sc `
            -Executable $Executable `
            -RuntimeDirectory $RuntimeDir `
            -Background `
            -VisualAblation `
            -PollSeconds $PollSeconds

        $exitCode = $LASTEXITCODE
        $elapsed = (Get-Date) - $startTime
        $status = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }
        Write-Host "Completed $sc in $([math]::Round($elapsed.TotalSeconds, 2))s with ExitCode $exitCode [$status]" -ForegroundColor Green

        # Locate latest run folder for this scenario
        $latestRun = Get-ChildItem -Path 'reports\scene_harness' -Directory |
            Where-Object { $_.Name -like "*_$sc`_NORMAL*" } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

        $metrics = [ordered]@{
            Status = $status
            ExitCode = $exitCode
            DurationSeconds = [math]::Round($elapsed.TotalSeconds, 2)
            Timestamp = (Get-Date).ToString('o')
            RunId = if ($latestRun) { $latestRun.Name } else { $null }
            RunPath = if ($latestRun) { $latestRun.FullName } else { $null }
        }

        if ($latestRun) {
            $verPath = Join-Path $latestRun.FullName 'verification.status.json'
            $diffPath = Join-Path $latestRun.FullName 'visual_difference.json'
            $capturesDir = Join-Path $latestRun.FullName 'captures'

            if (Test-Path $verPath) {
                $verData = Get-Content $verPath -Raw | ConvertFrom-Json
                $metrics['HarnessCaptureStatus'] = $verData.captureStatus
                $metrics['DxrInitialization'] = $verData.dxrInitialization
                $metrics['TLAS'] = $verData.tlas
                $metrics['DispatchRays'] = $verData.dispatchRays
                $metrics['ProductiveGameLight'] = $verData.productiveGameLight
                $metrics['DxrExecutionStatus'] = $verData.dxrExecutionStatus
                $metrics['DxrCompositionStatus'] = $verData.dxrCompositionStatus
                $metrics['VerifiedStages'] = $verData.verifiedCaptureStages
            }

            if (Test-Path $diffPath) {
                $diffData = Get-Content $diffPath -Raw | ConvertFrom-Json
                if ($diffData.comparisons.same_frame_dxr_off_vs_on) {
                    $cmp = $diffData.comparisons.same_frame_dxr_off_vs_on
                    $metrics['DxrOffVsOn_MAE'] = $cmp.mae
                    $metrics['DxrOffVsOn_DiffPixels'] = $cmp.differingPixels
                    $metrics['DxrOffVsOn_DiffPercent'] = $cmp.differingPixelsPercent
                }
            }

            # Collate visual evidence into reports/OCZ_RTX_15_ACCEPTANCE/<scenario>
            if (Test-Path $capturesDir) {
                $targetScenarioDir = Join-Path $acceptanceEvidenceDir $sc
                if (-not (Test-Path $targetScenarioDir)) {
                    New-Item -ItemType Directory -Path $targetScenarioDir -Force | Out-Null
                }
                Get-ChildItem -Path $capturesDir -Filter '*.png' | ForEach-Object {
                    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $targetScenarioDir $_.Name) -Force
                }
                Get-ChildItem -Path $capturesDir -Filter '*.html' | ForEach-Object {
                    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $targetScenarioDir $_.Name) -Force
                }
                $metrics['EvidenceFolder'] = $targetScenarioDir
            }
        }
        
        $results[$sc] = $metrics
    } catch {
        Write-Host "Error executing $($sc): $_" -ForegroundColor Red
        $results[$sc] = [ordered]@{
            Status = 'ERROR'
            ErrorMessage = $_.ToString()
            Timestamp = (Get-Date).ToString('o')
        }
    }

    Start-Sleep -Seconds 2
}

$summaryPath = 'reports\nightly\OCZ_RTX_15_SUITE_RUN.json'
$results | ConvertTo-Json -Depth 6 | Set-Content $summaryPath -Encoding UTF8
Write-Host ""
Write-Host "Acceptance suite complete. Results saved to $summaryPath" -ForegroundColor Cyan
