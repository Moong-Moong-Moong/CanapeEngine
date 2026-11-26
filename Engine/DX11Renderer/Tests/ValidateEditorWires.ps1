param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/EditorWireValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
$shaderDir = Join-Path $validationDir 'Shaders'
New-Item -ItemType Directory -Path $shaderDir -Force | Out-Null
Copy-Item -Path (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') -Destination $shaderDir -Recurse -Force
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
fxc /nologo /WX /Zpr /T vs_5_0 /E VSMain /Fo NUL Engine\DX11Renderer\Shader\EditorWire.hlsl
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /T ps_5_0 /E PSMain /Fo NUL Engine\DX11Renderer\Shader\EditorWire.hlsl
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nr:false /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\EditorWireValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\EditorWireValidation\Debug.obj /FeIntermediate\EditorWireValidation\Debug.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\EditorWireValidation\Debug.exe
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Shipping /p:Platform=x64 /m /nr:false /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MD /Z7 /DNDEBUG /DCANAPE_RENDER_DEBUG=0 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\EditorWireValidation.cpp Binaries\x64\Shipping\DX11Renderer\DX11Renderer.lib /FoIntermediate\EditorWireValidation\Shipping.obj /FeIntermediate\EditorWireValidation\Shipping.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\EditorWireValidation\Shipping.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Editor wire validation failed (exit $LASTEXITCODE)." }
}
finally { Pop-Location }
