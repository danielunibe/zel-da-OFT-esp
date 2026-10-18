[CmdletBinding()]
param(
    [string]$RunScenario = $env:COUCH_DXR_SCENARIO,
    [string]$TruthMode = $env:COUCH_DXR_MODE,
    [string]$Executable,
    [string]$RuntimeDirectory,
    [switch]$Background,
    [switch]$PrimaryGateCounters,
    [switch]$TraceDxrMatrices,
    [switch]$EnableD3D12DebugLayer,
    [switch]$VisualAblation,
    [switch]$MonitorCapture,
    [ValidateRange(0, 3)][int]$RtxQuality = 3,
    [int]$PollSeconds = 60,
    [string]$RendererBackend = 'D3D12',
    [int]$VisualProfile = 2
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'file-hash.ps1')
if ($Background -and $MonitorCapture) { throw 'MonitorCapture requires a visible acceptance run.' }

function Stop-WithCode([string]$Code, [string]$Message, [int]$ExitCode) {
    [Console]::Error.WriteLine("$Code : $Message")
    exit $ExitCode
}

function Get-NvidiaGpuLoad {
    $nvidiaSmi = Get-Command 'nvidia-smi.exe' -ErrorAction SilentlyContinue
    if ($null -eq $nvidiaSmi) {
        return [pscustomobject]@{ Installed = $false; Available = $false; Error = 'NVIDIA_SMI_NOT_FOUND' }
    }
    $process = $null
    try {
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $nvidiaSmi.Source
        $startInfo.Arguments = '--query-gpu=memory.total,memory.used,utilization.gpu --format=csv,noheader,nounits'
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.CreateNoWindow = $true
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_START_FAILED' }
        }
        if (-not $process.WaitForExit(3000)) {
            $process.Kill()
            $process.WaitForExit(1000) | Out-Null
            return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_TIMEOUT' }
        }
        $output = $process.StandardOutput.ReadToEnd()
        if ($process.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($output)) {
            return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_QUERY_FAILED' }
        }
        $sample = @($output -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
        if ($sample.Count -eq 0) {
            return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_EMPTY_RESPONSE' }
        }
        $values = @($sample -split ',' | ForEach-Object { $_.Trim() })
        if ($values.Count -lt 3) {
            return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_RESPONSE_INVALID' }
        }
        return [pscustomobject]@{
            Installed = $true
            Available = $true
            Error = $null
            MemoryTotalMiB = [int]$values[0]
            MemoryUsedMiB = [int]$values[1]
            UtilizationPercent = [int]$values[2]
        }
    } catch {
        return [pscustomobject]@{ Installed = $true; Available = $false; Error = 'NVIDIA_SMI_QUERY_FAILED' }
    } finally {
        if ($null -ne $process) { $process.Dispose() }
    }
}

function Stop-ForGpuCapacity([string]$Code, [string]$Message, $GpuLoad) {
    $preflightId = (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + "_PREFLIGHT_$RunScenario`_$TruthMode"
    $preflightRoot = Join-Path (Join-Path $workspaceRoot 'reports\scene_harness') $preflightId
    New-Item -ItemType Directory -Force -Path $preflightRoot | Out-Null
    $harnessProcesses = @(
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like 'soh*.exe' } |
            ForEach-Object { [ordered]@{ pid = $_.ProcessId; executable = $_.Name } }
    )
    [ordered]@{
        schemaVersion = 1
        status = 'BLOCKED'
        blocker = $Code
        message = $Message
        scenarioId = $RunScenario
        viewId = $scenario.viewId
        truthMode = $TruthMode
        audioOutput = 'MUTED_NULL_BACKEND'
        gpuLoad = $GpuLoad
        existingGameProcesses = $harnessProcesses
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $preflightRoot 'preflight.status.json') -Encoding utf8
    Set-Content -LiteralPath (Join-Path $preflightRoot 'RUN_BLOCKED') -Value $Code -Encoding utf8
    Stop-WithCode $Code "$Message Report: $preflightRoot" $(if ($Code -eq 'HARNESS_GPU_CAPACITY_BUSY') { 39 } else { 40 })
}

if ([string]::IsNullOrWhiteSpace($RunScenario)) { $RunScenario = 'BASELINE_KOKIRI' }
if ([string]::IsNullOrWhiteSpace($TruthMode)) { $TruthMode = 'NORMAL' }
if ($VisualAblation -and ($TruthMode -ne 'NORMAL' -or $VisualProfile -ne 2)) {
    Stop-WithCode 'HARNESS_UNSUPPORTED_ABLATION_CONFIGURATION' 'VisualAblation requires NORMAL mode and HYBRID_RT profile 2.' 22
}

