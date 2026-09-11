# 厂家仪器接入契约

现已增加网口TCP只读首版，可与485并行；具体入口、已采用的协议规则和待确认项见 [网口首版接入说明](网口首版接入说明.md)。

## 交付状态与边界

两端程序已经把开关统一路由到 `IInstrumentAdapter::requestSetting`，包含权限检查、用户确认、一次一条命令、5 秒回执超时、迟到回执丢弃、真实回读检查及审计。

**随包默认仍是模拟适配器。已内置485只读状态驱动，控制数据区目录仍不等于完整的控制驱动，尚未经实机验证。模板不是一个已完成的厂家驱动。** 不猜测帧格式、寄存器、安全范围或接线；从 [设备目录说明](../../src/device/README.md) 开始定位。

真实采集数据解码与科学管线未经确认，当前版本对真实适配器的采集入口明确阻断。厂商完成原始谱图、方法参数、科学算法与报告的联合验证后，才能解除门控；不能把合成谱图当作实测。

## 2026-09-10离子源回读单位更正

高压模块原始整数÷10得到V，映射到`telemetry.ionSourceVoltageV`；例如49→4.9 V。`health.ionSourceKv`仍保持公共接口的kV单位，存储V÷1000（0.0049 kV），界面再按V显示。此前将结果标为kV的解释已由用户更正。表格保留原始整数，去掉原先误导的V后缀；导出兼容保留旧键`highVoltageV`（实际是原始值），另提供`ionSourceVoltageV`与正确单位的`ionSourceKv`。断开、主控超时或读数过期均恢复未知，有效零值显示0。此映射仅用于读取，不开放电压设定。

## 内置485状态读取（2026-09-08）

已按《便携式质谱485通讯协议(2).pdf》接入串口状态查询和回读显示。无需厂家插件即可使用；默认启动仍是模拟模式，选择串口后切换为真实485只读适配器。当前没有实机联调结果。

### 使用

1. 构建时安装对应 Qt 的 **SerialPort** 模块，与 Core/Widgets 等使用同一套版本、编译器和架构。
2. 启动工作站，打开“仪器配置 → 运行状态”（也可由仪器监控的查看状态入口进入）。选择USB转485对应COM口，点击“连接并读取”。只记住所选端口，不因保存过端口而自动连接。
3. 固定通信参数：**9600 bps、8数据位、无校验、1停止位、无流控**。串口打开后先显示等待回读，收到有效23字节状态数据后才显示连接正常。
4. 每秒尝试一次状态查询，每次最多等待1500 ms，始终只有一条未完成查询。失败、断线或超时后关闭端口并清空读数，检查设备后手动重连。
5. 也可启动前设置 `QITEST_RS485_PORT=COM3`（COM3只是示例，必须替换为实际端口）。它与 `QITEST_INSTRUMENT_PLUGIN` 互斥；指定485后失败不会回退模拟器。重启前取消该环境变量可恢复默认模拟模式。

### 报文与数据映射

完整查询帧：`55 88 30 00 01 01 AA`。帧为 `55 88 CMD LEN_H LEN_L PAYLOAD AA`，长度仅计数据区，16位字段高字节在前，本版无校验字节。

状态应答为命令 `0x30`、数据长度 `0x0017`（23字节，完整帧29字节）。以下偏移从数据区第一个字节起算：

| 偏移 | 字段 | 处理 |
| --- | --- | --- |
| 0 / 1 / 2 | 观察灯 / TD与离子阱合并加热 / 废液泵 | EE开启，FF关闭 |
| 3 | 载气选择 | EE外载气，FF内载气；不是供气开关 |
| 4 / 5 / 6 | HV24V / RF24V / 隔膜泵 | EE开启，FF关闭 |
| 7–8 | 高压模块原始值 | uint16；按2026-09-10用户更正，÷10得到离子源电压(V)，如49→4.9 V；不是倍增器电压 |
| 9–10 | 高压模块电流 | uint16，μA |
| 11–12 | 真空规原始值 | uint16，mV；压力换算尚未确认 |
| 13–14 | 气压 | uint16，Torr |
| 15–16 / 17–18 | TD温度 / 离子阱温度 | uint16 ÷ 10，℃ |
| 19–20 | EFC流量 | uint16 ÷ 200，mL/min |
| 21–22 | 气泵PWM | uint16，% |

解码检查头、地址、长度、尾、状态值及协议明确的电压/电流/真空规/PWM范围；支持拆帧、连续帧和数据内的55/AA。忽略查询回显及其他命令，不将1字节控制ACK当状态帧。协议没有CRC，结构检查不能发现所有传输位错误。

当前只发送上述状态查询。加热、泵、电源、温度和流量设定均未发送；设定倍率、合并加热语义及网口/485控制归属待进一步确认后接入。未接入的数据项显示“未提供/未知”，禁止以0或模拟值代替。实际温度/流量不冒充设定值确认。

485正常回读不代表质谱采集已接通，`connected=true` 时 `ready` 仍为false，真实谱图采集入口保持阻断。本次没有实现网口。

### 工程位置和验收

- `src/device/Rs485Protocol.*`：报文解析与状态解码。
- `src/device/Rs485Instrument.*`：异步QSerialPort轮询、缓存、失效处理。
- `src/ui/Rs485ConnectionPanel.*`：串口选择和15项回读。
- `tests/Rs485Tests.cpp`：协议与虚拟串口传输测试。
- `tests/InstrumentControlTests.cpp`、`tests/UiSmokeTests.cpp`：控制器与界面回归；测试使用内存串口，绝不枚举后自动连接实机。

