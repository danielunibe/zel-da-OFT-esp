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

function Test-PngReadable([string]$Path, [int]$Width, [int]$Height) {
    $image = $null
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop | Out-Null
        $image = [System.Drawing.Image]::FromFile($Path)
        return $image.Width -eq $Width -and $image.Height -eq $Height
    } catch {
        return $false
    } finally {
        if ($null -ne $image) { $image.Dispose() }
    }
}

$harnessHalfFloatCSharp = @'
using System;

public static class HarnessHalfFloat
{
    public static float HalfToSingle(ushort bits)
    {
        int sign = (bits >> 15) & 0x1;
        int exp = (bits >> 10) & 0x1F;
        int frac = bits & 0x3FF;
        uint outBits;
        if (exp == 0)
        {
            if (frac == 0)
            {
                outBits = (uint)(sign << 31);
            }
            else
            {
                int e = 113;
                while ((frac & 0x400) == 0)
                {
                    frac <<= 1;
                    e--;
                }
                frac &= 0x3FF;
                outBits = (uint)((sign << 31) | (e << 23) | (frac << 13));
            }
        }
        else if (exp == 31)
        {
            outBits = (uint)((sign << 31) | 0x7F800000u | ((uint)frac << 13));
        }
        else
        {
            outBits = (uint)((sign << 31) | ((exp + 112) << 23) | (frac << 13));
        }
        return BitConverter.ToSingle(BitConverter.GetBytes(unchecked((int)outBits)), 0);
    }

    public sealed class ComparisonResult
    {
        public double ByteMae;
        public double FloatMae;
        public double MaxAbsDelta;
        public long DifferingPixels;
        public long TotalPixels;
        public bool NonFiniteEncountered;
    }

    public static ComparisonResult Compare(byte[] a, byte[] b)
    {
        var result = new ComparisonResult();
        long channelCount = Math.Min(a.Length, b.Length) / 2;
        result.TotalPixels = channelCount / 4;
        long byteCount = Math.Min(a.Length, b.Length);
        double byteSum = 0.0;
        double floatSum = 0.0;
        double maxDelta = 0.0;
        bool nonFinite = false;
        for (long byteIndex = 0; byteIndex < byteCount; byteIndex++)
        {
            byteSum += Math.Abs((int)a[byteIndex] - (int)b[byteIndex]);
        }
        for (long channel = 0; channel < channelCount; channel++)
        {
            int offset = (int)(channel * 2);
            ushort bitsA = (ushort)(a[offset] | (a[offset + 1] << 8));
            ushort bitsB = (ushort)(b[offset] | (b[offset + 1] << 8));
            float valueA = HalfToSingle(bitsA);
            float valueB = HalfToSingle(bitsB);
            if (float.IsNaN(valueA) || float.IsInfinity(valueA) ||
                float.IsNaN(valueB) || float.IsInfinity(valueB))
            {
                nonFinite = true;
            }
            double delta = Math.Abs((double)valueA - (double)valueB);
            floatSum += delta;
            if (delta > maxDelta)
            {
                maxDelta = delta;
            }
        }
        long differing = 0;
        for (long pixel = 0; pixel < result.TotalPixels; pixel++)
        {
            bool differs = false;
            for (int c = 0; c < 4; c++)
            {
                int offset = (int)((pixel * 4 + c) * 2);
                ushort bitsA = (ushort)(a[offset] | (a[offset + 1] << 8));
                ushort bitsB = (ushort)(b[offset] | (b[offset + 1] << 8));
                if (bitsA != bitsB)
                {
                    differs = true;
                    break;
                }
            }
            if (differs)
            {
                differing++;
            }
        }
        result.ByteMae = byteCount > 0 ? byteSum / byteCount : 0.0;
        result.FloatMae = channelCount > 0 ? floatSum / channelCount : 0.0;
        result.MaxAbsDelta = maxDelta;
        result.DifferingPixels = differing;
        result.NonFiniteEncountered = nonFinite;
        return result;
    }
}
'@
$script:HalfFloatType = $null
$script:HalfFloatMethod = 'powershell-sample'
try {
    Add-Type -TypeDefinition $harnessHalfFloatCSharp -ErrorAction Stop
} catch {
    # The type may already be registered by an earlier invocation in this session.
}
try {
    $script:HalfFloatType = [HarnessHalfFloat]
    $script:HalfFloatMethod = 'csharp-full'
} catch {
    $script:HalfFloatType = $null
    $script:HalfFloatMethod = 'powershell-sample'
}