$validModes = @('NORMAL', 'HIT_MISS', 'WORLD_NORMAL', 'HIT_DISTANCE', 'INSTANCE_IDS', 'SYNTHETIC_LAMBERT', 'GAME_LIGHT_LAMBERT')
if ($TruthMode -notin $validModes) { Stop-WithCode 'HARNESS_UNKNOWN_TRUTH_MODE' "Unsupported TRUTH_MODE '$TruthMode'." 22 }
if ($PrimaryGateCounters -and $TruthMode -ne 'NORMAL') {
    Stop-WithCode 'HARNESS_GATE_REQUIRES_NORMAL_MODE' 'Gate diagnostics bypass AnyHit alpha and cannot be combined with a Truth measurement.' 22
}

$validRendererBackends = @('D3D12', 'D3D11')
if ($RendererBackend -cnotin $validRendererBackends) {
    Stop-WithCode 'HARNESS_UNKNOWN_RENDERER_BACKEND' "Unsupported RENDERER_BACKEND '$RendererBackend'." 21
}
$visualProfileName = if ($VisualProfile -eq 3) { 'PATH_TRACED_EXPERIMENTAL' } else { 'HYBRID_RT' }
if ($RendererBackend -ceq 'D3D11') {
    Stop-WithCode 'HARNESS_DXR_REQUIRES_D3D12' 'The DXR hybrid path requires the Direct3D 12 backend; the D3D11 backend cannot activate the DXR hybrid path.' 41
}
if ($VisualProfile -ne 2 -and $VisualProfile -ne 3) {
    Stop-WithCode 'HARNESS_PROFILE_REQUIRES_DXR' 'The DXR harness requires the HYBRID_RT (2) or PATH_TRACED_EXPERIMENTAL (3) visual profile.' 42
}
Write-Host "Renderer backend: $RendererBackend (forced via COUCH_D3D12=1)"
Write-Host "Visual profile: $VisualProfile ($visualProfileName; forced via COUCH_VISUAL_PROFILE=$VisualProfile)"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$manifestPath = Join-Path $PSScriptRoot 'scenarios\scenarios.v1.json'
$runtimeRoot = Join-Path $workspaceRoot 'runtime\build-test'
if (-not [string]::IsNullOrWhiteSpace($RuntimeDirectory)) {
    $runtimeRoot = [System.IO.Path]::GetFullPath($RuntimeDirectory)
}
$defaultExe = Join-Path $repoRoot 'x64\Release\soh.exe'
if ([string]::IsNullOrWhiteSpace($Executable)) { $Executable = $defaultExe }
$Executable = (Resolve-Path -LiteralPath $Executable -ErrorAction SilentlyContinue).Path
if ([string]::IsNullOrWhiteSpace($Executable)) { Stop-WithCode 'HARNESS_EXECUTABLE_NOT_FOUND' 'Build soh.exe before launching, or pass -Executable with the Release build path.' 23 }
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { Stop-WithCode 'HARNESS_MANIFEST_NOT_FOUND' $manifestPath 24 }
if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) { Stop-WithCode 'HARNESS_RUNTIME_NOT_FOUND' $runtimeRoot 25 }

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) { Stop-WithCode 'HARNESS_MANIFEST_VERSION_UNSUPPORTED' 'Expected schemaVersion=1.' 26 }
$scenario = @($manifest.scenarios | Where-Object { $_.id -ceq $RunScenario }) | Select-Object -First 1
if ($null -eq $scenario) { Stop-WithCode 'HARNESS_SCENARIO_NOT_FOUND' "Scenario '$RunScenario' is absent." 27 }
if ($scenario.status -cne 'MAPPED') { Stop-WithCode 'HARNESS_SCENARIO_UNMAPPED' "Scenario '$RunScenario' has no verified mapping." 28 }
if ($null -ne $scenario.lightingContract -and [bool]$scenario.lightingContract.requiredComparison.sameFrame) {
    if ($TruthMode -ne 'NORMAL' -or $VisualProfile -ne 2) {
        Stop-WithCode 'HARNESS_LIGHTING_CONTRACT_REQUIRES_NORMAL_HYBRID' `
            'This lighting contract needs the HYBRID_RT NORMAL capture so the off/on pair shares one scene frame.' 22
    }
    $VisualAblation = $true
}
$diagnostic = @($manifest.diagnosticModes | Where-Object { $_.id -ceq $TruthMode }) | Select-Object -First 1
if ($null -eq $diagnostic) { Stop-WithCode 'HARNESS_UNKNOWN_TRUTH_MODE' "Mode '$TruthMode' is not declared in schema v1." 29 }
if ($scenario.entrance -lt 0 -or $scenario.entrance -gt 65535 -or $scenario.expectedScene -lt 0 -or
    $scenario.expectedScene -gt 255 -or $scenario.expectedRoom -lt 0 -or $scenario.expectedRoom -gt 127) {
    Stop-WithCode 'HARNESS_INVALID_SCENARIO_MAPPING' 'Entrance/scene/room are outside supported ranges.' 30
}
$mapping = @($manifest.verifiedMappings | Where-Object {
    $_.id -ceq $scenario.mappingEvidenceId -and [int]$_.entrance -eq [int]$scenario.entrance -and
    [int]$_.scene -eq [int]$scenario.expectedScene -and [int]$_.room -eq [int]$scenario.expectedRoom
}) | Select-Object -First 1
if ($null -eq $mapping) { Stop-WithCode 'HARNESS_INVALID_ENTRANCE' 'Entrance/scene/room tuple is not verified in the manifest.' 30 }

$assetNames = @('es.o2r', 'oot.o2r', 'soh.o2r')
$executableIdentity = [ordered]@{
    path = $Executable
    sizeBytes = (Get-Item -LiteralPath $Executable).Length
    sha256 = (Get-HarnessFileHash -LiteralPath $Executable -Algorithm SHA256).Hash
}
$assetHashes = @{}
foreach ($assetName in $assetNames) {
    $assetPath = Join-Path $runtimeRoot $assetName
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) { Stop-WithCode 'HARNESS_OTR_NOT_FOUND' $assetPath 31 }
    $assetHashes[$assetName] = (Get-HarnessFileHash -LiteralPath $assetPath -Algorithm SHA256).Hash
}
$allO2rBefore = @{}
foreach ($assetFile in Get-ChildItem -LiteralPath $runtimeRoot -Filter '*.o2r' -File) {
    $allO2rBefore[$assetFile.Name] = (Get-HarnessFileHash -LiteralPath $assetFile.FullName -Algorithm SHA256).Hash
}

$gpuLoad = Get-NvidiaGpuLoad
if ($gpuLoad.Installed -and -not $gpuLoad.Available) {
    Stop-ForGpuCapacity 'HARNESS_GPU_CAPACITY_UNKNOWN' "Could not verify NVIDIA GPU capacity ($($gpuLoad.Error)); refusing to start another renderer." $gpuLoad
}
if ($gpuLoad.Available -and $gpuLoad.MemoryTotalMiB -gt 0) {
    $gpuMemoryPercent = [math]::Round(100.0 * $gpuLoad.MemoryUsedMiB / $gpuLoad.MemoryTotalMiB, 1)
    if ($gpuMemoryPercent -ge 85.0 -or $gpuLoad.UtilizationPercent -ge 95) {
        $message = "NVIDIA GPU is busy: $($gpuLoad.MemoryUsedMiB)/$($gpuLoad.MemoryTotalMiB) MiB VRAM ($gpuMemoryPercent%), utilization $($gpuLoad.UtilizationPercent)%. Free GPU capacity before starting another renderer."
        Stop-ForGpuCapacity 'HARNESS_GPU_CAPACITY_BUSY' $message $gpuLoad
    }
}

$originalSaveRoot = Join-Path $runtimeRoot 'Save'
if (-not (Test-Path -LiteralPath $originalSaveRoot -PathType Container)) { Stop-WithCode 'HARNESS_SAVE_ROOT_MISSING' $originalSaveRoot 32 }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$runId = "${stamp}_$RunScenario`_$TruthMode"
$runRoot = Join-Path (Join-Path $workspaceRoot 'reports\scene_harness') $runId
$sandboxSaveRoot = Join-Path $runRoot 'sandbox\Save'
New-Item -ItemType Directory -Force -Path $sandboxSaveRoot | Out-Null
$originalSaveCanonical = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $originalSaveRoot).Path).TrimEnd('\')
$sandboxSaveCanonical = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $sandboxSaveRoot).Path).TrimEnd('\')
if ([string]::Equals($originalSaveCanonical, $sandboxSaveCanonical, [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-WithCode 'HARNESS_SAVE_ISOLATION_NOT_PROVEN' 'Sandbox resolves to the original Save directory.' 34
}

$originalHashesBefore = @{}
foreach ($saveFile in Get-ChildItem -LiteralPath $originalSaveRoot -Filter '*.sav' -File) {
    $originalHashesBefore[$saveFile.Name] = (Get-HarnessFileHash -LiteralPath $saveFile.FullName -Algorithm SHA256).Hash
}

Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $runRoot 'manifest.input.json')
$manifestResolvedLauncher = [ordered]@{
    schemaVersion = 1
    runId = $runId
    scenarioId = $RunScenario
    viewId = $scenario.viewId
    truthMode = $TruthMode
    backendTruthMode = [int]$diagnostic.backendMode
    executableIdentity = $executableIdentity
    rendererBackend = $RendererBackend
    visualProfile = [int]$VisualProfile
    visualProfileName = $visualProfileName
    d3d12DebugLayerRequested = [bool]$EnableD3D12DebugLayer
    background = [bool]$Background
    primaryGateCounters = [bool]$PrimaryGateCounters
    diagnosticAnyHitAlphaBypass = [bool]$PrimaryGateCounters
    dxrMatrixTrace = [bool]$TraceDxrMatrices
    visualAblation = [bool]$VisualAblation
    monitorCapture = [bool]$MonitorCapture
    rtxQuality = $RtxQuality
    presentation = if ($Background) { 'SKIPPED_BACKGROUND' } else { 'NORMAL_DXGI' }
    entrance = ('0x{0:X3}' -f [int]$scenario.entrance)
    scene = ('0x{0:X}' -f [int]$scenario.expectedScene)
    room = [int]$scenario.expectedRoom
    audioOutput = 'MUTED_NULL_BACKEND'
    originalSaveRoot = $originalSaveCanonical
    sandboxSaveRoot = $sandboxSaveCanonical
    originalHashesBefore = $originalHashesBefore
    saveMode = 'TEMP_DEBUG_SAVE'
    assetHashes = $assetHashes
    gpuLoadAtLaunch = $gpuLoad
    newOtrGenerated = $false
}
$manifestResolvedLauncher | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'manifest.launcher.json') -Encoding utf8

$harnessEnvNames = @('COUCH_DXR_HARNESS', 'COUCH_DXR_CAPTURE_SEQUENCE', 'COUCH_DXR_MANIFEST', 'COUCH_DXR_OUTPUT',
    'COUCH_DXR_SAVE_ROOT', 'COUCH_DXR_SCENARIO', 'COUCH_DXR_MODE', 'COUCH_DXR_TRUTH_MODE',
    'COUCH_DXR_TARGET_SCENE', 'COUCH_FORENSIC_CAPTURES', 'COUCH_DXR_TRUTH_SEQUENCE',
    'COUCH_DXR_TRUTH_V2_SEQUENCE', 'COUCH_DXR_DIAGNOSTIC_SEQUENCE', 'COUCH_D3D12_DEBUG',
    'COUCH_D3D12', 'COUCH_VISUAL_PROFILE', 'COUCH_DXR_BACKGROUND', 'COUCH_DIAGNOSTICS_PATH',
    'COUCH_DXR_GPU_GATE_COUNTERS', 'COUCH_DXR_PRIMARY_GATE_COUNTERS', 'COUCH_DXR_MATRIX_TRACE',
    'COUCH_DXR_FORCE_ANY_HIT_COVERAGE', 'COUCH_DXR_GATE_DIAGNOSTIC', 'COUCH_DXR_VISUAL_ABLATION',
    'COUCH_DXR_MONITOR_CAPTURE', 'COUCH_RTX_QUALITY', 'COUCH_DXR_NAVI_PILOT', 'COUCH_DXR_NAVI_ISOLATION',
    'COUCH_DXR_NAVI_POINT_GAIN', 'COUCH_DXR_NAVI_POINT_TINT', 'COUCH_DXR_NIGHT_MODE', 'COUCH_DXR_NAVI_SOLE_LIGHT', 'COUCH_DXR_ROUTE',
    'COUCH_DXR_LAVA_LIGHTING', 'COUCH_DXR_LAVA_EMISSION', 'COUCH_DXR_LAVA_LIGHT_GAIN')
$savedHarnessEnv = @{}
foreach ($name in $harnessEnvNames) { $savedHarnessEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$env:COUCH_DXR_HARNESS = '1'
$env:COUCH_DXR_NAVI_PILOT = if ($scenario.naviPilot) { '1' } else { '0' }
$env:COUCH_DXR_NAVI_ISOLATION = if ($scenario.naviPilot -and $scenario.naviPilot.isolate -eq $false) { '0' } else { '1' }
$naviPointGain = 1.0
if ($scenario.naviPilot -and $null -ne $scenario.naviPilot.pointLightGain) {
    $naviPointGain = [double]$scenario.naviPilot.pointLightGain
    if ([double]::IsNaN($naviPointGain) -or [double]::IsInfinity($naviPointGain) -or
        $naviPointGain -lt 0.0 -or $naviPointGain -gt 128.0) {
        throw 'naviPilot.pointLightGain must be finite and between 0 and 128.'
    }
}
$env:COUCH_DXR_NAVI_POINT_GAIN = $naviPointGain.ToString('R', [System.Globalization.CultureInfo]::InvariantCulture)
$naviPointTint = @(1.0, 1.0, 1.0)
if ($scenario.naviPilot -and $null -ne $scenario.naviPilot.pointLightTint) {
    $configuredTint = $scenario.naviPilot.pointLightTint
    if ($configuredTint -isnot [array] -or $configuredTint.Count -ne 3) {
        throw 'naviPilot.pointLightTint must be an RGB array with three values between 0 and 1.'
    }
    $naviPointTint = @($configuredTint | ForEach-Object { [double]$_ })
    foreach ($channel in $naviPointTint) {
        if ([double]::IsNaN($channel) -or [double]::IsInfinity($channel) -or
            $channel -lt 0.0 -or $channel -gt 1.0) {
            throw 'naviPilot.pointLightTint must be an RGB array with three values between 0 and 1.'
        }
    }
}
$naviPositionOffset = $null
if ($scenario.naviPilot -and $null -ne $scenario.naviPilot.positionOffset) {
    $configuredOffset = $scenario.naviPilot.positionOffset
    if ($configuredOffset -isnot [array] -or $configuredOffset.Count -ne 3) {
        throw 'naviPilot.positionOffset must be a finite XYZ array with three values.'
    }
    $naviPositionOffset = @($configuredOffset | ForEach-Object { [double]$_ })
    foreach ($axis in $naviPositionOffset) {
        if ([double]::IsNaN($axis) -or [double]::IsInfinity($axis)) {
            throw 'naviPilot.positionOffset must be a finite XYZ array with three values.'
        }
    }
}
$env:COUCH_DXR_NAVI_POINT_TINT = ($naviPointTint | ForEach-Object {
    ([double]$_).ToString('R', [System.Globalization.CultureInfo]::InvariantCulture)
}) -join ','
$isNightScenario = ($scenario.nightFlag -eq 1 -or $scenario.nightMode -eq $true)
$isNaviSoleLight = ($scenario.naviSoleLight -eq $true)
$env:COUCH_DXR_NIGHT_MODE = if ($isNightScenario -or $isNaviSoleLight) { '1' } else { '0' }
$env:COUCH_DXR_NAVI_SOLE_LIGHT = if ($isNaviSoleLight) { '1' } else { '0' }
$route = if ($isNaviSoleLight) { 'ISOLATED_LIGHT_LAB' } elseif ($diagnostic.backendMode -ne 0) { 'AOV_DEBUG' } else { 'PRODUCTION' }
$env:COUCH_DXR_ROUTE = $route
Write-Host "[COUCH-HARNESS-ROUTE] Scenario=$RunScenario Route=$route SoleLight=$env:COUCH_DXR_NAVI_SOLE_LIGHT NightMode=$env:COUCH_DXR_NIGHT_MODE"
$env:COUCH_DXR_CAPTURE_SEQUENCE = '1'
$env:COUCH_DXR_MANIFEST = $manifestPath
$env:COUCH_DXR_OUTPUT = $runRoot
$env:COUCH_DXR_SAVE_ROOT = $sandboxSaveCanonical
$env:COUCH_DXR_SCENARIO = $RunScenario
$env:COUCH_DXR_MODE = $TruthMode
$env:COUCH_DXR_TRUTH_MODE = [string][int]$diagnostic.backendMode
$env:COUCH_DXR_TARGET_SCENE = ('0x{0:X}' -f [int]$scenario.expectedScene)
$env:COUCH_FORENSIC_CAPTURES = '1'
$env:COUCH_DXR_TRUTH_SEQUENCE = '0'
$env:COUCH_DXR_TRUTH_V2_SEQUENCE = '0'
$env:COUCH_DXR_DIAGNOSTIC_SEQUENCE = '0'
$env:COUCH_DXR_GPU_GATE_COUNTERS = if ($PrimaryGateCounters) { '1' } else { '0' }
$env:COUCH_DXR_PRIMARY_GATE_COUNTERS = if ($PrimaryGateCounters) { '1' } else { '0' }
$env:COUCH_DXR_MATRIX_TRACE = if ($TraceDxrMatrices) { '1' } else { '0' }
$env:COUCH_DXR_FORCE_ANY_HIT_COVERAGE = '0'
$env:COUCH_DXR_GATE_DIAGNOSTIC = '0'
$env:COUCH_D3D12_DEBUG = if ($EnableD3D12DebugLayer) { '1' } else { '0' }
$env:COUCH_D3D12 = '1'
$env:COUCH_VISUAL_PROFILE = [string]$VisualProfile
$env:COUCH_DXR_BACKGROUND = if ($Background) { '1' } else { '0' }
$env:COUCH_DIAGNOSTICS_PATH = Join-Path $runRoot 'harness-backend.log'
$env:COUCH_DXR_VISUAL_ABLATION = if ($VisualAblation) { '1' } else { '0' }
$env:COUCH_DXR_MONITOR_CAPTURE = if ($MonitorCapture) { '1' } else { '0' }
$env:COUCH_RTX_QUALITY = [string]$RtxQuality

$stdoutPath = Join-Path $runRoot 'stdout.log'
$stderrPath = Join-Path $runRoot 'stderr.log'
 $launchFailure = $null
try {
    $launchWindowStyle = if ($MonitorCapture) { 'Normal' } else { 'Hidden' }
    $process = Start-Process -FilePath $Executable -WorkingDirectory $runtimeRoot -PassThru `
        -WindowStyle $launchWindowStyle `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
} catch {
    $launchFailure = $_.Exception.Message
} finally {
    foreach ($name in $harnessEnvNames) {
        $oldValue = $savedHarnessEnv[$name]
        if ($null -eq $oldValue) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable($name, $oldValue, 'Process') }
    }
}
if ($launchFailure) { Stop-WithCode 'HARNESS_PROCESS_START_FAILED' $launchFailure 36 }

try {
$started = Get-Date
# OCZ-RECOVERY-02: 300s was too short for slow scenes (Goron City renders ~1 fps,
# needs camera warmup 120 + stable 30 game frames before the capture request).
$maximumPollSeconds = 900
$deadline = $started.AddSeconds([Math]::Max(1, [Math]::Min($PollSeconds, $maximumPollSeconds)))
$startupDeadline = $started.AddSeconds(20)
$windowReady = $false
$statusPath = Join-Path $runRoot 'launcher.status.json'
$d3d12LogPath = Join-Path $runRoot 'harness-d3d12.log'
$backendDeviceRemoved = $false
while ((Get-Date) -lt $deadline) {
    # Present is held before the next game update can create CAPTURE_COMPLETE.
    # Capture as soon as its evidence exists, then release that same GPU frame.
    if ($MonitorCapture -and
        (Test-Path -LiteralPath (Join-Path $runRoot 'presentation.evidence.jsonl')) -and
        -not (Test-Path -LiteralPath (Join-Path $runRoot 'monitor.capture.complete'))) {
        & (Join-Path $PSScriptRoot 'capture-client.ps1') -ProcessId $process.Id -RunRoot $runRoot
    }
    if (-not $windowReady -and (Get-Date) -lt $startupDeadline -and -not $process.HasExited) {
        $process.Refresh()
        if ($Background) {
            $windowReady = (Test-Path -LiteralPath (Join-Path $runRoot 'harness-backend.log') -PathType Leaf) -and
                [bool](Select-String -LiteralPath (Join-Path $runRoot 'harness-backend.log') -SimpleMatch '[COUCH-HARNESS][WINDOW_HIDDEN] visible=0 foreground=0' -Quiet)
        } else {
            $windowReady = -not [string]::IsNullOrWhiteSpace($process.MainWindowTitle)
        }
    }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'RUN_PASS') -PathType Leaf) { break }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -PathType Leaf) { break }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_COMPLETE') -PathType Leaf) {
        if ($MonitorCapture -and -not (Test-Path -LiteralPath (Join-Path $runRoot 'monitor.capture.complete'))) {
            & (Join-Path $PSScriptRoot 'capture-client.ps1') -ProcessId $process.Id -RunRoot $runRoot
        }
        if (-not $process.HasExited) {
            $process.WaitForExit(8000) | Out-Null
        }
        break
    }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_PARTIAL') -PathType Leaf) {
        if (-not $process.HasExited) {
            $process.WaitForExit(8000) | Out-Null
        }
        break
    }
    if (-not $windowReady -and (Get-Date) -ge $startupDeadline) {
        Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_STARTUP_TIMEOUT' -Encoding utf8
        break
    }
    if ((Test-Path -LiteralPath $d3d12LogPath -PathType Leaf) -and
        [bool](Select-String -LiteralPath $d3d12LogPath -SimpleMatch '[COUCH-D3D12][DEVICE_REMOVED]' -Quiet)) {
        $backendDeviceRemoved = $true
        Remove-Item -LiteralPath (Join-Path $runRoot 'RUN_PASS') -Force -ErrorAction SilentlyContinue
        Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_D3D12_DEVICE_REMOVED' -Encoding utf8
        break
    }
    if ($process.HasExited) { break }
    Start-Sleep -Milliseconds 300
}

