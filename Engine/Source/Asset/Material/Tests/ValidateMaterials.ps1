param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/MaterialValidation'
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /MDd /D_DEBUG /Od /Z7 /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /ICommon /IEngine /IEngine\Source /external:Ivcpkg_installed\x64-windows-static-md\x64-windows-static-md\include /external:W0 /FoIntermediate\MaterialValidation\ Engine\Source\Asset\Material\Tests\MaterialFileValidation.cpp Engine\Source\Asset\Material\MaterialFile.cpp Common\CNDebug\DebugLog.cpp /FeIntermediate\MaterialValidation\MaterialFileValidation.exe /link user32.lib
if errorlevel 1 exit /b %errorlevel%
Intermediate\MaterialValidation\MaterialFileValidation.exe Intermediate\MaterialValidation\Files
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Material validation failed (exit $LASTEXITCODE)." }
}
finally { Pop-Location }
