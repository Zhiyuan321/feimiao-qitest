param(
    [string]$QtRoot = $env:QITEST_QT_ROOT,
    [string]$MinGWRoot = $env:QITEST_MINGW_ROOT,
    [string]$CMakeExe = $env:QITEST_CMAKE,
    [string]$BuildDir = "",
    [string]$OutputRoot = "",
    [switch]$WithTests
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if (!$QtRoot) { $QtRoot = "D:\Qt\5.15.2\mingw81_64" }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$QtInstall = Split-Path (Split-Path $QtRoot -Parent) -Parent
if (!$MinGWRoot) { $MinGWRoot = Join-Path $QtInstall "Tools\mingw810_64" }
if (!$CMakeExe) { $CMakeExe = Join-Path $QtInstall "Tools\CMake_64\bin\cmake.exe" }
if (!$BuildDir) { $BuildDir = Join-Path $ProjectDir "build\win7-qt5152-release" }
if (!$OutputRoot) { $OutputRoot = Join-Path $ProjectDir "build\packages" }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$Gcc = Join-Path $MinGWRoot "bin\g++.exe"
$Make = Join-Path $MinGWRoot "bin\mingw32-make.exe"
$Qmake = Join-Path $QtRoot "bin\qmake.exe"
$Deploy = Join-Path $QtRoot "bin\windeployqt.exe"
foreach ($required in @($Gcc, $Make, $Qmake, $Deploy, $CMakeExe,
    (Join-Path $QtRoot "bin\Qt5SerialPort.dll"))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing tool/module: $required" }
}
function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}
$OriginalPath = $env:PATH
try {
    $env:PATH = (Join-Path $MinGWRoot "bin") + ";" + (Join-Path $QtRoot "bin") + ";" + $env:PATH
    $version = & $Qmake -query QT_VERSION
    if ($LASTEXITCODE -ne 0 -or $version -ne "5.15.2") { throw "Requires Qt 5.15.2, found: $version" }
    $machine = & $Gcc -dumpmachine
    $compilerVersion = & $Gcc -dumpfullversion
    if ($machine -ne "x86_64-w64-mingw32" -or $compilerVersion -ne "8.1.0") {
        throw "Requires MinGW 8.1.0 x64; found $compilerVersion / $machine"
    }
    $tests = if ($WithTests) { "ON" } else { "OFF" }
    Invoke-Checked $CMakeExe @("-S", $ProjectDir, "-B", $BuildDir, "-G", "MinGW Makefiles",
        "-DCMAKE_BUILD_TYPE=Release", "-DQITEST_WIN7=ON", "-DQITEST_WIN7_QT_VERSION=5.15.2",
        "-DQITEST_BUILD_TESTS=$tests", "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_CXX_COMPILER=$Gcc", "-DCMAKE_MAKE_PROGRAM=$Make")
    Invoke-Checked $CMakeExe @("--build", $BuildDir, "--parallel", "4", "--target", "QITestWorkstation")
    if ($WithTests) {
        Invoke-Checked $CMakeExe @("--build", $BuildDir, "--parallel", "4", "--target",
            "qitest_network_tests", "qitest_rs485_tests", "qitest_device_tests", "qitest_ai_tests", "qitest_core_tests", "qitest_ui_tests")
        Invoke-Checked $Deploy @("--compiler-runtime", "--no-translations", (Join-Path $BuildDir "qitest_ui_tests.exe"))
        Copy-Item -LiteralPath (Join-Path $QtRoot "plugins\platforms\qoffscreen.dll") -Destination (Join-Path $BuildDir "platforms\qoffscreen.dll")
        $CTest = Join-Path (Split-Path $CMakeExe -Parent) "ctest.exe"
        Invoke-Checked $CTest @("--test-dir", $BuildDir, "-R", "qitest_(network|rs485|device|ai|core)_tests", "--output-on-failure")
        Invoke-Checked (Join-Path $BuildDir "qitest_ui_tests.exe") @("-platform", "offscreen",
            "networkPanelConnectsAlongside485", "rs485StatusPanelReadsAndInvalidates", "instrumentPowerButtonsReflectPartialState")
    }
    # Every delivery gets a new folder: never delete or overwrite an earlier package.
    $name = "Feimiao-Win7-Qt5.15.2-TCP-485-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff")
    $PackageDir = Join-Path $OutputRoot $name
    if (Test-Path -LiteralPath $PackageDir) { throw "Output already exists: $PackageDir" }
    New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null
    $App = Join-Path $PackageDir "飞秒质谱工作站.exe"
    Copy-Item -LiteralPath (Join-Path $BuildDir "飞秒质谱工作站.exe") -Destination $App
    # Qt 5.15 MinGW PE debug detection is unreliable; auto mode accepts matching
    # MinGW plugins, while --release can wrongly exclude qwindows.dll.
    # The executable itself was built with CMAKE_BUILD_TYPE=Release above.
    Invoke-Checked $Deploy @("--compiler-runtime", "--no-translations", $App)
    foreach ($required in @("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5Network.dll",
        "Qt5Sql.dll", "Qt5SerialPort.dll", "Qt5Svg.dll", "platforms\qwindows.dll",
        "sqldrivers\qsqlite.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageDir $required))) { throw "Deployment missing: $required" }
    }
    if (Get-ChildItem -LiteralPath $PackageDir -Filter "Qt6*.dll" -Recurse) { throw "Qt6 DLL found in Qt5 package" }
    foreach ($folder in @("config", "knowledge", "notices")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "resources\$folder") | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $ProjectDir "config\ai-model-manifest.json") -Destination (Join-Path $PackageDir "resources\config")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "resources\knowledge\operator_manual_zh.md") -Destination (Join-Path $PackageDir "resources\knowledge")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "third_party\notices\Qt-LGPL-3.0.txt") -Destination (Join-Path $PackageDir "resources\notices")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "THIRD_PARTY_NOTICES.md") -Destination (Join-Path $PackageDir "resources\notices")
    @'