if (-not $backendDeviceRemoved -and (Test-Path -LiteralPath $d3d12LogPath -PathType Leaf) -and
    [bool](Select-String -LiteralPath $d3d12LogPath -SimpleMatch '[COUCH-D3D12][DEVICE_REMOVED]' -Quiet)) {
    $backendDeviceRemoved = $true
    Remove-Item -LiteralPath (Join-Path $runRoot 'RUN_PASS') -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_D3D12_DEVICE_REMOVED' -Encoding utf8
}
$deviceRemovedLogLine = $null
if ($backendDeviceRemoved) {
    $deviceRemovedLogLine = Select-String -LiteralPath $d3d12LogPath -SimpleMatch '[COUCH-D3D12][DEVICE_REMOVED]' |
        Select-Object -Last 1 -ExpandProperty Line
    [ordered]@{
        status = 'FAIL'
        blocker = 'HARNESS_D3D12_DEVICE_REMOVED'
        log = 'harness-d3d12.log'
        evidenceLine = $deviceRemovedLogLine
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'd3d12-device.status.json') -Encoding utf8
}

$originalHashesAfter = @{}
foreach ($saveFile in Get-ChildItem -LiteralPath $originalSaveRoot -Filter '*.sav' -File) {
    $originalHashesAfter[$saveFile.Name] = (Get-HarnessFileHash -LiteralPath $saveFile.FullName -Algorithm SHA256).Hash
}
$originalUnchanged = $true
foreach ($saveName in $originalHashesBefore.Keys) {
    if (-not $originalHashesAfter.ContainsKey($saveName) -or $originalHashesBefore[$saveName] -cne $originalHashesAfter[$saveName]) { $originalUnchanged = $false }
}
if ($originalHashesAfter.Count -ne $originalHashesBefore.Count) { $originalUnchanged = $false }
$saveIntegrity = [ordered]@{
    status = if ($originalUnchanged) { 'PASS' } else { 'FAIL' }
    originalHashesBefore = $originalHashesBefore
    originalHashesAfter = $originalHashesAfter
    temporarySaveRoot = $sandboxSaveCanonical
}
$saveIntegrity | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'save-integrity.json') -Encoding utf8
Add-Content -LiteralPath (Join-Path $runRoot 'harness.log') -Value (([ordered]@{ marker = '[HARNESS_SAVE_CHECK]'; status = $saveIntegrity.status;
    originalHashesBefore = $originalHashesBefore; originalHashesAfter = $originalHashesAfter } | ConvertTo-Json -Compress -Depth 6)) -Encoding utf8
