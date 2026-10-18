# Ejecutor de los 8 Escenarios Obligatorios de Producción OCZ-RTX-12
param(
    [string[]]$Scenarios = @(
        "TEMPLE_INTERIOR",
        "TEMPLE_INTERIOR_NIGHT",
        "BASELINE_KOKIRI",
        "NAVI_TEMPLE_REFLECTIVE_FLOOR",
        "NAVI_LAKE_WATER_FEET",
        "NAVI_LAKE_NIGHT_WATER_FEET",
        "HYRULE_FIELD_DAY",
        "HYRULE_FIELD_NIGHT"
    )
)

$ErrorActionPreference = "Continue"
$baseDir = "c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
$harnessScript = Join-Path $baseDir "source\shipwright\scripts\scene-harness\run-scene.ps1"
$exePath = Join-Path $baseDir "build\OCZ_RTX_12_VISUAL_QUALITY\soh.exe"
$runtimeDir = Join-Path $baseDir "build\OCZ_RTX_12_VISUAL_QUALITY"

Write-Host "=========================================================="
Write-Host "INICIANDO SUITE COMPLETA OCZ-RTX-12 (8 ESCENARIOS DE PRODUCCIÓN)"
Write-Host "Modo: Producción PBR Fresnel (VisualAblation activo para DXR OFF/ON sincronizados)"
Write-Host "=========================================================="

$summaryList = [System.Collections.Generic.List[PSCustomObject]]::new()

foreach ($scen in $Scenarios) {
    Write-Host "`n>>> [EJECUTANDO ESCENARIO: $scen] <<<"
    Get-Process soh -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    $start = Get-Date
    try {
        & $harnessScript `
            -RunScenario $scen `
            -TruthMode "NORMAL" `
            -Executable $exePath `
            -RuntimeDirectory $runtimeDir `
            -VisualProfile 2 `
            -RendererBackend "D3D12" `
            -VisualAblation `
            -Background `
            -PollSeconds 150

        $exitCode = $LASTEXITCODE
        $elapsed = [int]((Get-Date) - $start).TotalSeconds

        # Localizar el último run generado
        $harnessReportsDir = Join-Path $baseDir "reports\scene_harness"
        $latestRun = Get-ChildItem $harnessReportsDir -Directory | 
            Where-Object { $_.Name -like "*_$scen`_NORMAL" } | 
            Sort-Object LastWriteTime -Descending | 
            Select-Object -First 1

        $runPath = if ($latestRun) { $latestRun.FullName } else { "" }
        $capComplete = if ($runPath) { Test-Path (Join-Path $runPath "CAPTURE_COMPLETE") } else { $false }
        $offRaw = if ($runPath) { Test-Path (Join-Path $runPath "captures\comparison_dxr_off_frame425_raw.png") } else { $false }
        $onRaw = if ($runPath) { Test-Path (Join-Path $runPath "captures\comparison_dxr_on_frame425_raw.png") } else { $false }

        Write-Host "[RESULTADO $scen] ExitCode=$exitCode CapComplete=$capComplete OffOnSync=$($offRaw -and $onRaw) Tiempo=${elapsed}s"

        $summaryList.Add([PSCustomObject]@{
            Scenario = $scen
            ExitCode = $exitCode
            CaptureComplete = $capComplete
            OffOnSync = ($offRaw -and $onRaw)
            DurationSec = $elapsed
            RunPath = $runPath
        })
    }
    catch {
        Write-Host "[ERROR $scen] $($_.Exception.Message)"
        $summaryList.Add([PSCustomObject]@{
            Scenario = $scen
            ExitCode = -1
            CaptureComplete = $false
            OffOnSync = $false
            DurationSec = [int]((Get-Date) - $start).TotalSeconds
            Error = $_.Exception.Message
            RunPath = ""
        })
    }

    Get-Process soh -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host "`n=========================================================="
Write-Host "RESUMEN FINAL DE LA SUITE DE PRODUCCIÓN OCZ-RTX-12"
Write-Host "=========================================================="
$summaryList | Format-Table Scenario, ExitCode, CaptureComplete, OffOnSync, DurationSec -AutoSize

$outJson = Join-Path $baseDir "reports\scene_harness\OCZ_RTX_12_FINAL_SUITE_RESULTS.json"
$summaryList | ConvertTo-Json -Depth 5 | Set-Content $outJson -Encoding UTF8
Write-Host "Resultados consolidados guardados en: $outJson"
