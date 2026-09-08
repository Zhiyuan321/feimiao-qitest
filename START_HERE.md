# 飞秒质谱工作站 · 中文导航

这份文件供接手工程师阅读。英文文件夹是稳定的编译路径，不必先理解所有英文名，按下面的中文功能进入即可。

## 第一次运行

先读 [工程入口与运行步骤](USER_GUIDE.md)，再读 [Windows 7 编译说明](WINDOWS7_BUILD_GUIDE.md)。
用 Qt Creator 打开 CMakeLists.txt，运行目标选 QITestWorkstation；带 tests 的目标不是软件主界面。

## 按功能找代码

| 想处理什么 | 中文说明入口 | 主要代码 |
| --- | --- | --- |
| 程序启动、选择厂家插件 | [源码总览](src/README.md) | src/main.cpp |
| 接串口、网口、厂家 SDK | [仪器接口](src/device/README.md) | IInstrumentAdapter.h、IInstrumentPlugin.h |
| 找厂家驱动示例 | [插件模板](examples/vendor_adapter/README.md) | VendorPlugin.cpp |
| 按钮下发、回执、超时 | [流程控制](src/app/README.md) | AppController.cpp |
| 窗口、设置、按钮外观 | [界面](src/ui/README.md) | MainWindow.cpp |
| 统一颜色、字体、控件 | [界面主题](src/ui/scientz/theme/README.md) | ScientzTheme.cpp |
| 分析、积分、定量、校准 | [科学计算](src/core/README.md) | 各 Engine / Calibration 文件 |
| 数据字段与单位 | [公共数据结构](src/domain/README.md) | Models.h |
| 导入、记录、数据库 | [数据存储](src/storage/README.md) | WorkspaceRepository、各 Codec |
| 谱库、用户标准 | [谱库](src/library/README.md) | 各 Repository |
| PDF 与结果报告 | [报告](src/report/README.md) | ReportGenerator.cpp |
| 基础助手、模型问答 | [智能助手](src/ai/README.md) | AiCommandRouter、LocalAiBridge |
| 权限与危险操作 | [安全](src/security/README.md) | AuthorizationPolicy.cpp |
| 排错与诊断导出 | [诊断](src/support/README.md) | DiagnosticBundle.cpp |
| 回归测试 | [测试说明](tests/README.md) | 按模块选择测试 |
| 源码打包、安装包制作 | [维护脚本](scripts/README.md) | 各 package / verify 脚本 |

“主要代码”中的文件位于对应说明所在目录；界面主题等特殊路径以链接为准。

## 命名原则

- 中文用于说明标题、关键注释和用户看到的文字。
- 保留 src、device、CMakeLists.txt 等路径，以及类名、函数名、插件标识，不破坏编译和接口兼容。
- QITestQt 是历史源码目录名；QITestWorkstation 是主程序构建目标，均不是另一套产品。
- scientz 是界面内部的历史路径名，不需要为调试再打开另一个工程。
- 第三方目录和许可证保留原状，不批量翻译上游代码。
- 建议工程解压到 C:\Dev\Feimiao 等短英文路径，降低老工具链处理路径的风险。

## 调接口先走这一条

按钮 → AppController 校验与确认 → IInstrumentAdapter 下发 → 厂家实际回读 → settingFinished → 界面更新。

“已经发送”不是“执行成功”；模拟器正常也不是实机通过。详细断点和回执要求见仪器接口说明。
