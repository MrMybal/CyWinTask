[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.5.3',
    [string]$InnoCompiler
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
& dotnet publish (Join-Path $projectRoot 'CyWinTask.csproj') -c Release -r win-x64 --self-contained true -o $publishDir "-p:Version=$Version" "-p:PathMap=$projectRoot=/_/CyWinTask" '-p:DebugType=None' '-p:DebugSymbols=false' '-p:PublishTrimmed=false' '-p:PublishSingleFile=false' '-p:RuntimeFrameworkVersion=8.0.31'
if ($LASTEXITCODE -ne 0) { throw 'Publication .NET en échec.' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $publishDir 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $publishDir
@"
CyWinTask $Version - Windows x64

Version autonome : le runtime .NET Desktop est inclus.
Extraire TOUT le dossier ZIP, puis lancer CyWinTask.exe.
Conserver les DLL et les autres fichiers à côté de l'exécutable.
Sources : https://github.com/MrMybal/CyWinTask
Licence : voir LICENSE.txt et les notices du runtime .NET incluses.
"@ | Set-Content (Join-Path $publishDir 'LIRE-MOI.txt') -Encoding utf8
# Include redistribution licenses from the exact runtime packages restored above.
$assets = Get-Content (Join-Path $projectRoot 'obj/project.assets.json') -Raw | ConvertFrom-Json
$packageRoots = @($assets.packageFolders.PSObject.Properties.Name)
foreach ($package in @('microsoft.netcore.app.runtime.win-x64', 'microsoft.windowsdesktop.app.runtime.win-x64')) {
    $packagePath = $packageRoots | ForEach-Object { Join-Path $_ "$package/8.0.31" } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $packagePath) { throw "Runtime package not found: $package" }
    $noticeDir = Join-Path $publishDir "licenses/$package"
    New-Item -ItemType Directory -Force $noticeDir | Out-Null
    $notices = @(Get-ChildItem -LiteralPath $packagePath -File | Where-Object { $_.Name -match '^(LICENSE|THIRD-PARTY-NOTICES)(\..*)?$' })
    if (-not $notices.Count) { throw "Missing runtime license: $package" }
    $notices | Copy-Item -Destination $noticeDir
}
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
