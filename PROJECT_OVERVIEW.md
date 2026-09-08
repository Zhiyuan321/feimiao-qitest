# QITest Instrument Console Profile

- 产品：飞秒质谱工作站
- 密度：Compact
- 首发平台：macOS
- 唯一技术底座：Qt 6.10.2 Widgets + C++17 + CMake
- 主任务：登录 -> 启动检查 -> 仪器就绪 -> 采集 -> 确定性分析 -> 复核 -> 报告
- 主框架：顶部 Identity/Command Bar + 中央科学画布 + 右侧 Inspector/Telemetry + 底部 Status Rail
- 产品依据：飞秒质谱仪 Word 文档 2.3 的全部页面、子页和操作逻辑
- 仪器边界：当前仅使用显式模拟适配器；真实协议、参数范围和安全互锁接入后才能发送真实命令
- AI 边界：AI 通过可替换适配器进行解释和工具协调；科学数值始终来自本地确定性 C++ 引擎
- 当前参考库：SWGDRUG 3.14 EI 数据仅进入可追溯 SQLite 参考层；在真实离子化方式与匹配算法完成验证前，不参与正式自动判定
- 当前本地模型：Qwen3.5-4B Q4_K_M GGUF；由 `LocalAiBridge` 隔离，允许以后替换兼容服务
