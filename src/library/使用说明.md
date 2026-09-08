# 公共谱库与用户标准

SpectralLibraryRepository 查询随软件提供的参考库；UserStandardRepository 管理用户标准与导入导出。
公共库保持只读，用户编辑存入独立数据，不要直接覆盖公共库。
新增字段需同步校验、存储、编辑界面与导入导出；无实证的匹配不能写成确定鉴定结论。
对应 tests/LibraryTests.cpp；界面在 ui/UserStandardsPage。
