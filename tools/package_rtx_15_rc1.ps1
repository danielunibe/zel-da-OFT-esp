$ErrorActionPreference = 'Stop'

$src = 'build\OCZ_RTX_14_UNREAL_VALIDATED_NIGHTLY'
$dest = 'build\OCZ_RTX_15_GLOBAL_LIGHTING_RC1'

if (-not (Test-Path $dest)) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
}

Write-Host "Packaging OCZ-RTX-15 RC1 into $dest..."
Copy-Item (Join-Path $src 'oot.o2r') $dest -Force
Copy-Item (Join-Path $src 'soh.o2r') $dest -Force
Copy-Item (Join-Path $src 'es.o2r') $dest -Force
Copy-Item (Join-Path $src 'gamecontrollerdb.txt') $dest -Force
Copy-Item (Join-Path $src 'shipofharkinian.json') $dest -Force
if (Test-Path (Join-Path $src 'Save')) {
    Copy-Item (Join-Path $src 'Save') $dest -Recurse -Force
}
Copy-Item 'source\shipwright\x64\Release\soh.exe' $dest -Force

$hlslHash = (Get-FileHash -Algorithm SHA256 'source\shipwright\libultraship\src\fast\shaders\dxr_shadow.hlsl').Hash
$dxilHash = (Get-FileHash -Algorithm SHA256 'source\shipwright\libultraship\include\fast\backends\dxr_shadow_cso.h').Hash
$exeHash = (Get-FileHash -Algorithm SHA256 (Join-Path $dest 'soh.exe')).Hash

$metadata = [ordered]@{
    BuildId = 'OCZ_RTX_15_GLOBAL_LIGHTING_RC1'
    Timestamp = (Get-Date).ToString('o')
    GitHead = '653f51c5ce3b726ea399d6cb9de8a037533d1526'
    SubmoduleLibultrashipHead = '17a0b7939bd05f5e617cef89457ca43774fc9a9f'
    HlslSha256 = $hlslHash
    DxilHeaderSha256 = $dxilHash
    ExeSha256 = $exeHash
    BuildConfiguration = 'Release'
    Compiler = 'MSVC 19.44 / CMake 3.31'
    DxcVersion = 'dxcompiler.dll 1.9.2602.17'
    ShaderAbiVersion = 4
    SemanticReconstructionContract = 'SemanticReconstructionContract.h (v1.0)'
    PhysicalImprovements = @(
        'Global Native Light Route: Universal capture of all active native point lights (torches, flames, poes, lanterns, fairies, magic) from LightContext without scene-isolated exceptions',
        'Physical BRDF Response: Attenuation 1 - (d/r)^2, GGX direct specular/diffuse, and raytraced visibility contact shadows for all active native point lights',
        'Energy Conservation: Elimination of double illumination between hybrid primary raster pass and secondary DXR reflection/GI bounces via splitPointFromRelight',
        'Clean Lifecycle: Dynamic registration, real-time positional updates, flicker handling, and zero-residual extinction upon flame death or room transitions'
    )
}

$jsonPath = Join-Path $dest 'build_metadata.json'
$metadata | ConvertTo-Json -Depth 5 | Set-Content $jsonPath -Encoding UTF8
Write-Host "RC1 candidate packaged successfully in $dest"
Write-Host "Executable SHA256: $exeHash"
