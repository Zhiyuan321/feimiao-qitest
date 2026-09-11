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
if (!$QtRoot) { $QtRoot = "D:\Qt\5.12.12\5.12.12\mingw73_64" }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$QtInstall = Split-Path (Split-Path $QtRoot -Parent) -Parent
if (!$MinGWRoot) { $MinGWRoot = Join-Path $QtInstall "Tools\mingw730_64" }
if (!$CMakeExe) {
    foreach ($candidate in @((Join-Path $QtInstall "Tools\CMake_64\bin\cmake.exe"),
            (Join-Path (Split-Path $QtInstall -Parent) "Tools\CMake_64\bin\cmake.exe"))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $CMakeExe = $candidate; break }
    }
    if (!$CMakeExe) {
        $found = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($found) { $CMakeExe = $found.Source }
    }
    if (!$CMakeExe) { throw "Set -CMakeExe to CMake 3.24 or newer" }
}
if (!$BuildDir) { $BuildDir = Join-Path $ProjectDir "build\win7-qt51212-release" }
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
    if ($LASTEXITCODE -ne 0 -or $version -ne "5.12.12") { throw "Requires Qt 5.12.12, found: $version" }
    $machine = & $Gcc -dumpmachine
    $compilerVersion = & $Gcc -dumpfullversion
    if ($machine -ne "x86_64-w64-mingw32" -or $compilerVersion -ne "7.3.0") {
        throw "Requires MinGW 7.3.0 x64; found $compilerVersion / $machine"
    }
    $tests = if ($WithTests) { "ON" } else { "OFF" }
    Invoke-Checked $CMakeExe @("-S", $ProjectDir, "-B", $BuildDir, "-G", "MinGW Makefiles",
        "-DCMAKE_BUILD_TYPE=Release", "-DQITEST_WIN7=ON", "-DQITEST_QT_VERSION=5.12.12",
        "-DQITEST_BUILD_TESTS=$tests", "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_CXX_COMPILER=$Gcc", "-DCMAKE_MAKE_PROGRAM=$Make")
    Invoke-Checked $CMakeExe @("--build", $BuildDir, "--parallel", "4", "--target", "QITestWorkstation")
    if ($WithTests) {
        Invoke-Checked $CMakeExe @("--build", $BuildDir, "--parallel", "4", "--target",
            "qitest_network_tests", "qitest_rs485_tests", "qitest_device_tests", "qitest_ai_tests", "qitest_core_tests", "qitest_ui_tests")
        Invoke-Checked $Deploy @("--release", "--compiler-runtime", "--no-translations", (Join-Path $BuildDir "qitest_ui_tests.exe"))
        Copy-Item -LiteralPath (Join-Path $QtRoot "plugins\platforms\qoffscreen.dll") -Destination (Join-Path $BuildDir "platforms\qoffscreen.dll")
        $CTest = Join-Path (Split-Path $CMakeExe -Parent) "ctest.exe"
        Invoke-Checked $CTest @("--test-dir", $BuildDir, "-R", "qitest_(network|rs485|device|ai|core)_tests", "--output-on-failure")
        Invoke-Checked (Join-Path $BuildDir "qitest_ui_tests.exe") @("-platform", "offscreen",
            "networkPanelConnectsAlongside485", "rs485StatusPanelReadsAndInvalidates", "instrumentPowerButtonsReflectPartialState")
    }
    # Every delivery gets a new folder: never delete or overwrite an earlier package.
    $name = "Feimiao-Win7-Qt5.12.12-TCP-485-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff")
    $PackageDir = Join-Path $OutputRoot $name
    if (Test-Path -LiteralPath $PackageDir) { throw "Output already exists: $PackageDir" }
    New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null
    $App = Join-Path $PackageDir "飞秒质谱工作站.exe"
    Copy-Item -LiteralPath (Join-Path $BuildDir "飞秒质谱工作站.exe") -Destination $App
    # The executable is built as Release; deploy the corresponding release DLLs and plugins explicitly.
    Invoke-Checked $Deploy @("--release", "--compiler-runtime", "--no-translations", $App)
    foreach ($required in @("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5Network.dll",
        "Qt5Sql.dll", "Qt5SerialPort.dll", "Qt5Svg.dll", "platforms\qwindows.dll",
        "sqldrivers\qsqlite.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageDir $required))) { throw "Deployment missing: $required" }
    }
    if (Get-ChildItem -LiteralPath $PackageDir -Include "Qt[6-9]*.dll" -Recurse) { throw "Non-Qt5 DLL found in Qt 5.12.12 package" }
    foreach ($folder in @("config", "knowledge", "notices")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "resources\$folder") | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $ProjectDir "config\ai-model-manifest.json") -Destination (Join-Path $PackageDir "resources\config")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "resources\knowledge\operator_manual_zh.md") -Destination (Join-Path $PackageDir "resources\knowledge")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "third_party\notices\Qt-LGPL-3.0.txt") -Destination (Join-Path $PackageDir "resources\notices")
    Copy-Item -LiteralPath (Join-Path $ProjectDir "THIRD_PARTY_NOTICES.md") -Destination (Join-Path $PackageDir "resources\notices")
    @'
