param([switch]$Deploy)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install MSVC x64/x86 Build Tools and Windows SDK first.' }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
# Use English diagnostics when the corresponding VS language pack is installed.
$env:VSLANG = '1033'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$qt = 'C:\Qt\6.11.1\msvc2022_64'
$buildRoot = Join-Path (Split-Path $repo -Parent) 'AccuShot-build'
$build = Join-Path $buildRoot 'release'
& $cmake -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    '-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe' `
    "-DQt6_DIR=$qt/lib/cmake/Qt6" '-DACCUSHOT_EXECUTABLE_NAME=AccuShot'
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmake --build $build --parallel 6
if ($LASTEXITCODE) { throw 'Build failed; see compiler diagnostics above.' }
if ($Deploy) {
    $destination = Join-Path $buildRoot 'dist\AccuShot'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath "$build\AccuShot.exe" -Destination "$destination\AccuShot.exe"
    Copy-Item -LiteralPath "$build\AccuShot.pdb" -Destination "$destination\AccuShot.pdb"
    & "$qt\bin\windeployqt.exe" --release --no-translations "$destination\AccuShot.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
}
