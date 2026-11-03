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

$joltInclude = 'vcpkg_installed/x64-windows-static-md/x64-windows-static-md/include'
# Same JPH defines as JoltPhysics.vcxproj; the test includes Jolt math through the private conversion header.
$joltDefines = '/DJPH_USE_CPU_COMPUTE /DJPH_OBJECT_STREAM /DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED'

# Use embedded compiler debug information to avoid this machine's PDB-manager C1902.
# No project settings are changed.
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
"$msbuild" Engine\JoltPhysics\JoltPhysics.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc $crt /utf-8 /W4 /WX $joltDefines /ICommon /IEngine /external:I$joltInclude /external:W0 Engine\JoltPhysics\Tests\PhysicsValidation.cpp Binaries\x64\$Configuration\JoltPhysics\JoltPhysics.lib $joltLib /FoIntermediate\PhysicsValidation\$Configuration\PhysicsValidation.obj /FeIntermediate\PhysicsValidation\$Configuration\PhysicsValidation.exe /link user32.lib
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
$exe = Join-Path $validationDir 'PhysicsValidation.exe'
$firstRun = Join-Path $validationDir 'run1.txt'
$secondRun = Join-Path $validationDir 'run2.txt'
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Physics validation build ($Configuration) failed (exit $LASTEXITCODE)." }

    # Two processes must print the same bytes: a digest that differs between runs means the simulation is not deterministic.
    $failed = $false
    foreach ($run in @($firstRun, $secondRun)) {
        & cmd.exe /c "`"$exe`" > `"$run`" 2>&1"
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    Get-Content $firstRun | ForEach-Object { Write-Host $_ }
    if ($failed) { throw "Physics validation ($Configuration) failed." }
    $firstHash = (Get-FileHash $firstRun -Algorithm SHA256).Hash
    $secondHash = (Get-FileHash $secondRun -Algorithm SHA256).Hash
    if ($firstHash -ne $secondHash) { throw "Physics validation ($Configuration): the two runs printed different output." }
    $digest = (Get-Content $firstRun | Where-Object { $_ -like 'DIGEST*' }) -join ' | '
    Write-Host "RESULT PASS two runs identical; $digest"
}
finally {
    Pop-Location
}