if (Test-Path -LiteralPath (Join-Path $runRoot 'evidence.json') -PathType Leaf) {
    $gameEvidence = Get-Content -LiteralPath (Join-Path $runRoot 'evidence.json') -Raw | ConvertFrom-Json
    $gameEvidence | Add-Member -NotePropertyName saveIntegrity -NotePropertyValue $saveIntegrity -Force
    $gameEvidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $runRoot 'evidence.json') -Encoding utf8
}

$d3d12DebugLayerStatus = 'NOT_RUN_DEBUG_LAYER_OFF'
if ($EnableD3D12DebugLayer) {
    $d3d12Log = Join-Path $runRoot 'harness-d3d12.log'
    $debugLayerWasEnabled = (Test-Path -LiteralPath $d3d12Log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $d3d12Log -SimpleMatch '[COUCH-D3D12] Debug Layer ENABLED' -Quiet)
    $d3d12DebugLayerStatus = if ($debugLayerWasEnabled) { 'DEBUG_LAYER_ENABLED_LOG_CONFIRMED' }
                             else { 'DEBUG_LAYER_REQUESTED_BUT_NOT_CONFIRMED' }
}
[ordered]@{
    status = $d3d12DebugLayerStatus
    requested = [bool]$EnableD3D12DebugLayer
    backendLog = if (Test-Path -LiteralPath (Join-Path $runRoot 'harness-d3d12.log')) { 'harness-d3d12.log' } else { $null }
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'd3d12-debug-layer.status.json') -Encoding utf8

