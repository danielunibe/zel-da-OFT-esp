param(
    [string[]]$Scenarios = @(
        'NAVI_TEMPLE_REFLECTIVE_FLOOR',
        'BASELINE_KOKIRI',
        'NAVI_LAKE_WATER_FEET',
        'NAVI_LAKE_NIGHT_WATER_FEET',
        'HYRULE_FIELD_DAY',
        'HYRULE_FIELD_NIGHT'
    ),
    [string]$Executable = 'build\OCZ_RTX_14_UNREAL_VALIDATED_NIGHTLY\soh.exe',
    [string]$RuntimeDir = 'build\OCZ_RTX_14_UNREAL_VALIDATED_NIGHTLY'
)

$ErrorActionPreference = 'Stop'
$harnessScript = 'source\shipwright\scripts\scene-harness\run-scene.ps1'

$results = [ordered]@{}

foreach ($sc in $Scenarios) {
    Write-Host "============================================================"
    Write-Host "Executing scenario: $sc"
    Write-Host "============================================================"
    $startTime = Get-Date

    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $harnessScript `
            -RunScenario $sc `
            -Executable $Executable `
            -RuntimeDirectory $RuntimeDir `
            -Background `
            -VisualAblation

        $elapsed = (Get-Date) - $startTime
        Write-Host "Completed $sc in $($elapsed.TotalSeconds)s with exit code $LASTEXITCODE"
        $results[$sc] = [ordered]@{
            Status = if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }
            ExitCode = $LASTEXITCODE
            DurationSeconds = [math]::Round($elapsed.TotalSeconds, 2)
        }
    } catch {
        Write-Host "Error executing $($sc): $_"
        $results[$sc] = [ordered]@{
            Status = 'ERROR'
            ErrorMessage = $_.ToString()
        }
    }

    # Cool down briefly between scene launches
    Start-Sleep -Seconds 3
}

$summaryPath = 'reports\nightly\OCZ_NIGHTLY_14_SUITE_RUN.json'
$results | ConvertTo-Json -Depth 5 | Set-Content $summaryPath -Encoding UTF8
Write-Host "Suite run complete. Summary saved to $summaryPath"
