# 工作流程协调

先读 AppController.h 的 public slots 和 signals，再找 cpp 中对应实现。
- 控制按钮：updateInstrumentSetting；返回 true 是已提交，不是执行成功。
- 回执：构造函数内 settingFinished 连接；匹配请求号、控制键及实际回读。
- 检测：startDetection / cancelDetection / finishAcquisition。
- 导入：importRunArchives；后台工作见 storage/ArchiveImportWorker。
- 报告与复核：exportSelectedReport / markCurrentRunReviewed。
- 深度问答：askAiAssistant，科学证据由程序提供。

状态从控制器发给界面；不要在 MainWindow 中单独保存另一套硬件真值。
调试设备先看 ../device/README.md；回归测试是 tests/InstrumentControlTests.cpp、WorkspaceTests.cpp。
