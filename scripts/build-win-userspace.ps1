# Builds mihomo.exe and wintun.dll into .build\win.
# Fairlead uses mihomo for TUN, process routing, WireGuard, and AmneziaWG.
# scripts\build-win.bat ships them to dist\win\bin. Go is downloaded into .build and is not required on PATH.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$cache = Join-Path $root ".build\win-userspace"
$dist = Join-Path $root ".build\win"
$goVersion = "1.27.3"
$mihomoRef = "v1.19.34"
$goZip = Join-Path $cache "go.zip"
$goRoot = Join-Path $cache "go"
New-Item -ItemType Directory -Force -Path $cache, $dist | Out-Null

function Get-File($url, $dest) {
	Write-Host "get $url"
	curl.exe -fsSL --retry 3 -o $dest $url
	if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
}

if (-not (Test-Path (Join-Path $goRoot "bin\go.exe"))) {
	Get-File "https://go.dev/dl/go$goVersion.windows-amd64.zip" $goZip
	if (Test-Path $goRoot) { Remove-Item -Recurse -Force $goRoot }
	Expand-Archive -Path $goZip -DestinationPath $cache -Force
}

$env:Path = "$(Join-Path $goRoot 'bin');$env:Path"
$env:GOTOOLCHAIN = "local"
$env:CGO_ENABLED = "0"
$env:GOOS = "windows"
$env:GOARCH = "amd64"
$env:GOSUMDB = "off"
$env:GOPROXY = "https://proxy.golang.org,direct"

$mihomo = Join-Path $cache "mihomo"
if (-not (Test-Path (Join-Path $mihomo ".git"))) {
	if (Test-Path $mihomo) { Remove-Item -Recurse -Force $mihomo }
	git clone --depth 1 --branch $mihomoRef https://github.com/MetaCubeX/mihomo.git $mihomo
} else {
	Push-Location $mihomo
	git fetch --depth 1 origin tag $mihomoRef
	git checkout -f $mihomoRef
	Pop-Location
}

Push-Location $mihomo
go build -trimpath -ldflags "-s -w" -o (Join-Path $dist "mihomo.exe") .
if ($LASTEXITCODE -ne 0) { throw "mihomo build failed" }
Pop-Location

$wintunZip = Join-Path $cache "wintun.zip"
Get-File "https://www.wintun.net/builds/wintun-0.14.1.zip" $wintunZip
$wintunDir = Join-Path $cache "wintun"
if (Test-Path $wintunDir) { Remove-Item -Recurse -Force $wintunDir }
Expand-Archive -Path $wintunZip -DestinationPath $wintunDir -Force
Copy-Item (Join-Path $wintunDir "wintun\bin\amd64\wintun.dll") (Join-Path $dist "wintun.dll") -Force
Write-Host "wrote .build\win\mihomo.exe, wintun.dll"
