param(
    [string]$DxcPath = $env:DXC,
    [string]$OutputPath = ""
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$shaderPath = Join-Path $repoRoot 'libultraship\src\fast\shaders\dxr_shadow.hlsl'
$headerPath = if ($OutputPath) { $OutputPath } else { Join-Path $repoRoot 'libultraship\include\fast\backends\dxr_shadow_cso.h' }
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($headerPath)) | Out-Null

if (-not $DxcPath) {
    $dxcCommand = Get-Command dxc.exe -ErrorAction SilentlyContinue
    if ($dxcCommand) {
        $DxcPath = $dxcCommand.Source
    }
}

if (-not $DxcPath) {
    $wingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    $dxcPackage = Get-ChildItem -LiteralPath $wingetPackages -Directory -Filter 'Microsoft.DirectX.ShaderCompiler*' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dxcPackage) {
        $candidate = Join-Path $dxcPackage.FullName 'bin\x64\dxc.exe'
        if (Test-Path -LiteralPath $candidate) {
            $DxcPath = $candidate
        }
    }
}

if (-not $DxcPath -or -not (Test-Path -LiteralPath $DxcPath)) {
    throw 'Could not locate dxc.exe. Set DXC or pass -DxcPath.'
}
if (-not (Test-Path -LiteralPath $shaderPath)) {
    throw "DXR shader source not found: $shaderPath"
}

$tempDxil = Join-Path $env:TEMP ("dxr-shadow-{0}.dxil" -f [guid]::NewGuid().ToString('N'))
try {
    & $DxcPath -T lib_6_5 -HV 2021 -Fo $tempDxil $shaderPath
    if ($LASTEXITCODE -ne 0) {
        throw "dxc.exe failed with exit code $LASTEXITCODE"
    }

    $bytecode = [System.IO.File]::ReadAllBytes($tempDxil)
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('#pragma once')
    $lines.Add('')
    $lines.Add('static const unsigned char g_pDxrShadowBytecode[] = {')
    for ($offset = 0; $offset -lt $bytecode.Length; $offset += 16) {
        $end = [Math]::Min($offset + 16, $bytecode.Length)
        $values = for ($i = $offset; $i -lt $end; ++$i) { '0x{0:x2}' -f $bytecode[$i] }
        $lines.Add('  ' + ($values -join ', ') + ',')
    }
    $lines.Add('};')

    [System.IO.File]::WriteAllLines($headerPath, $lines, [System.Text.Encoding]::ASCII)
    Write-Output "Generated $headerPath ($($bytecode.Length) bytes)"
}
finally {
    if (Test-Path -LiteralPath $tempDxil) {
        Remove-Item -LiteralPath $tempDxil -Force
    }
}
