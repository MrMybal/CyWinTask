[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYMCP_REVISION') -Raw).Trim()
$local = [IO.Path]::GetFullPath((Join-Path $root '../CyMCPSDK'))
$target = Join-Path $root 'artifacts/cymcp-pinned'
$safeTarget = [IO.Path]::GetFullPath($target).Replace('\','/')
if (Test-Path -LiteralPath (Join-Path $target 'CMakeLists.txt')) {
    $actual = & git -c "safe.directory=$safeTarget" -C $target rev-parse HEAD 2>$null
    $changes = & git -c "safe.directory=$safeTarget" -C $target status --porcelain --untracked-files=normal 2>$null
    if ($LASTEXITCODE -eq 0 -and $actual.Trim() -eq $revision -and -not $changes) {
        Write-Output "CyMCPSDK épinglé prêt : $revision"
        return
    }
    throw 'La copie CyMCPSDK épinglée contient des changements ou une autre révision.'
}
New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
if (Test-Path -LiteralPath (Join-Path $local 'CMakeLists.txt')) {
    & git clone --no-hardlinks --no-checkout $local $target
} else {
    & git clone --no-checkout https://github.com/MrMybal/CyMCPSDK.git $target
}
if ($LASTEXITCODE -ne 0) { throw 'Clone de CyMCPSDK impossible.' }
& git -c "safe.directory=$safeTarget" -C $target checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw "Révision CyMCPSDK $revision introuvable." }
Write-Output "CyMCPSDK épinglé prêt : $revision"
