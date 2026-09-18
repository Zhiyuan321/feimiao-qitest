# 工作流程协调

2026-09-18：内置适配器手动分子泵开启的宿主保护时限为11秒，给串口驱动10秒启动确认留出余量；其他普通设置每次显式恢复5秒。一键启停继续由设备各步骤管理时限，不受宿主5秒总时限影响。

先读 AppController.h 的 public slots 和 signals，再找 cpp 中对应实现。
- 控制按钮：updateInstrumentSetting；返回 true 是已提交，不是执行成功。
- 内置网口/485按instrumentSettingAvailable逐项开放已接入控制，不把readOnly=false作为开放全部硬件的捷径。一键启停powerOn=true/false沿用确认和审计，但不使用5秒总超时；界面powerOff能力映射powerOn=false，重启读到运行状态即可关闭。每步仍由传输层限时确认，等真空/降温/转速时可cancelInstrumentStartup停止后续步骤。
- 回执：构造函数内 settingFinished 连接；匹配请求号、控制键及实际回读。
- 成功提示：预览路径只显示本地“已更新”；只有真实适配器的请求号、字段和回读全部匹配，才显示“设置成功”。
- 检测：startDetection / cancelDetection / finishAcquisition。NetworkInstrument 完成开启确认、按预设秒数计时、关闭确认后进入保存。真实记录标为 DEVICE_UNVALIDATED，保留原始谱与扫描序列，不使用演示谱库。已指定库路径时冻结 .lib 内容，在完成后执行各个定性离子的一级阈值筛查，见 core/IonThresholdScreening；未配置或读取失败时保留原始数据并明确提示。
- 实机开始检测的 checkDetectionStart：当前方法须与当前连接成功回传的参数一致，本次设置仍在等待或失败时不能沿用旧成功；真空度须有有效网口回读且0 < p < 0.01 mbar（E-03及更低压力）；离子阱实测温度须有限且严格大于85.0℃。入口预检和实际启动均调用，未达标不建采集会话、不清除旧结果、不发开启指令。detectionStartRejected一次携带全部失败原因；离线预览和历史数据重新分析保留原流程。
- 网口波形兼容策略：按用户确认，上传0x81/0x82仅记录CRC，不以其不匹配阻断采集。检测输入快照保存waveform_crc_policy=record_only，随sampleInfo落库和归档；状态及控制回执CRC仍强制校验。
- instrumentPreset / saveInstrumentPreset：读取并校验本机参数预设，保留旧四字段调用兼容；新增字段全量保存。检测时间默认为20秒，库/维护方法/基数据路径默认为空，不沿用截图中的其他电脑路径。保存不产生硬件指令或改变已确认设备状态。
- 分析与落库：AnalysisWorker 使用输入快照和线程内独立数据库连接；完成后主线程发布结果。关闭程序等待当前事务，不强制终止写入线程。
- 导入：importRunArchives；后台工作见 storage/ArchiveImportWorker。
- 报告：exportSelectedReport，当前检测结果可直接导出，不要求人工复核；旧归档复核字段仅兼容读取，不参与流程。
- 深度问答：askAiAssistant，科学证据由程序提供。

状态从控制器发给界面；不要在 MainWindow 中单独保存另一套硬件真值。
调试设备先看 ../device/README.md；回归测试是 tests/InstrumentControlTests.cpp、WorkspaceTests.cpp。

2026-09-17离子源电压方法下发：old.pcapng中方法成功后0x50连续三条BE，用户确认设定3800 V；结合协议2.5V控制量对应5000V、控制量乘100，采用V/20编码为单字节（0～5000 V，20 V步进）。3500 V为AF，3800 V为BE，0仍发00。预检在任何写入之前完成；485只处理TD/离子阱/EFC/PWM，source由TCP接入，三次0x50均成功后才确认整套方法并恢复心跳。TD=0按用户确认仍发485 0x02数据0000，不跳过、不替换关加热命令。监控温度/电压保持设备回读，不能用设定值覆盖。此次旧抓包有气压波峰，新旧载气模式和多项方法参数不同，不能把0x50修复视为气压问题已实机解决。
