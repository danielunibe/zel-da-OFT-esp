param(
    [string]$DxcPath = $env:DXC,
    [string]$OutputPath = "",
    [string]$TargetProfile = "lib_6_5",
    [string]$HlslVersion = "2021"
)

$ErrorActionPreference = 'Stop'

function Get-Sha256Bytes([byte[]]$Bytes) {
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($hasher.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant()
    } finally { $hasher.Dispose() }
}

function Get-Sha256File([string]$Path) {
    return Get-Sha256Bytes ([System.IO.File]::ReadAllBytes($Path))
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$shaderPath = Join-Path $repoRoot 'libultraship\src\fast\shaders\dxr_shadow.hlsl'
$headerPath = if ($OutputPath) { $OutputPath } else { Join-Path $repoRoot 'libultraship\include\fast\backends\dxr_shadow_cso.h' }
$headerDirectory = [System.IO.Path]::GetDirectoryName($headerPath)
[System.IO.Directory]::CreateDirectory($headerDirectory) | Out-Null

if (-not $DxcPath) {
    $dxcCommand = Get-Command dxc.exe -ErrorAction SilentlyContinue
    if ($dxcCommand) { $DxcPath = $dxcCommand.Source }
}

if (-not $DxcPath) {
    $wingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $dxcPackage = Get-ChildItem -LiteralPath $wingetPackages -Directory -Filter 'Microsoft.DirectX.ShaderCompiler*' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dxcPackage) {
        $candidate = Join-Path $dxcPackage.FullName 'bin\x64\dxc.exe'
        if (Test-Path -LiteralPath $candidate) { $DxcPath = $candidate }
    }
}

if (-not $DxcPath -or -not (Test-Path -LiteralPath $DxcPath)) {
    throw 'Could not locate dxc.exe. Set DXC or pass -DxcPath.'
}
if (-not (Test-Path -LiteralPath $shaderPath)) { throw "DXR shader source not found: $shaderPath" }

