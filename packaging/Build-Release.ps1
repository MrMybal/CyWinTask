[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.7.0',
    [string]$InnoCompiler,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $InnoCompiler) {
    $candidates = @(
        (Join-Path $projectRoot 'artifacts/tools/inno/ISCC.exe'),
        "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
        "$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $InnoCompiler) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $InnoCompiler = $command.Source }
    }
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler)) {
    throw 'Installer Inno Setup 6 ou fournir -InnoCompiler avec le chemin de ISCC.exe.'
}
& (Join-Path $PSScriptRoot 'Build-Icon.ps1')
$releaseDir = Join-Path $projectRoot "artifacts/releases/$Version"
$publishDir = Join-Path $projectRoot ('artifacts/staging/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $releaseDir, $publishDir | Out-Null
& (Join-Path $projectRoot 'native/Build-Native.ps1') -OutputDir $publishDir -Offline:$Offline
if ($LASTEXITCODE -ne 0) { throw 'Compilation native en échec.' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $publishDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.fr.md') -Destination $publishDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'VERSION.md') -Destination $publishDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'VERSION.fr.md') -Destination $publishDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $publishDir
$imageDir = Join-Path $publishDir 'docs/screenshots'
$logoDir = Join-Path $publishDir 'Assets'
$nativeDir = Join-Path $publishDir 'native'
New-Item -ItemType Directory -Force $imageDir, $logoDir, $nativeDir | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/screenshots/processes.png') -Destination $imageDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/screenshots/performance.png') -Destination $imageDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'Assets/cywintask-logo-transparent.png') -Destination $logoDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'native/CYUI_REVISION') -Destination $nativeDir
@"
CyWinTask $Version - Windows x64

Interface Dear ImGui et CyUI ; collecteur .NET autonome inclus.
Extraire TOUT le dossier ZIP, puis lancer CyWinTask.exe.
Conserver le dossier bridge et les autres fichiers à côté de l'exécutable.
Sources : https://github.com/MrMybal/CyWinTask
Licences : voir LICENSE.txt et le dossier licenses.
"@ | Set-Content (Join-Path $publishDir 'LIRE-MOI.txt') -Encoding utf8
$unexpected = @(Get-ChildItem $publishDir -Recurse -File | Where-Object { $_.Name -match 'diagnostics\.json$|^preview.*\.png$|\.pdb$|\.pfx$|^\.env' })
if ($unexpected.Count) { throw 'Fichiers locaux inattendus dans la publication.' }
& python (Join-Path $projectRoot 'scripts/check-distribution-privacy.py') $publishDir
if ($LASTEXITCODE -ne 0) { throw 'Distribution privacy check failed.' }
$zipPath = Join-Path $releaseDir "CyWinTask-$Version-win-x64-portable.zip"
Compress-Archive -Path (Join-Path $publishDir '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
& python (Join-Path $projectRoot 'scripts/check-distribution-privacy.py') $zipPath
if ($LASTEXITCODE -ne 0) { throw 'ZIP privacy check failed.' }
& $InnoCompiler "/DAppVersion=$Version" "/DPublishDir=$publishDir" "/DReleaseDir=$releaseDir" (Join-Path $PSScriptRoot 'CyWinTask.iss')
if ($LASTEXITCODE -ne 0) { throw 'Compilation Inno Setup en échec.' }
$setupPath = Join-Path $releaseDir "CyWinTask-$Version-win-x64-setup.exe"
@($zipPath, $setupPath) | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path $_ -Leaf)
} | Set-Content (Join-Path $releaseDir 'SHA256SUMS.txt') -Encoding ascii
Get-Item $zipPath, $setupPath, (Join-Path $releaseDir 'SHA256SUMS.txt')
