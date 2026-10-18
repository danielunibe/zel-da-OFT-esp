# Script de preparación e integridad de build para OCZ-RTX-12
param()

$baseDir = "c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
$targetBuildDir = Join-Path $baseDir "build\OCZ_RTX_12_VISUAL_QUALITY"

if (-not (Test-Path $targetBuildDir)) {
    New-Item -ItemType Directory -Path $targetBuildDir -Force | Out-Null
}

$sourceDir = Join-Path $baseDir "source\shipwright\build\OCZ_NAVI_09_RELEASE\out\Release"
$sourceExe = Join-Path $sourceDir "soh.exe"

if (-not (Test-Path $sourceExe)) {
    Write-Error "No se encontró el ejecutable en $sourceExe"
    exit 1
}

Copy-Item $sourceExe (Join-Path $targetBuildDir "soh.exe") -Force

# Buscar archivos oot.o2r, soh.o2r, es.o2r en runtime/ocz-rtx-09, sourceDir o raíz
$assetNames = @("oot.o2r", "soh.o2r", "es.o2r", "gamecontrollerdb.txt", "shipofharkinian.json")
foreach ($asset in $assetNames) {
    $found = $null
    $candidates = @(
        (Join-Path $sourceDir $asset),
        (Join-Path $baseDir "runtime\ocz-rtx-09\$asset"),
        (Join-Path $baseDir "runtime\build-test\$asset"),
        (Join-Path $baseDir $asset),
        (Join-Path $baseDir "source\shipwright\$asset")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $found = $c
            break
        }
    }
    if ($found) {
        Copy-Item $found (Join-Path $targetBuildDir $asset) -Force
        Write-Host "Copiado $asset desde $found"
    } else {
        Write-Warning "Asset $asset no encontrado en rutas estándar"
    }
}

# Copiar directorio Save
$saveSource = Join-Path $baseDir "runtime\build-test\Save"
$saveTarget = Join-Path $targetBuildDir "Save"
if (Test-Path $saveSource) {
    Copy-Item $saveSource $saveTarget -Recurse -Force
    Write-Host "Save copiado desde $saveSource a $saveTarget"
}

# Obtener Hashes de integridad
$hlslPath = Join-Path $baseDir "source\shipwright\libultraship\src\fast\shaders\dxr_shadow.hlsl"
$csoHeaderPath = Join-Path $baseDir "source\shipwright\libultraship\include\fast\backends\dxr_shadow_cso.h"
$exePath = Join-Path $targetBuildDir "soh.exe"

$hlslHash = (Get-FileHash $hlslPath -Algorithm SHA256).Hash
$csoHash = (Get-FileHash $csoHeaderPath -Algorithm SHA256).Hash
$exeHash = (Get-FileHash $exePath -Algorithm SHA256).Hash

# Git info
$gitHead = (git -C $baseDir rev-parse HEAD 2>$null)
$gitStatus = (git -C $baseDir status --porcelain 2>$null)
$submoduleHead = (git -C (Join-Path $baseDir "source\shipwright\libultraship") rev-parse HEAD 2>$null)

$metadata = [PSCustomObject]@{
    BuildId = "OCZ_RTX_12_VISUAL_QUALITY"
    Timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:sszzz")
    GitHead = $gitHead
    SubmoduleLibultrashipHead = $submoduleHead
    GitStatusSummary = ($gitStatus | Out-String).Trim()
    HlslSha256 = $hlslHash
    DxilHeaderSha256 = $csoHash
    ExeSha256 = $exeHash
    BuildConfiguration = "Release"
    Compiler = "MSVC (Ninja / CMake)"
    PbrParameters = [PSCustomObject]@{
        Water = [PSCustomObject]@{
            MaterialType = "WATER"
            Roughness = 0.04
            Specular = 0.51
            F0 = 0.0204
            Reflectivity = 1.0
            PhysicalModel = "Fresnel Schlick + GGX + Normal preservation"
        }
        Ceramic = [PSCustomObject]@{
            MaterialType = "CERAMIC"
            Roughness = 0.28
            Specular = 0.90
            F0 = 0.036
            Reflectivity = 1.0
            PhysicalModel = "GGX Specular + Fresnel dielectric"
        }
    }
    RoutesSupported = @("PRODUCTION", "ISOLATED_LIGHT_LAB", "AOV_DEBUG")
}

$metadataPath = Join-Path $targetBuildDir "build_metadata.json"
$metadata | ConvertTo-Json -Depth 5 | Set-Content -Path $metadataPath -Encoding UTF8
Write-Host "Metadata guardada en $metadataPath"

Write-Host "=== Contenido de $targetBuildDir ==="
Get-ChildItem $targetBuildDir | Select-Object Name, Length, LastWriteTime