$script:PngDecodeAvailable = $false
try {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    $pngProbe = New-Object -TypeName System.Drawing.Bitmap -ArgumentList 1, 1
    $pngProbe.Dispose()
    $script:PngDecodeAvailable = $true
} catch {
    $script:PngDecodeAvailable = $false
}

function Convert-HalfBitsToSingle([uint16]$Bits) {
    $sign = [int](($Bits -shr 15) -band 1)
    $exp = [int](($Bits -shr 10) -band 0x1F)
    $frac = [int]($Bits -band 0x3FF)
    if ($exp -eq 0) {
        if ($frac -eq 0) { return 0.0 }
        $e = 113
        while (($frac -band 0x400) -eq 0) {
            $frac = $frac -shl 1
            $e--
        }
        $frac = $frac -band 0x3FF
        $value = [math]::Pow(2.0, $e - 127) * (1.0 + $frac / 1024.0)
        if ($sign -eq 1) { return -$value }
        return $value
    }
    if ($exp -eq 31) {
        if ($frac -eq 0) {
            if ($sign -eq 1) { return [double]::NegativeInfinity }
            return [double]::PositiveInfinity
        }
        return [double]::NaN
    }
    $value = [math]::Pow(2.0, $exp - 15) * (1.0 + $frac / 1024.0)
    if ($sign -eq 1) { return -$value }
    return $value
}

function Get-StagePayloadPath([string]$RunRoot, $Metadata) {
    if ($null -eq $Metadata) { return $null }
    $payloadName = [string]$Metadata.payload
    if ([string]::IsNullOrWhiteSpace($payloadName)) { return $null }
    return Join-Path (Join-Path $RunRoot 'captures') $payloadName
}

function Compare-Rgba16FBytesSampled([byte[]]$BytesA, [byte[]]$BytesB) {
    $result = [ordered]@{
        byteMae = 0.0
        floatMae = 0.0
        maxAbsDelta = 0.0
        differingPixels = 0
        differingPixelsPercent = 0.0
        totalPixels = 0
        sampledPixels = 0
        nonFinite = $false
    }
    $channelCount = [Math]::Floor([Math]::Min($BytesA.Length, $BytesB.Length) / 2.0)
    $totalPixels = [Math]::Floor($channelCount / 4.0)
    $result.totalPixels = [long]$totalPixels
    $byteSum = 0.0
    $floatSum = 0.0
    $maxDelta = 0.0
    $differing = 0
    $nonFinite = $false
    $sampled = 0
    for ($pixel = 0; $pixel -lt $totalPixels; $pixel += 64) {
        $pixelDiffers = $false
        for ($channel = 0; $channel -lt 4; $channel++) {
            $index = [int](($pixel * 4 + $channel) * 2)
            $byteSum += [Math]::Abs([int]$BytesA[$index] - [int]$BytesB[$index])
            $byteSum += [Math]::Abs([int]$BytesA[$index + 1] - [int]$BytesB[$index + 1])
            $bitsA = [uint16]($BytesA[$index] -bor ($BytesA[$index + 1] -shl 8))
            $bitsB = [uint16]($BytesB[$index] -bor ($BytesB[$index + 1] -shl 8))
            $valueA = Convert-HalfBitsToSingle $bitsA
            $valueB = Convert-HalfBitsToSingle $bitsB
            if ([double]::IsNaN($valueA) -or [double]::IsInfinity($valueA) -or
                [double]::IsNaN($valueB) -or [double]::IsInfinity($valueB)) { $nonFinite = $true }
            $delta = [Math]::Abs($valueA - $valueB)
            $floatSum += $delta
            if ($delta -gt $maxDelta) { $maxDelta = $delta }
            if ($valueA -ne $valueB) { $pixelDiffers = $true }
        }
        $sampled++
        if ($pixelDiffers) { $differing++ }
    }
    $sampledChannels = $sampled * 4
    if ($sampledChannels -gt 0) {
        $result.byteMae = $byteSum / ($sampledChannels * 2)
        $result.floatMae = $floatSum / $sampledChannels
    }
    $result.maxAbsDelta = $maxDelta
    $result.differingPixels = $differing
    if ($sampled -gt 0) { $result.differingPixelsPercent = [math]::Round(100.0 * $differing / $sampled, 6) }
    $result.sampledPixels = $sampled
    $result.nonFinite = $nonFinite
    return $result
}