网口TCP与485状态读取测试包 / Qt 5.12.12 / MinGW 7.3 x64 / Release

完整解压后运行“飞秒质谱工作站.exe”，保留所有DLL与子目录。
2026-09-11交互更新：样品分析只有“开始检测”；启动成功后变为“检测中”并禁用，检测完成显示“检测已完成”，点击“确认”后恢复“开始检测”。本次仅调整交互，不解除真实采集尚未接通的限制。
设置 -> 仪器控制 -> 运行状态：选择“485串口”或“网口TCP”标签，可同时连接。
485：选择实际COM口，点击“连接并读取”。
离子源电压按2026-09-10更正，高压模块原始值÷10显示为V，例如49→4.9 V；485表保留原始整数。
串口参数9600、8N1、无流控。USB转485适配器需安装支持目标系统的驱动。
分子泵与主控板共用COM：在“485串口”勾选“同时读取分子泵”，选择原来的COM，点击“连接并读取”，同一页面同时显示两部分。
主控板和398/310/313/326四条查询依次发送，串口只打开一次。分子泵转速直接RPM，电流/电压原始值除100为A/V，控制器温度即泵体温度、直接℃。原始值保留在说明与导出报文中，回复校验仍待确认，不作控制依据。
分子泵查询超时则暂停泵查询、清空泵值，主控板继续读取；重新连接可重试。点击“导出485报文”保存双方收发记录。
点击“断开”统一停止主控板与分子泵读取，网口不受影响。
网口：点击“开始监听”，默认本机0.0.0.0、TCP端口11000。仪器主动连接电脑的实际网卡IP和11000端口。
0.0.0.0是监听所有IPv4网卡，不能填写为仪器的目标IP；可选电脑实际网卡IP进行监听。
网口状态采用2026-09-09修订：21字节数据区，长度0017，第9字节01开启/00关闭。
网口收到有效状态后显示倍增管高压、真空规原始值、真空度（mbar）、实验状态；谱图解析待确认。
真空度按2026-09-09确认公式换算为mbar，在主界面及“网口TCP”表格显示；原始2826对应约5.02E-05 mbar。超时或断线后读数失效。
网口状态超时默认5秒，可按实际上传周期调整；断线后继续监听，等待仪器重连。
同一仪器IP的新连接会自动接替未关闭的旧连接；切换时等待新状态，不保留旧读数。
导出最近报文包含连接次数、同IP接替次数、拒绝次数及最近64条连接事件，便于排查停收。
可导出最近256条CRC有效网口帧供联调；不是完整采集记录。网口与485读数分别失效。
样品分析→气压图：0x82每两字节大端无符号取数，V=原始值/65535*2.5*5.7。当前仅展示最新单包，横轴为采样点序号；时间参数和周期边界待确认，暂不拼接完整周期。
设置→射频调谐：管理员/工程师可发送检测/结束（调谐启停）。等待协议应答，3秒无应答则隔离旧连接并提示状态未知；不得视为硬件物理状态已核实。RF图换算尚未确认。
其他加热、泵、电源或方法参数控制不发送；完整真实谱图采集未开放。
方法参数：管理员完整编辑；普通角色仅扫描模式、进样时间0～600 ms/0.01 ms，可打开/导出/保存新版本，其余参数保持原方法。沿用现有角色机制，未新增账户管理。方法保存仍为离线草稿。
485收到有效状态后才显示连接正常；断线/超时清空485读数，需要手动重连串口。
默认启动为模拟模式；首次连接485或开始网口监听后切换为真实设备回读，失败或停止不会自动退回模拟。

目标为Windows 7 SP1 64位；请在实际工控机核对启动、串口/网口读数和断线恢复。
编译、依赖收集及开发机测试不等于Win7实机验收通过。
此包不含AI模型、推理程序、外部参考谱库或用户运行数据库。

本包动态使用Qt 5.12.12，实际模块见Qt5*.dll；许可文本见resources/notices。
Qt 5.12.12对应源码：https://download.qt.io/archive/qt/5.12/5.12.12/single/
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
