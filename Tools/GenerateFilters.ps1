param(
	[Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
	[string[]]$Projects
)

$ErrorActionPreference = 'Stop'

$msbuildNamespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
$itemTypes = @('ClInclude', 'ClCompile', 'None', 'FxCompile', 'Text', 'ResourceCompile', 'Image', 'Natvis', 'CustomBuild')
$md5 = [System.Security.Cryptography.MD5]::Create()

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild)
{
	throw 'MSBuild from a Visual Studio installation is required.'
}

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

function Get-ProjectRelativePath([string]$path, [string]$projectDirectory)
{
	if (-not [System.IO.Path]::IsPathRooted($path))
	{
		return $path
	}
	$relative = ([uri]$projectDirectory).MakeRelativeUri([uri]$path).ToString()
	return [uri]::UnescapeDataString($relative) -replace '/', '\'
}

foreach ($project in $Projects)
{
	$projectPath = (Resolve-Path $project).Path
	$projectName = [System.IO.Path]::GetFileNameWithoutExtension($projectPath)
	$projectDirectory = (Split-Path $projectPath -Parent) + '\'

	# Keep filters already assigned to existing items; only new items get a filter chosen here.
	$existingFilters = @{}
	# Keep the identifiers of filters that already exist so unrelated filters do not change.
	$existingIdentifiers = @{}
	if (Test-Path "$projectPath.filters")
	{
		[xml]$existingXml = Get-Content "$projectPath.filters" -Raw
		$existingNamespaces = New-Object System.Xml.XmlNamespaceManager($existingXml.NameTable)
		$existingNamespaces.AddNamespace('m', $msbuildNamespace)
		foreach ($node in $existingXml.SelectNodes('//m:ItemGroup/m:Filter[@Include]', $existingNamespaces))
		{
			$identifier = $node.SelectSingleNode('m:UniqueIdentifier', $existingNamespaces)
			if ($identifier)
			{
				$existingIdentifiers[$node.GetAttribute('Include')] = $identifier.InnerText
			}
		}
		foreach ($node in $existingXml.SelectNodes('//m:ItemGroup/m:*[@Include and not(self::m:Filter)]', $existingNamespaces))
		{
			$filterNode = $node.SelectSingleNode('m:Filter', $existingNamespaces)
			$existingFilters["$($node.LocalName)|$($node.GetAttribute('Include'))"] = if ($filterNode) { $filterNode.InnerText } else { '' }
		}
	}

	# A new item joins the filter of an existing item in its folder, so files added to a hand-picked filter stay there.
	$folderFilters = @{}
	foreach ($key in ($existingFilters.Keys | Sort-Object))
	{
		$folder = Split-Path $key.Split('|', 2)[1] -Parent
		if (-not $folderFilters.ContainsKey($folder))
		{
			$folderFilters[$folder] = $existingFilters[$key]
		}
	}

	# Evaluated items, so item lists and wildcards become the files Solution Explorer shows.
	$evaluation = & $msbuild $projectPath -nologo "-getItem:$($itemTypes -join ',')" -p:Configuration=Debug -p:Platform=x64
	if ($LASTEXITCODE -ne 0)
	{
		throw "MSBuild could not evaluate ${projectPath}: $evaluation"
	}
	$evaluated = ($evaluation -join "`n") | ConvertFrom-Json

	$items = New-Object System.Collections.Generic.List[object]
	foreach ($type in $itemTypes)
	{
		foreach ($item in $evaluated.Items.$type)
		{
			$include = Get-ProjectRelativePath $item.Identity $projectDirectory
			$key = "$type|$include"
			$folder = Split-Path $include -Parent
			$filter = if ($existingFilters.ContainsKey($key)) { $existingFilters[$key] }
				elseif ($folderFilters.ContainsKey($folder)) { $folderFilters[$folder] }
				else { Get-FilterPath $include }
			$items.Add([pscustomobject]@{ Type = $type; Include = $include; Filter = $filter })
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
			$identifier = if ($existingIdentifiers.ContainsKey($filter)) { $existingIdentifiers[$filter] } else { "{$(Get-StableGuid "$projectName|$filter")}" }
			[void]$builder.AppendLine("      <UniqueIdentifier>$identifier</UniqueIdentifier>")
			[void]$builder.AppendLine('    </Filter>')
		}
		[void]$builder.AppendLine('  </ItemGroup>')
	}

	foreach ($type in $itemTypes)
	{
		$typed = [System.Collections.Generic.List[object]]@($items | Where-Object { $_.Type -eq $type })
		if ($typed.Count -eq 0)
		{
			continue
		}
		# Ordinal, like the filter set: culture sorting differs between Windows PowerShell and pwsh.
		$typed.Sort({ param($left, $right) [System.StringComparer]::OrdinalIgnoreCase.Compare($left.Include, $right.Include) })

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
