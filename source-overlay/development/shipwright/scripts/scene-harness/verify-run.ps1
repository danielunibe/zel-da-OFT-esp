[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [Parameter(Mandatory = $true)][string]$OriginalSaveRoot,
    [switch]$AsLibrary
)

$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()
$verifiedStageCount = 0

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-JsonFile([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure "$Label is missing"
        return $null
    }
    try {
        return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    } catch {
        Add-Failure "$Label is invalid JSON: $($_.Exception.Message)"
        return $null
    }
}

function Test-RunValue($Object, [string]$Property, $Expected, [string]$Label) {
    if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Property] -or
        [string]$Object.$Property -cne [string]$Expected) {
        Add-Failure "$Label does not match the resolved run"
    }
}

try {
    $resolvedRunRoot = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RunRoot).Path).TrimEnd('\')
    $resolvedSaveRoot = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $OriginalSaveRoot).Path).TrimEnd('\')
} catch {
    $result = [ordered]@{ schemaVersion = 1; status = 'FAIL'; failures = @("Run or original-save path cannot be resolved: $($_.Exception.Message)") }
    if (-not $AsLibrary) { $result | ConvertTo-Json -Depth 6; exit 1 }
    [pscustomobject]$result
    return
}

$resolvedManifest = Read-JsonFile (Join-Path $resolvedRunRoot 'manifest.launcher.json') 'Launcher manifest'
$gameManifest = Read-JsonFile (Join-Path $resolvedRunRoot 'manifest.resolved.json') 'Resolved game manifest'
$evidence = Read-JsonFile (Join-Path $resolvedRunRoot 'evidence.json') 'Harness evidence'
$request = Read-JsonFile (Join-Path $resolvedRunRoot 'capture.request.json') 'Capture request'
$ack = Read-JsonFile (Join-Path $resolvedRunRoot 'capture.ack.json') 'Capture acknowledgment'
$saveIntegrity = Read-JsonFile (Join-Path $resolvedRunRoot 'save-integrity.json') 'Save integrity evidence'
$dxrLogPath = Join-Path $resolvedRunRoot 'harness-dxr.log'
$dxrLog = if (Test-Path -LiteralPath $dxrLogPath -PathType Leaf) { Get-Content -LiteralPath $dxrLogPath -Raw } else { '' }
$latestLightStart = $dxrLog.LastIndexOf('[COUCH-DXR-LIGHT-SYNC]')
$latestLightEnd = if ($latestLightStart -ge 0) { $dxrLog.IndexOf([char]10, $latestLightStart) } else { -1 }
$latestLightRecord = if ($latestLightStart -lt 0) { '' } elseif ($latestLightEnd -lt 0) { $dxrLog.Substring($latestLightStart) } else { $dxrLog.Substring($latestLightStart, $latestLightEnd - $latestLightStart) }
$productiveGameLight = $latestLightRecord -match 'sameFrame=1' -and $latestLightRecord -match 'match=YES'
$gameLightSyncClassification = if ($latestLightStart -lt 0) { 'NOT_RECORDED' }
    elseif ($latestLightRecord -match 'sameFrame=0') { 'TEMPORAL_MISMATCH' }
    elseif ($productiveGameLight) { 'PASS' }
    else { 'SAME_FRAME_MISMATCH' }

if (Test-Path -LiteralPath (Join-Path $resolvedRunRoot 'RUN_FAIL') -PathType Leaf) { Add-Failure 'RUN_FAIL marker exists' }
if (-not (Test-Path -LiteralPath (Join-Path $resolvedRunRoot 'RUN_PASS') -PathType Leaf)) { Add-Failure 'RUN_PASS marker is missing' }