$shaderBytes = [System.IO.File]::ReadAllBytes($shaderPath)
$shaderSource = [System.Text.Encoding]::UTF8.GetString($shaderBytes)
# The library #includes the per-material models. Hash them together with the
# main source so an edit to WaterMaterial.hlsli or LavaMaterial.hlsli also
# invalidates the baked bytecode instead of shipping a stale DXIL.
$shaderIncludeNames = @('WaterMaterial.hlsli', 'LavaMaterial.hlsli')
$shaderDirectory = Split-Path -Parent $shaderPath
function Get-DxrShaderSourceHash {
    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add([System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($shaderPath)))
    foreach ($includeName in $script:shaderIncludeNames) {
        $includePath = Join-Path $script:shaderDirectory $includeName
        $parts.Add([System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($includePath)))
    }
    return Get-Sha256Bytes ([System.Text.Encoding]::UTF8.GetBytes(($parts -join '|')))
}
foreach ($includeName in $shaderIncludeNames) {
    $includePath = Join-Path $shaderDirectory $includeName
    if (-not (Test-Path -LiteralPath $includePath)) {
        throw "The DXR library is missing required include: $includeName"
    }
    if ($shaderSource -notmatch [regex]::Escape("#include `"$includeName`"")) {
        throw "dxr_shadow.hlsl does not include $includeName."
    }
}
$shaderHashBefore = Get-DxrShaderSourceHash
$libRoot = Join-Path $repoRoot 'libultraship'
$layoutPath = Join-Path $libRoot 'include\fast\backends\DxrAccelerationStructureManager.h'
$shadowRendererPath = Join-Path $libRoot 'src\fast\backends\DxrShadowRenderer.cpp'
$surfaceRendererPath = Join-Path $libRoot 'src\fast\backends\DxrSurfaceRenderer.cpp'
$layoutSource = [System.IO.File]::ReadAllText($layoutPath)
$shadowRendererSource = [System.IO.File]::ReadAllText($shadowRendererPath)
$surfaceRendererSource = [System.IO.File]::ReadAllText($surfaceRendererPath)
$abiMarker = [regex]::Match($shaderSource, '(?m)^\s*#define\s+COUCH_DXR_SHADER_ABI_VERSION\s+(\d+)\s*$')
if (-not $abiMarker.Success) { throw 'The DXR shader is missing its CPU/GPU ABI version marker.' }
$shaderAbiVersion = [uint32]$abiMarker.Groups[1].Value
$cpuAbiMarker = [regex]::Match($layoutSource, '(?m)kDxrShaderAbiVersion\s*=\s*(\d+)')
if (-not $cpuAbiMarker.Success -or [uint32]$cpuAbiMarker.Groups[1].Value -ne $shaderAbiVersion) {
    throw 'The DXR HLSL ABI marker does not match the CPU ABI declaration.'
}
if ($layoutSource -notmatch 'sizeof\(DxrHitVertex\)\s*==\s*56' -or
    $layoutSource -notmatch 'sizeof\(DxrInstanceMaterialData\)\s*==\s*128' -or
    $layoutSource -notmatch 'offsetof\(DxrInstanceMaterialData,\s*textureIndex1\)\s*==\s*80' -or
    $layoutSource -notmatch 'offsetof\(DxrInstanceMaterialData,\s*primitiveColor\)\s*==\s*96' -or
    $layoutSource -notmatch 'offsetof\(DxrInstanceMaterialData,\s*waterOptics\)\s*==\s*112' -or
    $shaderSource -notmatch 'instanceIndex\s*\*\s*128u' -or
    $shaderSource -notmatch 'vertexIndex\s*\*\s*56u' -or
    $shaderSource -notmatch 'Load4\(address\s*\+\s*80u\)' -or
    $shaderSource -notmatch 'Load4\(address\s*\+\s*96u\)' -or
    $shaderSource -notmatch 'Load4\(address\s*\+\s*112u\)') {
    throw 'CPU and HLSL hit-vertex/material record strides do not match ABI v5.'
}

$requiredExports = @('RayGenShadow', 'ClosestHitShadow', 'AnyHitShadow',
                     'RayGenReflection', 'ClosestHitReflection', 'AnyHitReflection')
foreach ($exportName in $requiredExports) {
    if ($shaderSource -notmatch ("(?s)\b" + [regex]::Escape($exportName) + "\s*\(")) {
        throw "The DXR library is missing required export source: $exportName"
    }
}

$versionLines = & $DxcPath --version 2>&1
if ($LASTEXITCODE -ne 0) { throw "Unable to query DXC version (exit $LASTEXITCODE)." }
$dxcVersion = ($versionLines | ForEach-Object { [string]$_ }) -join ' '
$dxcVersion = $dxcVersion.Trim()
if ([string]::IsNullOrWhiteSpace($dxcVersion)) { throw 'DXC returned an empty version string.' }
$compileFlags = "-T $TargetProfile -HV $HlslVersion"

$tempDxil = Join-Path $env:TEMP ("dxr-shadow-{0}.dxil" -f [guid]::NewGuid().ToString('N'))
$tempHeader = Join-Path $headerDirectory (([System.IO.Path]::GetFileName($headerPath)) + ".{0}.tmp" -f [guid]::NewGuid().ToString('N'))
try {
    & $DxcPath -T $TargetProfile -HV $HlslVersion -Fo $tempDxil $shaderPath
    if ($LASTEXITCODE -ne 0) { throw "dxc.exe failed with exit code $LASTEXITCODE" }
    if ((Get-DxrShaderSourceHash) -cne $shaderHashBefore) {
        throw 'The DXR shader changed during compilation; no bytecode header was published.'
    }

    $bytecode = [System.IO.File]::ReadAllBytes($tempDxil)
    if ($bytecode.Length -lt 32) { throw 'DXC produced an empty or truncated DXIL container.' }
    $containerMagic = [System.Text.Encoding]::ASCII.GetString($bytecode, 0, 4)
    if ($containerMagic -ne 'DXBC') { throw "DXC output is not a DXIL container (magic=$containerMagic)." }

    # Verify the compiled library, not only its HLSL source text. DXR global
    # root signatures are created in C++; check their register contract here
    # because this library intentionally does not embed a root signature blob.
    $reflectionLines = & $DxcPath -dumpbin $tempDxil 2>&1
    if ($LASTEXITCODE -ne 0) { throw "DXC reflection failed with exit code $LASTEXITCODE." }
    $reflection = ($reflectionLines | ForEach-Object { [string]$_ }) -join "`n"
    foreach ($exportName in $requiredExports) {
        if ($reflection -notmatch ("(?m)^define\s+void\s+.*\?" + [regex]::Escape($exportName) + "@@")) {
            throw "Compiled DXIL is missing required export: $exportName"
        }
    }

    $requiredBindings = @(
        @{ Name = 'FrameConstants'; Pattern = '(?m)^;\s*FrameConstants\s+cbuffer\s+.*\bcb0\s+1\s*$' },
        @{ Name = 'gScene'; Pattern = '(?m)^;\s*gScene\s+texture\s+.*\bT0\s+t0\s+1\s*$' },
        @{ Name = 'gDepthTexture'; Pattern = '(?m)^;\s*gDepthTexture\s+texture\s+.*\bT1\s+t1\s+1\s*$' },
        @{ Name = 'gHitVertexData'; Pattern = '(?m)^;\s*gHitVertexData\s+texture\s+.*\bT2\s+t2\s+1\s*$' },
        @{ Name = 'gHitIndexData'; Pattern = '(?m)^;\s*gHitIndexData\s+texture\s+.*\bT3\s+t3\s+1\s*$' },
        @{ Name = 'gInstanceMaterialData'; Pattern = '(?m)^;\s*gInstanceMaterialData\s+texture\s+.*\bT4\s+t4\s+1\s*$' },
        @{ Name = 'gMaterialTextures'; Pattern = '(?m)^;\s*gMaterialTextures\s+texture\s+.*\bT5\s+t5\s+4096\s*$' },
        @{ Name = 'gRasterEnvironment'; Pattern = '(?m)^;\s*gRasterEnvironment\s+texture\s+.*\bT6\s+t4101\s+1\s*$' },
        @{ Name = 'gRasterSoftShadow'; Pattern = '(?m)^;\s*gRasterSoftShadow\s+texture\s+.*\bT7\s+t4102\s+1\s*$' },
        @{ Name = 'gShadowMask'; Pattern = '(?m)^;\s*gShadowMask\s+UAV\s+.*\bU0\s+u0\s+1\s*$' },
        @{ Name = 'gSurfaceMask'; Pattern = '(?m)^;\s*gSurfaceMask\s+UAV\s+.*\bU1\s+u1\s+1\s*$' },
        @{ Name = 'gEmissiveMask'; Pattern = '(?m)^;\s*gEmissiveMask\s+UAV\s+.*\bU2\s+u2\s+1\s*$' },
        @{ Name = 'gIndirectMask'; Pattern = '(?m)^;\s*gIndirectMask\s+UAV\s+.*\bU3\s+u3\s+1\s*$' },
        @{ Name = 'gReflectionColor'; Pattern = '(?m)^;\s*gReflectionColor\s+UAV\s+.*\bU4\s+u4\s+1\s*$' },
        @{ Name = 'gPrimaryGateCounters'; Pattern = '(?m)^;\s*gPrimaryGateCounters\s+UAV\s+.*\bU5\s+u5\s+1\s*$' },
        @{ Name = 'gMaterialSampler'; Pattern = '(?m)^;\s*gMaterialSampler\s+sampler\s+.*\bS0\s+s0\s+1\s*$' },
        @{ Name = 'gMaterialPointSampler'; Pattern = '(?m)^;\s*gMaterialPointSampler\s+sampler\s+.*\bS1\s+s1\s+1\s*$' }
    )
    foreach ($binding in $requiredBindings) {
        if ($reflection -notmatch $binding.Pattern) {
            throw "Compiled DXIL resource/register contract is missing or changed: $($binding.Name)"
        }
    }

    $rootSignatureChecks = @(
        @{ Name = 'shadow t1 depth table'; Source = $shadowRendererSource; Pattern = 'srvRange\.BaseShaderRegister\s*=\s*1\s*;' },
        @{ Name = 'shadow u0 output table'; Source = $shadowRendererSource; Pattern = 'uavRange\.BaseShaderRegister\s*=\s*0\s*;' },
        @{ Name = 'shadow t2-t4 root SRVs'; Source = $shadowRendererSource; Pattern = 'ShaderRegister\s*=\s*2\s*\+\s*i\s*;' },
        @{ Name = 'shadow t5 texture table'; Source = $shadowRendererSource; Pattern = 'textureRange\.BaseShaderRegister\s*=\s*5\s*;' },
        @{ Name = 'surface t1 depth table'; Source = $surfaceRendererSource; Pattern = 'ranges\[0\]\.BaseShaderRegister\s*=\s*1\s*;' },
        @{ Name = 'surface u1-u5 output table'; Source = $surfaceRendererSource; Pattern = 'ranges\[1\]\.BaseShaderRegister\s*=\s*1\s*;' },
        @{ Name = 'surface t2-t4 root SRVs'; Source = $surfaceRendererSource; Pattern = 'ShaderRegister\s*=\s*parameter\s*-\s*1\s*;' },
        @{ Name = 'surface t5 texture table'; Source = $surfaceRendererSource; Pattern = 'ranges\[2\]\.BaseShaderRegister\s*=\s*5\s*;' },
        @{ Name = 'surface t4101-t4102 auxiliary textures'; Source = $surfaceRendererSource; Pattern = 'ranges\[3\]\.BaseShaderRegister\s*=\s*4101\s*;' },
        @{ Name = 'root signatures s0-s1 static samplers'; Source = ($shadowRendererSource + "`n" + $surfaceRendererSource); Pattern = 'ShaderRegister\s*=\s*samplerIndex\s*;' }
    )
    foreach ($check in $rootSignatureChecks) {
        if ($check.Source -notmatch $check.Pattern) {
            throw "C++ DXR root-signature contract is missing or changed: $($check.Name)"
        }
    }

    $dxilHash = Get-Sha256Bytes $bytecode
    $buildIdentityInput = @($shaderHashBefore, $dxilHash, $dxcVersion, $compileFlags, $shaderAbiVersion) -join '|'
    $buildIdentity = Get-Sha256Bytes ([System.Text.Encoding]::UTF8.GetBytes($buildIdentityInput))
    $escapedDxcVersion = $dxcVersion.Replace('\', '\\').Replace('"', '\"').Replace("`r", ' ').Replace("`n", ' ')

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('#pragma once')
    $lines.Add('')
    $lines.Add(('static constexpr unsigned int kDxrShadowBytecodeAbiVersion = {0}u;' -f $shaderAbiVersion))
    $lines.Add(('static constexpr char kDxrShadowShaderSourceSha256[] = "{0}";' -f $shaderHashBefore))
    $lines.Add(('static constexpr char kDxrShadowBytecodeDxilSha256[] = "{0}";' -f $dxilHash))
    $lines.Add(('static constexpr char kDxrShadowBytecodeBuildId[] = "{0}";' -f $buildIdentity))
    $lines.Add(('static constexpr char kDxrShadowBytecodeDxcVersion[] = "{0}";' -f $escapedDxcVersion))
    $lines.Add(('static constexpr char kDxrShadowBytecodeCompileFlags[] = "{0}";' -f $compileFlags))
    $lines.Add('')
    $lines.Add('static const unsigned char g_pDxrShadowBytecode[] = {')
    for ($offset = 0; $offset -lt $bytecode.Length; $offset += 16) {
        $end = [Math]::Min($offset + 16, $bytecode.Length)
        $values = for ($i = $offset; $i -lt $end; ++$i) { '0x{0:x2}' -f $bytecode[$i] }
        $lines.Add('  ' + ($values -join ', ') + ',')
    }
    $lines.Add('};')

    [System.IO.File]::WriteAllLines($tempHeader, $lines, [System.Text.Encoding]::ASCII)
    Move-Item -LiteralPath $tempHeader -Destination $headerPath -Force
    Write-Output "Generated $headerPath"
    Write-Output "ABI=$shaderAbiVersion sourceSha256=$shaderHashBefore dxilSha256=$dxilHash buildId=$buildIdentity"
    Write-Output "DXC=$dxcVersion flags=$compileFlags bytes=$($bytecode.Length)"
    Write-Output "Verified exports=$($requiredExports.Count) bindings=$($requiredBindings.Count) rootSignatureContracts=$($rootSignatureChecks.Count)"
}
finally {
    if (Test-Path -LiteralPath $tempDxil) { Remove-Item -LiteralPath $tempDxil -Force }
    if (Test-Path -LiteralPath $tempHeader) { Remove-Item -LiteralPath $tempHeader -Force }
}