function Compare-Rgba16FStagePair([string]$RunRoot, [string]$StageA, [string]$StageB, $StageMetadata) {
    $label = "$StageA vs $StageB"
    $metrics = [ordered]@{
        stageA = $StageA
        stageB = $StageB
        method = $script:HalfFloatMethod
        byteMae = $null
        floatMae = $null
        maxAbsDelta = $null
        differingPixels = $null
        differingPixelsPercent = $null
        totalPixels = $null
        nonFinite = $null
    }
    $metaA = if ($null -ne $StageMetadata -and $null -ne $StageMetadata[$StageA]) { $StageMetadata[$StageA] } else { $null }
    $metaB = if ($null -ne $StageMetadata -and $null -ne $StageMetadata[$StageB]) { $StageMetadata[$StageB] } else { $null }
    if ($null -eq $metaA -or $null -eq $metaB) {
        Add-Failure "${label}: stage metadata is missing"
        return $metrics
    }
    if ([int]$metaA.width -ne [int]$metaB.width -or [int]$metaA.height -ne [int]$metaB.height) {
        Add-Failure "${label}: stage dimensions do not match"
        return $metrics
    }
    $payloadA = Get-StagePayloadPath $RunRoot $metaA
    $payloadB = Get-StagePayloadPath $RunRoot $metaB
    if ([string]::IsNullOrWhiteSpace($payloadA) -or -not (Test-Path -LiteralPath $payloadA -PathType Leaf) -or
        [string]::IsNullOrWhiteSpace($payloadB) -or -not (Test-Path -LiteralPath $payloadB -PathType Leaf)) {
        Add-Failure "${label}: stage payload is missing"
        return $metrics
    }
    $bytesA = [System.IO.File]::ReadAllBytes($payloadA)
    $bytesB = [System.IO.File]::ReadAllBytes($payloadB)
    if ($bytesA.Length -ne $bytesB.Length) {
        Add-Failure "${label}: stage payload sizes do not match"
        return $metrics
    }
    $metrics.totalPixels = [long][Math]::Floor([Math]::Min($bytesA.Length, $bytesB.Length) / 8.0)
    if ($null -ne $script:HalfFloatType) {
        $comparison = $script:HalfFloatType::Compare($bytesA, $bytesB)
        $metrics.byteMae = $comparison.ByteMae
        $metrics.floatMae = $comparison.FloatMae
        $metrics.maxAbsDelta = $comparison.MaxAbsDelta
        $metrics.differingPixels = $comparison.DifferingPixels
        if ($comparison.TotalPixels -gt 0) {
            $metrics.differingPixelsPercent = [math]::Round(100.0 * $comparison.DifferingPixels / $comparison.TotalPixels, 6)
        } else { $metrics.differingPixelsPercent = 0.0 }
        $metrics.nonFinite = [bool]$comparison.NonFiniteEncountered
    } else {
        $sampled = Compare-Rgba16FBytesSampled $bytesA $bytesB
        $metrics.byteMae = $sampled.byteMae
        $metrics.floatMae = $sampled.floatMae
        $metrics.maxAbsDelta = $sampled.maxAbsDelta
        $metrics.differingPixels = $sampled.differingPixels
        $metrics.differingPixelsPercent = $sampled.differingPixelsPercent
        $metrics.totalPixels = $sampled.totalPixels
        $metrics.nonFinite = [bool]$sampled.nonFinite
        $metrics.sampledPixels = $sampled.sampledPixels
    }
    if ($metrics.nonFinite) { Add-Failure "${label}: payload contains NaN or Inf half-float channels" }
    return $metrics
}

