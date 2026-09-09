# Windows 7：Qt 5.12.12 构建与TCP/485测试包

## 当前工具链

Windows 7构建固定使用 **Qt 5.12.12 / MinGW 7.3 64位**，目标为Windows 7 SP1 x64。开发机可使用Windows 10/11，保留现有Qt Creator即可。

本机实际工具位置（其他电脑按安装位置调整）：

- Qt：`D:/Qt/5.12.12/5.12.12/mingw73_64`
- 编译器：`D:/Qt/5.12.12/Tools/mingw730_64/bin/g++.exe`
- 调试器：`D:/Qt/5.12.12/Tools/mingw730_64/bin/gdb.exe`
- CMake：`D:/Qt/Tools/CMake_64/bin/cmake.exe`，至少3.24。
- Qt模块：Core、Gui、Widgets、Network、Sql、Svg、SerialPort；测试另需Test。

## 一条命令构建和打包

在工程根目录的PowerShell执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_win7_qt51212.ps1
```

脚本固定检查Qt 5.12.12、MinGW 7.3.0 x64；使用独立的 `build/win7-qt51212-release`，构建Release主程序，自动收集Qt/MinGW运行库、平台插件和随包配置。输出为新的 `build/packages/Feimiao-Win7-Qt5.12.12-TCP-485-时间戳` 目录和同名ZIP。

工具安装在其他位置时可指定：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_win7_qt51212.ps1 -QtRoot C:\Qt\5.12.12\mingw73_64 -MinGWRoot C:\Qt\Tools\mingw730_64 -CMakeExe C:\Qt\Tools\CMake_64\bin\cmake.exe
```

追加 `-WithTests` 执行网络、485、设备控制器、核心计算、AI证据以及网络/485/控制状态界面专项测试，不是全量UI回归。无头测试如缺字体，可设置 `QITEST_UI_FONT=C:/Windows/Fonts/msyh.ttc`。

测试包不包含AI模型、推理程序、外部参考谱库和用户运行数据库。完整解压后运行“飞秒质谱工作站.exe”，保持DLL、插件子目录及资源齐全。此脚本生成便携测试ZIP，不生成安装向导EXE。

## Qt Creator构建

1. 打开 `CMakeLists.txt`，启用 **Desktop Qt 5.12.12 MinGW 64bit** Kit。
2. Qt版本指向上述Qt目录的 `bin/qmake.exe`；C/C++分别指向配套 `gcc.exe`、`g++.exe`；调试器指向上述GDB；生成器为 **MinGW Makefiles**。
3. 新建构建目录，例如 `build/creator-qt51212-release`，不要复用其他Qt版本或编译器的缓存。设置：

```text
QITEST_WIN7:BOOL=ON
QITEST_WIN7_QT_VERSION:STRING=5.12.12
QITEST_BUILD_TESTS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
```

4. 点击执行CMake，再构建并运行 **QITestWorkstation**。测试目标不是主窗口。
5. 部署配置选普通“部署设置”，不要使用Automatic Application Manager Deploy Configuration。遇到appman-controller不存在时，检查此项；本程序无需Application Manager部署。
6. 设置 → 仪器控制 → 运行状态，可选择“485串口”或“网口TCP”。两者可同时连接，只读状态；详细步骤见 [网口首版说明](docs/integration/网口首版接入说明.md)。

新Windows构建默认使用此Qt5方案。旧缓存若保留其他版本，会报明确错误；修改版本值并使用匹配Qt套件和新构建目录。原有Qt6/Windows10+及macOS路径仍由 `QITEST_WIN7=OFF` 选择，不能用于Win7。

## 交付与验证

脚本以 `--release` 显式部署Release运行库，避免自动判断误选Debug DLL。打包必须使用Qt 5.12.12目录内的windeployqt，不能混用其他Qt版本的DLL。脚本检查Qt5SerialPort、qwindows、qsqlite及MinGW运行库，并生成SHA256清单。USB转485驱动需在目标工控机安装。

Qt编译器、DLL与外部厂家插件必须匹配版本及架构；更换Qt版本后厂家插件需要配套重编译。原有网口/485实现保留。

2026-09-09：在可写副本中，Qt 5.12.12/MinGW 7.3 Release主程序编译、网络/485/设备控制器/核心/AI证据五组测试及网络/485/控制状态三项UI专项测试通过；新版打包脚本生成便携ZIP。

验证记录只记录实际执行结果；开发机编译/测试通过不等于Win7 SP1工控机、硬件读数、驱动和断线恢复已验收。历史验证结果不能替代当前版本测试。

## 其他维护入口

- `scripts/package_windows_qt512_cross.sh`：macOS交叉构建Qt 5.12.12，需要额外工具链。
- `scripts/package_windows_installer.py`：从完整已校验Windows运行目录制作安装向导。
- 根目录 `package-windows.ps1`：Qt6/Windows10+维护路径，不用于Win7。
- `scripts/package_source.py`：源码打包。
- `config/ai-model-manifest.json`、`models/README.md`：AI模型部署说明。windeployqt不会收集模型和AI子进程全部依赖。
