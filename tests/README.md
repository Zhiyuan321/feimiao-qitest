# 测试不是主程序

2026-09-18：slowPumpStartupKeepsBoardFreshAndCanResume覆盖6.9秒延迟启动与10秒零电流超时；zeroCurrentTimeoutKeepsConnectionAndPendingReply核对10秒期限及在途半帧；manualPumpStartupWaitsBeyondFiveSeconds验证手动启动不被宿主5秒提前取消。使用内存串口和回环TCP，无实机操作。

首次开发请运行 QITestWorkstation；需要回归检查时开启 QITEST_BUILD_TESTS=ON。

| 文件 | 关注点 |
| --- | --- |
| PumpTests.cpp / PumpTestDevice.h | 同COM主控板/泵顺序查询、仅打开一次、无重叠、原始字符取数、用户确认倍率及失效清空、错误回复拒绝、超时隔离、主控有效期、日志有界；合成帧不代表实机 |
| NetworkTests.cpp / NetworkTestFrames.h | TCP拆帧/CRC/真空度mbar公式与失效/21字节状态（01/00）/可选实机报文回放/超时/断线重连/同IP未断开时接替、双通道独立、有限原始帧导出；本机回环不是实机 |
| Rs485Tests.cpp / Rs485TestDevice.h | 只读查询、拆包、错误帧、状态倍率、超时清空；内存设备不接实机 |
| InstrumentControlTests.cpp | 请求、回执、超时、模拟与手动状态一致性 |
| CoreTests.cpp | 科学计算、方法与校准边界 |
| WorkspaceTests.cpp | 保存、归档、事务与损坏数据 |
| LibraryTests.cpp | 谱库和用户标准 |
| AiEvidenceTests.cpp | 基础指令、证据和模型边界 |
| LocalAiBridgeTests.cpp | 模型进程生命周期与问答失败 |
| SecurityTests.cpp | 单机完整功能与硬件安全边界 |
| UiSmokeTests.cpp | 窗口、页面、按钮及布局 |

统一通过 ctest --test-dir 构建目录 --output-on-failure 运行。
fixtures/ 是受控测试样本，不是用户检测记录；合成案例不代表实机科学验收。
2026-09-17开机回归：PumpTests验证指令后新电流确认、缺失/回显/错参数/迟到回复不确认及不重发；Rs485Tests验证部件ACK与状态回读、EFC乘10；NetworkTests验证真空阶段与载气流量动态切换、取消和断线；UiSmokeTests::realStartupControlsAndCancellationFitSmallScreen验证真实入口、超过5秒继续等待及1024×768/700布局。均使用内存串口与回环TCP，不连接实物。
新增接口至少测试：正常回读、设备拒绝、无回执、迟到回执、回读与目标不符。

开机掉线补充：NetworkTests::slowPumpStartupKeepsBoardFreshAndCanResume覆盖TD延迟回执后泵电流延迟变为正值（旧逻辑可复现主控板读数失效）、持续电流0时原控制时限仍失败、重连后余温达标仍可显式补全中断开机且不重启泵。UiSmokeTests::coldRunningDeviceCanContinueHeating新增interrupted-warm-reconnect行，验证真实继续按钮、确认入口和小屏布局。PumpTests原有无回包、迟到回包、静默间隔与失效回归同时运行。
持续零电流补充：上述NetworkTests验证期限失败但485保持连接、开关能力在正常回读后恢复且不发送离子阱升温。PumpTests::zeroCurrentTimeoutKeepsConnectionAndPendingReply模拟启动命令回显、持续零电流、控制期限到时主控半帧在途，验证自动快照保留有效读数、无抢发、轮询继续、迟到正电流不会把已失败控制改成成功。仍回归真正无回复的断口行为。

