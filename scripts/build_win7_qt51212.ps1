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
    if (Get-ChildItem -LiteralPath $PackageDir -File -Recurse | Where-Object { $_.Name -match '^Qt[6-9].*\.dll$' }) {
        throw "Non-Qt5 DLL found in Qt 5.12.12 package"
    }
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
2026-09-18报告旧结果残留修复：每次新检测正式开始立即清空上一份显示；报告表格只保留本次可疑行，本次没有可疑项时显示空表及提示，搜索与页面切换不会复活旧行。启动不自动打开历史结果，旧记录仍保留，通过打开数据查看。筛查公式、阈值和质量轴系数未改变；三聚氰胺未检出仍须提供本次保存归档或筛查详情对应行核对。
2026-09-18姓名与报告模板：开始检测姓名必填，检测归档和PDF按姓名命名，同名自动追加序号。PDF采用客户检测报告模板，被检人使用检测时姓名；表格为序号、化合物名称、定量离子、是否检出，不含浓度，只列可疑化合物，无可疑项明确提示。定量离子读取该次冻结谱库，缺少的仪器或离子模式信息显示未提供。
2026-09-18检测停止时序：停止前若心跳尚未回复，暂停新心跳并最多等待3秒；收齐后间隔400ms再发停止。停止发送后仍为500ms重试一次、3秒应答超时，必须实际回复停止成功且数据完整才报完成。心跳无回复、断线或停止无回复均明确报错，不伪造检测完成。现场请连续多次检测验证；异常时保留网口JSON和Wireshark抓包。
方法确认恢复修复：已活动的方法也可以再次点击“设为当前方法”。网口重连后需重新下发并等待确认，不再要求先切换其他方法。启动拦截会说明当前连接未确认还是参数不一致；网口JSON诊断新增方法确认状态及原因。
2026-09-16 COM4连接兼容：按旧软件抓包，高压回读原始值按完整uint16接收，移除原值大于5000导致整帧拒收/连接超时的限制；37887附近读数可继续主控板和分子泵轮询。帧结构和其他字段校验保留。已通过两份SPM中20条状态的本地回放及连续轮询测试，仍需现场验证连接。
网口心跳接入：本版按2026-09-16旧程序抓包，TCP连接后每2秒发送0x30/0x23。设为当前方法时先暂停心跳，依次完成485与网口0x81方法设置；0x81成功约100ms后顺序发送三条0x50/0x00，每条确认成功后再发下一条，全部确认才提示成功并恢复心跳。失败保持暂停，重试成功或新连接恢复；超时/断线停止旧心跳。网口TXT/JSON导出包含心跳发送记录与调度状态；现场仍需验证是否解决5秒重连。
解析诊断更新：网口TXT/JSON新增RX_RAW原始接收字节和本次采集首次错误现场，保留未通过检查的数据及长度/帧尾/CRC判定信息。出错后直接导出报文，不要重新监听或再次检测。心跳0x30回包已单独识别计数。
完整周期接收修复：0x81/0x82支持整周期大帧，长度后两个字节按16位大端周期编号（从0开始，每周期+1）处理。按用户确认的旧软件行为，上传波形action=0x20的0x81/0x82仅记录CRC，不因不匹配拒收；现场0000 CRC保持原样接收。状态、心跳及方法设置等控制回复仍强制校验CRC。帧长/帧尾仍检查，0x81整帧点数须匹配方法，重复/跳号拒绝；周期号作为秒时间轴，TIC对该周期强度求和。日志及采集归档保存兼容策略，本机回放通过不代表实机验收通过。
2026-09-15采集接入：参数预设中的检测时间（秒）在下次开始检测时读取；网口0x15/0x22开启确认后计时，到时发送0x15/0x23并等待关闭确认；500ms无回复时最多补发一次，原3秒期限不延长，等待关闭时暂停新增心跳。补发后成功保留3秒迟到回复隔离期，防止影响下一次开启。0x81按完整周期帧接收并记录CRC，每2字节大端无符号原值为强度；TIC每1秒周期求和，EIC按质量窗口求和。保存原始谱与扫描序列，打开实机归档仍保留原值。
旧抓包固定字段：data[20]/data[21]=1/3000，data[25—44]尾部按旧抓包；冷却、进样、质量范围等仍随方法输入。datafit校准换算保留，未把旧抓包的电压数值写死。此版本是旧发送方式对照测试包，实机连续检测通过前不能宣称交替异常已解决。
当前只支持周期10000的Fullscan，最多5000周期/100万采样点；已更新为2026-09-15用户提供datafit.json的Fullscan系数，同步用于方法下发与m/z反算。超过801/1001的范围分别需要datafit_1000/datafit_2000，未配置时不下发。更新软件后请重新将方法设为当前并确认成功。实机一级阈值筛查按下述2026-09-16规则执行；其余未接入的维护及分析预设仍只保存。
2026-09-16谱库文件管理：设置与维护→参考谱库→我的谱库。支持新建/导入.lib，列表按库文件显示名称、文件修改日期、编辑/删除；编辑页可保存、另存为、导出以及添加/修改/删除物质。支持名称、CAS、母离子、定性/定量离子、类别、内标及两级阈值；保留旧库internal/external和未知字段。列表删除只移除登记，原文件保留。当前依据旧代码截图接入JSON数组格式，未用真实旧库样本做双向验证；选择参数预设中的库路径后按各个定性离子的一级阈值规则筛查。
2026-09-17部件按钮：离子源高压按已确认的485 HV_24V（0x09）控制，开启01、关闭02，等成功应答和状态回读；夹管阀按网口0x22控制，开启22、关闭23，等成功应答。分别需要485或网口有效连接。RF按用户确认的485 RF_24V（0x10）控制，开启01、关闭02，等待成功应答并核对RF状态位；连接485即可操作。
2026-09-17重启状态恢复：软件重开、网口与485有效回读后，按隔膜泵状态和分子泵电流恢复运行显示，真空和温度单独判断。分子泵已运行时不重发泵和载气指令；温度低且设定未确认时允许点击继续开机，补齐温度步骤。电流尚未回读时等待有效回读。重启后根据有效回读启用一键关机。

