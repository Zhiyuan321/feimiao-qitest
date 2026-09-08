# 界面层

先按页面定位，不必通读整个 MainWindow：
- MainWindow：主窗口、导航、页面组装、状态连接。
- NetworkConnectionPanel / Rs485ConnectionPanel：运行状态页的网口TCP与485标签，可同时连接。
- InstrumentWorkbench：调谐、质量轴等工作区。
- MethodEditorDialog：方法编辑。
- SpectrumPlot / ChromatogramDialog：谱图与提取积分。
- CalibrationPage：定量曲线。
- UserStandardsPage / StandardComparisonDialog：用户标准与比较。
- ChatTranscript：消息显示、清空及数量限制。
- scientz/theme：当前项目的统一样式；scientz/models：动作注册。

按钮表达操作意图；实际硬件设置必须交给 AppController，不直接写端口。
以 1024×768 小屏为主要检查尺寸；不要用大量小字、阻塞等待或动画掩盖状态。
tests/UiSmokeTests.cpp 检查交互；更改布局还需查看实际画面。
