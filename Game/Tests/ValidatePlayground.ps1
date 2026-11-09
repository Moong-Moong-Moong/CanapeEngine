param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$validationDir = Join-Path $repoRoot "Intermediate/PlaygroundValidation/$Configuration"
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force

$vcpkgDir = 'vcpkg_installed\x64-windows-static-md\x64-windows-static-md'
if ($Configuration -eq 'Debug') {
    $crt = '/MDd /D_DEBUG'
    $vcpkgLibs = "$vcpkgDir\debug\lib\Jolt.lib $vcpkgDir\debug\lib\librttr_core_d.lib"
}
else {
    $crt = '/MD /DNDEBUG'
    $vcpkgLibs = "$vcpkgDir\lib\Jolt.lib $vcpkgDir\lib\librttr_core.lib"
}

# The libraries are built by MSBuild with the project's own settings; only the test's own compile uses embedded
# debug information (/Z7) to avoid this machine's PDB-manager C1902.
# The game's puzzle and script sources are compiled in; its entry point and application class are not.
# The scene is the real JSON file, opened through the SceneManager.
# The program runs in the validation directory because the renderer loads Shaders/ from the working directory.
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\JoltPhysics\JoltPhysics.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\Runtime.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
cl /nologo /std:c++20 /EHsc $crt /utf-8 /W4 /DCANAPE_RENDER_DEBUG=1 /ICommon /IEngine /IEngine\Source /I$vcpkgDir\include /IGame\Source Game\Tests\PlaygroundValidation.cpp Game\Source\Scripts\*.cpp Game\Source\Puzzle\*.cpp Binaries\x64\$Configuration\Runtime\Runtime.lib Binaries\x64\$Configuration\DX11Renderer\DX11Renderer.lib Binaries\x64\$Configuration\JoltPhysics\JoltPhysics.lib $vcpkgLibs /FoIntermediate\PlaygroundValidation\$Configuration\ /FeIntermediate\PlaygroundValidation\$Configuration\PlaygroundValidation.exe /link user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib shell32.lib ole32.lib advapi32.lib
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
$exe = Join-Path $validationDir 'PlaygroundValidation.exe'
$output = Join-Path $validationDir 'run.txt'
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Playground validation build ($Configuration) failed (exit $LASTEXITCODE)." }

    Push-Location $validationDir
    try {
        & cmd.exe /c "`"$exe`" `"$repoRoot`" > `"$output`" 2>&1"
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    Get-Content $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) { throw "Playground validation ($Configuration) failed (exit $exitCode)." }
}
finally {
    Pop-Location
}
