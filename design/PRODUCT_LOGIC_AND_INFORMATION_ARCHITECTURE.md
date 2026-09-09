# 飞秒质谱工作站：产品意图、任务逻辑与信息层级

更新日期：2026-08-31

## 一、产品真正要解决的问题

这不是通用数据分析软件，也不是把另一款新芝软件换皮。它是一套面向海关/现场筛查场景、可嵌入质谱仪屏幕的工作站。核心目标按优先级排列为：

1. **科学结果可信且可复核**：不能把候选匹配直接写成正式鉴定结论。
2. **仪器操作安全且可恢复**：采集、降温、关机和关键设置必须经过状态门控。
3. **长期稳定且可追溯**：检测、谱图、方法版本、质量检查、操作者和报告形成同一条记录链。
4. **零基础人员能完成主流程**：默认路径短，下一步明确；专业能力按任务逐层展开。
5. **在上述条件成立后再优化速度和视觉**。

SWGDRUG 将质谱列为能提供结构信息的高选择性技术，但明确指出分析方案仍需要经过验证并结合其他不相关技术；UNODC 对毒品分析强调可靠、有效、标准化、符合法证要求和质量保证。因此软件中的“可疑、需复核、证据缺口”不是保守装饰，而是科学边界。

## 二、唯一主流程

```text
登录/受控演示
    ↓
程序与仪器自检、预热
    ↓
准备：确认活动方法、样本/案件信息、仪器就绪
    ↓
运行方法：采集 → 确定性分析 → 谱库匹配 → 质量门控
    ↓
复核：查看 TIC、MS/MS、候选证据、异常和缺口
    ↓
生成报告/归档
```

- 软件打开后只有一个明确的正常下一步。
- “运行方法”是整条采集与分析事务，不是仅播放一条演示曲线。
- 没有数据时在主页原位提供“运行当前方法”或“导入归档”，不创建无意义欢迎页。
- 报告必须来自当前已保存记录，不能脱离谱图、候选与质量状态单独生成。
- 真实仪器协议未接入前，模拟适配器必须继续清楚标记，不能伪装成真实验证。

## 三、界面层级

### 永久状态层

始终可见且位置固定：当前阶段、仪器是否就绪、关键报警、当前方法/记录。报警颜色只在需要操作员注意时出现，正常状态不使用大面积高饱和色。

### 一级：高频任务

顶部固定为三个逻辑组：

| 位置 | 功能 | 原因 |
|---|---|---|
| 左侧 | 智能台 | 随时辅助当前任务，但不是主流程页面 |
| 中央 | 主页、运行方法、生成报告、任务 | 操作员每天反复使用的工作流 |
| 右侧 | 仪器栏、降温、开关机、设置 | 与设备和系统安全相关，需要稳定位置 |

Apple 的工具栏指南建议最多三个逻辑组：leading 放导航/侧栏，center 放常用控制，trailing 放必须持续可用的检查器和重要操作。这一原则只用于信息秩序；实现仍为 Qt Widgets/C++，不引入 Swift。

### 二级：任务菜单

“任务”只收纳有真实页面和数据模型的低频专业任务：

- 编辑方法：方法版本、校验摘要、明确激活。
- 参考谱库：本地检索、来源与版本说明。
- 定量曲线：导入校准点、确定性拟合、R² 与适用范围提示。
- 检测记录：分页/有界历史列表、打开单条记录、归档与复核状态。

Agilent MassHunter 的官方工作流资料说明，同一工作流应装载特定方法与特定布局，并只呈现该任务相关的列和编辑区。这里采用同样的任务收敛思想，但不复制它的 UI。

### 三级：设置与维护

设置不再与检测任务并列。右侧分类固定，中间始终显示所选任务的真实内容：

- 仪器：配置、离子源、载气节省。
- 维护：降温/开关机、调谐、清洗、诊断。
- 检测与数据：视图、文件、处理参数、谱库与方法维护。
- 用户与帮助：账户、权限、帮助、版本和诊断信息。

如果协议或正式数据尚未具备，页面必须说明缺失条件并阻断执行，不能放一个可点击但无行为的空按钮。

## 四、中央内容如何避免“空”与“堆”

- 中央永远优先给谱图、结果表和当前任务，而不是品牌图、宣传图或技术栈文字。
- 数据未到达时显示就绪条件和唯一下一步；数据到达后原位替换为谱图和结论。
- 不用卡片数量制造丰富感。一个区域能表达清楚时不拆成多个边框容器。
- 当前视图不需要的右栏或智能台应收起，让图表自然扩展。
- 智能台与仪器栏可独立开关并可同时打开；窗口不足时以中央数据区最小宽度为约束，优先压缩或收起侧栏，不用互斥规则替代响应式布局。

## 五、引擎边界

```text
Qt Widgets / QAction / Model-View
              ↓
       AppController 状态机
        ↙       ↓        ↘
仪器适配器  科学分析核心  工作区/审计
               ↓             ↓
        谱库与定量引擎    报告/归档
               ↘             ↙
          可替换本地 AI 连接器
```

