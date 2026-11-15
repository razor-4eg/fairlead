$ErrorActionPreference = "Stop"
$langs = @{
	en = "Auto"
	ru = "Авто"
	de = "Automatisch"
	es = "Automático"
	fr = "Automatique"
	pt = "Automático"
	"zh-Hans" = "自动"
}
Get-ChildItem (Join-Path (Split-Path $PSScriptRoot -Parent) "platform\mac\Support") -Filter Localizable.strings -Recurse | ForEach-Object {
	$code = $_.Directory.Name -replace "\.lproj$", ""
	$auto = $langs[$code]
	if (-not $auto) { $auto = "Auto" }
	$lines = [System.Collections.Generic.List[string]]([System.IO.File]::ReadAllLines($_.FullName))
	$has = $false
	foreach ($line in $lines) {
		if ($line.StartsWith('"Auto" =')) { $has = $true; break }
	}
	if (-not $has) {
		$insert = '"Auto" = "' + $auto + '";'
		$idx = -1
		for ($i = 0; $i -lt $lines.Count; $i++) {
			if ($lines[$i].StartsWith('"Language" =')) { $idx = $i; break }
		}
		if ($idx -ge 0) {
			$lines.Insert($idx, $insert)
		} else {
			$lines.Add($insert)
		}
		[System.IO.File]::WriteAllLines($_.FullName, $lines)
		Write-Host "added Auto to $code"
	} else {
		Write-Host "ok $code"
	}
}
