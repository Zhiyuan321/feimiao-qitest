# 飞秒质谱工作站 · 工程师入口

**不熟悉英文目录名？先打开 [中文功能导航](START_HERE.md)，按要调试的功能找代码。**

这是 **C++17 / Qt Widgets / CMake** 产品源码，不是测试示例，也不是 QML 或 qmake 工程。

## 第一次运行

1. 解压到短路径，如 C:\Dev\Feimiao，进入包含 CMakeLists.txt 的目录。
2. 用 Qt Creator 打开 **CMakeLists.txt**，不要单独打开 cpp 或新建空工程。
3. Windows 7 目标选择 **Qt 5.15.2 / MinGW 8.1 64位** Kit。
4. CMake 配置设置 **QITEST_WIN7=ON、QITEST_WIN7_QT_VERSION=5.15.2、QITEST_BUILD_TESTS=OFF**。
5. 构建并运行目标 **QITestWorkstation**。MSVC Kit 生成 QITestWorkstation.exe；MinGW / Mac 仍生成“飞秒质谱工作站”。

详见 [Windows 构建步骤与排错](WINDOWS7_BUILD_GUIDE.md)。

网口联调入口：**设置 → 仪器控制 → 运行状态 → 网口TCP**，默认监听11000；可与485同时连接。已接入范围与操作步骤见 [网口首版说明](docs/integration/网口首版接入说明.md)。

## 名称说明

| 名称 | 含义 |
| --- | --- |
| QITestQt | 源码旧目录名，不是测试软件 |
| QITestWorkstation | 正式主程序目标，运行它 |
| qitest_*_tests | 自动化测试，不是产品窗口 |
| qitest_library_import | 谱库导入维护工具，不是主程序 |

## 代码目录

**接手调试先读 [源码总览](src/README.md)，厂家接口直接看 [设备接入](src/device/README.md)。各业务子目录均有中文 README。**

| 位置 | 职责 |
| --- | --- |
| CMakeLists.txt | 唯一工程入口 |
| src/main.cpp | 程序启动 |
| src/ui/ | 窗口、控件、绘图、方法编辑 |
| src/app/ | 工作流程与操作协调 |
| src/core/、src/domain/ | 科学计算、数据结构 |
| src/device/ | 仪器适配与模拟设备 |
| src/storage/、src/library/、src/report/ | 记录、谱库、报告 |
| src/ai/、src/security/ | 助手路由、模型桥接、安全控制 |
| resources/、config/、data/ | 图标、配置与随包数据 |
| tests/、examples/ | 测试与示例 |
| third_party/ | 第三方源码，首次启动无需单独编译 AI |
| models/ | 模型说明和许可证，源码 ZIP 不含权重 |
| cmake/、scripts/ | 工具链、维护和打包脚本 |
| docs/ | [架构、仪器接入与验证导航](docs/README.md) |

本机 build-*、.qa、.tools 是开发产物，不进入源码 ZIP。不要把其他电脑的构建目录复制过来使用。

## 平台与能力边界

- Windows 7 SP1 x64：Qt 5.15.2 / MinGW 8.1，QITEST_WIN7=ON。
- Mac：独立原生程序，当前默认构建路径需要 Qt 6.9 或以上，不使用 Windows Kit。
- package-windows.ps1 是 Qt 6 / Windows 10+ 维护路径，**不要用于 Win7**。
- 首次只构建主程序，不运行交叉编译、模型编译、安装打包脚本。
- 没有模型仍可编译并使用模拟采集、离线分析、方法、谱库、记录及基础助手；深度问答需部署模型及推理引擎。
- 当前模型为 Qwen3.5-0.8B Q4_0，以 config/ai-model-manifest.json 为准。
- 模拟操作不等于真实仪器已接通，实际控制必须按协议逐项联调。

Windows本机一键构建485测试包：`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_win7_qt5152.ps1`。工具路径和验证范围见 WINDOWS7_BUILD_GUIDE.md。
