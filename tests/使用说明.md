# 测试不是主程序

首次开发请运行 QITestWorkstation；需要回归检查时开启 QITEST_BUILD_TESTS=ON。

| 文件 | 关注点 |
| --- | --- |
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
