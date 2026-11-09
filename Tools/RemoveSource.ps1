param(
	# Source file without extension, relative to the project (e.g. Source\Scripts\EnemyAI).
	# Its .h and .cpp go to the Recycle Bin and leave the project.
	[Parameter(Mandatory = $true, Position = 0)]
	[string]$Path,

	[string]$Project = ''
)

$ErrorActionPreference = 'Stop'

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Project)
{
	$Project = Join-Path $toolsDir '..\Game\Game.vcxproj'
}

$Path = $Path -replace '/', '\'
if ($Path -notmatch '^Source(\\[A-Za-z0-9_]+)+$')
{
	throw "'$Path' is not a source file under Source."
}

$projectPath = (Resolve-Path $Project).Path
$projectDir = Split-Path $projectPath -Parent
$includes = @("$Path.h", "$Path.cpp")

Add-Type -AssemblyName Microsoft.VisualBasic
$removed = @()
foreach ($include in $includes)
{
	$file = Join-Path $projectDir $include
	if (Test-Path $file)
	{
		[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($file, 'OnlyErrorDialogs', 'SendToRecycleBin')
		$removed += $include
	}
}

$xml = New-Object System.Xml.XmlDocument
$xml.PreserveWhitespace = $true
$xml.Load($projectPath)

$msbuildNamespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
$namespaceManager = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$namespaceManager.AddNamespace('m', $msbuildNamespace)

$items = @($xml.SelectNodes('//m:ItemGroup/m:ClInclude | //m:ItemGroup/m:ClCompile', $namespaceManager) |
	Where-Object { $includes -contains $_.GetAttribute('Include') })
foreach ($item in $items)
{
	$previous = $item.PreviousSibling
	if ($previous -and $previous.NodeType -eq [System.Xml.XmlNodeType]::Whitespace)
	{
		$item.ParentNode.RemoveChild($previous) | Out-Null
	}
	$item.ParentNode.RemoveChild($item) | Out-Null
}

if ($removed.Count -eq 0 -and $items.Count -eq 0)
{
	throw "Nothing to remove for $Path."
}

if ($items.Count -gt 0)
{
	$settings = New-Object System.Xml.XmlWriterSettings
	$settings.Encoding = New-Object System.Text.UTF8Encoding $true
	$settings.Indent = $false
	$writer = [System.Xml.XmlWriter]::Create($projectPath, $settings)
	$xml.Save($writer)
	$writer.Close()

	& (Join-Path $toolsDir 'GenerateFilters.ps1') $projectPath | Out-Null
}

foreach ($include in $removed)
{
	Write-Host "Moved $include to the Recycle Bin"
}
Write-Host "Removed $($items.Count) item(s) from $(Split-Path $projectPath -Leaf)"
