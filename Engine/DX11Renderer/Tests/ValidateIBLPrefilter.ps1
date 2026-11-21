param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/IBLPrefilterValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
& (Join-Path $PSScriptRoot 'ValidateIBLIrradiance.ps1') -VisualStudioPath $VisualStudioPath
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /T ps_5_0 /E PSPrefilter /Fo NUL Engine\DX11Renderer\Shader\IBLPrefilter.hlsl
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\IBLPrefilterValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\IBLPrefilterValidation\IBLPrefilterValidation.obj /FeIntermediate\IBLPrefilterValidation\IBLPrefilterValidation.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\IBLPrefilterValidation\IBLPrefilterValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "IBL prefilter validation failed (exit $LASTEXITCODE)." }
}
finally { Pop-Location }
