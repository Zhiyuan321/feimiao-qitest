# 架构与修改路线

唯一源码入口是根目录 CMakeLists.txt。业务阅读顺序见 [src/README.md](../../src/README.md)，各模块职责只在对应目录说明中维护，避免复制多份后过期。

## 调一个功能，找到一个负责层

| 任务 | 入口 | 需要检查 |
| --- | --- | --- |
| 控制按钮没有反馈 | app/AppController.cpp → device/IInstrumentAdapter.h | 请求号、key、回读、超时；InstrumentControlTests |
| 接厂家 SDK / 串口 / TCP | device/README.md → examples/vendor_adapter | 固件、单位、线程、真实台架 |
| 页面和按钮布局 | ui/README.md | UiSmokeTests 与 1024×768 实际画面 |
| 曲线、积分、校准 | core/README.md | CoreTests，单位与可手算案例 |
| 导入和保存 | storage/README.md | WorkspaceTests，损坏文件与事务 |
| 谱库编辑 | library/README.md | LibraryTests，公共库只读 |
| 自然语言导航 | ai/AiCommandRouter.cpp | AiEvidenceTests，否定和歧义指令 |
| 深度问答慢或失败 | ai/LocalAiBridge.cpp + config/ai-model-manifest.json | LocalAiBridgeTests，实际模型独立测试 |
| PDF 与复核 | report/README.md | 保存证据、复核状态与实际 PDF |

表中代码路径以 src/ 为起点，examples、tests、config 为工程根目录下路径。

## 不应混合的职责

- UI 发意图，控制器维护流程；设备接口负责通信，不能反过来操作窗口。
- 设备状态来自真实回读，不从按钮颜色、保存预设或模型回答推断。
- 科学数值由 core 计算，AI 只提供可选解释。
- 每个导入线程有自己的数据库连接，不能跨线程复用 GUI 的连接。
- 插件 ABI 改动需同步重编宿主和插件。

## 交付与参数

当前交付目录为软件开发/05-交付。用户拿安装 EXE，开发人员拿 Windows源码.zip。
旧 07 路径不再作为当前交付入口。
模型与低资源参数统一看 config/ai-model-manifest.json，不在多个说明中重复写死。
Mac 与 Windows 使用各自构建目录，不互相覆盖 CMakeCache。

## 本次整理边界

保留原类名、目录引用、公共接口与计算逻辑；中文 README 和关键接口注释用于帮助人工接手。
不删除自动化测试或第三方许可证，也不把重命名文件当成修复硬件功能。
真实设备、长时间运行和 Windows 7 工控机性能仍需单独验收。
