# Windows 7 源码：从解压到运行

## 1. 开发环境

建议在 Windows 10/11 开发电脑编译，再到 Windows 7 SP1 x64 验证。

- CMake 3.24 或以上。
- Qt **5.12.12 / MinGW 7.3 64位** 组件和匹配编译器。
- Qt 官方归档：[Qt 5.12.12 Windows 安装器](https://download.qt.io/archive/qt/5.12/5.12.12/qt-opensource-windows-x86-5.12.12.exe)，文件大小 3,987,337,112 bytes，SHA-256 `27955827c9129e58c9147b201eee33f92d2b8360d9de58643800eb4ff1163f5a`。安装时选择 `MinGW 7.3 64-bit` 组件；该 3.7 GB SDK 不随本仓库上传。
- Qt Creator 是 IDE，其版本不等于项目使用的 Qt 库版本。
- Qt 模块：Core、Gui、Widgets、Network、Sql、Svg；开启测试另需 Test。
- 源码包不含 Qt SDK、编译器、EXE/DLL 和模型权重。当前模型的固定下载地址、大小和 SHA-256 见 `models/使用说明.md`；模型不是编译基础功能的前置条件。

## 2. Qt Creator

打开根目录 **CMakeLists.txt**，选择上述 Kit，首次配置设置：

```text
QITEST_WIN7:BOOL=ON
QITEST_BUILD_TESTS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
```

构建并运行 **QITestWorkstation**。不要选择 qitest_*_tests 或 qitest_library_import。
误选过 Qt 6 时，应换空构建目录重新配置，不要复用旧 CMakeCache。

## 3. 等价 PowerShell 命令

进入源码根目录。Qt 路径按实际安装位置调整，每步成功后再执行下一步：

```powershell
$env:PATH = "C:\Qt\Tools\mingw730_64\bin;C:\Qt\5.12.12\mingw73_64\bin;" + $env:PATH
cmake -S . -B build-win7 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DQITEST_WIN7=ON -DQITEST_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=C:/Qt/5.12.12/mingw73_64
cmake --build build-win7 --target QITestWorkstation --parallel 2
& ".\build-win7\飞秒质谱工作站.exe"
```

从保留上述 PATH 的终端运行；Qt Creator 使用所选 Kit 的环境。

## 4. 排错

| 现象 | 检查 |
| --- | --- |
| 找不到 Qt6 | 未设置 QITEST_WIN7=ON |
| 找不到 Qt5Config.cmake | CMAKE_PREFIX_PATH 指向 Qt 安装目录，不是 bin |
| Qt 版本不匹配 | 精确要求 5.12.12，不是 5.12.0 / 5.15 |
| 编译器不能工作 | 检查匹配的 MinGW 64位、PATH 和生成器 |
| 缺 Qt5 DLL | 从正确 Kit / 终端运行，交付前收集运行库 |
| 没有产品窗口 | 运行目标选择 QITestWorkstation |
| 深度问答不可用 | 源码不含模型，先验证基础功能 |

提供问题时请发 Kit 截图、配置命令、第一条完整错误及前后日志，不要只发最后一行“构建失败”。

## 5. 可选测试

主程序跑通后再开启测试：

```powershell
cmake -S . -B build-win7 -DQITEST_BUILD_TESTS=ON
cmake --build build-win7 --parallel 2
ctest --test-dir build-win7 --output-on-failure
```

## 6. 开发与交付的区别

编译出的 EXE 仍依赖 Qt DLL 和资源，不能单独发给用户。
windeployqt --release --compiler-runtime 可收集 Qt 运行库，但不会收集模型和 AI 子进程全部依赖。
普通用户使用“飞秒质谱工作站安装程序.exe”，工程师使用源码 ZIP。

## 7. 维护入口（首次运行不需要）

- scripts/package_source.py：生成不含模型的源码 ZIP 和校验清单。
- scripts/package_windows_qt512_cross.sh：Mac 交叉编译专用，需要额外配置工具链。
- scripts/package_windows_installer.py：从完整已校验 Windows 运行目录生成安装 EXE。
- third_party/llama.cpp-b10752：本项目的 Win7 推理引擎适配源码，不代表上游官方支持 Win7。
- config/ai-model-manifest.json：当前 Qwen3.5-0.8B Q4_0 模型配置。
- models/使用说明.md：模型固定下载链接、文件大小、SHA-256 和替换规则；模型权重不上传 GitHub。

没有模型仍可编译和运行基础功能。模型解释与受控操作路由分离。
真实仪器必须按协议联调；Wine 检查不能代替 Win7 工控机的驱动、性能和硬件验收。
