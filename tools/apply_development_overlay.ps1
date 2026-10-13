[CmdletBinding()]
param(
    [string]$TargetSourceDir = "",
    [switch]$AllowDirtyTarget
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($TargetSourceDir)) {
    $TargetSourceDir = Join-Path $RepoRoot "source\shipwright"
}
$TargetSourceDir = (Resolve-Path -LiteralPath $TargetSourceDir).Path
$TargetLus = Join-Path $TargetSourceDir "libultraship"
$SwBase = "4aaad850bd5540cd77c2d83f3ad348d3b38605b2"
$LusBase = "17a0b7939bd05f5e617cef89457ca43774fc9a9f"

function Assert-GitHead([string]$Path, [string]$Expected, [string]$Label) {
    $head = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $Expected) {
        throw "$Label HEAD must be $Expected. Found '$head'."
    }
}

function Assert-Clean([string]$Path, [string]$Label) {
    $status = & git -C $Path status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read Git status for $Label at $Path."
    }
    if (($status | Measure-Object).Count -gt 0 -and -not $AllowDirtyTarget) {
        throw "$Label is dirty. Use a clean checkout or pass -AllowDirtyTarget after reviewing the overwrite scope."
    }
}

function Copy-Overlay([string]$OverlayRoot, [string]$DestinationRoot) {
    $files = @(Get-ChildItem -LiteralPath $OverlayRoot -Force -Recurse -File)
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($OverlayRoot.Length).TrimStart('\', '/')
        $destination = Join-Path $DestinationRoot $relative
        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    return $files.Count
}

$OverlayRoot = Join-Path $RepoRoot "source-overlay\development"
$ShipwrightOverlay = Join-Path $OverlayRoot "shipwright"
$LibultrashipOverlay = Join-Path $OverlayRoot "libultraship"
if (-not (Test-Path -LiteralPath $ShipwrightOverlay) -or -not (Test-Path -LiteralPath $LibultrashipOverlay)) {
    throw "Development overlay is incomplete: $OverlayRoot"
}
if (-not (Test-Path -LiteralPath $TargetLus)) {
    throw "Expected libultraship submodule at $TargetLus"
}

Assert-GitHead $TargetSourceDir $SwBase "Shipwright"
Assert-GitHead $TargetLus $LusBase "libultraship"
Assert-Clean $TargetSourceDir "Shipwright"
Assert-Clean $TargetLus "libultraship"

$shipwrightCount = Copy-Overlay $ShipwrightOverlay $TargetSourceDir
$libultrashipCount = Copy-Overlay $LibultrashipOverlay $TargetLus
Write-Host "Applied development snapshot files: Shipwright=$shipwrightCount, libultraship=$libultrashipCount" -ForegroundColor Green
Write-Host "This snapshot is experimental and does not certify a release or visible RTX output." -ForegroundColor Yellow