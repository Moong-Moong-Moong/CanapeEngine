param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot "Intermediate/PhysicsValidation/$Configuration"
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null

if ($Configuration -eq 'Debug') {
    $crt = '/MDd /D_DEBUG'
    $joltLib = 'vcpkg_installed\x64-windows-static-md\x64-windows-static-md\debug\lib\Jolt.lib'
}
else {
    $crt = '/MD /DNDEBUG'
    $joltLib = 'vcpkg_installed\x64-windows-static-md\x64-windows-static-md\lib\Jolt.lib'
}

# Use embedded compiler debug information to avoid this machine's PDB-manager C1902.
# No project settings are changed. The test only sees the public JoltPhysics headers.
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
"$msbuild" Engine\JoltPhysics\JoltPhysics.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc $crt /utf-8 /W4 /WX /ICommon /IEngine Engine\JoltPhysics\Tests\PhysicsValidation.cpp Binaries\x64\$Configuration\JoltPhysics\JoltPhysics.lib $joltLib /FoIntermediate\PhysicsValidation\$Configuration\PhysicsValidation.obj /FeIntermediate\PhysicsValidation\$Configuration\PhysicsValidation.exe /link user32.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\PhysicsValidation\$Configuration\PhysicsValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Physics validation ($Configuration) failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
