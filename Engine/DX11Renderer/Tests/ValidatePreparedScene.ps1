param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/PreparedSceneValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /ICommon /IEngine Engine\DX11Renderer\Tests\PreparedSceneValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\PreparedSceneValidation\Debug.obj /FeIntermediate\PreparedSceneValidation\Debug.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\PreparedSceneValidation\Debug.exe
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Shipping /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MD /Z7 /DNDEBUG /DCANAPE_RENDER_DEBUG=0 /utf-8 /ICommon /IEngine Engine\DX11Renderer\Tests\PreparedSceneValidation.cpp Binaries\x64\Shipping\DX11Renderer\DX11Renderer.lib /FoIntermediate\PreparedSceneValidation\Shipping.obj /FeIntermediate\PreparedSceneValidation\Shipping.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\PreparedSceneValidation\Shipping.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Prepared scene validation failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