- `IInstrumentAdapter` 隔离模拟设备和未来真实协议。
- `AnalysisEngine`、`QuantitationEngine` 只做确定性计算，不依赖 AI。
- `WorkspaceRepository` 用事务保存检测全链路，界面只做有界读取。
- `LocalAiBridge` 是模型连接器；模型升级只替换路径/服务，不修改科学算法和 UI。
- `AiCommandRouter` 只能路由白名单低风险操作；关键仪器命令不得由自然语言直接执行。

### 两套并行驱动引擎

1. **数据与仪器驱动引擎**：`IInstrumentAdapter` 输出统一的 `InstrumentHealth`、`InstrumentTelemetry` 和原始谱图；`AnalysisEngine`、`QuantitationEngine`、谱库与质量门控完成确定性处理；`WorkspaceRepository` 在同一事务链中保存原始谱图、处理谱图、峰、候选、质量检查和当次仪器遥测。厂家协议到位后只增加真实适配器，不改变 UI、科学计算或数据结构。
2. **本地语言操作引擎**：`LocalKnowledgeStore` 检索本地手册，`AiEvidenceBuilder` 只把上述结构化状态和确定性结果交给可替换的 Qwen 连接器解释；`AiCommandRouter` 在模型之前处理页面白名单命令。模型不能写入仪器遥测、科学数值、候选结论和正式报告状态。

两套引擎并行但不互相替代：没有 AI 时数据采集、分析、复核和报告继续运行；没有真实仪器数据时 AI 必须明确说明缺口，不能补造数值。

### 厂家界面补充的遥测契约

参考 QitVenture 6 的实际界面，统一遥测至少覆盖：分子泵转速/电流/电压/温度、真空度、载气模式/气压/流速、离子阱温度、TD 温度、离子源电压、倍增器电压、抽气流量和注射泵余量。主页只展示异常与关键值；“仪器 → 查看全部仪器参数”展示完整列表，避免把所有字段堆在小屏首屏。

## 六、嵌入式 Qt 决策

- 当前唯一实现是 **Qt 5.12.12 + C++17 + Qt Widgets**。
- Qt 官方说明：Widgets 适合结构明确的传统界面；在嵌入式 Linux 上 Widgets 使用软件渲染，简单、低动画、低重绘界面仍可能合适，复杂动画界面则应评估 Qt Quick。
- 本项目刻意保持低动画、稳定布局和 QPainter 科学图表，因此在目标硬件参数未知前不迁移 QML；必须先在实际屏幕分辨率、CPU/GPU、触控设备上测量。
- 嵌入式部署需要单独验证 EGLFS/Wayland、触控设备映射、旋转、DPI、断电恢复和全屏启动。Mac 版验证不能替代目标设备验证。

## 七、GitHub 项目取舍

- OpenMS 是成熟的跨平台 C++ 质谱分析基础设施，支持 mzML/mzXML 等标准格式和多种分析工具。未来若接入标准质谱文件，可在独立适配层评估其格式/算法能力。
- TOPPView 适合专家型 1D/2D/3D 数据探索，但界面复杂度不适合直接移植到仪器小屏。
- Qwt 提供成熟的 Qt 科学绘图交互，但在当前自绘谱图满足需求前不新增依赖；只有缩放、数据拾取和大数据降采样成为真实瓶颈后再进行许可证与性能评估。
- 不采用汽车仪表盘、深色大表盘、强动画 HMI 模板；它们强调动态视觉，不符合质谱复核和长时间稳定运行的任务。

## 八、来源

- 项目权威需求：`要求/飞秒质谱仪软件界面优化设计.docx` 与 `要求/飞秒质谱仪中文版.pdf`。
- Apple Human Interface Guidelines — Toolbars: https://developer.apple.com/design/human-interface-guidelines/toolbars
- Apple Human Interface Guidelines — Sidebars: https://developer.apple.com/design/human-interface-guidelines/sidebars
- ISA101 Human-Machine Interfaces: https://www.isa.org/standards-and-publications/isa-standards/isa-standards-committees/isa101
- Qt for Embedded Linux: https://doc.qt.io/qt-6/embedded-linux.html
- Qt 5.12 Embedded Linux: https://doc.qt.io/archives/qt-5.12/embedded-linux.html
- Agilent MassHunter Qualitative Analysis: https://www.agilent.com/en/product/software-informatics/mass-spectrometry-software/data-analysis/qualitative-analysis
- SCIEX OS brochure: https://sciex.com/content/dam/SCIEX/pdf/brochures/sciexosbrochure.pdf
- SWGDRUG Recommendations 8.1: https://swgdrug.org/Documents/SWGDRUG%20Recommendations%20Version%208.1_FINAL_ForPosting_Rev%201-23-23.pdf
- UNODC drug-testing laboratory QMS guidance: https://www.unodc.org/documents/scientific/QMS_Ebook.pdf
- OpenMS: https://github.com/OpenMS/OpenMS
