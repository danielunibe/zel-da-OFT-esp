$src = 'build\OCZ_RTX_19_ENGINE_PROGRAMMING_RC4'
$dest5 = 'build\OCZ_RTX_21_NATIVE_DXR_ENGINE_RC5'
$dest6 = 'build\OCZ_RTX_21_NATIVE_DXR_ENGINE_RC6'

foreach ($dest in @($dest5, $dest6)) {
    if (-not (Test-Path $dest)) {
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
    }
    New-Item -ItemType Directory -Force -Path "$dest\logs" | Out-Null
    New-Item -ItemType Directory -Force -Path "$dest\mods" | Out-Null
    New-Item -ItemType Directory -Force -Path "$dest\Save" | Out-Null

    Copy-Item "source\shipwright\x64\Release\soh.exe" "$dest\soh.exe" -Force
    Copy-Item "$src\oot.o2r" "$dest\oot.o2r" -Force
    Copy-Item "$src\soh.o2r" "$dest\soh.o2r" -Force
    Copy-Item "$src\es.o2r" "$dest\es.o2r" -Force
    Copy-Item "$src\shipofharkinian.json" "$dest\shipofharkinian.json" -Force
    Copy-Item "$src\imgui.ini" "$dest\imgui.ini" -Force
    Copy-Item "$src\timesplitdata.json" "$dest\timesplitdata.json" -Force
    if (Test-Path "$src\Save\*") {
        Copy-Item "$src\Save\*" "$dest\Save\" -Force
    }

    $hash = (Get-FileHash -Algorithm SHA256 "$dest\soh.exe").Hash
    $bytes = (Get-Item "$dest\soh.exe").Length
    Write-Host "Candidate Deployed: $dest\soh.exe ($bytes bytes, SHA256: $hash)"
}
