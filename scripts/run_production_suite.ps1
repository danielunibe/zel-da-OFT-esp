# Suite de Ejecución de Escenarios Obligatorios para OCZ-RTX-12
param(
    [string[]]$ScenariosToRun = @(
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

$baseDir = "c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
$harnessScript = Join-Path $baseDir "source\shipwright\scripts\scene-harness\run-scene.ps1"
$verifyScript = Join-Path $baseDir "source\shipwright\scripts\scene-harness\verify-run.ps1"
$exePath = Join-Path $baseDir "build\OCZ_RTX_12_VISUAL_QUALITY\soh.exe"
$runtimeDir = Join-Path $baseDir "build\OCZ_RTX_12_VISUAL_QUALITY"

Write-Host "=========================================================="
Write-Host "OCZ-RTX-12: EJECUCIÓN DE SUITE DE PRODUCCIÓN VISUAL"
Write-Host "Ejecutable: $exePath"
Write-Host "Runtime: $runtimeDir"
Write-Host "Escenarios: $($ScenariosToRun -join ', ')"
Write-Host "=========================================================="

# Limpiar procesos previos de soh si los hubiera
Get-Process soh -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$results = [System.Collections.Generic.List[PSCustomObject]]::new()

foreach ($scenarioId in $ScenariosToRun) {
    Write-Host "`n>>> [RUNNING] Escenario: $scenarioId <<<"
    $start = Get-Date

    try {
        & $harnessScript `
            -RunScenario $scenarioId `
            -TruthMode "NORMAL" `
            -Executable $exePath `
            -RuntimeDirectory $runtimeDir `
            -VisualProfile 2 `
            -RendererBackend "D3D12" `
            -Background `
            -PollSeconds 90

        $exitCode = $LASTEXITCODE
        $elapsedSec = [int]((Get-Date) - $start).TotalSeconds
        Write-Host "[FINISHED] $scenarioId con ExitCode: $exitCode (Tiempo: ${elapsedSec}s)"

        # Buscar el reporte generado más reciente para este escenario
        $harnessReportsDir = Join-Path $baseDir "reports\scene_harness"
        $latestRun = Get-ChildItem $harnessReportsDir -Directory | 
            Where-Object { $_.Name -like "*_$scenarioId`_NORMAL" } | 
            Sort-Object LastWriteTime -Descending | 
            Select-Object -First 1

        $runRoot = if ($latestRun) { $latestRun.FullName } else { $null }
        $runPass = if ($runRoot) { Test-Path (Join-Path $runRoot "RUN_PASS") } else { $false }
        $captureComplete = if ($runRoot) { Test-Path (Join-Path $runRoot "CAPTURE_COMPLETE") } else { $false }

        # Verificar si hay capturas generadas
        $captures = @()
        if ($runRoot) {
            $captures = Get-ChildItem $runRoot -Filter "*.png" | Select-Object -ExpandProperty Name
        }

        $results.Add([PSCustomObject]@{
            ScenarioId = $scenarioId
            ExitCode = $exitCode
            RunPass = $runPass
            CaptureComplete = $captureComplete
            RunRoot = $runRoot
            Captures = ($captures -join ", ")
            DurationSeconds = $elapsedSec
        })
    }
    catch {
        Write-Host "[ERROR] Excepción ejecutando $scenarioId : $($_.Exception.Message)"
        $results.Add([PSCustomObject]@{
            ScenarioId = $scenarioId
            ExitCode = -1
            RunPass = $false
            CaptureComplete = $false
            RunRoot = $null
            Captures = ""
            DurationSeconds = [int]((Get-Date) - $start).TotalSeconds
            Error = $_.Exception.Message
        })
    }

    # Asegurar que el proceso se cerró antes del siguiente
    Get-Process soh -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host "`n=========================================================="
Write-Host "RESUMEN DE EJECUCIÓN DE SUITE OCZ-RTX-12"
Write-Host "=========================================================="
$results | Format-Table ScenarioId, ExitCode, RunPass, CaptureComplete, DurationSeconds -AutoSize

$suiteSummaryPath = Join-Path $baseDir "reports\scene_harness\OCZ_RTX_12_SUITE_SUMMARY.json"
$results | ConvertTo-Json -Depth 4 | Set-Content $suiteSummaryPath -Encoding UTF8
Write-Host "Resumen guardado en: $suiteSummaryPath"
