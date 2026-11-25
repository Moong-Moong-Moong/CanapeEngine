param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$validationDir = Join-Path $repoRoot "Intermediate/UndoHistoryValidation/$Configuration"
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null

if ($Configuration -eq 'Debug') { $crt = '/MDd /D_DEBUG /Od' } else { $crt = '/MD /DNDEBUG /O2' }

$out = "Intermediate\UndoHistoryValidation\$Configuration"
$sources = @(
    'Editor\Source\Undo\Tests\UndoHistoryValidation.cpp',
    'Editor\Source\Undo\UndoHistory.cpp'
) -join ' '
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc $crt /utf-8 /W4 /Z7 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /ICommon /IEditor\Source /Fo$out\ $sources /Fe$out\UndoHistoryValidation.exe
if errorlevel 1 exit /b %errorlevel%
$out\UndoHistoryValidation.exe
exit /b %errorlevel%
"@

$batchPath = Join-Path $validationDir 'run.cmd'
Set-Content -Path $batchPath -Value $batch -Encoding ASCII
Push-Location $repoRoot
try {
    $ErrorActionPreference = 'Continue'
    & cmd.exe /c $batchPath 2>&1 | ForEach-Object { "$_" }
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
