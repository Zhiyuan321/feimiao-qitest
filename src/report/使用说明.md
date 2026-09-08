# 报告生成

ReportGenerator 使用已保存的分析与复核证据生成报告，不由模型计算结果。
修改报告先确认数据来源、单位、候选与正式结论的区别，以及人工复核是否完成。
检查实际 PDF 的中文、分页和表头；改参数后旧证据不能继续作为当前结果输出。
对应 tests/WorkspaceTests.cpp 与 UiSmokeTests.cpp。
