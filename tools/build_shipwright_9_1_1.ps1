param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$devRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $devRoot 'source\shipwright'
$buildRoot = Join-Path $devRoot 'build\shipwright-9.1.1-clean'
$vcpkgRoot = Join-Path $devRoot 'tools\vcpkg-9c147d5304087fe85104b776b019c45745fa9c11'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path (Join-Path $sourceRoot '.git')) -or -not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    throw 'Expected pinned source and vcpkg roots were not found under DEV_ROOT.'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1
if (-not $vsPath) { throw 'Visual Studio with x64 C++ tools was not found.' }
$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) { throw "VsDevCmd.bat not found: $vsDevCmd" }

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$configure = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set VCPKG_ROOT=$vcpkgRoot && cmake -S `"$sourceRoot`" -B `"$buildRoot`" -G `"Visual Studio 17 2022`" -T v143 -A x64 -DCMAKE_BUILD_TYPE=$Configuration -DVCPKG_ROOT=`"$vcpkgRoot`""
cmd /d /s /c $configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($ConfigureOnly) { exit 0 }

$build = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set VCPKG_ROOT=$vcpkgRoot && cmake --build `"$buildRoot`" --config $Configuration --target GenerateSohOtr -- /nodeReuse:false && cmake --build `"$buildRoot`" --config $Configuration -- /nodeReuse:false"
cmd /d /s /c $build
exit $LASTEXITCODE
