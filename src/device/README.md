# 厂家工程师从这里接入

## 已内置网口TCP首版

在“仪器控制 → 运行状态 → 网口TCP”点击开始监听（默认11000），设备主动连接电脑。只读倍增管电压、真空规原始值、换算后的真空度（mbar）和实验状态；可与485同时连接，分别失效。0x82气压按用户代码换算为V，暂按单包采样序号显示；射频页支持管理员/工程师发送调谐启停，等待协议应答，不视作物理状态回读。其他控制和完整谱图采集未开放。详见 [网口首版接入说明](../../docs/integration/网口首版接入说明.md)。

## 已内置485只读驱动

在“仪器配置 → 运行状态”选择实际COM口后连接，9600/8N1/无流控；无需厂家插件。查询0x30状态，并可勾选同串口分子泵四项顺序查询；回读温度、流量、气压、原始真空规电压、高压模块读数和部件标志。高压模块原始值按用户2026-09-10更正，除以10得到离子源电压(V)，例如49→4.9 V。控制与真实采集尚未接入；错误/超时清空读数并关闭端口。完整数据偏移、启动环境变量及实机验收项见 `../../docs/integration/VENDOR_INSTRUMENT_INTEGRATION.md`。

## 文件职责

- PumpProtocol.* / PumpReader.*：按用户四条ASCII查询和固定位置取数的分子泵只读试验；按用户确认倍率显示RPM/A/V/℃，保留原始值与有限收发日志，回复校验仍待确认。同一个COM统一排队查询，在同一485页面显示。详见[试读说明](../../docs/integration/分子泵试读说明.md)。

- NetworkProtocol.*：TCP帧、CRC与21字节状态解码（2026-09-09修订，实验状态01/00）。
- NetworkInstrument.*：TCP监听、同IP新连接接替旧连接、读数有效期、连接事件/原始帧导出与485并行状态。

- Rs485Protocol.*：最新485协议帧与23字节状态解码。
- Rs485Instrument.*：单串口统一调度主控板/泵；泵超时暂停泵查询，主控超时或串口失败关闭。
- IInstrumentAdapter.h：宿主与驱动的公共接口。
- IInstrumentPlugin.h：插件工厂与 ABI 标识。
- SimulatedInstrument.*：模拟实现，用来理解调用顺序，不要复制其假数值到真实驱动。
- VendorControlCatalog.h：协议控制数据区与出处，**不包含完整报文/传输/CRC**。

## 最短联调顺序

1. 从 ../../examples/vendor_adapter 开始。插件用与宿主一致的 Qt、编译器和位数。
2. descriptor 返回实际型号且 simulation=false。
3. health / telemetry / confirmedSettings 返回工作线程维护的实际回读缓存；不要阻塞 GUI。
4. validateSetting 检查能力、连接、单位、互锁及厂家安全范围。
5. requestSetting 保存请求号，异步发送；收到实际状态后发 settingFinished。
6. 方法参数走 validateMethodParameters / requestMethodParameters；逐字段确认单位、范围、报文和实际回读后，再发 methodParametersFinished。不要直接照搬模拟器数值。
7. 控制器要求 requestId、key、readback 对应原请求；超时不自动重发。
8. 最后才联调高压、泵等危险动作，台架与设备互锁必不可少。

## 建议断点与日志

main.cpp 的插件加载 → AppController::updateInstrumentSetting → 驱动 requestSetting → settingFinished 回调。
记录请求号、key、目标值、发送/回读时间、实际值和错误；不要只记录“发送成功”。
当前宿主等待窗口为 5 秒。耗时长流程需设计独立状态契约，不能把 ACK 当动作完成。
已接受但未完成的动作不能报 success=true 并伪造回读。

## 启用插件

在 PowerShell 设置绝对路径后启动宿主：

```powershell
$env:QITEST_INSTRUMENT_PLUGIN = "C:\Dev\driver\QITestVendorAdapter.dll"
& "C:\Dev\app\飞秒质谱工作站.exe"
```

未指定插件或485串口时使用模拟器；指定插件加载失败会报错退出，不会偷偷退回模拟。
详细契约见 ../../docs/integration/VENDOR_INSTRUMENT_INTEGRATION.md。
对应 tests/InstrumentControlTests.cpp；这组测试通过不代表实机通过。

插件 ABI 已升级为 1.2；接口新增方法参数校验、异步请求和完整回读，宿主与插件必须同步重编译。
