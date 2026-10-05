param(
	[Parameter(Mandatory = $true, Position = 0)]
	[string]$Name,

	[string]$Folder = '',

	[string[]]$With = @(),

	[switch]$HeaderOnly,

	[string]$Project = ''
)

$ErrorActionPreference = 'Stop'

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Project)
{
	$Project = Join-Path $toolsDir '..\Game\Game.vcxproj'
}

if ($Name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$')
{
	throw "'$Name' is not a valid C++ class name."
}

$lifecycleOrder = @('Awake', 'OnEnable', 'Start', 'Update', 'LateUpdate', 'OnDisable', 'OnDestroy')
$With = @($With | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($function in $With)
{
	if ($lifecycleOrder -notcontains $function)
	{
		throw "'$function' is not a MonoBehaviour function. Use: $($lifecycleOrder -join ', ')"
	}
}

if ($Folder -and ($Folder -notmatch '^[A-Za-z0-9_]+([\\/][A-Za-z0-9_]+)*$'))
{
	throw "'$Folder' is not a valid folder name."
}

$requested = @('Awake', 'Start', 'Update') + $With
$functions = $lifecycleOrder | Where-Object { $requested -contains $_ }

$projectPath = (Resolve-Path $Project).Path
$projectDir = Split-Path $projectPath -Parent

$relativeFolder = 'Source\Scripts'
if ($Folder)
{
	$relativeFolder = Join-Path $relativeFolder ($Folder -replace '/', '\')
}
$includeFolder = ($relativeFolder -replace '^Source\\', '') -replace '\\', '/'

$targetDir = Join-Path $projectDir $relativeFolder
$headerRelative = Join-Path $relativeFolder "$Name.h"
$sourceRelative = Join-Path $relativeFolder "$Name.cpp"
$headerPath = Join-Path $projectDir $headerRelative
$sourcePath = Join-Path $projectDir $sourceRelative

if ((Test-Path $headerPath) -or (Test-Path $sourcePath))
{
	throw "$Name already exists in $relativeFolder."
}

$newline = "`r`n"
$indent = '    '

$header = New-Object System.Text.StringBuilder
[void]$header.Append("#pragma once$newline$newline")
[void]$header.Append("#include <Engine.h>$newline$newline")
[void]$header.Append("class $Name final : public Canape::MonoBehaviour$newline")
[void]$header.Append("{$newline")
[void]$header.Append("protected:$newline")
foreach ($function in $functions)
{
	if ($HeaderOnly)
	{
		[void]$header.Append("$indent" + "void $function() override$newline")
		[void]$header.Append("$indent{$newline")
		[void]$header.Append("$indent}$newline")
		if ($function -ne $functions[-1])
		{
			[void]$header.Append($newline)
		}
	}
	else
	{
		[void]$header.Append("$indent" + "void $function() override;$newline")
	}
}
[void]$header.Append("};$newline")

$encoding = New-Object System.Text.UTF8Encoding $true
New-Item -ItemType Directory -Force $targetDir | Out-Null
[System.IO.File]::WriteAllText($headerPath, $header.ToString(), $encoding)

if (-not $HeaderOnly)
{
	$source = New-Object System.Text.StringBuilder
	[void]$source.Append("#include `"$includeFolder/$Name.h`"$newline")
	foreach ($function in $functions)
	{
		[void]$source.Append($newline)
		[void]$source.Append("void ${Name}::${function}()$newline")
		[void]$source.Append("{$newline")
		[void]$source.Append("}$newline")
	}
	[System.IO.File]::WriteAllText($sourcePath, $source.ToString(), $encoding)
}

$xml = New-Object System.Xml.XmlDocument
$xml.PreserveWhitespace = $true
$xml.Load($projectPath)

$msbuildNamespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
$namespaceManager = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$namespaceManager.AddNamespace('m', $msbuildNamespace)

function Add-ProjectItem([string]$itemType, [string]$include)
{
	$existing = $xml.SelectNodes("//m:ItemGroup/m:$itemType", $namespaceManager)
	$element = $xml.CreateElement($itemType, $msbuildNamespace)
	$element.SetAttribute('Include', $include)

	if ($existing.Count -gt 0)
	{
		$last = $existing[$existing.Count - 1]
		$group = $last.ParentNode
		$group.InsertAfter($element, $last) | Out-Null
		$group.InsertAfter($xml.CreateWhitespace("$newline    "), $last) | Out-Null
		return
	}

	$group = $xml.CreateElement('ItemGroup', $msbuildNamespace)
	$group.AppendChild($xml.CreateWhitespace("$newline    ")) | Out-Null
	$group.AppendChild($element) | Out-Null
	$group.AppendChild($xml.CreateWhitespace("$newline  ")) | Out-Null

	$anchor = $xml.SelectSingleNode("//m:Import[contains(@Project, 'Microsoft.Cpp.targets')]", $namespaceManager)
	$xml.DocumentElement.InsertBefore($group, $anchor) | Out-Null
	$xml.DocumentElement.InsertBefore($xml.CreateWhitespace("$newline  "), $anchor) | Out-Null
}

Add-ProjectItem 'ClInclude' $headerRelative
if (-not $HeaderOnly)
{
	Add-ProjectItem 'ClCompile' $sourceRelative
}

$settings = New-Object System.Xml.XmlWriterSettings
$settings.Encoding = $encoding
$settings.Indent = $false
$writer = [System.Xml.XmlWriter]::Create($projectPath, $settings)
$xml.Save($writer)
$writer.Close()

& (Join-Path $toolsDir 'GenerateFilters.ps1') $projectPath | Out-Null

Write-Host "Created $headerRelative"
if (-not $HeaderOnly)
{
	Write-Host "Created $sourceRelative"
}
Write-Host "Registered in $(Split-Path $projectPath -Leaf)"
