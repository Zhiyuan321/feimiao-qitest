# 报告生成

ReportGenerator 使用已保存的分析证据生成报告，不由模型计算结果。
报告不再包含复核步骤、复核状态或复核要求；仍保留数据来源、单位、质量信息及可疑筛查结果的适用范围。
检查实际 PDF 的中文、分页和表头；改参数后旧证据不能继续作为当前结果输出。
对应 tests/WorkspaceTests.cpp 与 UiSmokeTests.cpp。