网口TCP与485状态读取测试包 / Qt 5.15.2 / MinGW 8.1 x64 / Release

完整解压后运行“飞秒质谱工作站.exe”，保留所有DLL与子目录。
设置 -> 仪器控制 -> 运行状态：选择“485串口”或“网口TCP”标签，可同时连接。
485：选择实际COM口，点击“连接并读取”。
串口参数9600、8N1、无流控。USB转485适配器需安装支持目标系统的驱动。
网口：点击“开始监听”，默认本机0.0.0.0、TCP端口11000。仪器主动连接电脑的实际网卡IP和11000端口。
0.0.0.0是监听所有IPv4网卡，不能填写为仪器的目标IP；可选电脑实际网卡IP进行监听。
网口收到有效状态后显示倍增管高压、真空规原始值、实验状态；压力换算和谱图解析待确认。
网口状态超时默认5秒，可按实际上传周期调整；断线后继续监听，等待仪器重连。
可导出最近256条CRC有效网口帧供联调；不是完整采集记录。网口与485读数分别失效。
仅查询状态，不发送加热、泵、电源或参数控制命令，不进行真实谱图采集。
485收到有效状态后才显示连接正常；断线/超时清空485读数，需要手动重连串口。
默认启动为模拟模式；首次连接485或开始网口监听后为真实只读模式，失败或停止不会自动退回模拟。

目标为Windows 7 SP1 64位；请在实际工控机核对启动、串口/网口读数和断线恢复。
编译、依赖收集及开发机测试不等于Win7实机验收通过。
此包不含AI模型、推理程序、外部参考谱库或用户运行数据库。

本包动态使用Qt 5.15.2，实际模块见Qt5*.dll；许可文本见resources/notices。
Qt 5.15.2对应源码：https://download.qt.io/archive/qt/5.15/5.15.2/single/
本次实际Qt版本为5.15.2，历史第三方核查记录中的5.12.12不代表本包版本。
'@ | Set-Content -LiteralPath (Join-Path $PackageDir "测试说明.txt") -Encoding UTF8
    $hashLines = Get-ChildItem -LiteralPath $PackageDir -File -Recurse | Sort-Object FullName | ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + "  " +
            $_.FullName.Substring($PackageDir.Length + 1).Replace('\', '/')
    }
    [IO.File]::WriteAllLines((Join-Path $PackageDir "SHA256SUMS.txt"), [string[]]$hashLines, [Text.UTF8Encoding]::new($false))
    $Zip = $PackageDir + ".zip"
    Compress-Archive -LiteralPath $PackageDir -DestinationPath $Zip
    Write-Host "Package: $PackageDir"
    Write-Host "ZIP: $Zip"
} finally { $env:PATH = $OriginalPath }
