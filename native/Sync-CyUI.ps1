[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$target = Join-Path $root 'third_party/CyUI'
$revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYUI_REVISION') -Raw).Trim()
$source = 'https://github.com/MrMybal/CyUI.git'
if (-not (Test-Path -LiteralPath $target)) {
    New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
    & git clone $source $target
    if ($LASTEXITCODE -ne 0) { throw 'CyUI clone failed. Access to the private repository is required.' }
}
if (Test-Path -LiteralPath (Join-Path $target '.git')) {
    $changes = & git -c "safe.directory=$($target.Replace('\','/'))" -C $target status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the CyUI checkout.' }
    if ($changes) { throw 'CyUI has local changes; keep them before switching revisions.' }
    & git -c "safe.directory=$($target.Replace('\','/'))" -C $target fetch origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'Cannot fetch the pinned CyUI revision.' }
    & git -c "safe.directory=$($target.Replace('\','/'))" -C $target checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check out the pinned CyUI revision.' }
} elseif (Test-Path -LiteralPath (Join-Path $target 'REVISION')) {
    $localRevision = (Get-Content -LiteralPath (Join-Path $target 'REVISION') -Raw).Trim()
    if ($localRevision -ne $revision) { throw 'The local CyUI source snapshot has a different revision.' }
} else {
    throw 'third_party/CyUI exists but is not a CyUI checkout or pinned source snapshot.'
}
Write-Output "CyUI revision $revision is ready."