function Get-DecodedPngPixels([string]$Path) {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
    $bitmap = New-Object -TypeName System.Drawing.Bitmap -ArgumentList $Path
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        $rect = New-Object -TypeName System.Drawing.Rectangle -ArgumentList 0, 0, $width, $height
        $bmpData = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $stride = [Math]::Abs($bmpData.Stride)
            $rowBytes = $width * 4
            $pixels = [byte[]]::new($rowBytes * $height)
            for ($y = 0; $y -lt $height; $y++) {
                [System.Runtime.InteropServices.Marshal]::Copy(
                    [System.IntPtr]::Add($bmpData.Scan0, $y * $stride), $pixels, $y * $rowBytes, $rowBytes)
            }
        } finally {
            $bitmap.UnlockBits($bmpData)
        }
    } finally {
        $bitmap.Dispose()
    }
    return [ordered]@{ Width = $width; Height = $height; Bytes = $pixels }
}

function Compare-PngStagePair([string]$RunRoot, [string]$StageA, [string]$StageB, $StageMetadata) {
    $label = "$StageA vs $StageB"
    $metrics = [ordered]@{
        stageA = $StageA
        stageB = $StageB
        pngDecode = 'system-drawing'
        mae = $null
        differingPixels = $null
        differingPixelsPercent = $null
        totalPixels = $null
    }
    $metaA = if ($null -ne $StageMetadata -and $null -ne $StageMetadata[$StageA]) { $StageMetadata[$StageA] } else { $null }
    $metaB = if ($null -ne $StageMetadata -and $null -ne $StageMetadata[$StageB]) { $StageMetadata[$StageB] } else { $null }
    if ($null -eq $metaA -or $null -eq $metaB) {
        Add-Failure "${label}: stage metadata is missing"
        return $metrics
    }
    if ([int]$metaA.width -ne [int]$metaB.width -or [int]$metaA.height -ne [int]$metaB.height) {
        Add-Failure "${label}: stage dimensions do not match"
        return $metrics
    }
    $payloadA = Get-StagePayloadPath $RunRoot $metaA
    $payloadB = Get-StagePayloadPath $RunRoot $metaB
    if ([string]::IsNullOrWhiteSpace($payloadA) -or -not (Test-Path -LiteralPath $payloadA -PathType Leaf) -or
        [string]::IsNullOrWhiteSpace($payloadB) -or -not (Test-Path -LiteralPath $payloadB -PathType Leaf)) {
        Add-Failure "${label}: stage payload is missing"
        return $metrics
    }
    $decodedA = $null
    $decodedB = $null
    if ($script:PngDecodeAvailable) {
        try {
            $decodedA = Get-DecodedPngPixels $payloadA
            $decodedB = Get-DecodedPngPixels $payloadB
        } catch {
            $decodedA = $null
            $decodedB = $null
        }
    }
    if ($null -ne $decodedA -and $null -ne $decodedB -and
        $decodedA.Width -eq $decodedB.Width -and $decodedA.Height -eq $decodedB.Height) {
        $bytesA = $decodedA.Bytes
        $bytesB = $decodedB.Bytes
        $metrics.totalPixels = [long]$decodedA.Width * [long]$decodedA.Height
    } else {
        if ($script:PngDecodeAvailable) { Add-Failure "${label}: decoded PNG dimensions do not match" }
        $metrics.pngDecode = 'raw-fallback'
        $bytesA = [System.IO.File]::ReadAllBytes($payloadA)
        $bytesB = [System.IO.File]::ReadAllBytes($payloadB)
    }
    if ($bytesA.Length -ne $bytesB.Length) {
        Add-Failure "${label}: payload sizes do not match"
        return $metrics
    }
    $sum = 0.0
    for ($i = 0; $i -lt $bytesA.Length; $i++) {
        $sum += [Math]::Abs([int]$bytesA[$i] - [int]$bytesB[$i])
    }
    if ($bytesA.Length -gt 0) { $metrics.mae = $sum / $bytesA.Length } else { $metrics.mae = 0.0 }
    if ($null -ne $metrics.totalPixels) {
        $pixelCount = [long]$metrics.totalPixels
        $differing = 0
        for ($pixel = 0; $pixel -lt $pixelCount; $pixel++) {
            $offset = [int]($pixel * 4)
            $pixelDiffers = $false
            for ($channel = 0; $channel -lt 4; $channel++) {
                if ($bytesA[$offset + $channel] -ne $bytesB[$offset + $channel]) { $pixelDiffers = $true; break }
            }
            if ($pixelDiffers) { $differing++ }
        }
        $metrics.differingPixels = $differing
        if ($pixelCount -gt 0) { $metrics.differingPixelsPercent = [math]::Round(100.0 * $differing / $pixelCount, 6) }
    }
    return $metrics
}

