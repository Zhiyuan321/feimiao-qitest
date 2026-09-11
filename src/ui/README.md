# 界面层

先按页面定位，不必通读整个 MainWindow：
- MainWindow：主窗口、导航、页面组装、状态连接。样品分析统一“开始检测”；检测中禁用按钮与操作入口，完成弹窗点击“确认”后恢复。失败/取消不弹成功提示，打开历史记录不触发完成确认。
- NetworkConnectionPanel / Rs485ConnectionPanel：运行状态页的网口TCP与485标签，可同时连接。
- Rs485ConnectionPanel同时显示主控板状态与分子泵换算值（说明保留原始值），一组COM连接按钮，支持共享收发报文导出。
- DeviceWaveformPanel：样品分析中的单包气压图、射频调谐启停；RF曲线编码/倍率仍待确认。
- InstrumentWorkbench：质量轴等离线工作区。
- MethodEditorDialog：管理员完整参数；普通角色仅扫描模式和进样时间（0～600 ms、0.01 ms）。保存新版本保留原方法其余参数，仍为离线草稿，不下发设备。当前沿用既有登录角色，未实现本地账户创建/密码管理。
- SpectrumPlot / ChromatogramDialog：谱图与提取积分。
- CalibrationPage：定量曲线。
- UserStandardsPage / StandardComparisonDialog：用户标准与比较。
- ChatTranscript：消息显示、清空及数量限制。
- scientz/theme：当前项目的统一样式；scientz/models：动作注册。

按钮表达操作意图；实际硬件设置必须交给 AppController，不直接写端口。
以 1024×768 小屏为主要检查尺寸；不要用大量小字、阻塞等待或动画掩盖状态。
tests/UiSmokeTests.cpp 检查交互；更改布局还需查看实际画面。
