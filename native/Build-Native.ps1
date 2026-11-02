[CmdletBinding()]
param(
    [string]$OutputDir,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist-native' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) { throw 'Visual Studio avec les outils C++ est requis.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
foreach ($tool in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Outil de compilation introuvable : $tool" }
}
$build = Join-Path $PSScriptRoot 'build-ninja'
New-Item -ItemType Directory -Force $build | Out-Null
$configure = '"{0}" -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="{3}"' -f $cmake, $PSScriptRoot, $build, $ninja
$compile = '"{0}" --build "{1}" --config Release -j 4' -f $cmake, $build
$buildLog = Join-Path $build 'build-script.log'
& cmd.exe /d /c ('call "{0}" >nul && {1} && {2}' -f $vcvars, $configure, $compile) *> $buildLog
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $buildLog -Tail 80
    throw 'Compilation Dear ImGui en échec.'
}
Write-Output 'Frontal Dear ImGui compilé.'

$bridgeProject = Join-Path $root 'Bridge/CyWinTask.Bridge.csproj'
$restore = @('restore', $bridgeProject, '-r', 'win-x64', '-p:RuntimeFrameworkVersion=8.0.31')
if ($Offline) {
    $packages = Join-Path $env:USERPROFILE '.nuget/packages'
    $restore += "-p:RestorePackagesPath=$packages"
    $restore += '--source'
    $restore += $packages
}
& dotnet @restore
if ($LASTEXITCODE -ne 0) { throw 'Restauration .NET en échec.' }
$bridgeOutput = Join-Path $OutputDir 'bridge'
New-Item -ItemType Directory -Force $OutputDir, $bridgeOutput | Out-Null
& dotnet publish $bridgeProject -c Release -r win-x64 --self-contained true --no-restore -o $bridgeOutput '-p:RuntimeFrameworkVersion=8.0.31' '-p:DebugType=None' '-p:DebugSymbols=false'
if ($LASTEXITCODE -ne 0) { throw 'Publication du collecteur en échec.' }
Copy-Item -LiteralPath (Join-Path $build 'CyWinTask.exe') -Destination $OutputDir -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $OutputDir 'LICENSE.txt') -Force
$licenseDir = Join-Path $OutputDir 'licenses'
New-Item -ItemType Directory -Force $licenseDir | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'third_party/CyUI/LICENSE') -Destination (Join-Path $licenseDir 'CyUI.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'third_party/CyUI/THIRD_PARTY_NOTICES.md') -Destination (Join-Path $licenseDir 'CyUI-Third-Party-Notices.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'third_party/CyUI/third_party/imgui/LICENSE.txt') -Destination (Join-Path $licenseDir 'Dear-ImGui.txt') -Force
$stbLicenses = Join-Path $licenseDir 'stb'
New-Item -ItemType Directory -Force $stbLicenses | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'third_party/CyUI/third_party/imgui') -Filter 'imstb_*.h' -File |
    Copy-Item -Destination $stbLicenses -Force

$assets = Get-Content (Join-Path $root 'Bridge/obj/project.assets.json') -Raw | ConvertFrom-Json
$packageRoots = @($assets.packageFolders.PSObject.Properties.Name)
$needed = @('microsoft.netcore.app.runtime.win-x64/8.0.31')
$needed += @($assets.libraries.PSObject.Properties | Where-Object { $_.Value.type -eq 'package' } | ForEach-Object Name)
foreach ($entry in ($needed | Select-Object -Unique)) {
    $parts = $entry.Split('/')
    if ($parts.Count -ne 2) { continue }
    $name = $parts[0].ToLowerInvariant()
    $package = $packageRoots | ForEach-Object { Join-Path $_ "$name/$($parts[1])" } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $package) { throw "Paquet introuvable : $name" }
    $notices = @(Get-ChildItem -LiteralPath $package -File | Where-Object { $_.Name -match '^(LICENSE|THIRD-PARTY-NOTICES)(\..*)?$' })
    if (-not $notices.Count) { throw "Licence manquante : $name" }
    $target = Join-Path $licenseDir $name
    New-Item -ItemType Directory -Force $target | Out-Null
    $notices | Copy-Item -Destination $target -Force
}
Get-Item (Join-Path $OutputDir 'CyWinTask.exe'), (Join-Path $bridgeOutput 'CyWinTask.Bridge.exe')
