$ErrorActionPreference = 'Stop'

$src = 'build\OCZ_RTX_12_VISUAL_QUALITY'
$dest = 'build\OCZ_RTX_14_UNREAL_VALIDATED_NIGHTLY'

if (-not (Test-Path $dest)) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
}

Write-Host "Copying assets to $dest..."
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
    BuildId = 'OCZ_RTX_14_UNREAL_VALIDATED_NIGHTLY'
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
        'Temple of Time: Translucent additive sunbeam (Tex_0161D8) classified with MATERIAL_FLAG_DECORATIVE_BEAM, resolving white plane artifact without solid occlusion',
        'Lake Hylia: Underwater column gate (gLakeHyliaWaterTempleGateTex) reclassified from WATER to STONE (roughness 0.75, specular 0.25, reflectivity 0.04), eliminating water Fresnel reflection on stone',
        'Kokiri Forest: Slope-scaled contact bias in RayGenShadow preserving sharp contact shadows on ladders and actor feet'
    )
}

$jsonPath = Join-Path $dest 'build_metadata.json'
$metadata | ConvertTo-Json -Depth 5 | Set-Content $jsonPath -Encoding UTF8
Write-Host "Nightly candidate packaged successfully in $dest"
Write-Host "Executable SHA256: $exeHash"
