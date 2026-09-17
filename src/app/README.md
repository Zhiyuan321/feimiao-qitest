# 工作流程协调

先读 AppController.h 的 public slots 和 signals，再找 cpp 中对应实现。
- 控制按钮：updateInstrumentSetting；返回 true 是已提交，不是执行成功。
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
