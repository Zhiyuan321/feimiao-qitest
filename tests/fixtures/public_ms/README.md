# OpenMS BSA 回归样本

来源：[OpenMS 官方 BSA1.mzML](https://github.com/OpenMS/OpenMS/blob/25b3b040a4541a6725b01a5749f5aa958fa9449d/share/OpenMS/examples/BSA/BSA1.mzML)。仓库许可随附 `LICENSE-OpenMS.txt`；不代表 OpenMS 对本产品的认可。

- 原文件 13,642,066 字节，1,684 次扫描、479,455 个谱点。
- SHA-256：`dc9ed61d595328d4ef2f1de47d21f41b83e2eae7c9145e1d9b88e910c8cec2f7`。
- 本目录仅保留前 64 次扫描、32,248 个谱点，不改变原时间、m/z、强度，不排序、过滤、补点或归一化。
- `openms_bsa.scan.csv` 可用工作站的检测记录→导入数据读取；这是转换后的开放 CSV，不表示软件已经原生支持所有 mzML 编码。
- `expected.json` 使用独立 Python float64 / math.fsum 计算 TIC、BPC、EIC 和梯形积分，与 C++ 引擎逐点比较，并验证 SQLite/归档往返无变化。
- EIC 使用首扫描基峰的 m/z ± 0.5 Da，MS1。它只是可复现的测试窗口，不是物质鉴定或有效分析方法。

## 数值边界

文件头首扫描 TIC 为 6,937,649，保存谱点求和为 4,996,359.667358398，两者不是同一数值。本实现明确从保存谱点重算，不能拿头字段强行作为相同定义的标准答案，也不能推断差异原因。

本片段 TIC 原始时间积分为 448,880,539.63234085（保存强度 × 秒）；扣除端点直线后为负数，保留有符号结果，不强行改成正面积。该片段不是已确认色谱峰，不能直接报告浓度。

本数据没有已知浓度标签，**不用于验证浓度准确度**。校准由另行标注的合成标准点测试（线性、加权、退化输入、禁止外推）；真实样品的科学适用性仍需实测验证。

## 复现

将上述固定版本原文件下载至项目 `.qa/public-ms/BSA1.mzML`，然后在 QITestQt 运行：

```sh
python3 scripts/prepare_public_ms_fixture.py .qa/public-ms/BSA1.mzML tests/fixtures/public_ms
```

转换脚本验证原文件哈希，无联网、无后台服务。原文件不随源码交付。64 次扫描 CSV 同时内置为可选示例，只有点击“载入示例曲线”才载入独立的 PUBLIC_EXAMPLE 记录；不加入业务谱库或 AI 训练库，不自动覆盖客户记录。
