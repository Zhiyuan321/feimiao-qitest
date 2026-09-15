# 记录与文件读写

WorkspaceRepository 管理工作库、方法版本、检测记录和审计。
RunArchiveCodec / ScanSeriesCodec 处理检测归档和扫描序列。
CalibrationDocument / IntegrationDocument 保存校准与积分证据。
ArchiveImportWorker 负责后台批量导入，每个线程独立数据库连接。

调试时可设 QITEST_WORKSPACE_DB 指向专用测试库，不要拿用户正式记录测试删除或迁移。
修改格式时考虑旧数据、截断文件、重复导入和事务失败。失败应返回错误，不能留下半条成功记录。
对应 tests/WorkspaceTests.cpp；保存预设不等于执行硬件设置。

实机缺失监控读数以 SQL NULL 保存、读出恢复 NaN；初始化时事务迁移旧版非空监控表，保留旧记录。原始谱和扫描序列不因缺失监控读数而保存失败。
