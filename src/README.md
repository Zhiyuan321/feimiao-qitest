# 业务源码阅读顺序

先看 main.cpp（启动）→ app/AppController.h（对外操作）→ device/IInstrumentAdapter.h（厂家接口）。调界面再看 ui/，调算法再看 core/，不必从 MainWindow.cpp 第一行读到底。

```text
按钮 / 基础助手指令
  → AppController：校验、确认、审计、维护状态
    → IInstrumentAdapter：厂家实现通信，返回真实回读
    → core：确定性分析 → storage / report
    → LocalAiBridge：可选模型解释，不承担硬件控制
```

各子目录的 README 说明入口、边界和测试。公共数据结构在 domain/Models.h。
本次保留编译路径、类名和插件 ABI，不移动代码打断厂家接入。
