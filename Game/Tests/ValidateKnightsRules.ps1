param(
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$validationDir = Join-Path $repoRoot 'Intermediate/KnightsRulesValidation'
if (-not $VisualStudioPath) {
    $pf86 = if (${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} } else { 'C:\Program Files (x86)' }
    $vswhere = Join-Path $pf86 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'A Visual Studio C++ installation is required.' }
}
$pf86 = if (${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} } else { 'C:\Program Files (x86)' }
# vcvars64.bat가 vswhere를 PATH에서 찾으므로 설치 폴더를 추가한다.
$env:PATH = "$env:PATH;$pf86\Microsoft Visual Studio\Installer"
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null

# 룰 코어는 표준 라이브러리만 쓰므로 엔진에 링크하지 않고 소스만 컴파일한다.
# KnightsAi.cpp는 Task 4 전에는 없으므로 있을 때만 목록에 넣는다.
$sources = @('Game\Tests\KnightsRulesValidation.cpp', 'Game\Source\KnightsGame\KnightsRules.cpp')
if (Test-Path (Join-Path $repoRoot 'Game/Source/KnightsGame/KnightsAi.cpp')) {
    $sources += 'Game\Source\KnightsGame\KnightsAi.cpp'
}
$sourceList = $sources -join ' '

$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set _CL_=/Z7
cl /nologo /std:c++23preview /EHsc /MD /utf-8 /W4 /WX /IGame\Source $sourceList /FoIntermediate\KnightsRulesValidation\ /FeIntermediate\KnightsRulesValidation\KnightsRulesValidation.exe
exit /b %errorlevel%
"@
$batchPath = Join-Path $validationDir 'validate.cmd'
[IO.File]::WriteAllText($batchPath, $batch.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($false))
$exe = Join-Path $validationDir 'KnightsRulesValidation.exe'
Push-Location $repoRoot
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Knights rules validation build failed (exit $LASTEXITCODE)." }

    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Knights rules validation failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}
