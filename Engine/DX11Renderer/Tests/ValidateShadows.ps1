param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/ShadowValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force

# Use embedded compiler debug information to avoid this machine's PDB-manager C1902.
# No project settings are changed. The test itself runs WARP with the D3D11 debug layer.
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
for %%E in (PSMain PSShadowMasked PSDebugShadowVisibility PSDebugWorldNormal PSDebugUV PSDebugMipLOD) do (
  fxc /nologo /WX /Zpr /T ps_5_0 /E %%E /Fo NUL Engine\DX11Renderer\Shader\Mesh.hlsl
  if errorlevel 1 exit /b 1
)
for %%E in (VSMain VSShadow) do (
  fxc /nologo /WX /Zpr /T vs_5_0 /E %%E /Fo NUL Engine\DX11Renderer\Shader\Mesh.hlsl
  if errorlevel 1 exit /b 1
  fxc /nologo /WX /Zpr /D STATIC_VERTEX=1 /T vs_5_0 /E %%E /Fo NUL Engine\DX11Renderer\Shader\Mesh.hlsl
  if errorlevel 1 exit /b 1
)
fxc /nologo /WX /Zpr /T vs_5_0 /E VSMain /Fo NUL Engine\DX11Renderer\Shader\FinalOutput.hlsl
if errorlevel 1 exit /b %errorlevel%
for %%E in (PSMain PSCameraDepth) do (
  fxc /nologo /WX /Zpr /T ps_5_0 /E %%E /Fo NUL Engine\DX11Renderer\Shader\FinalOutput.hlsl
  if errorlevel 1 exit /b 1
)
cl /nologo /std:c++20 /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /ICommon /IEngine Engine\DX11Renderer\Tests\ShadowValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\ShadowValidation\ShadowValidation.obj /FeIntermediate\ShadowValidation\ShadowValidation.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\ShadowValidation\ShadowValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Shadow validation failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
