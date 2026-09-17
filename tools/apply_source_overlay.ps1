# ============================================================
# apply_source_overlay.ps1
# Aplica el árbol de modificaciones de Couch Edition sobre
# el checkout limpio de upstream de Shipwright y LibUltraShip.
# ============================================================
[CmdletBinding()]
param (
    [string]$TargetSourceDir = ""
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($TargetSourceDir)) {
    $TargetSourceDir = Join-Path $RepoRoot "source" "shipwright"
}

$OverlayRoot = Join-Path $RepoRoot "source-overlay"

Write-Host "=== APLICANDO SOURCE-OVERLAY DE COUCH EDITION ===" -ForegroundColor Cyan
Write-Host "Overlay Root:     $OverlayRoot"
Write-Host "Target Source:    $TargetSourceDir"

if (-not (Test-Path $OverlayRoot)) {
    Write-Error "No se encontro el directorio de overlay en $OverlayRoot"
    exit 1
}

if (-not (Test-Path $TargetSourceDir)) {
    Write-Error "No se encontro el directorio destino en $TargetSourceDir. Ejecute primero tools/setup_upstream.ps1"
    exit 1
}

# 1. Overlay de Shipwright
$SwOverlay = Join-Path $OverlayRoot "shipwright"
if (Test-Path $SwOverlay) {
    Write-Host "Copiando modificaciones de Shipwright..." -ForegroundColor Yellow
    Copy-Item -Path "$SwOverlay\*" -Destination $TargetSourceDir -Recurse -Force
}

# 2. Overlay de LibUltraShip
$LusOverlay = Join-Path $OverlayRoot "libultraship"
$TargetLus = Join-Path $TargetSourceDir "libultraship"
if ((Test-Path $LusOverlay) -and (Test-Path $TargetLus)) {
    Write-Host "Copiando modificaciones de LibUltraShip..." -ForegroundColor Yellow
    Copy-Item -Path "$LusOverlay\*" -Destination $TargetLus -Recurse -Force
}

Write-Host "=== SOURCE-OVERLAY APLICADO EXITOSAMENTE ===" -ForegroundColor Green
exit 0
