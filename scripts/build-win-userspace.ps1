# Builds amneziawg-go.exe and wintun.dll into .build\win.
# amneziawg-go carries plain WireGuard too. Ordinary configs send no obfuscation fields.
# scripts\build-win.bat copies them next to Fairlead.exe. Go is downloaded into .build and is not required on PATH.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$cache = Join-Path $root ".build\win-userspace"
$dist = Join-Path $root ".build\win"
$goVersion = "1.27.3"
$awgRef = "v3.1.20260828"
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

$awg = Join-Path $cache "amneziawg-go"
if (Test-Path $awg) { Remove-Item -Recurse -Force $awg }
git clone --depth 1 --branch $awgRef https://github.com/amnezia-vpn/amneziawg-go.git $awg
Push-Location $awg
$uapi = Join-Path $awg "ipc\uapi_windows.go"
(Get-Content $uapi -Raw).Replace('O:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)S:(ML;;NWNRNX;;;HI)', 'D:P(A;;GA;;;SY)(A;;GA;;;BA)') | Set-Content $uapi -NoNewline
go build -trimpath -ldflags "-s -w" -o (Join-Path $dist "amneziawg-go.exe") .
if ($LASTEXITCODE -ne 0) { throw "amneziawg-go build failed" }
Pop-Location

$wintunZip = Join-Path $cache "wintun.zip"
Get-File "https://www.wintun.net/builds/wintun-0.14.1.zip" $wintunZip
$wintunDir = Join-Path $cache "wintun"
if (Test-Path $wintunDir) { Remove-Item -Recurse -Force $wintunDir }
Expand-Archive -Path $wintunZip -DestinationPath $wintunDir -Force
Copy-Item (Join-Path $wintunDir "wintun\bin\amd64\wintun.dll") (Join-Path $dist "wintun.dll") -Force
Write-Host "wrote .build\win\amneziawg-go.exe, wintun.dll"
