[CmdletBinding()]
param(
    [string]$OutputDir,
    [string]$CyUISource,
    [string]$CyMCPSDKSource,
    [string]$CyHWInfoSource,
    [switch]$Offline,
    [switch]$Checks
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist-native' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (-not $CyUISource) {
    $localCyUI = [IO.Path]::GetFullPath((Join-Path $root '../CyImgui_Interface'))
    $revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYUI_REVISION') -Raw).Trim()
    $vendoredCyUI = Join-Path $root 'third_party/CyUI'
    if (Test-Path -LiteralPath (Join-Path $localCyUI 'src/modules/DocumentViews.cpp')) {
        $actual = & git -c "safe.directory=$($localCyUI.Replace('\','/'))" -C $localCyUI rev-parse HEAD
        if ($LASTEXITCODE -eq 0 -and $actual.Trim() -eq $revision) {
            $CyUISource = $localCyUI
        } else {
            $snapshot = Join-Path $root "artifacts/cyui-$revision"
            if (-not (Test-Path -LiteralPath (Join-Path $snapshot 'CMakeLists.txt'))) {
                New-Item -ItemType Directory -Force (Join-Path $root 'artifacts') | Out-Null
                $archive = Join-Path $root "artifacts/cyui-$revision.zip"
                & git -c "safe.directory=$($localCyUI.Replace('\','/'))" -C $localCyUI archive --format=zip "--output=$archive" $revision
                if ($LASTEXITCODE -ne 0) { throw "Révision CyUI $revision introuvable localement." }
                Expand-Archive -LiteralPath $archive -DestinationPath $snapshot -Force
                Set-Content -LiteralPath (Join-Path $snapshot 'REVISION') -Value $revision
            }
            $CyUISource = $snapshot
        }
    } elseif ((Test-Path -LiteralPath (Join-Path $vendoredCyUI 'REVISION')) -and
        ((Get-Content -LiteralPath (Join-Path $vendoredCyUI 'REVISION') -Raw).Trim() -eq $revision)) {
        $CyUISource = $vendoredCyUI
    } else { $CyUISource = $vendoredCyUI }
}
$CyUISource = [IO.Path]::GetFullPath($CyUISource)
if (-not $CyMCPSDKSource) {
    $localSDK = [IO.Path]::GetFullPath((Join-Path $root '../CyMCPSDK'))
    $pinnedSDK = Join-Path $root 'artifacts/cymcp-pinned'
    $vendoredSDK = Join-Path $root 'third_party/CyMCPSDK'
    $revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYMCP_REVISION') -Raw).Trim()
    foreach ($candidate in @($localSDK, $pinnedSDK, $vendoredSDK)) {
        if (-not (Test-Path -LiteralPath (Join-Path $candidate 'CMakeLists.txt'))) { continue }
        $safeCandidate = [IO.Path]::GetFullPath($candidate).Replace('\','/')
        $actual = & git -c "safe.directory=$safeCandidate" -C $candidate rev-parse HEAD 2>$null
        if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $revision) { continue }
        $changes = & git -c "safe.directory=$safeCandidate" -C $candidate status --porcelain --untracked-files=normal
        if ($LASTEXITCODE -eq 0 -and -not $changes) {
            $CyMCPSDKSource = $candidate
            break
        }
    }
    if (-not $CyMCPSDKSource) {
        if ($Offline) { throw 'CyMCPSDK épinglé introuvable en mode hors ligne. Exécuter native/Sync-CyMCPSDK.ps1.' }
        & (Join-Path $PSScriptRoot 'Sync-CyMCPSDK.ps1')
        $CyMCPSDKSource = $pinnedSDK
    }
}
$CyMCPSDKSource = [IO.Path]::GetFullPath($CyMCPSDKSource)
if (-not $CyHWInfoSource) {
    $localHWInfo = [IO.Path]::GetFullPath((Join-Path $root '../CyHWInfo'))
    $revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYHWINFO_REVISION') -Raw).Trim()
    if (Test-Path -LiteralPath (Join-Path $localHWInfo 'CMakeLists.txt')) {
        $safeLocalHWInfo = $localHWInfo.Replace('\','/')
        $actual = & git -c "safe.directory=$safeLocalHWInfo" -C $localHWInfo rev-parse HEAD
        $changes = & git -c "safe.directory=$safeLocalHWInfo" -C $localHWInfo status --porcelain --untracked-files=normal
        if ($LASTEXITCODE -ne 0) { throw 'Lecture du dépôt CyHWInfo impossible.' }
        if ($actual.Trim() -eq $revision -and -not $changes) {
            $CyHWInfoSource = $localHWInfo
        } else {
            $snapshot = Join-Path $root "artifacts/cyhwinfo-$revision"
            if (-not (Test-Path -LiteralPath (Join-Path $snapshot 'CMakeLists.txt'))) {
                New-Item -ItemType Directory -Force (Join-Path $root 'artifacts') | Out-Null
                $archive = Join-Path $root "artifacts/cyhwinfo-$revision.zip"
                & git -c "safe.directory=$safeLocalHWInfo" -C $localHWInfo archive --format=zip "--output=$archive" $revision
                if ($LASTEXITCODE -ne 0) { throw "Révision CyHWInfo $revision introuvable localement." }
                Expand-Archive -LiteralPath $archive -DestinationPath $snapshot -Force
                Set-Content -LiteralPath (Join-Path $snapshot 'REVISION') -Value $revision
            }
            $CyHWInfoSource = $snapshot
        }
    } else { $CyHWInfoSource = Join-Path $root 'artifacts/CyHWInfo-source' }
}
$CyHWInfoSource = [IO.Path]::GetFullPath($CyHWInfoSource)
$hwRevision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CYHWINFO_REVISION') -Raw).Trim()
if (-not (Test-Path -LiteralPath (Join-Path $CyHWInfoSource 'CMakeLists.txt'))) {
    if ($Offline) { throw 'CyHWInfo local introuvable en mode hors ligne.' }
    & git clone --no-checkout https://github.com/MrMybal/CyHWInfo.git $CyHWInfoSource
    if ($LASTEXITCODE -ne 0) { throw 'Clone CyHWInfo impossible avec les identifiants Git existants.' }
    & git -c "safe.directory=$($CyHWInfoSource.Replace('\','/'))" -C $CyHWInfoSource checkout --detach $hwRevision
    if ($LASTEXITCODE -ne 0) { throw "Révision CyHWInfo $hwRevision introuvable." }
}
$hwActual = if (Test-Path -LiteralPath (Join-Path $CyHWInfoSource 'REVISION')) {
    (Get-Content -LiteralPath (Join-Path $CyHWInfoSource 'REVISION') -Raw).Trim()
} else {
    & git -c "safe.directory=$($CyHWInfoSource.Replace('\','/'))" -C $CyHWInfoSource rev-parse HEAD
}
if ($hwActual.Trim() -ne $hwRevision) {
    throw "CyHWInfo doit être à la révision épinglée $hwRevision."
}
$runtime8 = @(& dotnet --list-runtimes | Where-Object { $_ -match '^Microsoft\.NETCore\.App 8\.' })
if ($runtime8.Count -eq 0) { throw 'Le runtime .NET 8 est requis pour les capteurs CyHWInfo.' }
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
if ($Offline) {
    $env:NUGET_PACKAGES = Join-Path $root 'artifacts/nuget-packages'
    $env:RestoreSources = Join-Path $root 'artifacts/nuget-feed'
    $env:RestoreIgnoreFailedSources = 'true'
}
$configure = '"{0}" -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="{3}" -DCYUI_ROOT="{4}" -DCYMCP_SOURCE_DIR="{5}" -DCYHWINFO_SOURCE_DIR="{6}"' -f $cmake, $PSScriptRoot, $build, $ninja, $CyUISource, $CyMCPSDKSource, $CyHWInfoSource
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
    $packages = Join-Path $root 'artifacts/nuget-packages'
    $restore += "-p:RestorePackagesPath=$packages"
    $restore += '--source'
    $restore += $packages
    $localFeed = Join-Path $root 'artifacts/nuget-feed'
    if (Test-Path -LiteralPath $localFeed) {
        $restore += '--source'
        $restore += $localFeed
    }
    $restore += '--source'
    $restore += (Join-Path $env:USERPROFILE '.nuget/packages')
}
& dotnet @restore
if ($LASTEXITCODE -ne 0) { throw 'Restauration .NET en échec.' }
$bridgeOutput = Join-Path $OutputDir 'bridge'
New-Item -ItemType Directory -Force $OutputDir, $bridgeOutput | Out-Null
& dotnet publish $bridgeProject -c Release -r win-x64 --self-contained true --no-restore -o $bridgeOutput '-p:RuntimeFrameworkVersion=8.0.31' '-p:DebugType=None' '-p:DebugSymbols=false'
if ($LASTEXITCODE -ne 0) { throw 'Publication du collecteur en échec.' }
Copy-Item -LiteralPath (Join-Path $build 'CyWinTask.exe') -Destination $OutputDir -Force
Copy-Item -LiteralPath (Join-Path $build 'CyWinTaskCli.exe') -Destination $OutputDir -Force
$sensorRuntime = Join-Path $OutputDir 'cyhwinfo-lhm'
New-Item -ItemType Directory -Force $sensorRuntime | Out-Null
Copy-Item -Path (Join-Path $build 'cyhwinfo-lhm/*') -Destination $sensorRuntime -Recurse -Force
& dotnet publish (Join-Path $CyHWInfoSource 'src/lhm_bridge/CyHWInfo.SensorBridge.csproj') `
    --configuration Release --no-self-contained --no-restore --output $sensorRuntime `
    "-p:BaseIntermediateOutputPath=$build/cyhwinfo/lhm_bridge/obj/" `
    "-p:BaseOutputPath=$build/cyhwinfo/lhm_bridge/bin/" `
    '-p:RestoreLockedMode=true' '-p:DebugType=None' '-p:DebugSymbols=false'
