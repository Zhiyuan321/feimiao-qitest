# Windows 7：Qt 5.15.2 构建与485测试包

## 当前工具链

当前Windows方案使用 **Qt 5.15.2 / MinGW 8.1 64位**，目标为Windows 7 SP1 x64。开发机可以使用Windows 10/11；Qt Creator可以继续使用现有版本。

本机已有工具链：

- Qt：`D:/Qt/5.15.2/mingw81_64`
- 编译器：`D:/Qt/Tools/mingw810_64/bin/g++.exe`
- CMake：`D:/Qt/Tools/CMake_64/bin/cmake.exe`（要求3.24或以上）
- Qt模块：Core、Gui、Widgets、Network、Sql、Svg、**SerialPort**；测试另需Test。

路径是本机示例，其他电脑请传入实际安装路径。Qt6的DLL不能与此包混用。仅开启Win7宏不会把Qt6转换为Win7运行库。

## 一条命令构建和打包

在工程根目录打开PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_win7_qt5152.ps1
```

脚本配置独立的 `build/win7-qt5152-release`，构建Release主程序，并自动收集Qt与MinGW运行库。每次生成新的 `build/packages/Feimiao-Win7-Qt5.15.2-485-时间戳` 文件夹和同名ZIP，不覆盖以前的包。

不同安装位置可指定：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_win7_qt5152.ps1 -QtRoot C:\Qt\5.15.2\mingw81_64 -MinGWRoot C:\Qt\Tools\mingw810_64 -CMakeExe C:\Qt\Tools\CMake_64\bin\cmake.exe
```

追加 `-WithTests` 会先执行485、设备控制器、核心计算、AI证据测试及485/控制状态界面专项测试。此选项不是全量UI测试。

测试包用于485读取和基础界面联调；不附带AI模型、推理程序、外部参考谱库或用户运行数据库。完整解压后运行“飞秒质谱工作站.exe”，保留所有DLL与子目录。详细操作见包内“测试说明.txt”。

## 在Qt Creator里构建

1. 打开工程 `CMakeLists.txt`，选择 **Desktop Qt 5.15.2 MinGW 64-bit** Kit。若未自动识别，在Kits的Qt Versions中添加上述Qt目录的 `bin/qmake.exe`，编译器选择MinGW 8.1 x64。
2. 新建构建目录，例如 `build/creator-qt5152-release`；不要复用Qt6/MSVC构建缓存。
3. 设置：

```text
QITEST_WIN7:BOOL=ON
QITEST_WIN7_QT_VERSION:STRING=5.15.2
QITEST_BUILD_TESTS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
```

4. 构建/运行目标选择 **QITestWorkstation**。此MinGW Kit生成“飞秒质谱工作站.exe”，`qitest_*_tests`不是主程序。
5. 在“设置 → 仪器控制 → 运行状态”选择实际COM口，点击“连接并读取”。9600/8N1/无流控，只查询状态，未开放硬件控制或真实谱图采集。

新Windows构建默认开启Qt5方案。已经存在的Qt6缓存会保留原来的OFF值；使用Qt5 Kit时请显式核对ON。macOS仍使用Qt6分支。

## 打包工具的实际用法

上述脚本会调用匹配Qt5工具链的windeployqt。**这套MinGW环境不要强制传 `--release`**：Qt5.15源码说明MinGW的PE调试标记不能可靠判断，此选项会错误筛掉平台插件，报“Unable to find the platform plugin”。程序仍按CMake Release编译，打包时让工具自动选择MinGW插件。

```powershell
$env:PATH = "D:\Qt\5.15.2\mingw81_64\bin;D:\Qt\Tools\mingw810_64\bin;" + $env:PATH
& "D:\Qt\5.15.2\mingw81_64\bin\windeployqt.exe" --compiler-runtime --no-translations "D:\测试包\飞秒质谱工作站.exe"
```

脚本还检查Qt5SerialPort、qwindows、qsqlite和MinGW运行库，并生成SHA256清单。USB转485适配器的驱动需另在目标机器安装。

## 保留的旧方案

Qt5.12.12旧工具链仍可使用，需显式指定 `QITEST_WIN7_QT_VERSION=5.12.12`，并使用配套MinGW 7.3和新的构建目录。`scripts/package_windows_qt512_cross.sh`保持Mac交叉构建旧版本的用途，已明确固定5.12.12；旧校验/安装脚本也保持其原有交付目录约定。

根目录 `package-windows.ps1` 属于Qt6旧维护路径；本次Qt5.15.2测试包使用新的 `scripts/build_win7_qt5152.ps1`。

## 验证边界

2026-09-08：在本机Windows、Qt5.15.2/MinGW8.1 Release下，主程序编译通过；485协议、设备控制器、核心计算、AI证据四组测试通过；485回读/失效、控制开关回读、固定横屏布局三项UI测试通过。仅记录这些已执行测试，不代表全量UI回归通过。

Win7 SP1目标工控机、实际USB转485驱动和硬件读数仍需现场验收，开发机测试不替代实机验证。
