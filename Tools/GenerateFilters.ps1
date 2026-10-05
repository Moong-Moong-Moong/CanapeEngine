param(
	[Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
	[string[]]$Projects
)

$ErrorActionPreference = 'Stop'

$msbuildNamespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
$itemTypes = @('ClInclude', 'ClCompile', 'None', 'FxCompile', 'Text', 'ResourceCompile', 'Image', 'Natvis', 'CustomBuild')
$md5 = [System.Security.Cryptography.MD5]::Create()

function Get-StableGuid([string]$seed)
{
	$bytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($seed))
	return ([guid]::new($bytes)).ToString().ToUpperInvariant()
}

function Get-FilterPath([string]$include)
{
	$normalized = $include -replace '/', '\'
	while ($normalized.StartsWith('..\'))
	{
		$normalized = $normalized.Substring(3)
	}
	$normalized = $normalized.TrimStart('.', '\')
	return Split-Path $normalized -Parent
}

foreach ($project in $Projects)
{
	$projectPath = (Resolve-Path $project).Path
	$projectName = [System.IO.Path]::GetFileNameWithoutExtension($projectPath)
	[xml]$xml = Get-Content $projectPath -Raw

	$namespaceManager = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
	$namespaceManager.AddNamespace('m', $msbuildNamespace)

	$items = New-Object System.Collections.Generic.List[object]
	foreach ($type in $itemTypes)
	{
		foreach ($node in $xml.SelectNodes("//m:ItemGroup/m:$type", $namespaceManager))
		{
			$items.Add([pscustomobject]@{ Type = $type; Include = $node.GetAttribute('Include'); Filter = (Get-FilterPath $node.GetAttribute('Include')) })
		}
	}

	$filters = New-Object System.Collections.Generic.SortedSet[string]([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($item in $items)
	{
		$folder = $item.Filter
		while ($folder)
		{
			[void]$filters.Add($folder)
			$folder = Split-Path $folder -Parent
		}
	}

	$builder = New-Object System.Text.StringBuilder
	[void]$builder.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
	[void]$builder.AppendLine("<Project ToolsVersion=`"4.0`" xmlns=`"$msbuildNamespace`">")

	if ($filters.Count -gt 0)
	{
		[void]$builder.AppendLine('  <ItemGroup>')
		foreach ($filter in $filters)
		{
			[void]$builder.AppendLine("    <Filter Include=`"$filter`">")
			[void]$builder.AppendLine("      <UniqueIdentifier>{$(Get-StableGuid "$projectName|$filter")}</UniqueIdentifier>")
			[void]$builder.AppendLine('    </Filter>')
		}
		[void]$builder.AppendLine('  </ItemGroup>')
	}

	foreach ($type in $itemTypes)
	{
		$typed = $items | Where-Object { $_.Type -eq $type } | Sort-Object Include
		if (-not $typed)
		{
			continue
		}

		[void]$builder.AppendLine('  <ItemGroup>')
		foreach ($item in $typed)
		{
			if ($item.Filter)
			{
				[void]$builder.AppendLine("    <$type Include=`"$($item.Include)`">")
				[void]$builder.AppendLine("      <Filter>$($item.Filter)</Filter>")
				[void]$builder.AppendLine("    </$type>")
			}
			else
			{
				[void]$builder.AppendLine("    <$type Include=`"$($item.Include)`" />")
			}
		}
		[void]$builder.AppendLine('  </ItemGroup>')
	}

	[void]$builder.AppendLine('</Project>')

	$filtersPath = "$projectPath.filters"
	$content = $builder.ToString() -replace "`r?`n", "`r`n"
	[System.IO.File]::WriteAllText($filtersPath, $content, (New-Object System.Text.UTF8Encoding $true))
	Write-Host "$filtersPath : $($filters.Count) filters, $($items.Count) items"
}
