param([string]$PackageDir)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    $ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $PackageDir = [System.IO.Path]::GetFullPath(
        (Join-Path $ProjectDir "..\06-Windows交付\飞秒质谱工作站-Demo"))
}

$Required = @(
    "飞秒质谱工作站.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "platforms\qwindows.dll",
    "sqldrivers\qsqlite.dll",
    "resources\data\qitest_spectral_library.sqlite",
    "resources\config\ai-model-manifest.json",
    "resources\knowledge\operator_manual_zh.md",
    "resources\notices\THIRD_PARTY_NOTICES.md"
)
foreach ($RelativePath in $Required) {
    $Path = Join-Path $PackageDir $RelativePath
    if (-not (Test-Path -LiteralPath $Path)) { throw "交付包缺少：$RelativePath" }
}

$Library = Join-Path $PackageDir "resources\data\qitest_spectral_library.sqlite"
if ((Get-Item -LiteralPath $Library).Length -le 0) { throw "参考谱库为空" }
Write-Host "Windows Demo 结构校验通过：$PackageDir"
