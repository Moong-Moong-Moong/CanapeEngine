param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/MeshShaderValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force

# Compile all affected entry points plus existing shader consumers. The WARP test
# links the actual runtime compiler/include handler, without the renderer library.
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
for %%S in (MeshStaticVS MeshSkinnedVS Mesh) do (
  fxc /nologo /WX /Zpr /T vs_5_0 /E VSMain /Fo NUL Shaders\%%S.hlsl
  if errorlevel 1 exit /b 1
)
fxc /nologo /WX /Zpr /D STATIC_VERTEX=1 /T vs_5_0 /E VSMain /Fo NUL Shaders\Mesh.hlsl
if errorlevel 1 exit /b %errorlevel%
for %%S in (MeshPS Mesh) do (
  fxc /nologo /WX /Zpr /T ps_5_0 /E PSMain /Fo NUL Shaders\%%S.hlsl
  if errorlevel 1 exit /b 1
)
fxc /nologo /WX /Zpr /D STATIC_VERTEX=1 /T ps_5_0 /E PSMain /Fo NUL Shaders\Mesh.hlsl
if errorlevel 1 exit /b %errorlevel%
for %%E in (PSDebugShadowVisibility PSDebugPcssFilterRadius PSDebugWorldNormal PSDebugUV PSDebugMipLOD) do (
  fxc /nologo /WX /Zpr /T ps_5_0 /E %%E /Fo NUL Shaders\Debug\MeshDebug.hlsl
  if errorlevel 1 exit /b 1
)
fxc /nologo /WX /Zpr /T vs_5_0 /E VSShadow /Fo NUL Shaders\Shadow\ShadowDepth.hlsl
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /D STATIC_VERTEX=1 /T vs_5_0 /E VSShadow /Fo NUL Shaders\Shadow\ShadowDepth.hlsl
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /D SKINNED_VERTEX=1 /T vs_5_0 /E VSShadow /Fo NUL Shaders\Shadow\ShadowDepth.hlsl
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /T ps_5_0 /E PSShadowMasked /Fo NUL Shaders\Shadow\ShadowDepth.hlsl
if errorlevel 1 exit /b %errorlevel%
for %%S in (FinalOutput Skybox) do (
  fxc /nologo /WX /Zpr /T vs_5_0 /E VSMain /Fo NUL Shaders\%%S.hlsl
  if errorlevel 1 exit /b 1
  fxc /nologo /WX /Zpr /T ps_5_0 /E PSMain /Fo NUL Shaders\%%S.hlsl
  if errorlevel 1 exit /b 1
)
fxc /nologo /WX /Zpr /T ps_5_0 /E PSCameraDepth /Fo NUL Shaders\Debug\DepthDebug.hlsl
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MD /Z7 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"$repoRoot/Common" /I"$repoRoot/Engine" "$repoRoot/Engine/DX11Renderer/Tests/MeshShaderValidation.cpp" "$repoRoot/Engine/DX11Renderer/Pipeline/DX11ShaderCompiler.cpp" /FeMeshShaderValidation.exe /link d3d11.lib d3dcompiler.lib dxguid.lib
if errorlevel 1 exit /b %errorlevel%
MeshShaderValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $validationDir
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Mesh shader validation failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