$allO2rAfter = @{}
foreach ($assetFile in Get-ChildItem -LiteralPath $runtimeRoot -Filter '*.o2r' -File) {
    $allO2rAfter[$assetFile.Name] = (Get-HarnessFileHash -LiteralPath $assetFile.FullName -Algorithm SHA256).Hash
}
$newO2r = @($allO2rAfter.Keys | Where-Object { -not $allO2rBefore.ContainsKey($_) })
$changedO2r = @($allO2rBefore.Keys | Where-Object {
    -not $allO2rAfter.ContainsKey($_) -or $allO2rBefore[$_] -cne $allO2rAfter[$_]
})
if ($newO2r.Count -gt 0 -or $changedO2r.Count -gt 0) {
    Remove-Item -LiteralPath (Join-Path $runRoot 'RUN_PASS') -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_OTR_CHANGED' -Encoding utf8
    [ordered]@{ status = 'FAIL'; blocker = 'HARNESS_OTR_CHANGED'; newO2r = $newO2r; changedO2r = $changedO2r;
        pid = $process.Id; runId = $runId } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $statusPath -Encoding utf8
    Stop-WithCode 'HARNESS_OTR_CHANGED' 'An O2R archive was created, removed, or modified during the run.' 40
}

if (-not $originalUnchanged) {
    Remove-Item -LiteralPath (Join-Path $runRoot 'RUN_PASS') -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_ORIGINAL_SAVE_CHANGED' -Encoding utf8
    [ordered]@{ status = 'FAIL'; blocker = 'HARNESS_ORIGINAL_SAVE_CHANGED'; pid = $process.Id; runId = $runId } |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding utf8
    Stop-WithCode 'HARNESS_ORIGINAL_SAVE_CHANGED' 'An original save hash changed during the run.' 37
}

