# Strip WASDK payload that Fairlead does not need from dist\win\bin.
$ErrorActionPreference = "SilentlyContinue"
$bin = Join-Path (Split-Path $PSScriptRoot -Parent) "dist\win\bin"
if (-not (Test-Path -LiteralPath $bin)) { exit 0 }

$keepLang = @(
  "en-us", "en-GB", "ru-RU", "de-DE", "es-ES", "es-MX",
  "fr-FR", "fr-CA", "pt-BR", "pt-PT", "zh-CN", "zh-TW"
)
Get-ChildItem -LiteralPath $bin -Directory | Where-Object {
  ($_.Name -match "^[a-z]{2}(-[A-Za-z0-9]+)?$") -and ($keepLang -notcontains $_.Name)
} | Remove-Item -Recurse -Force

$junkName = "^(Microsoft\.Web\.WebView2\.Core\.Projection\.dll$|onnxruntime|DirectML|Microsoft\.Windows\.AI\.|Microsoft\.Windows\.Widgets|Microsoft\.Windows\.Workloads|Microsoft\.Windows\.Private\.Workloads|Microsoft\.Graphics\.Imaging|RestartAgent\.exe$|WindowsAppRuntime\.png$|map\.html$|workloads)"
Get-ChildItem -LiteralPath $bin -Recurse -File | Where-Object {
  $_.Name -match $junkName -or $_.Extension -match "^\.(pdb|ilk|winmd|iobj|ipdb)$"
} | Remove-Item -Force

$npu = Join-Path $bin "NpuDetect"
if (Test-Path -LiteralPath $npu) {
  Remove-Item -LiteralPath $npu -Recurse -Force
}

$map = Join-Path $bin "Microsoft.UI.Xaml\Assets\map.html"
if (Test-Path -LiteralPath $map) {
  Remove-Item -LiteralPath $map -Force
}
