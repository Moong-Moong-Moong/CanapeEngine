param([string]$VisualStudioPath)

# UI phase-1 validation:
#   1. UICoreValidation: layout, painting and pointer routing on the CPU (UI sources only).
#   2. UIPassValidation: the DX11 UI pass on WARP with the D3D11 debug layer (links DX11Renderer.lib).
# No project settings are changed; outputs go to Intermediate/UIValidation.
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/UIValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
# stb_truetype comes from the vcpkg manifest; build the solution once so vcpkg installs it.
$stbInclude = Join-Path $repoRoot 'vcpkg_installed/x64-windows-static-md/x64-windows-static-md/include'
if (-not (Test-Path (Join-Path $stbInclude 'stb_truetype.h'))) { throw "stb_truetype.h not found under $stbInclude. Build Runtime once to install vcpkg packages." }
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Path (Join-Path $validationDir 'Shaders') -Force | Out-Null
Copy-Item (Join-Path $repoRoot 'Engine/DX11Renderer/Shader/*') (Join-Path $validationDir 'Shaders') -Recurse -Force

$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
fxc /nologo /WX /Zpr /T vs_5_0 /E VSMain /Fo NUL Engine\DX11Renderer\Shader\UI.hlsl
if errorlevel 1 exit /b %errorlevel%
fxc /nologo /WX /Zpr /T ps_5_0 /E PSMain /Fo NUL Engine\DX11Renderer\Shader\UI.hlsl
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /W4 /WX /D_DEBUG /utf-8 /ICommon /IEngine /IEngine\Source /I"$stbInclude" Engine\Source\UI\Tests\UICoreValidation.cpp Engine\Source\UI\UIElement.cpp Engine\Source\UI\UIImage.cpp Engine\Source\UI\UIButton.cpp Engine\Source\UI\UIPainter.cpp Engine\Source\UI\UIPointerRouter.cpp Engine\Source\UI\UIBox.cpp Engine\Source\UI\UIText.cpp Engine\Source\UI\Text\UIFont.cpp Engine\Source\UI\Text\UIGlyphAtlas.cpp Engine\Source\UI\Text\UITextLayout.cpp /FoIntermediate\UIValidation\ /FeIntermediate\UIValidation\UICoreValidation.exe
if errorlevel 1 exit /b %errorlevel%
Intermediate\UIValidation\UICoreValidation.exe
if errorlevel 1 exit /b %errorlevel%
"$msbuild" Engine\DX11Renderer\DX11Renderer.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++23preview /EHsc /MDd /Z7 /D_DEBUG /DCANAPE_RENDER_DEBUG=1 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /ICommon /IEngine Engine\DX11Renderer\Tests\UIPassValidation.cpp Binaries\x64\Debug\DX11Renderer\DX11Renderer.lib /FoIntermediate\UIValidation\UIPassValidation.obj /FeIntermediate\UIValidation\UIPassValidation.exe /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\UIValidation\UIPassValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "UI validation failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