2026-09-18质量轴校准：设置与维护→分析校准→质量轴校准。手填至少3组不同实测/理论m/z，拟合校准后点击同步校准，再重新设为当前方法并等全部成功回执。新系数同时用于方法下发和后续质谱质量轴，历史记录保持原样；可撤销上次同步。系数在本机用户数据目录fullscan-calibration.json自动保存，重启继续使用；更换仪器需使用对应校准。同步不自动发送硬件命令；仍需实际标准物质复测验证。

2026-09-18启动真空门槛：隔膜泵开启后，真空度须严格小于8 mbar（8E0）才能开启分子泵，等于8不放行；一键开机与手动开启使用同一门槛。分子泵开启后，有载气等待E-03及更低、无载气等待E-05及更低的规则不变。

2026-09-18分子泵启动等待：确认时限延长为10秒，泵电流查询仍约0.5秒一次；手动和一键开机均生效。有效回包持续0 A时停止后续步骤并保持485连接；真实通讯无回包超时仍单独处理。

2026-09-17持续零电流修正：分子泵启停确认期限内有正常回包但电流不满足目标时，提示启动/停止未确认，停止后续步骤但保持485和监控，不再直接断口。保留在途查询、自动异常日志及真实无回包超时处理，不重发启停、不把0 A当作已开启；泵为什么仍为0 A需要实机继续排查。

2026-09-17开机掉线修正：等待分子泵电流确认时交替查询主控板，避免主控板读数因未轮询而过期并断开485；保留原超时和200ms静默间隔。开机中断重连后，双泵运行但温度设定未确认，即使余温达标也可明确点击继续开机，仅补温度步骤。关机降速时仍等待转速归零。异常日志保留具体取消原因。本机模拟测试通过，仍需实机验证。

2026-09-17离子源电压：方法中的电压通过网口0x50设置，3800 V发BE，3500 V发AF；范围0～5000 V，20 V步进。三条设置均成功应答后才确认方法，拒绝/超时不报成功。TD=0仍通过485 0x02发送0000，监控显示实测值。本包不等于实机气压波峰问题已解决，需使用相同载气模式和方法复测。

2026-09-17系统状态：常用部件的真空系统就绪、实测腔内温度80～90℃、TD温度220～280℃时显示“已就绪”；读数失效或不达标显示“未就绪”。此卡不再依赖方法下发，开始检测仍检查当前方法成功回执、真空和离子阱温度严格大于85℃。