function Get-VisualDifference([string]$RunRoot, [string]$RunId, [bool]$SameFrame, $Frame, $StageMetadata) {
    $stageSummaries = [ordered]@{}
    foreach ($stage in @('RASTER_HDR', 'COMPOSITE_PRE_TONEMAP', 'RASTER_SCENE_COLOR_COPY',
            'POST_ACES_PRE_PRESENT', 'FINAL_SDR_PRE_PRESENT')) {
        $metadata = if ($null -ne $StageMetadata -and $null -ne $StageMetadata[$stage]) { $StageMetadata[$stage] } else { $null }
        $stageSummaries[$stage] = [ordered]@{
            width = if ($null -ne $metadata) { [int]$metadata.width } else { $null }
            height = if ($null -ne $metadata) { [int]$metadata.height } else { $null }
            format = if ($null -ne $metadata) { [string]$metadata.format } else { $null }
        }
    }
    $rasterHdrVsComposite = Compare-Rgba16FStagePair $RunRoot 'RASTER_HDR' 'COMPOSITE_PRE_TONEMAP' $StageMetadata
    $compositeVsRasterCopy = Compare-Rgba16FStagePair $RunRoot 'COMPOSITE_PRE_TONEMAP' 'RASTER_SCENE_COLOR_COPY' $StageMetadata
    $postAcesVsFinalSdr = Compare-PngStagePair $RunRoot 'POST_ACES_PRE_PRESENT' 'FINAL_SDR_PRE_PRESENT' $StageMetadata
    return [ordered]@{
        schemaVersion = 1
        runId = $RunId
        sameFrame = $SameFrame
        frame = $Frame
        stages = $stageSummaries
        comparisons = [ordered]@{
            raster_hdr_vs_composite_pre_tonemap_observation = $rasterHdrVsComposite
            composite_pre_tonemap_vs_raster_scene_color_copy_observation = $compositeVsRasterCopy
            post_aces_vs_final_pre_present_observation = $postAcesVsFinalSdr
        }
        causalAttribution = 'NOT_ESTABLISHED_BY_PIXEL_DIFFERENCE'
        dxrVisualVerified = 'PARTIAL'
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
$d3d12LogPath = Join-Path $resolvedRunRoot 'harness-d3d12.log'
$d3d12Log = if (Test-Path -LiteralPath $d3d12LogPath -PathType Leaf) { Get-Content -LiteralPath $d3d12LogPath -Raw } else { '' }
$latestLightStart = $dxrLog.LastIndexOf('[COUCH-DXR-LIGHT-SYNC]')
$latestLightEnd = if ($latestLightStart -ge 0) { $dxrLog.IndexOf([char]10, $latestLightStart) } else { -1 }
$latestLightRecord = if ($latestLightStart -lt 0) { '' } elseif ($latestLightEnd -lt 0) { $dxrLog.Substring($latestLightStart) } else { $dxrLog.Substring($latestLightStart, $latestLightEnd - $latestLightStart) }
$productiveGameLight = $latestLightRecord -match 'sameFrame=1' -and $latestLightRecord -match 'match=YES'
$gameLightSyncClassification = if ($latestLightStart -lt 0) { 'NOT_RECORDED' }
    elseif ($latestLightRecord -match 'sameFrame=0') { 'TEMPORAL_MISMATCH' }
    elseif ($productiveGameLight) { 'PASS' }
    else { 'SAME_FRAME_MISMATCH' }

$manifestRendererBackend = 'D3D12'
$manifestVisualProfile = 2
$manifestVisualProfileName = 'HYBRID_RT'
if ($null -ne $resolvedManifest) {
    if ($null -ne $resolvedManifest.PSObject.Properties['rendererBackend'] -and
        -not [string]::IsNullOrWhiteSpace([string]$resolvedManifest.rendererBackend)) {
        $manifestRendererBackend = [string]$resolvedManifest.rendererBackend
    }
    if ($null -ne $resolvedManifest.PSObject.Properties['visualProfile']) {
        $manifestVisualProfile = [int]$resolvedManifest.visualProfile
    }
    if ($null -ne $resolvedManifest.PSObject.Properties['visualProfileName'] -and
        -not [string]::IsNullOrWhiteSpace([string]$resolvedManifest.visualProfileName)) {
        $manifestVisualProfileName = [string]$resolvedManifest.visualProfileName
    }
}
$dxrProfileMarker = "DXR profile active: '$manifestVisualProfileName' ($manifestVisualProfile)"
$backendConfirmed = $d3d12Log -match [regex]::Escape('[COUCH-D3D12] Backend = D3D12')
$dxrProfileActive = $d3d12Log -match [regex]::Escape($dxrProfileMarker)
$compositeExecuted = $false
$stageMetadata = @{}
$sameFrame = $false
$verifiedFrame = $null
$visualDifference = $null
$dxrExecutionStatus = 'NOT_RUN'
$dxrCompositionStatus = 'NOT_RUN'
$dxrVisualVerified = 'NOT_RUN'

if (Test-Path -LiteralPath (Join-Path $resolvedRunRoot 'RUN_FAIL') -PathType Leaf) { Add-Failure 'RUN_FAIL marker exists' }
if (-not (Test-Path -LiteralPath (Join-Path $resolvedRunRoot 'CAPTURE_COMPLETE') -PathType Leaf)) {
    Add-Failure 'CAPTURE_COMPLETE marker is missing'
}

if ($null -ne $resolvedManifest -and $null -ne $evidence -and $null -ne $request -and $null -ne $ack) {
    foreach ($object in @(@{ value = $evidence; label = 'Evidence' }, @{ value = $request; label = 'Capture request' }, @{ value = $ack.request; label = 'Acknowledgment request' })) {
        Test-RunValue $object.value 'scenario_id' $resolvedManifest.scenarioId "$($object.label) scenario_id"
        Test-RunValue $object.value 'view_id' $resolvedManifest.viewId "$($object.label) view_id"
        Test-RunValue $object.value 'truth_mode' $resolvedManifest.truthMode "$($object.label) truth_mode"
        Test-RunValue $object.value 'scene' $resolvedManifest.scene "$($object.label) scene"
        Test-RunValue $object.value 'room' $resolvedManifest.room "$($object.label) room"
    }
    Test-RunValue $ack 'schema_version' 2 'Acknowledgment schema version'
    Test-RunValue $ack 'sequence' 1 'Acknowledgment sequence'
    Test-RunValue $ack 'result' 'CAPTURE_COMPLETE' 'Acknowledgment result'
    Test-RunValue $ack 'capture_mask' 255 'Acknowledgment capture mask'
    Test-RunValue $evidence 'schema_version' 2 'Evidence schema version'
    Test-RunValue $evidence 'result' 'CAPTURE_COMPLETE' 'Evidence result'
    Test-RunValue $evidence 'capture_status' 'CAPTURE_COMPLETE' 'Capture status'
    Test-RunValue $evidence 'audio_output' 'MUTED_NULL_BACKEND' 'Audio output'
    Test-RunValue $evidence 'bloom_only' 'NOT_PRESENT' 'Bloom declaration'
    Test-RunValue $evidence 'temp_save' $true 'Temporary debug save'
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

    if ($dxrLog -match 'DEVICE_REMOVED|DEVICE_HUNG') { Add-Failure 'DXR device removal or hang marker is present' }
    foreach ($property in @('scenario_id', 'view_id', 'truth_mode', 'scene', 'room', 'sequence', 'game_frame', 'stable_frames',
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
        RASTER_SCENE_COLOR_COPY = 'raster_scene_color_copy'
        POST_ACES_PRE_PRESENT = 'post_aces_pre_present'
        FINAL_SDR_PRE_PRESENT = 'final_sdr_pre_present'
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
        $stageMetadata[$stage] = $metadata
        if ($null -eq $metadata) { continue }
        if ([string]$metadata.stage -cne $stage) { Add-Failure "Metadata stage does not match $stage" }
        if ([System.IO.Path]::GetFileName([string]$metadata.payload) -cne [System.IO.Path]::GetFileName($capturePath)) {
            Add-Failure "Metadata payload does not match acknowledged capture $stage"
        }
        if ([int]$metadata.width -le 0 -or [int]$metadata.height -le 0 -or [int]$metadata.frame -lt 0) {
            Add-Failure "Metadata dimensions or frame are invalid for $stage"
        }
        Test-RunValue $metadata 'frame' ([int]$ack.capture_frame) "$stage capture frame"
        if ([string]::IsNullOrWhiteSpace([string]$metadata.expectedColorSpace)) {
            Add-Failure "$stage expected color-space metadata is missing"
        }
        foreach ($property in @('scenario_id', 'view_id', 'truth_mode', 'scene', 'room', 'sequence')) {
            $expected = if ($property -eq 'sequence') { 1 } else { $request.$property }
            Test-RunValue $metadata.request $property $expected "$stage metadata request $property"
        }
        if ($stage -in @('POST_ACES_PRE_PRESENT', 'FINAL_SDR_PRE_PRESENT')) {
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
            if (-not (Test-PngReadable $capturePath ([int]$metadata.width) ([int]$metadata.height))) {
                Add-Failure "$stage PNG cannot be decoded at its recorded dimensions"
            }
            $captureStem = [System.IO.Path]::GetFileNameWithoutExtension($capturePath) -replace '_raw$', ''
            $previewPath = Join-Path (Split-Path -Parent $capturePath) ($captureStem + '_opaque_preview.png')
            if (-not (Test-Path -LiteralPath $previewPath -PathType Leaf) -or (Get-Item -LiteralPath $previewPath).Length -le 0) {
                Add-Failure "$stage opaque preview PNG is missing"
            } elseif (-not (Test-PngReadable $previewPath ([int]$metadata.width) ([int]$metadata.height))) {
                Add-Failure "$stage opaque preview PNG cannot be decoded at its recorded dimensions"
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
                if ($stage -in @('RASTER_HDR', 'COMPOSITE_PRE_TONEMAP', 'RASTER_SCENE_COLOR_COPY',
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
                $payloadStream = [System.IO.File]::OpenRead($capturePath)
                try {
                    if ($payloadStream.Length -ne $expectedBytes) { Add-Failure "$stage payload is not fully readable" }
                } finally { $payloadStream.Dispose() }
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
                    Test-RunValue $stats 'finite_signals' $true "$stage finite-signal status"
                    Test-RunValue $stats 'payload_complete' $true "$stage payload completion status"
                }
            }
        }
        foreach ($property in @('view_hash', 'projection_hash', 'player_state_hash')) {
            Test-RunValue $metadata.request $property $request.$property "$stage metadata request $property"
        }
        if ($failures.Count -eq $stageFailureCount) { ++$verifiedStageCount }
    }
    if ($ackCaptureProperties.Count -ne $stages.Count) { Add-Failure 'Acknowledgment capture set does not contain exactly eight stages' }

    $stageFrameValues = @()
    foreach ($stage in $stages.Keys) {
        $metadata = if ($null -ne $stageMetadata -and $null -ne $stageMetadata[$stage]) { $stageMetadata[$stage] } else { $null }
        if ($null -ne $metadata -and $null -ne $metadata.PSObject.Properties['frame']) {
            $stageFrameValues += [int]$metadata.frame
        }
    }
    $sameFrame = ($stageFrameValues.Count -eq $stages.Count) -and ((@($stageFrameValues | Select-Object -Unique)).Count -eq 1)
    if (-not $sameFrame) { Add-Failure 'Capture stages do not share the same frame' }
    $verifiedFrame = if ($stageFrameValues.Count -gt 0) { $stageFrameValues[0] } else { $null }
    if ($null -ne $verifiedFrame) {
        Test-RunValue $ack 'capture_frame' $verifiedFrame 'Acknowledgment capture frame'
        Test-RunValue $evidence 'capture_frame' $verifiedFrame 'Evidence capture frame'
    }
    $dxrFrame = if ($null -ne $ack.dxr_frame) { $ack.dxr_frame } else { $null }
    $dxrFrameMatches = $null -ne $dxrFrame -and $sameFrame -and
        [int]$dxrFrame.frame -eq [int]$verifiedFrame -and
        $dxrFrame.request_matches_capture -eq $true
    $dxrExecutionStatus = 'NOT_RUN'
    $dxrCompositionStatus = 'NOT_RUN'
    if ($dxrFrameMatches -and $dxrFrame.requested -and $dxrFrame.adapter_compatible -and
        $dxrFrame.tlas_valid -and $dxrFrame.dispatch_executed -and
        [uint64]$dxrFrame.dispatch_calls -gt 0 -and $dxrFrame.signals_produced -and
        [uint64]$dxrFrame.signal_outputs_written -gt 0) {
        $dxrExecutionStatus = 'PASS'
    } elseif ($dxrFrameMatches -and $dxrFrame.requested) {
        $dxrExecutionStatus = 'PARTIAL'
    }
    if ($dxrFrameMatches -and $dxrExecutionStatus -eq 'PASS' -and
        $dxrFrame.composition_executed -and [uint64]$dxrFrame.composite_draws -gt 0) {
        $dxrCompositionStatus = 'PASS'
    } elseif ($dxrFrameMatches -and $dxrFrame.requested) {
        $dxrCompositionStatus = 'PARTIAL'
    }
    $dxrVisualVerified = if ($dxrFrameMatches -and $dxrFrame.requested) { 'PARTIAL' } else { 'NOT_RUN' }
    $compositeExecuted = $dxrCompositionStatus -eq 'PASS'
    Test-RunValue $evidence 'dxr_execution_status' $dxrExecutionStatus 'DXR execution status'
    Test-RunValue $evidence 'dxr_composition_status' $dxrCompositionStatus 'DXR composition status'
    Test-RunValue $evidence 'dxr_visual_verified' $dxrVisualVerified 'DXR visual verification status'
    $runIdForReport = if ($null -ne $resolvedManifest) { [string]$resolvedManifest.runId } else { $null }
    try {
        $visualDifference = Get-VisualDifference $resolvedRunRoot $runIdForReport $sameFrame $verifiedFrame $stageMetadata
        $visualDifference | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath (Join-Path $resolvedRunRoot 'visual_difference.json') -Encoding utf8
    } catch {
        Add-Failure "Visual difference computation failed: $($_.Exception.Message)"
        $visualDifference = $null
    }
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

$captureStatus = if ($verifiedStageCount -eq 8 -and $sameFrame) { 'CAPTURE_COMPLETE' } else { 'PARTIAL' }
$presentationStatus = 'NOT_RUN'
$presentationEvidence = $null
$presentationPath = Join-Path $resolvedRunRoot 'presentation.evidence.jsonl'
if ((Test-Path -LiteralPath $presentationPath -PathType Leaf) -and ($null -ne $verifiedFrame)) {
    foreach ($line in Get-Content -LiteralPath $presentationPath) {
        try {
            $entry = $line | ConvertFrom-Json
            if ([int]$entry.forensic_frame -eq [int]$verifiedFrame) { $presentationEvidence = $entry }
        } catch { }
    }
    if ($null -ne $presentationEvidence) { $presentationStatus = [string]$presentationEvidence.dxgi_present_call }
}
$status = if ($failures.Count -eq 0) { 'PARTIAL' } else { 'FAIL' }
$result = [ordered]@{
    schemaVersion = 2
    status = $status
    captureStatus = $captureStatus
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
    rendererBackend = $manifestRendererBackend
    visualProfile = $manifestVisualProfile
    dxrProfileActive = [bool]$dxrProfileActive
    dxrExecutionStatus = $dxrExecutionStatus
    dxrCompositionStatus = $dxrCompositionStatus
    dxrVisualVerified = $dxrVisualVerified
    compositeExecuted = [bool]$compositeExecuted
    sameFrame = [bool]$sameFrame
    visualDifference = $visualDifference
    presentationStatus = $presentationStatus
    physicalPresentation = 'NOT_VERIFIABLE_BY_AUTOMATED_BACKBUFFER_CAPTURE'
    presentationEvidence = $presentationEvidence
    failures = @($failures)
}
$statusPath = Join-Path $resolvedRunRoot 'verification.status.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statusPath -Encoding utf8

if ($AsLibrary) {
    [pscustomobject]$result
    return
}
$result | ConvertTo-Json -Depth 8
if ($status -eq 'FAIL') { exit 1 }
exit 0
