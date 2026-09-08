param(
    [string]$QtRoot = $env:QITEST_QT_ROOT,
    [string]$BuildDir = "build-windows"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = "C:\Qt\6.10.2\msvc2022_64"
}
$QtRoot = [System.IO.Path]::GetFullPath($QtRoot)
$BuildPath = Join-Path $ProjectDir $BuildDir
$DeliveryRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir "..\06-Windows交付"))
$DeliveryDir = Join-Path $DeliveryRoot "飞秒质谱工作站-Demo"
$ZipPath = Join-Path $DeliveryRoot "飞秒质谱工作站-Windows-Demo.zip"
$ModelName = "Qwen3.5-4B-Q4_K_M.gguf"

if (-not (Test-Path (Join-Path $QtRoot "bin\windeployqt.exe"))) {
    throw "未找到 Qt Windows 工具：$QtRoot"
}

cmake -S $ProjectDir -B $BuildPath -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=$QtRoot
cmake --build $BuildPath --config Release --parallel
ctest --test-dir $BuildPath -C Release --output-on-failure

New-Item -ItemType Directory -Force -Path $DeliveryRoot | Out-Null
if (Test-Path $DeliveryDir) {
    Remove-Item -LiteralPath $DeliveryDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $DeliveryDir | Out-Null

$ExeCandidates = @(
    (Join-Path $BuildPath "飞秒质谱工作站.exe"),
    (Join-Path $BuildPath "Release\飞秒质谱工作站.exe")
)
$ExeSource = $ExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $ExeSource) { throw "Windows 可执行文件未生成" }
Copy-Item -LiteralPath $ExeSource -Destination $DeliveryDir
& (Join-Path $QtRoot "bin\windeployqt.exe") --release --compiler-runtime `
    (Join-Path $DeliveryDir "飞秒质谱工作站.exe")

$Resources = Join-Path $DeliveryDir "resources"
foreach ($Folder in @("data", "config", "knowledge", "notices")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Resources $Folder) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $ProjectDir "data\library\qitest_spectral_library.sqlite") `
    -Destination (Join-Path $Resources "data\qitest_spectral_library.sqlite")
Copy-Item -LiteralPath (Join-Path $ProjectDir "config\ai-model-manifest.json") `
    -Destination (Join-Path $Resources "config\ai-model-manifest.json")
Copy-Item -LiteralPath (Join-Path $ProjectDir "resources\knowledge\operator_manual_zh.md") `
    -Destination (Join-Path $Resources "knowledge\operator_manual_zh.md")
Copy-Item -LiteralPath (Join-Path $ProjectDir "THIRD_PARTY_NOTICES.md") `
    -Destination (Join-Path $Resources "notices\THIRD_PARTY_NOTICES.md")
Copy-Item -Path (Join-Path $ProjectDir "third_party\notices\*") `
    -Destination (Join-Path $Resources "notices") -Recurse -Force

$RuntimeRoot = if ($env:QITEST_LLAMA_RUNTIME) {
    $env:QITEST_LLAMA_RUNTIME
} else {
    Join-Path $ProjectDir ".tools\llama-runtime-windows"
}
$Server = Join-Path $RuntimeRoot "llama-server.exe"
$Model = Join-Path $ProjectDir "models\qwen\$ModelName"
if ((Test-Path $Server) -and (Test-Path $Model)) {
    $AiDir = Join-Path $Resources "ai"
    New-Item -ItemType Directory -Force -Path $AiDir | Out-Null
    Copy-Item -Path (Join-Path $RuntimeRoot "*") -Destination $AiDir -Recurse -Force
    Copy-Item -LiteralPath $Model -Destination (Join-Path $AiDir $ModelName)
} else {
    Write-Warning "未找到 Windows llama.cpp 运行时；Demo 仍可运行确定性检测核心。"
}

if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
Compress-Archive -Path (Join-Path $DeliveryDir "*") -DestinationPath $ZipPath
Write-Host "Windows Demo 已生成：$ZipPath"
