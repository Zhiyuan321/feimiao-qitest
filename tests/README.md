# 测试不是主程序

首次开发请运行 QITestWorkstation；需要回归检查时开启 QITEST_BUILD_TESTS=ON。

| 文件 | 关注点 |
| --- | --- |
| PumpTests.cpp / PumpTestDevice.h | 同COM主控板/泵顺序查询、仅打开一次、无重叠、原始字符取数、用户确认倍率及失效清空、错误回复拒绝、超时隔离、主控有效期、日志有界；合成帧不代表实机 |
| NetworkTests.cpp / NetworkTestFrames.h | TCP拆帧/CRC/真空度mbar公式与失效/21字节状态（01/00）/可选实机报文回放/超时/断线重连/同IP未断开时接替、双通道独立、有限原始帧导出；本机回环不是实机 |
| Rs485Tests.cpp / Rs485TestDevice.h | 只读查询、拆包、错误帧、状态倍率、超时清空；内存设备不接实机 |
| InstrumentControlTests.cpp | 请求、回执、超时、模拟与手动状态一致性 |
| CoreTests.cpp | 科学计算、方法与校准边界 |
| WorkspaceTests.cpp | 保存、归档、事务与损坏数据 |
| LibraryTests.cpp | 谱库和用户标准 |
| AiEvidenceTests.cpp | 基础指令、证据和模型边界 |
| LocalAiBridgeTests.cpp | 模型进程生命周期与问答失败 |
| SecurityTests.cpp | 权限与安全 |
| UiSmokeTests.cpp | 窗口、页面、按钮及布局 |

统一通过 ctest --test-dir 构建目录 --output-on-failure 运行。
fixtures/ 是受控测试样本，不是用户检测记录；合成案例不代表实机科学验收。
新增接口至少测试：正常回读、设备拒绝、无回执、迟到回执、回读与目标不符。

485相关回归：`ctest --test-dir 构建目录 -R "qitest_(rs485|device|ui|ai)_tests" --output-on-failure`。设备/界面测试使用临时INI配置，不读写操作者注册表或已保存的COM口。Windows测试运行目录需部署同版本Qt DLL与平台插件（QtTest和offscreen也需可加载）；GUI截图可指定QITEST_UI_CAPTURE_DIR。
