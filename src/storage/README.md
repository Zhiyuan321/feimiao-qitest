# 记录与文件读写

WorkspaceRepository 管理工作库、方法版本、检测记录和审计。
RunArchiveCodec / ScanSeriesCodec 处理检测归档和扫描序列。
CalibrationDocument / IntegrationDocument 保存校准与积分证据。
ArchiveImportWorker 负责后台批量导入，每个线程独立数据库连接。

调试时可设 QITEST_WORKSPACE_DB 指向专用测试库，不要拿用户正式记录测试删除或迁移。
修改格式时考虑旧数据、截断文件、重复导入和事务失败。失败应返回错误，不能留下半条成功记录。
对应 tests/WorkspaceTests.cpp；保存预设不等于执行硬件设置。

实机检测开始时在 sample_info.ion_screening_snapshot 冻结指定 .lib 的内容、原文件 SHA256、路径、规则版本和质量窗口。AnalysisWorker 完成各个定性离子的一级阈值筛查，保存候选及完整扫描。历史记录和归档导入依据记录内快照重建完整筛查详情，不读取当前可变谱库，不使用模拟库补结果。库文件不可用时保留原始数据并明确筛查未完成；没有指定库时仍显示筛查未配置。

实机缺失监控读数以 SQL NULL 保存、读出恢复 NaN；初始化时事务迁移旧版非空监控表，保留旧记录。原始谱和扫描序列不因缺失监控读数而保存失败。

筛查规则版本 eic-ion-sum-2 修正旧版固定三个离子的限制；兼容 eic-three-ion-sum-1 快照，重开旧记录/归档时使用原扫描和库快照按实际离子数重新计算，结果标注新规则版本；不改写旧库或原始数据。
