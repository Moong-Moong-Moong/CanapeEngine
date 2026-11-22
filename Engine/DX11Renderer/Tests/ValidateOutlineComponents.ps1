param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/OutlineValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
$vcpkgTargets = Join-Path $VisualStudioPath 'VC/vcpkg/scripts/buildsystems/msbuild/vcpkg.targets'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
"$msbuild" Engine\Runtime.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1 /nr:false /v:minimal /nologo /p:VcpkgManifestInstall=false "/p:ForceImportAfterCppTargets=$vcpkgTargets"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine /IEngine\Source /external:Ivcpkg_installed\x64-windows-static-md\x64-windows-static-md\include /external:W0 Engine\DX11Renderer\Tests\OutlineComponentValidation.cpp Binaries\x64\Debug\Runtime\Runtime.lib Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib Binaries\x64\Debug\DX11Simulation\DX11Simulation.lib Binaries\x64\Debug\JoltPhysics\JoltPhysics.lib vcpkg_installed\x64-windows-static-md\x64-windows-static-md\debug\lib\*.lib /FoIntermediate\OutlineValidation\Component.obj /FeIntermediate\OutlineValidation\Component.exe /link user32.lib gdi32.lib windowscodecs.lib ole32.lib xinput.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\OutlineValidation\Component.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'components.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Outline component validation failed (exit $LASTEXITCODE)." }
}
finally { Pop-Location }
