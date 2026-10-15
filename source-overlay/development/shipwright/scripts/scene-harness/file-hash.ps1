function Get-HarnessFileHash {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [ValidateSet('SHA256')][string]$Algorithm = 'SHA256'
    )
    # Windows PowerShell spawned by Python/MSBuild may lack Get-FileHash's
    # script module. Keep integrity checks independent of module autoloading.
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [System.IO.File]::OpenRead($LiteralPath)
        $hash = [System.BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '')
        return [pscustomobject]@{ Algorithm = 'SHA256'; Hash = $hash; Path = $LiteralPath }
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $hasher.Dispose()
    }
}