if ($LASTEXITCODE -ne 0) { throw 'Publication du pont de capteurs CyHWInfo en échec.' }
Get-ChildItem -LiteralPath $sensorRuntime -Filter '*.pdb' -File -Recurse |
    Remove-Item -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $OutputDir 'LICENSE.txt') -Force
$licenseDir = Join-Path $OutputDir 'licenses'
New-Item -ItemType Directory -Force $licenseDir | Out-Null
Copy-Item -LiteralPath (Join-Path $CyUISource 'LICENSE') -Destination (Join-Path $licenseDir 'CyUI.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyUISource 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $licenseDir 'CyUI-Third-Party-Notices.md') -Force
Copy-Item -LiteralPath (Join-Path $CyUISource 'third_party/imgui/LICENSE.txt') -Destination (Join-Path $licenseDir 'Dear-ImGui.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyMCPSDKSource 'LICENSE') -Destination (Join-Path $licenseDir 'CyMCPSDK.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyHWInfoSource 'LICENSE') -Destination (Join-Path $licenseDir 'CyHWInfo.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyHWInfoSource 'THIRD-PARTY-NOTICES.txt') -Destination (Join-Path $licenseDir 'CyHWInfo-Third-Party-Notices.txt') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party/hwinfo/LICENSE') -Destination (Join-Path $licenseDir 'hwinfo.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyMCPSDKSource 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $licenseDir 'CyMCPSDK-Third-Party-Notices.md') -Force
Copy-Item -LiteralPath (Join-Path $CyMCPSDKSource 'third_party/nlohmann/LICENSE.MIT') -Destination (Join-Path $licenseDir 'nlohmann-json.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyMCPSDKSource 'third_party/httplib/LICENSE') -Destination (Join-Path $licenseDir 'cpp-httplib.txt') -Force
Copy-Item -LiteralPath (Join-Path $CyMCPSDKSource 'third_party/picosha2/LICENSE') -Destination (Join-Path $licenseDir 'PicoSHA2.txt') -Force
foreach ($notice in @(
    'third_party/md4c/LICENSE.md',
    'third_party/lexilla/License.txt',
    'third_party/scintilla/License.txt',
    'third_party/SciTE-Properties-License.txt',
    'third_party/jetbrains-mono/OFL.txt'
)) {
    $name = 'CyUI-' + ($notice -replace '[\\/]', '-')
    Copy-Item -LiteralPath (Join-Path $CyUISource $notice) -Destination (Join-Path $licenseDir $name) -Force
}
$stbLicenses = Join-Path $licenseDir 'stb'
New-Item -ItemType Directory -Force $stbLicenses | Out-Null
Get-ChildItem -LiteralPath (Join-Path $CyUISource 'third_party/imgui') -Filter 'imstb_*.h' -File |
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
    $target = Join-Path $licenseDir $name
    New-Item -ItemType Directory -Force $target | Out-Null
    if ($notices.Count) {
        $notices | Copy-Item -Destination $target -Force
    } elseif ($name -in @('blacksharp.core', 'diskinfotoolkit', 'librehardwaremonitorlib', 'ramspdtoolkit-ndd')) {
        $spec = Join-Path $package "$name.nuspec"
        if (-not (Test-Path -LiteralPath $spec) -or
            -not ((Get-Content -LiteralPath $spec -Raw) -match '<license type="expression">MPL-2\.0</license>')) {
            throw "Licence MPL-2.0 non confirmée : $name"
        }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses/MPL-2.0.txt') -Destination (Join-Path $target 'LICENSE.txt')
    } elseif ($name -eq 'mono.posix.netstandard') {
        $spec = Join-Path $package "$name.nuspec"
        if (-not (Test-Path -LiteralPath $spec) -or
            -not ((Get-Content -LiteralPath $spec -Raw) -match 'linkid=869050')) {
            throw "Licence Mono non confirmée : $name"
        }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses/Mono-LICENSE.txt') -Destination (Join-Path $target 'LICENSE.txt')
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses/Mono-PATENTS.txt') -Destination (Join-Path $target 'PATENTS.txt')
    } else {
        throw "Licence manquante : $name"
    }
}
Get-Item (Join-Path $OutputDir 'CyWinTask.exe'), (Join-Path $OutputDir 'CyWinTaskCli.exe'), (Join-Path $bridgeOutput 'CyWinTask.Bridge.exe')
if ($Checks) {
    $testBuild = '"{0}" --build "{1}" --target CyWinTaskAssistantChecks CyWinTaskUiChecks CyWinTaskConsoleChecks CyWinTaskHardwareChecks CyWinTaskSensorChecks -j 4' -f $cmake, $build
    & cmd.exe /d /c ('call "{0}" >nul && {1}' -f $vcvars, $testBuild) *> $buildLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $buildLog -Tail 80
        throw 'Compilation des contrôles Assistant en échec.'
    }
    foreach ($testName in @('CyWinTaskAssistantChecks.exe', 'CyWinTaskUiChecks.exe', 'CyWinTaskConsoleChecks.exe', 'CyWinTaskHardwareChecks.exe', 'CyWinTaskSensorChecks.exe')) {
        $testExecutable = Join-Path $OutputDir $testName
        Copy-Item -LiteralPath (Join-Path $build $testName) -Destination $testExecutable -Force
        try {
            & $testExecutable
            if ($LASTEXITCODE -ne 0) { throw "Contrôles $testName en échec." }
        } finally {
            Remove-Item -LiteralPath $testExecutable -Force
        }
    }
}
