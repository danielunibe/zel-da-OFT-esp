# ============================================================
# setup_upstream.ps1
# Clona y prepara los repositorios upstream de Harbour Masters
# en los commits fijados exactos requeridos por Couch Edition.
# ============================================================
[CmdletBinding()]
param (
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $RepoRoot "source"
}

$ShipwrightDir = Join-Path $SourceDir "shipwright"
$ShipwrightCommit = "4aaad850bd5540cd77c2d83f3ad348d3b38605b2"
$LibUltraShipCommit = "17a0b7939bd05f5e617cef89457ca43774fc9a9f"

Write-Host "=== PREPARANDO ARBOL UPSTREAM HARBOUR MASTERS ===" -ForegroundColor Cyan
Write-Host "Directorio destino: $ShipwrightDir"

if (-not (Test-Path $SourceDir)) {
    New-Item -ItemType Directory -Path $SourceDir -Force | Out-Null
}

if (-not (Test-Path $ShipwrightDir)) {
    Write-Host "Clonando Shipwright..." -ForegroundColor Yellow
    git clone https://github.com/HarbourMasters/Shipwright.git $ShipwrightDir
}

Push-Location $ShipwrightDir
try {
    Write-Host "Checkout Shipwright commit fijado: $ShipwrightCommit..." -ForegroundColor Yellow
    git checkout $ShipwrightCommit

    Write-Host "Actualizando submodulos recursivamente..." -ForegroundColor Yellow
    git submodule update --init --recursive

    $LusDir = Join-Path $ShipwrightDir "libultraship"
    Push-Location $LusDir
    try {
        Write-Host "Checkout LibUltraShip commit fijado: $LibUltraShipCommit..." -ForegroundColor Yellow
        git checkout $LibUltraShipCommit
    }
    finally {
        Pop-Location
    }

    Write-Host "=== ARBOL UPSTREAM PREPARADO EXITOSAMENTE ===" -ForegroundColor Green
}
finally {
    Pop-Location
}
