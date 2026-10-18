$dest = "build\OCZ_RTX_19_ENGINE_PROGRAMMING_RC4"
if (-not (Test-Path $dest)) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
}
New-Item -ItemType Directory -Force -Path "$dest\logs" | Out-Null
New-Item -ItemType Directory -Force -Path "$dest\mods" | Out-Null
New-Item -ItemType Directory -Force -Path "$dest\Save" | Out-Null

Copy-Item "source\shipwright\x64\Release\soh.exe" "$dest\soh.exe" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\oot.o2r" "$dest\oot.o2r" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\soh.o2r" "$dest\soh.o2r" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\es.o2r" "$dest\es.o2r" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\shipofharkinian.json" "$dest\shipofharkinian.json" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\imgui.ini" "$dest\imgui.ini" -Force
Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\timesplitdata.json" "$dest\timesplitdata.json" -Force
if (Test-Path "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\Save\*") {
    Copy-Item "build\OCZ_RTX_18_NATIVE_LIGHTS_RC3\Save\*" "$dest\Save\" -Force
}

$hash = (Get-FileHash -Algorithm SHA256 "$dest\soh.exe").Hash
$bytes = (Get-Item "$dest\soh.exe").Length
Write-Host "RC4 Candidate Deployed: $dest\soh.exe ($bytes bytes, SHA256: $hash)"
