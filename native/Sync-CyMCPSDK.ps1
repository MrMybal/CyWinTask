[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYMCP_REVISION') -Raw).Trim()
$local = [IO.Path]::GetFullPath((Join-Path $root '../CyMCPSDK'))
if (Test-Path -LiteralPath (Join-Path $local 'CMakeLists.txt')) {
    $actual = & git -c "safe.directory=$($local.Replace('\','/'))" -C $local rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $revision) {
        throw 'La copie locale de CyMCPSDK ne correspond pas à native/CYMCP_REVISION.'
    }
    Write-Output "CyMCPSDK local prêt : $revision"
    return
}
$target = Join-Path $root 'third_party/CyMCPSDK'
if (-not (Test-Path -LiteralPath $target)) {
    New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
    & git clone https://github.com/MrMybal/CyMCPSDK.git $target
    if ($LASTEXITCODE -ne 0) { throw 'Clone de CyMCPSDK en échec.' }
}
$actual = & git -c "safe.directory=$($target.Replace('\','/'))" -C $target rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'La copie de CyMCPSDK est invalide.' }
if ($actual.Trim() -ne $revision) {
    $changes = & git -c "safe.directory=$($target.Replace('\','/'))" -C $target status --porcelain
    if ($LASTEXITCODE -ne 0 -or $changes) { throw 'CyMCPSDK contient des modifications locales : changement de révision refusé.' }
    & git -c "safe.directory=$($target.Replace('\','/'))" -C $target fetch origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'Révision CyMCPSDK introuvable.' }
    & git -c "safe.directory=$($target.Replace('\','/'))" -C $target checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'Checkout de la révision CyMCPSDK en échec.' }
}
Write-Output "CyMCPSDK prêt : $revision"