气压连续曲线回归：NetworkTests验证大端/65535倍率、厂家时间公式（方法period/cooling）、两周期早期峰值不被末周期平线覆盖、重复帧；UiSmokeTests::pressureCurveSurvivesCompletedDetection验证末帧追加、结束保留、分钟横轴和新检测清空及1024×768/700。可设置QITEST_PRESSURE_CAPTURE为本地提取的旧抓包pressure.bin，运行pressureCaptureKeepsContinuousPeaks回放29周期7221点，验证每周期原始峰谷差和0.481333分钟末点，客户报文不提交Git。

485断线回归：PumpTests::powerTrafficDrainsBeforeNextQuery用延迟、分段的启停回传模拟总线未释放，验证开始/停止后不抢发下一次查询，仍由新电流/转速确认；UiSmokeTests::rs485FailureExportsAndSavesEvidence覆盖轮询超时关闭、原因可见、自动保存、离线实际导出按钮及文件对话框、重新连接后异常文件保留、1024×768/700布局。

升温诊断回归：NetworkTests::startupUsesActualCurrentAndDynamicGasVacuum从停机开始，覆盖真空达标后0x13发送、设备接受但实测37.5℃、拒绝及超时，验证提示不误报、失败仅上报一次、485导出保留控制证据；runningPumpsResumeHeatingWithoutRestart验证显式继续升温不重启泵/切换载气。UiSmokeTests::coldRunningDeviceCanContinueHeating验证“继续开机”真实按钮入口与1024×768/700布局。均为合成回读，不证明实机加热成功。

485相关回归：`ctest --test-dir 构建目录 -R "qitest_(rs485|device|ui|ai)_tests" --output-on-failure`。设备/界面测试使用临时INI配置，不读写操作者注册表或已保存的COM口。Windows测试运行目录需部署同版本Qt DLL与平台插件（QtTest和offscreen也需可加载）；GUI截图可指定QITEST_UI_CAPTURE_DIR。

2026-09-17 RF/HV回归：Rs485Tests::serialPowerWaitsForAcknowledgementAndActualFlag覆盖0x10/0x09开启关闭、拒绝、状态不符、无应答、错命令应答和迟到回复；InstrumentControlTests::serialPowerControlWithoutTcp验证纯485及网口适配器下仅485连接均可用；UiSmokeTests验证RF按钮确认、启停及忙时禁用。均使用合成回读，不连接实机。

2026-09-17关机回归：NetworkTests::shutdownWaitsForCoolingAndRotor覆盖双加热关闭0x08/02、75℃边界、转速未归零超过5秒仍等待、隔膜泵关闭顺序、加热拒绝/无应答/状态不符、取消、断网、转速回读超时和迟到回复。UiSmokeTests::reconnectDisplaysRunningDeviceAndBlocksRepeatedStartup验证重启运行状态下关按钮可用、确认弹窗、忙时禁用、等待超过宿主5秒窗口及最终关状态；纯内存串口/回环TCP，无实物操作。

UiSmokeTests::readinessUsesLiveVacuumAndTemperaturesWithoutMethod覆盖未下发方法但真空/温度达标时显示已就绪、TD/离子阱上下边界、温度/真空不达标、485断线和1024×768/700布局，并验证未确认方法仍不能开始检测。

离子源电压回归：NetworkTests::ionSourceVoltageMatchesCaptured3800V验证BE抓包原文、AF/00/FA编码与CRC、无效范围/步进；legacyMethodFollowupsRequireAllAcks覆盖0/3500/3800、三次成功回执、第二次拒绝/超时/断线、无关应答、无效电压不发送、TD0000实际下发且实测值不被设定覆盖。

2026-09-18启动真空门槛：molecularPumpEightMbarBoundary覆盖8 mbar两侧相邻浮点数、等于8不放行、无效读数；startupUsesActualCurrentAndDynamicGasVacuum通过真实网口编解码验证7.99/8.01两侧、手动启动校验和一键启动等待，并回归分子泵开启后的载气动态真空规则不变。

质量轴手填回归：UiSmokeTests::manualQuadraticCalibrationWorksheet覆盖默认3行/二次、空输入、126→127/237→238/303→304换算、撤销、重复实测值和不足3点。此次仅验证离线工作区，未验证同步下位机。
