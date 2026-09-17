# ============================================================
# build_from_overlay.ps1
# Proceso unificado para restaurar el source upstream,
# aplicar el overlay y compilar Couch Edition.
# ============================================================
[CmdletBinding()]
param (
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "OCARINA OF TIME PC - COUCH EDITION: BUILD FROM OVERLAY" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# Paso 1: Configurar upstream si no existe
$SourceDir = Join-Path $RepoRoot "source"
$ShipwrightDir = Join-Path $SourceDir "shipwright"
if (-not (Test-Path $ShipwrightDir)) {
    Write-Host "[1/3] Configurando arbol upstream..." -ForegroundColor Yellow
    & (Join-Path $RepoRoot "tools" "setup_upstream.ps1")
} else {
    Write-Host "[1/3] Arbol upstream ya presente." -ForegroundColor Green
}

# Paso 2: Aplicar overlay
Write-Host "[2/3] Aplicando source-overlay..." -ForegroundColor Yellow
& (Join-Path $RepoRoot "tools" "apply_source_overlay.ps1")

# Paso 3: Compilar
Write-Host "[3/3] Iniciando compilacion ($Configuration)..." -ForegroundColor Yellow
$BuildScript = Join-Path $RepoRoot "tools" "build_shipwright_9_1_1.ps1"
if (Test-Path $BuildScript) {
    & $BuildScript -Configuration $Configuration
} else {
    Write-Error "No se encontro el script de compilacion en $BuildScript"
    exit 1
}

Write-Host "============================================================" -ForegroundColor Green
Write-Host "BUILD FROM OVERLAY COMPLETADO EXITOSAMENTE" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
exit 0
