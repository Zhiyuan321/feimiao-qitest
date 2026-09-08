# 厂家工程师从这里接入

## 文件职责

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
6. 控制器要求 requestId、key、readback 对应原请求；超时不自动重发。
7. 最后才联调高压、泵等危险动作，台架与设备互锁必不可少。

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

未设置使用模拟器；指定插件加载失败会报错退出，不会偷偷退回模拟。
详细契约见 ../../docs/integration/VENDOR_INSTRUMENT_INTEGRATION.md。
对应 tests/InstrumentControlTests.cpp；这组测试通过不代表实机通过。
