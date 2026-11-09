param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release', 'Shipping')][string]$Configuration
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'A Visual Studio C++ installation is required.' }

$binDir = Join-Path $vsPath 'MSBuild/Current/Bin'
switch ($env:PROCESSOR_ARCHITECTURE) {
    'AMD64' { $binDir = Join-Path $binDir 'amd64' }
    'ARM64' { $binDir = Join-Path $binDir 'arm64' }
}
$msbuild = Join-Path $binDir 'MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild not found: $msbuild" }

$solution = Join-Path $PSScriptRoot '../Canape.slnx'
& $msbuild $solution "-p:Configuration=$Configuration" -p:Platform=x64 -m -nologo -v:minimal
exit $LASTEXITCODE
