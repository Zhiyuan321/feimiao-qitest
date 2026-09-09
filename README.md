# 飞秒质谱工作站

本仓库保存飞秒质谱工作站的 Qt/C++ 源代码，供工程师继续开发、编译和对接仪器。

## 阅读顺序

1. [中文导航](START_HERE.md)
2. [工程入口与运行步骤](USER_GUIDE.md)
3. [项目架构](ARCHITECTURE.md)
4. [Windows 7 编译说明](WINDOWS7_BUILD_GUIDE.md)

## 兼容基线

- Windows 客户版：Qt 5.12.12 x64、Windows 7 SP1、8 GB 内存、1024×768 / 4:3 屏幕。
- macOS 预览版：Qt 5.12.12 / Apple Clang / x86_64，Apple Silicon 通过 Rosetta 2 运行；不作为对外交付或仓库发布件。
- 主程序构建目标：`QITestWorkstation`。

## 目录说明

- `src/`：程序主体、界面、仪器接口、科学计算、存储和报告。
- `tests/`：按模块划分的自动化测试。
- `resources/`：图标、样式及随程序发布的静态资源。
- `examples/`：厂家仪器适配器示例。
- `scripts/`：构建、打包和验证脚本。
- `docs/`、`design/`：接口、架构、交付与设计说明。

## Windows 发布文件

正式 Windows 安装包不写入 Git 历史，通过
[GitHub Release](https://github.com/Zhiyuan321/feimiao-qitest/releases/tag/v2026.09.08)
发布。Gitee 作为国内源码镜像时，只同步代码、分支和标签，不复制 GitHub Release 附件。

模型权重、客户检测数据、本地工具链、构建缓存和验证截图不进入仓库。

未经真实仪器、正式协议和客户验收的数据，不得作为正式检测结论。