实机验收还需在实际COM口接设备，核对一帧原始应答与仪器显示，验证拔插和断电后的失效提示。本机Qt6/MSVC编译和测试不替代Qt5.12.12/Win7目标机器、安装包及设备验收。

插件接口新增只读能力和缓存状态通知，ABI标识升级为 `cn.feimiao.InstrumentPlugin/1.1`，厂家插件须按新头文件同步重编译；旧1.0插件不会当作兼容接口加载。

## 接入步骤

1. 提供协议/SDK、仪器型号、固件、USB/串口/TCP 信息、开关含义、硬件互锁、安全范围、单位、回执示例、断线恢复策略、原始谱图样本。
2. 以 `examples/vendor_adapter` 为起点实现 `IInstrumentPlugin` 工厂及 `IInstrumentAdapter`。编译器、Qt 版本、架构与主程序一致：Win7 x64 Qt 5.12.12/MinGW 7.3；Mac 当前 Qt 6/Apple Clang。
3. `descriptor()` 返回真实设备标识且 simulation=false；`health()`、`telemetry()` 和 `confirmedSettings()` 只返回缓存的实际回读，不能在 GUI 线程同步等待硬件。
4. `validateSetting()` 核对厂家确认的安全范围、连接、实时互锁、能力及运行状态。主程序数值输入上限只是演示编辑器范围，**不是物理安全范围**。
5. `requestSetting(requestId,key,value)` 立即返回，在自有工作线程调用 SDK；收到匹配设备回执和回读后发 `settingFinished`。Qt 会将信号转回控制器线程。
6. 超时或设备拒绝：success=false。超时后晚到 ACK 被主程序丢弃；主动重新读状态后才恢复显示，不能自动重发高压/电源命令。
7. 将可信插件绝对路径设为 `QITEST_INSTRUMENT_PLUGIN` 再启动程序。未设置且未选择内置485时使用模拟器；指定插件加载失败就报错退出，不悄悄回退模拟。主程序不会联网下载插件。
8. 厂家必须补上真实身份认证/权限来源；当前本地角色与 `QITEST_OPERATOR_ROLE` 是原型配置，不是工业安全认证。最终硬件互锁必须由设备自身执行。

插件须使用相同头文件 ABI；改变接口需同步重编译宿主和插件。SDK 若需要 `.sys` 内核驱动，必须在真实 Windows 安装并验证；Wine 不能替代。

## 控制键

| key | 含义 | 值类型 |
| --- | --- | --- |
| powerOn | 仪器电源，不是电脑电源/电池 | bool |
| rfOn | RF | bool |
| ionHighVoltageOn | 离子源高压 | bool |
| ionSourceEnabled | 离子源启用 | bool |
| diaphragmPumpOn / molecularPumpOn | 隔膜泵 / 分子泵 | bool |
| pinchValveOn / internalCarrierGasOn | 夹管阀 / 内载气 | bool |
| cleaningModeOn / gasSavingOn / coolingModeOn | 清洗 / 节气 / 降温流程 | bool |
| trapTemperatureC | 离子阱温度 | number，℃ |
| inletFlowPercent / pumpFlowPercent | 进气 / 抽气设定 | number，% |
| efcMlMin | EFC 流量 | number，mL/min |
| ionSourceSetpointKv | 离子源电压设定 | number，kV |

当前 `保存预设` 只持久化 desired values，不向仪器下发，不修改回读状态。厂家应用多参数必须实现受控序列/设备原子事务，禁止用四次不等待的按钮调用冒充全部成功。

## 交互状态

```text
点击 → 权限/类型/厂家互锁校验 → 真实设备人工确认 → 发送（控件暂不可重复提交）
                                                ├→ 匹配回读 → 更新开关 / 审计
                                                ├→ 拒绝/回读不符 → 未确认 / 审计
                                                └→ 5秒超时 → 状态未知 / 不自动重试
```

设备操作耗时超过 5 秒时，现有契约会超时。不要用“已接受”加伪造目标回读来通过检查；应先设计“已接受/执行中/完成”的独立类型化状态契约及测试，再接入长流程。当前 settingFinished 不是长流程进度信号。

### 本次验证记录

2026-09-08，本机 Qt 6.11.1 / MSVC Debug：主程序编译通过；485协议测试、设备控制器回归、AI证据回归通过；界面专项 `rs485StatusPanelReadsAndInvalidates`、`fixedLandscapeNavigation`、`instrumentPowerButtonsReflectPartialState`、`foreignSavedPathFallsBackToLocalDocuments` 通过，并检查1024×768截图（截图数据来自内存测试设备）。

全量界面测试未通过：`bundledExampleLoadsThreePlotsWithoutAi` 的 `home->isVisibleTo(&window)` 检查失败，随后较长的导航测试被中止。本次不将整套UI回归、Win7安装包或实机联调标记为通过。

2026-09-08补充：485适配器已在Qt5.15.2/MinGW8.1 Release下编译，协议、设备控制器及485界面专项测试通过；不代表Win7实机已通过。该记录仅为历史验证，不表示当前仍支持此版本；当前打包入口为scripts/build_win7_qt51212.ps1，详见根目录 WINDOWS7_BUILD_GUIDE.md。
