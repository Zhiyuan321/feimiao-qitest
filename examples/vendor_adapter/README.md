# 厂家插件模板

先阅读 ../../src/device/README.md 与 ../../docs/integration/VENDOR_INSTRUMENT_INTEGRATION.md。
VendorPlugin.cpp 故意返回“未配置/拒绝”，不能当成已经接通的驱动。

此目录有独立 CMakeLists.txt，只构建插件，不生成主程序。
必须选与宿主相同的 Qt 5.12.12、编译器和架构，并显式传入 Qt 路径后核对配置结果。

先实现只读状态，再实现带回执的受控设置，最后做台架测试。
不得用 simulation=true 让实机绕过检查，不得返回合成谱图伪装真实采集。
