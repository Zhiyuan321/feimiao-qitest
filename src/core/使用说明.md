# 科学计算与方法数据

- AnalysisEngine：分析流程；ChromatogramEngine：TIC/EIC 与积分。
- CalibrationModel / QuantitationEngine：校准拟合与定量。
- MassAxisCalibration：质量轴离线拟合；MethodDraft：方法草案校验。
- SpectralComparison：谱图比较；QtCompat：Qt 5/6 兼容。

这一层不能调用模型来生成正式数值，也不要掺入界面尺寸或 SDK 传输。
修改公式必须明确输入单位、空数据、非法数值、外推边界，并补 tests/CoreTests.cpp 的可手算案例。
离线拟合成功不表示校准参数已写入仪器。
