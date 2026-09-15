# 科学计算与方法数据

- AnalysisEngine：分析流程；ChromatogramEngine：TIC/EIC 与积分。
- CalibrationModel / QuantitationEngine：校准拟合与定量。
- MassAxisCalibration：质量轴离线拟合；MethodDraft：方法草案校验。
- SpectralComparison：谱图比较；QtCompat：Qt 5/6 兼容。

这一层不能调用模型来生成正式数值，也不要掺入界面尺寸或 SDK 传输。
修改公式必须明确输入单位、空数据、非法数值、外推边界，并补 tests/CoreTests.cpp 的可手算案例。
离线拟合成功不表示校准参数已写入仪器。

2026-09-15 用户确认实机 TIC：一个完整周期为 1 秒，一张完整质谱对应一个 TIC 点，纵坐标为该张质谱所有强度的累加和，不乘 m/z 间隔，不跨周期累计。现有 ChromatogramEngine::trace 已按每张谱求和；时间由 SpectrumScan::timeSeconds 提供。网口接入层仍需完成周期组谱后再传入，不能把网络分包当作扫描。导入和公开示例继续使用文件中的真实时间，不统一改成 1 秒。界面的区间积分是对生成后的时间曲线求面积，和生成 TIC 点的求和分开。
