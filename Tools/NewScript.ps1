param(
	[Parameter(Mandatory = $true, Position = 0)]
	[string]$Name,

	[string]$Folder = '',

	[string[]]$With = @(),

	[switch]$HeaderOnly,

	# Creates a Canape::FsmState under Source instead of a ScriptComponent.
	[switch]$State,

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

$lifecycleOrder = @('Awake', 'OnEnable', 'Start', 'FixedUpdate', 'Update', 'LateUpdate', 'OnDisable', 'OnDestroy')
$With = @($With | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($function in $With)
{
	if ($lifecycleOrder -notcontains $function)
	{
		throw "'$function' is not a ScriptComponent function. Use: $($lifecycleOrder -join ', ')"
	}
}

if ($Folder -and ($Folder -notmatch '^[A-Za-z0-9_]+([\\/][A-Za-z0-9_]+)*$'))
{
	throw "'$Folder' is not a valid folder name."
}

if ($State)
{
	$baseClass = 'Canape::FsmState'
	$functions = @(
		@{ Name = 'Enter'; Parameters = '' },
		@{ Name = 'Update'; Parameters = 'float deltaTime'; Definition = '[[maybe_unused]] float deltaTime' },
		@{ Name = 'Exit'; Parameters = '' }
	)
}
else
{
	$baseClass = 'Canape::ScriptComponent'
	$requested = @('Awake', 'Start', 'FixedUpdate', 'Update') + $With
	$functions = @($lifecycleOrder | Where-Object { $requested -contains $_ } | ForEach-Object { @{ Name = $_; Parameters = '' } })
}

$projectPath = (Resolve-Path $Project).Path
$projectDir = Split-Path $projectPath -Parent

$relativeFolder = if ($State) { 'Source' } else { 'Source\Scripts' }
if ($Folder)
{
	$relativeFolder = Join-Path $relativeFolder ($Folder -replace '/', '\')
}
$includeFolder = ($relativeFolder -replace '^Source\\?', '') -replace '\\', '/'
$includePath = if ($includeFolder) { "$includeFolder/$Name.h" } else { "$Name.h" }

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
[void]$header.Append("class $Name final : public $baseClass$newline")
[void]$header.Append("{$newline")
[void]$header.Append("$indent" + "RTTR_ENABLE($baseClass)$newline")
[void]$header.Append("$indent" + "RTTR_REGISTRATION_FRIEND$newline$newline")
[void]$header.Append($(if ($State) { "public:$newline" } else { "protected:$newline" }))
foreach ($function in $functions)
{
	$signature = "void $($function.Name)($($function.Parameters))"
	if ($HeaderOnly)
	{
		$definition = if ($function.Definition) { $function.Definition } else { $function.Parameters }
		[void]$header.Append("$indent" + "void $($function.Name)($definition) override$newline")
		[void]$header.Append("$indent{$newline")
		[void]$header.Append("$indent}$newline")
		if ($function -ne $functions[-1])
		{
			[void]$header.Append($newline)
		}
	}
	else
	{
		[void]$header.Append("$indent$signature override;$newline")
	}
}
[void]$header.Append("};$newline")

$encoding = New-Object System.Text.UTF8Encoding $true
New-Item -ItemType Directory -Force $targetDir | Out-Null
[System.IO.File]::WriteAllText($headerPath, $header.ToString(), $encoding)

if (-not $HeaderOnly)
{
	$source = New-Object System.Text.StringBuilder
	[void]$source.Append("#include `"$includePath`"$newline")
	[void]$source.Append($newline)
	[void]$source.Append("RTTR_REGISTRATION$newline")
	[void]$source.Append("{$newline")
	[void]$source.Append("$indent" + "rttr::registration::class_<$Name>(`"$Name`")$newline")
	[void]$source.Append("$indent$indent" + ".constructor<>()(rttr::policy::ctor::as_raw_ptr);$newline")
	[void]$source.Append("}$newline")
	foreach ($function in $functions)
	{
		[void]$source.Append($newline)
		$definition = if ($function.Definition) { $function.Definition } else { $function.Parameters }
		[void]$source.Append("void ${Name}::$($function.Name)($definition)$newline")
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
