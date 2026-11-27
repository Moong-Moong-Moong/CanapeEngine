# Builds Game (Shipping) and packages only what the Knights board game needs.
# Output: Dist/KnightsGame/ and Dist/KnightsGame.zip
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools/PackageKnightsGame.ps1
#   powershell -ExecutionPolicy Bypass -File Tools/PackageKnightsGame.ps1 -SkipBuild
param(
    [switch]$SkipBuild,
    [switch]$NoZip
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configuration = 'Shipping'
$buildDir = Join-Path $repoRoot "Binaries/x64/$configuration/Game"
$contentSource = Join-Path $repoRoot 'Game/Content'
$distRoot = Join-Path $repoRoot 'Dist'
$packageDir = Join-Path $distRoot 'KnightsGame'
$zipPath = Join-Path $distRoot 'KnightsGame.zip'

$startupScene = 'KnightsTitle'
$scenes = @('KnightsTitle', 'KnightsGame')
$contentFiles = @(
    'Fonts/Pretendard-Regular.ttf',
    'Fonts/Pretendard-OFL.txt'
)

function Invoke-ShippingBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere not found: $vswhere" }
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'A Visual Studio C++ installation is required.' }

    $binDir = Join-Path $vsPath 'MSBuild/Current/Bin'
    switch ($env:PROCESSOR_ARCHITECTURE) {
        'AMD64' { $binDir = Join-Path $binDir 'amd64' }
        'ARM64' { $binDir = Join-Path $binDir 'arm64' }
    }
    $msbuild = Join-Path $binDir 'MSBuild.exe'
    if (-not (Test-Path $msbuild)) { throw "MSBuild not found: $msbuild" }

    $project = Join-Path $repoRoot 'Game/Game.vcxproj'
    Write-Host "[1/3] Building Game ($configuration)..."
    & $msbuild $project "-p:Configuration=$configuration" -p:Platform=x64 -m -nologo -v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
}

function Copy-Required([string]$source, [string]$destination) {
    if (-not (Test-Path $source)) { throw "Required file missing: $source" }
    $parent = Split-Path $destination -Parent
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Copy-Item -Path $source -Destination $destination -Recurse -Force
}

if ($SkipBuild) {
    Write-Host '[1/3] Skipping build (-SkipBuild).'
} else {
    Invoke-ShippingBuild
}

Write-Host "[2/3] Assembling $packageDir"
if (Test-Path $packageDir) { Remove-Item -Path $packageDir -Recurse -Force }
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

# Executable plus the engine files the build copies next to it.
Copy-Required (Join-Path $buildDir 'Game.exe') (Join-Path $packageDir 'KnightsGame.exe')
Copy-Required (Join-Path $buildDir 'Shaders') (Join-Path $packageDir 'Shaders')
Copy-Required (Join-Path $buildDir 'EngineContent') (Join-Path $packageDir 'EngineContent')

# Game content: only the Knights scenes and the UI font, taken from source.
foreach ($scene in $scenes) {
    $relative = "Scenes/$scene.json"
    Copy-Required (Join-Path $contentSource $relative) (Join-Path $packageDir "Content/$relative")
}
foreach ($relative in $contentFiles) {
    Copy-Required (Join-Path $contentSource $relative) (Join-Path $packageDir "Content/$relative")
}

# Pin the startup scene regardless of the developer's local Game.json.
$gameConfig = "{`r`n    `"startupScene`": `"$startupScene`"`r`n}`r`n"
[System.IO.File]::WriteAllText((Join-Path $packageDir 'Content/Game.json'), $gameConfig, (New-Object System.Text.UTF8Encoding($false)))

$sizeMb = (Get-ChildItem -Path $packageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB

if ($NoZip) {
    Write-Host '[3/3] Skipping zip (-NoZip).'
} else {
    Write-Host "[3/3] Compressing $zipPath"
    if (Test-Path $zipPath) { Remove-Item -Path $zipPath -Force }
    Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zipPath
}

Write-Host ('Done. Package: {0} ({1:N1} MB)' -f $packageDir, $sizeMb)
Write-Host 'Target PCs need the Microsoft Visual C++ Redistributable (x64).'