$backgroundCleanup = 'NOT_REQUESTED'
if ($Background -or $MonitorCapture) {
    if (-not $process.HasExited) {
        # Process did not exit on its own within deadline; fallback kill
        $process.Kill()
        $process.WaitForExit(5000) | Out-Null
        $backgroundCleanup = if ($process.HasExited) { 'FALLBACK_KILL_AFTER_TIMEOUT' } else { 'STOP_FAILED' }
    } else {
        $backgroundCleanup = 'GRACEFUL_EXIT_CONFIRMED'
    }
}

$evidenceVerification = 'NOT_RUN_CAPTURE_INCOMPLETE'
$evidenceVerificationFailed = $false
if (Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_COMPLETE') -PathType Leaf) {
    $verifyScript = Join-Path $PSScriptRoot 'verify-run.ps1'
    try {
        $verification = & $verifyScript -RunRoot $runRoot -OriginalSaveRoot $originalSaveRoot -AsLibrary
        if ($null -eq $verification -or $verification.status -ceq 'FAIL') {
            $evidenceVerificationFailed = $true
            $evidenceVerification = 'FAIL'
            Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_EVIDENCE_VERIFY_FAILED' -Encoding utf8
            [ordered]@{ status = 'FAIL'; blocker = 'HARNESS_EVIDENCE_VERIFY_FAILED'; verification = $verification;
                runId = $runId; pid = $process.Id } | ConvertTo-Json -Depth 8 |
                Set-Content -LiteralPath (Join-Path $runRoot 'evidence-verification.status.json') -Encoding utf8
        } else {
            $evidenceVerification = [string]$verification.status
        }
    } catch {
        $evidenceVerificationFailed = $true
        $evidenceVerification = 'FAIL'
        Set-Content -LiteralPath (Join-Path $runRoot 'RUN_FAIL') -Value 'HARNESS_EVIDENCE_VERIFY_FAILED' -Encoding utf8
        [ordered]@{ status = 'FAIL'; blocker = 'HARNESS_EVIDENCE_VERIFY_FAILED'; error = $_.Exception.Message;
            runId = $runId; pid = $process.Id } | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (Join-Path $runRoot 'evidence-verification.status.json') -Encoding utf8
    }
} elseif (Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_PARTIAL') -PathType Leaf) {
    $evidenceVerification = 'PARTIAL'
}