2026-09-17气压连续显示：按厂家代码连续追加0x82采样点，整次检测结束后保留全部已收气压曲线，不再只显示末周期。横轴时间/min=累计点号/1000/60*(已确认方法周期/data[8]*2)，使用方法原值；默认周期10000、data[8]=5000时每点4ms。纵轴仍为原始uint16/65535*2.5*5.7 V，页面显示最低值和峰值。时间参数未知时显示采样点。旧抓包7221点可重现29个周期的波峰；当前实机是否有波峰须看实际0x82原始数据。

2026-09-17串口断线排查版：分子泵启停后等待200ms连续静默再发查询，避免紧随的主控查询与泵回传争用。485异常原因直接显示，断开后仍可导出；异常时自动保存JSON，运行状态页可打开异常记录文件夹。重新连接不会覆盖已保存的异常文件。此次针对已提供关机报文修正，其他突然断线仍需新的异常记录核对，不能视为实机问题全部解决。

2026-09-17升温诊断：开机失败原因保留；温度设定确认与实测达标分开显示。485导出单独保留控制发送尝试、匹配应答和失败证据。此次视频为从停机开始、真空达标但离子阱37.5℃，尚需实机485报文判定原因，不能据本机测试声称加热故障已解决。

2026-09-17一键开机：连接485和网口，勾选同时读取分子泵，在设置与维护→常用部件点击一键“开”。先确认外载气模式，开启隔膜泵和TD加热250℃；真空度严格小于8 mbar（8E0）后发分子泵开启原文，用新电流>0确认；载气实际流量>0时等待E-03或更低，否则等待E-05或更低，再开启离子阱85℃加热。界面显示等待步骤，停止流程只停止后续步骤、不自动关停部件。一键关机先用485 0x08/02同时关闭TD和离子阱加热，离子阱实测低于75℃后停分子泵，等待新电流0和转速0 RPM再关闭隔膜泵。降温/减速无5秒总超时，各查询仍限时；异常或停止流程不继续关后续部件。最终仍须实机验收。
联调顺序：连接485/TCP，方法设为当前并收到成功回执，保存检测秒数，在样品分析开始检测。缺包、校验错误、断线、启停回执超时均不报检测完成；问题发生后导出网口报文。
2026-09-16检测启动条件：实机点击开始检测需同时满足当前方法已收到下位机成功回传、有效真空度大于0且小于0.01 mbar（E-03及更低压力）、离子阱实测温度严格大于85.0℃。设置等待中/失败不能沿用旧成功；无有效读数不放行。点击入口及填写样本后正式启动均检查，未达标弹窗列出所有原因，只有确认按钮关闭，不发送开启指令。
2026-09-16定性离子数量修正：每种物质可有1个、2个、3个或更多定性离子，一级阈值数量必须相同。每项跨帧累加都严格大于对应阈值才判可疑。旧记录若含扫描与谱库快照，重新打开即可按修正规则重算，不必补齐为3个离子。
2026-09-16 EIC与筛查：EIC显示时间/min和面积，各点保持逐帧值，20秒完整横轴为0.3333分钟。检测开始时冻结参数预设库路径指定的.lib；qualitify_ion各个定性离子分别按MS1±0.5 Da提取，各帧直接相加，与son_area同等数量一级阈值按顺序对应，所有项都严格大于阈值才为可疑。任一项等于或不足为未检出；离子或阈值为空、无效或数量不一致时显示未筛查。报告仅列可疑项，筛查详情可查看全部物质的比较证据。保存和归档包括所用谱库快照，重开不受当前库修改影响。未选库或库不可读会明确提示，仍保留原始采集数据。尚需连接真实仪器验证。
2026-09-15报告页更新：取消复核步骤，检测结果可直接生成PDF；启动恢复最近结果，只列可疑物质，筛查详情显示完整筛查面板；打开数据从文件夹选择历史数据；谱图查看以当前记录快照显示TIC/MS1/EIC，缺少时间序列时不补造数据。
2026-09-14方法字段修正：冷却时间写data[8]，进样时间写data[13]（380直接发380）；data[20]=1、data[21]=5000为独立固定组合。修改冷却时间不再改变开启间隔。周期校验同步使用冷却时间。0x29按“冷却时间错误”报告设置失败，不再误报无回传。
本版实际下发只接受整数进样时间；小数不会截断或自行乘100。
网口“导出报文”现可选择TXT十六进制文本或JSON。点击设为当前方法后，直接导出TXT，记事本查看TX发送、RX返回、时间及完整报文；仅保留最近256条，重新监听会清空。
2026-09-14网口诊断更新：未连接或没有有效报文也可点击“导出报文”；页面显示连接状态和接收计数。出现问题后直接导出，尽量不要先停止或重新监听。导出成功/失败在页面显示。本次修正诊断入口，网口通信故障原因仍须结合现场导出确认。
2026-09-11交互更新：样品分析只有“开始检测”；启动成功后变为“检测中”并禁用，检测完成显示“检测已完成”，点击“确认”后恢复“开始检测”。现已接入下述Fullscan定时采集流程。
设置 -> 仪器控制 -> 运行状态：选择“485串口”或“网口TCP”标签，可同时连接。
485：选择实际COM口，点击“连接并读取”。
离子源电压按2026-09-10更正，高压模块原始值÷10显示为V，例如49→4.9 V；485表保留原始整数。
串口参数9600、8N1、无流控。USB转485适配器需安装支持目标系统的驱动。
分子泵与主控板共用COM：在“485串口”勾选“同时读取分子泵”，选择原来的COM，点击“连接并读取”，同一页面同时显示两部分。
主控板和398/310/313/326四条查询依次发送，串口只打开一次。分子泵转速直接RPM，电流/电压原始值除100为A/V，控制器温度即泵体温度、直接℃。原始值保留在说明与导出报文中，回复校验仍待确认；按用户确认，以发送启停指令后新收到的310电流判断开启（>0）或关闭（=0）。
分子泵查询超时则暂停泵查询、清空泵值，主控板继续读取；重新连接可重试。点击“导出485报文”保存双方收发记录。
点击“断开”统一停止主控板与分子泵读取，网口不受影响。
网口：点击“开始监听”，默认本机0.0.0.0、TCP端口11000。仪器主动连接电脑的实际网卡IP和11000端口。
0.0.0.0是监听所有IPv4网卡，不能填写为仪器的目标IP；可选电脑实际网卡IP进行监听。
网口状态采用2026-09-09修订：21字节数据区，长度0017，第9字节01开启/00关闭。
网口收到有效状态后显示倍增管高压、真空规原始值、真空度（mbar）、实验状态；0x81完整分包用于质谱。
真空度按2026-09-09确认公式换算为mbar，在主界面及“网口TCP”表格显示；原始2826对应约5.02E-05 mbar。超时或断线后读数失效。
网口状态超时默认5秒，可按实际上传周期调整；断线后继续监听，等待仪器重连。
同一仪器IP的新连接会自动接替未关闭的旧连接；切换时等待新状态，不保留旧读数。
导出最近报文包含连接次数、同IP接替次数、拒绝次数及最近64条连接事件，便于排查停收。
可导出最近256条已接收网口帧供联调（波形CRC仅记录，控制及状态CRC须通过）；不是完整采集记录。网口与485读数分别失效。
气压图：正常检测结束后保留整次检测曲线并标注“检测已结束”，下一次开始检测清空旧曲线；取消/失败不标记完成，断开或重新监听仍清空。