if ($null -ne $resolvedManifest -and $null -ne $evidence -and $null -ne $request -and $null -ne $ack) {
    foreach ($object in @(@{ value = $evidence; label = 'Evidence' }, @{ value = $request; label = 'Capture request' }, @{ value = $ack.request; label = 'Acknowledgment request' })) {
        Test-RunValue $object.value 'scenario_id' $resolvedManifest.scenarioId "$($object.label) scenario_id"
        Test-RunValue $object.value 'view_id' $resolvedManifest.viewId "$($object.label) view_id"
        Test-RunValue $object.value 'truth_mode' $resolvedManifest.truthMode "$($object.label) truth_mode"
        Test-RunValue $object.value 'scene' $resolvedManifest.scene "$($object.label) scene"
        Test-RunValue $object.value 'room' $resolvedManifest.room "$($object.label) room"
    }
    Test-RunValue $ack 'schema_version' 1 'Acknowledgment schema version'
    Test-RunValue $ack 'sequence' 1 'Acknowledgment sequence'
    Test-RunValue $ack 'result' 'PASS' 'Acknowledgment result'
    Test-RunValue $ack 'capture_mask' 255 'Acknowledgment capture mask'
    Test-RunValue $evidence 'schema_version' 1 'Evidence schema version'
    Test-RunValue $evidence 'result' 'PASS' 'Evidence result'
    Test-RunValue $evidence 'audio_output' 'MUTED_NULL_BACKEND' 'Audio output'
    Test-RunValue $evidence 'bloom_only' 'NOT_PRESENT' 'Bloom declaration'
    Test-RunValue $evidence 'temp_save' $true 'Temporary debug save'
    Test-RunValue $evidence 'dxr_ready' $true 'DXR ready before capture'
    Test-RunValue $resolvedManifest 'audioOutput' 'MUTED_NULL_BACKEND' 'Launcher audio output'
    if ($resolvedManifest.newOtrGenerated -ne $false) { Add-Failure 'Launcher manifest does not confirm OTR preservation' }
    if ([string]::Equals([string]$resolvedManifest.originalSaveRoot, [string]$resolvedManifest.sandboxSaveRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Add-Failure 'Sandbox save root equals the original save root'
    }
    if (-not ([string]$resolvedManifest.sandboxSaveRoot).StartsWith($resolvedRunRoot + '\',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Add-Failure 'Sandbox save root is outside this run directory'
    }
    if ([int]$evidence.stable_frames -lt 30) { Add-Failure 'Evidence has fewer than 30 stable frames' }
    if ($evidence.save_sandbox_used -ne $true -or $evidence.temp_save -ne $true) { Add-Failure 'Evidence does not confirm isolated temporary debug save' }
    if ($request.sequence -ne 1 -or $ack.request.sequence -ne 1) { Add-Failure 'Capture request sequence is not 1' }

    if ($dxrLog -notmatch 'DXR Acceleration Structures ENABLED') { Add-Failure 'DXR initialization marker is missing' }
    if ($dxrLog -notmatch 'FIRST_TLAS\s*=\s*PASS[^\r\n]*Instances=[1-9]') { Add-Failure 'A non-empty first TLAS PASS marker is missing' }
    if ($dxrLog -notmatch 'First DispatchRays SUCCESS') { Add-Failure 'First DispatchRays success marker is missing' }
    if ($dxrLog -match 'DEVICE_REMOVED|DEVICE_HUNG') { Add-Failure 'DXR device removal or hang marker is present' }
    foreach ($property in @('scenario_id', 'view_id', 'truth_mode', 'scene', 'room', 'sequence',
            'view_hash', 'projection_hash', 'player_state_hash')) {
        Test-RunValue $ack.request $property $request.$property "Acknowledgment request $property"
    }

    if ($null -ne $gameManifest) {
        Test-RunValue $gameManifest.resolvedRun 'scenarioId' $resolvedManifest.scenarioId 'Resolved manifest scenario_id'
        Test-RunValue $gameManifest.resolvedRun 'viewId' $resolvedManifest.viewId 'Resolved manifest view_id'
        Test-RunValue $gameManifest.resolvedRun 'truthMode' $resolvedManifest.truthMode 'Resolved manifest truth_mode'
        Test-RunValue $gameManifest.resolvedRun 'expectedScene' $resolvedManifest.scene 'Resolved manifest scene'
        Test-RunValue $gameManifest.resolvedRun 'expectedRoom' $resolvedManifest.room 'Resolved manifest room'
        Test-RunValue $gameManifest.resolvedRun 'audioOutput' 'MUTED_NULL_BACKEND' 'Resolved manifest audio output'
    }
    if ($null -ne $evidence.saveIntegrity) {
        Test-RunValue $evidence.saveIntegrity 'status' 'PASS' 'Embedded save integrity status'
    } else { Add-Failure 'Evidence does not embed save integrity results' }

    $stages = [ordered]@{
        RASTER_HDR = 'raster_hdr'
        COMPOSITE_PRE_TONEMAP = 'composite_pre_tonemap'
        DXR_DIRECT_SHADED = 'dxr_direct_shaded'
        POST_ACES_PRE_PRESENT = 'post_aces_pre_present'
        FINAL_SDR = 'final_sdr'
        DXR_REFLECTION_RGBA16F = 'dxr_reflection'
        DXR_INDIRECT_RGBA16F = 'dxr_indirect'
        EMISSIVE_MASK = 'emissive_mask'
    }
    $ackCaptureProperties = if ($null -ne $ack.captures) { @($ack.captures.PSObject.Properties) } else { @() }
    foreach ($stage in $stages.Keys) {
        $stageFailureCount = $failures.Count
        $captureProperty = $ackCaptureProperties | Where-Object Name -CEQ $stage | Select-Object -First 1
        if ($null -eq $captureProperty -or [string]::IsNullOrWhiteSpace([string]$captureProperty.Value)) {
            Add-Failure "Acknowledgment is missing capture $stage"
            continue
        }
        $relativeCapture = [string]$captureProperty.Value
        $capturePath = [System.IO.Path]::GetFullPath((Join-Path $resolvedRunRoot $relativeCapture))
        if (-not $capturePath.StartsWith($resolvedRunRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            Add-Failure "Capture $stage escapes the run directory"
            continue
        }
        if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf) -or (Get-Item -LiteralPath $capturePath).Length -le 0) {
            Add-Failure "Capture payload $stage is missing or empty"
            continue
        }

        $stageName = $stages[$stage]
        $metadataPath = Join-Path (Join-Path $resolvedRunRoot 'captures') "$stageName.meta.json"
        $metadata = Read-JsonFile $metadataPath "$stage metadata for $stage"
        if ($null -eq $metadata) { continue }
        if ([string]$metadata.stage -cne $stage) { Add-Failure "Metadata stage does not match $stage" }
        if ([System.IO.Path]::GetFileName([string]$metadata.payload) -cne [System.IO.Path]::GetFileName($capturePath)) {
            Add-Failure "Metadata payload does not match acknowledged capture $stage"
        }
        if ([int]$metadata.width -le 0 -or [int]$metadata.height -le 0 -or [int]$metadata.frame -lt 0) {
            Add-Failure "Metadata dimensions or frame are invalid for $stage"
        }
        if ([string]::IsNullOrWhiteSpace([string]$metadata.expectedColorSpace)) {
            Add-Failure "$stage expected color-space metadata is missing"
        }
        foreach ($property in @('scenario_id', 'view_id', 'truth_mode', 'scene', 'room', 'sequence')) {
            $expected = if ($property -eq 'sequence') { 1 } else { $request.$property }
            Test-RunValue $metadata.request $property $expected "$stage metadata request $property"
        }
        if ($stage -in @('POST_ACES_PRE_PRESENT', 'FINAL_SDR')) {
            if ([System.IO.Path]::GetExtension($capturePath) -cne '.png') { Add-Failure "$stage must be captured as PNG" }
            if ([string]$metadata.format -cne 'R8G8B8A8_UNORM') { Add-Failure "$stage format must be R8G8B8A8_UNORM" }
            $pngSignature = [byte[]](137, 80, 78, 71, 13, 10, 26, 10)
            $stream = [System.IO.File]::OpenRead($capturePath)
            try {
                $header = [byte[]]::new(8)
                $read = $stream.Read($header, 0, $header.Length)
                $signatureMatches = $read -eq 8
                if ($signatureMatches) {
                    for ($byteIndex = 0; $byteIndex -lt $pngSignature.Length; $byteIndex++) {
                        if ($header[$byteIndex] -ne $pngSignature[$byteIndex]) {
                            $signatureMatches = $false
                            break
                        }
                    }
                }
                if (-not $signatureMatches) {
                    Add-Failure "$stage payload is not a valid PNG file"
                }
            } finally { $stream.Dispose() }
            $captureStem = [System.IO.Path]::GetFileNameWithoutExtension($capturePath) -replace '_raw$', ''
            $previewPath = Join-Path (Split-Path -Parent $capturePath) ($captureStem + '_opaque_preview.png')
            if (-not (Test-Path -LiteralPath $previewPath -PathType Leaf) -or (Get-Item -LiteralPath $previewPath).Length -le 0) {
                Add-Failure "$stage opaque preview PNG is missing"
            }
        } else {
            $formatInfo = switch -CaseSensitive ([string]$metadata.format) {
                'R16_FLOAT' { @{ Extension = '.r16f'; BytesPerPixel = 2 } }
                'R16G16_FLOAT' { @{ Extension = '.rg16f'; BytesPerPixel = 4 } }
                'R16G16B16A16_FLOAT' { @{ Extension = '.rgba16f'; BytesPerPixel = 8 } }
                'R32_FLOAT' { @{ Extension = '.r32f'; BytesPerPixel = 4 } }
                'R32G32_FLOAT' { @{ Extension = '.rg32f'; BytesPerPixel = 8 } }
                'R32G32B32A32_FLOAT' { @{ Extension = '.rgba32f'; BytesPerPixel = 16 } }
                default { $null }
            }
            if ($null -eq $formatInfo) {
                Add-Failure "$stage format is not a supported float format"
            } else {
                if ($stage -in @('RASTER_HDR', 'COMPOSITE_PRE_TONEMAP', 'DXR_DIRECT_SHADED',
                        'DXR_REFLECTION_RGBA16F', 'DXR_INDIRECT_RGBA16F') -and
                    [string]$metadata.format -cne 'R16G16B16A16_FLOAT') {
                    Add-Failure "$stage must retain its RGBA16F resource format"
                }
                if ($stage -eq 'EMISSIVE_MASK' -and [string]$metadata.format -notin @('R16_FLOAT', 'R32_FLOAT')) {
                    Add-Failure 'EMISSIVE_MASK must retain its R16F format or documented R32F fallback'
                }
                if ([System.IO.Path]::GetExtension($capturePath) -cne $formatInfo.Extension) {
                    Add-Failure "$stage extension does not match float format $($metadata.format)"
                }
                $expectedBytes = [long]$metadata.width * [long]$metadata.height * [long]$formatInfo.BytesPerPixel
                if ((Get-Item -LiteralPath $capturePath).Length -ne $expectedBytes) {
                    Add-Failure "$stage FP16 payload length does not match its dimensions and format"
                }
                $statsPath = Join-Path (Join-Path $resolvedRunRoot 'captures') "${stageName}_frame$($metadata.frame).stats.json"
                $stats = Read-JsonFile $statsPath "$stage stats"
                if ($null -eq $stats) {
                    Add-Failure "$stage statistics are missing or malformed"
                } else {
                    if ($stats.stage -cne $stage) { Add-Failure "$stage statistics stage id is invalid" }
                    if ([int]$stats.width -ne [int]$metadata.width -or [int]$stats.height -ne [int]$metadata.height) {
                        Add-Failure "$stage statistics dimensions do not match capture metadata"
                    }
                    if ([int]$stats.nan_channels -ne 0) { Add-Failure "$stage contains NaN channels" }
                    if ([int]$stats.inf_channels -ne 0) { Add-Failure "$stage contains Inf channels" }
                }
            }
        }
        foreach ($property in @('view_hash', 'projection_hash', 'player_state_hash')) {
            Test-RunValue $metadata.request $property $request.$property "$stage metadata request $property"
        }
        if ($failures.Count -eq $stageFailureCount) { ++$verifiedStageCount }
    }
    if ($ackCaptureProperties.Count -ne $stages.Count) { Add-Failure 'Acknowledgment capture set does not contain exactly eight stages' }
}

if ($null -ne $resolvedManifest) {
    foreach ($saveName in $resolvedManifest.originalHashesBefore.PSObject.Properties.Name) {
        $savePath = Join-Path $resolvedSaveRoot $saveName
        if (-not (Test-Path -LiteralPath $savePath -PathType Leaf)) {
            Add-Failure "Original save $saveName is missing"
            continue
        }
        $actualHash = (Get-FileHash -LiteralPath $savePath -Algorithm SHA256).Hash
        $expectedHash = [string]$resolvedManifest.originalHashesBefore.$saveName
        if ([string]::IsNullOrWhiteSpace($expectedHash) -or $actualHash -cne $expectedHash) {
            Add-Failure "Original save $saveName hash changed"
        }
    }
}
if ($null -ne $saveIntegrity) {
    Test-RunValue $saveIntegrity 'status' 'PASS' 'Save integrity status'
    foreach ($saveName in $saveIntegrity.originalHashesBefore.PSObject.Properties.Name) {
        Test-RunValue $saveIntegrity.originalHashesAfter $saveName $saveIntegrity.originalHashesBefore.$saveName "Post-run save hash $saveName"
    }
}

$status = if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' }
$result = [ordered]@{
    schemaVersion = 1
    status = $status
    runId = if ($resolvedManifest) { $resolvedManifest.runId } else { $null }
    scenarioId = if ($resolvedManifest) { $resolvedManifest.scenarioId } else { $null }
    viewId = if ($resolvedManifest) { $resolvedManifest.viewId } else { $null }
    truthMode = if ($resolvedManifest) { $resolvedManifest.truthMode } else { $null }
    verifiedCaptureStages = $verifiedStageCount
    dxrInitialization = ($dxrLog -match 'DXR Acceleration Structures ENABLED')
    tlas = ($dxrLog -match 'FIRST_TLAS\s*=\s*PASS[^\r\n]*Instances=[1-9]')
    dispatchRays = ($dxrLog -match 'First DispatchRays SUCCESS')
    productiveGameLight = $productiveGameLight
    gameLightSyncClassification = $gameLightSyncClassification
    bloomOnly = 'NOT_PRESENT'
    failures = @($failures)
}
$statusPath = Join-Path $resolvedRunRoot 'verification.status.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statusPath -Encoding utf8

if ($AsLibrary) {
    [pscustomobject]$result
    return
}
$result | ConvertTo-Json -Depth 8
if ($status -ne 'PASS') { exit 1 }
exit 0