$launcherStatus = if (Test-Path -LiteralPath (Join-Path $runRoot 'RUN_PASS')) { 'PASS' }
                  elseif (Test-Path -LiteralPath (Join-Path $runRoot 'RUN_FAIL')) { 'FAIL' }
                  elseif ((Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_COMPLETE')) -and $evidenceVerification -eq 'PARTIAL') { 'PARTIAL' }
                  elseif (Test-Path -LiteralPath (Join-Path $runRoot 'CAPTURE_PARTIAL')) { 'PARTIAL' }
                  elseif ($process.HasExited) { 'PROCESS_EXITED' }
                  elseif ($Background -or $MonitorCapture) { 'POLL_TIMEOUT_BACKGROUND_STOPPED' }
                  else { 'POLL_TIMEOUT_PROCESS_LEFT_OPEN' }
[ordered]@{
    status = $launcherStatus
    runId = $runId
    pid = $process.Id
    executable = $Executable
    workingDirectory = $runtimeRoot
    runRoot = $runRoot
    processExited = $process.HasExited
    background = [bool]$Background
    backgroundCleanup = $backgroundCleanup
    backendDeviceRemoved = $backendDeviceRemoved
    evidenceVerification = $evidenceVerification
    originalSaveUnchanged = $originalUnchanged
    originalHashesBefore = $originalHashesBefore
    originalHashesAfter = $originalHashesAfter
    newOtrGenerated = $false
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statusPath -Encoding utf8

Write-Output "DXR_SCENARIO_HARNESS=$launcherStatus"
Write-Output "RUN_ID=$runId"
Write-Output "PID=$($process.Id)"
Write-Output "ORIGINAL_SAVE_UNCHANGED=$($originalUnchanged.ToString().ToUpperInvariant())"
Write-Output "RUN_ROOT=$runRoot"
if ($launcherStatus -in @('POLL_TIMEOUT_PROCESS_LEFT_OPEN','POLL_TIMEOUT_BACKGROUND_STOPPED')) { exit 124 }
if ($launcherStatus -eq 'PROCESS_EXITED') { exit 38 }
if ($backendDeviceRemoved) {
    [Console]::Error.WriteLine('HARNESS_D3D12_DEVICE_REMOVED : The D3D12 device was removed or hung. Background tests close their own process; other game processes are untouched.')
    exit 42
}
if ($evidenceVerificationFailed) {
    [Console]::Error.WriteLine('HARNESS_EVIDENCE_VERIFY_FAILED : The capture set or its scenario metadata did not pass verification.')
    exit 43
}
if ($launcherStatus -eq 'FAIL') { exit 39 }
exit 0
} finally {
    # Verification or file-I/O failures must also close this hidden renderer.
    # The Process object was returned by this launch; no other PID is queried.
    if (($Background -or $MonitorCapture) -and $null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit(5000) | Out-Null
    }
}