气压图显示范围：纵坐标固定0.00～6.00 V，每格1.00 V，不随峰值扩大；超出范围仅裁切显示，接收值和换算不变。

样品分析→气压图：0x82每两字节大端无符号取数，V=原始值/65535*2.5*5.7。连续展示本次检测已收到的各周期；横轴按已确认方法计算为分钟，方法时间参数未确认时显示采样点。
设置→射频调谐：管理员/工程师可发送检测/结束（调谐启停）。等待协议应答，3秒无应答则隔离旧连接并提示状态未知；不得视为硬件物理状态已核实。RF图换算尚未确认。
Fullscan设置先逐项等待485基本设置应答，再发送网口0x81并等待方法应答；保存文件不等于已下发。方法及三条0x50后续指令全部应答成功后可开始定时采集。
方法参数：管理员完整编辑；普通角色仅扫描模式、进样时间0～600 ms/0.01 ms，可打开/导出/保存新版本，其余参数保持原方法。沿用现有角色机制，未新增账户管理。方法保存仍为离线草稿。
485收到有效状态后才显示连接正常；断线/超时清空485读数，需要手动重连串口。
默认启动为模拟模式；首次连接485或开始网口监听后切换为真实设备回读，失败或停止不会自动退回模拟。

开始检测的离子阱温度要求：实测83.0～87.0℃（85±2℃，含两端），无有效回读或超出范围时弹窗提示；方法成功回执和真空条件仍需满足。
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
