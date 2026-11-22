param([string]$VisualStudioPath, [switch]$Hardware)

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
$deviceArgument = if ($Hardware) { '--hardware' } else { '' }
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
for %%S in (OutlineMask OutlineComposite OutlineCompositeMSAA) do (
  fxc /nologo /WX /Zpr /T ps_5_0 /E PSMain /Fo NUL Engine\DX11Renderer\Shader\%%S.hlsl
  if errorlevel 1 exit /b 1
)
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\OutlineValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\OutlineValidation\Debug.obj /FeIntermediate\OutlineValidation\Debug.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\OutlineValidation\Debug.exe $deviceArgument
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Shipping /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MD /Z7 /DNDEBUG /DCANAPE_RENDER_DEBUG=0 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\OutlineValidation.cpp Binaries\x64\Shipping\DX11Renderer\DX11Renderer.lib /FoIntermediate\OutlineValidation\Shipping.obj /FeIntermediate\OutlineValidation\Shipping.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\OutlineValidation\Shipping.exe $deviceArgument
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Outline validation failed (exit $LASTEXITCODE)." }
}
finally { Pop-Location }
