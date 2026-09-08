# QITest 01 Qt/C++ 产品架构

本文件是飞秒质谱工作站的唯一工程架构依据。项目不保留 Swift、HTML、React 或其他并行桌面底座。

## 分层

```text
Qt Widgets UI + QAction Registry
              │
        AppController 状态机
   ┌──────────┼──────────┬───────────┐
   │          │          │           │
Device     Science    Workspace    Report
Adapter     Core       SQLite       Engine
   │          │          │           │
真实协议/   确定性数值   检测记录/    可追溯 PDF
模拟器      与质量门控   审计/复核
              │
       Read-only Spectral Library
              │
 Assistant Operation Layer
   ├─ AiCommandRouter -> QAction 白名单 -> 可见 UI 操作
   ├─ LocalKnowledgeStore -> 本地操作手册检索
   └─ LocalAiBridge -> llama-server -> 可替换 Qwen GGUF
```

## 状态与所有权

- `AppController` 是一次检测流程的唯一状态所有者：准备、采集、分析、结果、失败。
- `AnalysisEngine` 只接收结构化谱图与仪器健康状态，输出确定性结果，不访问 UI、网络或大语言模型。
- `WorkspaceRepository` 是用户工作数据的唯一持久化入口；使用独立可写 SQLite，事务提交并保留审计事件。
- 每次采集先建立持久化会话，终态仅为 `COMPLETED` / `CANCELLED` / `FAILED` / `INTERRUPTED`；下次启动自动恢复上次未收口的会话并留存审计。
- 方法采用不可覆盖的版本记录，保存参数 JSON、SHA-256、创建人、活动状态和审计事件；检测记录绑定方法版本与校验值。
- 历史记录保存原始谱图、处理后谱图、指标、识别峰、候选和质量检查，恢复时不重新运行当前算法。
- `SpectralLibraryRepository` 在 App 内只读；导入和升级只能通过 `qitest_library_import` 完成。
- `ReportGenerator` 使用保存后的运行编号、质量检查和候选证据生成 PDF。
- `AiCommandRouter` 只执行低风险、可见、可撤回的页面操作；采集、关机、删除和结论修改不允许由模糊语句直接执行。
- `LocalKnowledgeStore` 对随包操作手册做有界本地检索，不访问网络，也不把检索文本当作科学证据。
- `LocalAiBridge` 使用有上限的顺序队列处理连续输入，只能解释已有结构化证据与检索到的手册内容；缺失时不影响采集、分析、保存、复核和报告。
- `RunArchiveCodec` 使用版本化 JSON 与载荷 SHA-256 导入导出原始谱图；导入数据固定标记为 `IMPORTED_UNVALIDATED`。
- `AuthorizationPolicy` 集中控制采集、复核、导出、方法管理和硬件关键命令权限；UI 不自行定义权限。
- `DiagnosticBundle` 原子写出版本化 JSON，只包含组件状态、设备描述、恢复计数和最后一次运行元数据，明确不导出密码、原始谱图、AI 提示词或候选物名称。

## 仪器接入契约

`IInstrumentAdapter` 提供设备描述、健康状态、命令风险验证、采集与取消。所有命令先经过 `validate()`；硬件关键命令在模拟适配器中永久拒绝。真实适配器必须补齐：

1. 厂商签署或确认的通信协议与协议版本；
2. 原始帧、校验、超时、重试、断线恢复与幂等规则；
3. 参数单位、允许范围、互锁条件和危险动作确认；
4. 原始谱图样本、空白、标准物、混合物和故障样本；
5. 离子化方式、校准、质控与判定验证方案。

在这些资料进入项目以前，不允许猜测串口/TCP 命令，也不允许把模拟结果标成真实海关结论。

## 数据边界

- 正式参考库：随 App 打包、只读、带来源版本与 SHA-256。
- 工作区：位于系统 Application Support，保存运行数据和审计记录，不写入已签名 App Bundle。
- 报告：默认输出到用户“文稿/飞秒质谱报告”，包含记录编号、引擎版本、谱库版本、复核状态和边界声明。
- 诊断：默认输出到用户“文稿/飞秒质谱诊断”，用于支持与现场故障分析，不代替原始数据归档。
- AI：模型与运行时随包离线部署，服务只绑定回环地址并使用进程级临时 Token。
