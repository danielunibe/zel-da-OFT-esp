param(
    [ValidateSet('Acquire', 'Release', 'Check')]
    [string]$Action = 'Check',
    [string]$Agent = 'Gemini',
    [string]$Task = 'Build'
)

$devRoot = Split-Path -Parent $PSScriptRoot
$lockFile = Join-Path $devRoot '.agent_build.lock'

function Get-LockData {
    if (Test-Path $lockFile) {
        try {
            $content = Get-Content $lockFile -Raw | ConvertFrom-Json
            return $content
        } catch {
            return $null
        }
    }
    return $null
}

function Is-ProcessAlive([int]$pidToCheck) {
    try {
        $p = Get-Process -Id $pidToCheck -ErrorAction Stop
        return ($p -ne $null)
    } catch {
        return $false
    }
}

switch ($Action) {
    'Check' {
        $data = Get-LockData
        if ($data -ne $null) {
            $alive = Is-ProcessAlive -pidToCheck $data.PID
            if ($alive) {
                Write-Host "ACTIVE LOCK held by Agent '$($data.Agent)' (PID: $($data.PID), Task: '$($data.Task)', Time: $($data.Timestamp))"
                exit 1
            } else {
                Write-Host "STALE LOCK detected from Agent '$($data.Agent)' (Dead PID: $($data.PID)). Ready to recover."
                exit 0
            }
        } else {
            Write-Host "NO LOCK active. Build slot is free."
            exit 0
        }
    }

    'Acquire' {
        $data = Get-LockData
        if ($data -ne $null) {
            $alive = Is-ProcessAlive -pidToCheck $data.PID
            if ($alive) {
                Write-Error "BUILD_BUSY_BY_OTHER_AGENT: Lock currently held by Agent '$($data.Agent)' (PID: $($data.PID), Task: '$($data.Task)')"
                exit 1
            } else {
                Write-Warning "Removing stale build lock from dead PID $($data.PID) ($($data.Agent))."
                Remove-Item -Path $lockFile -Force -ErrorAction SilentlyContinue
            }
        }

        $myLock = @{
            Agent = $Agent
            PID = $PID
            Timestamp = (Get-Date -Format 'o')
            Task = $Task
        }
        $myLock | ConvertTo-Json | Set-Content -Path $lockFile -Force
        Write-Host "BUILD LOCK ACQUIRED by Agent '$Agent' (PID: $PID, Task: '$Task')"
        exit 0
    }

    'Release' {
        if (Test-Path $lockFile) {
            $data = Get-LockData
            if ($data -eq $null -or $data.PID -eq $PID -or -not (Is-ProcessAlive -pidToCheck $data.PID)) {
                Remove-Item -Path $lockFile -Force -ErrorAction SilentlyContinue
                Write-Host "BUILD LOCK RELEASED successfully."
            } else {
                Write-Warning "Lock file belongs to active PID $($data.PID) ($($data.Agent)); will not force release."
            }
        } else {
            Write-Host "No lock file to release."
        }
        exit 0
    }
}
