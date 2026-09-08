# 内置厂家谱图

来自项目“数据/厂家样本/原始文件/饼干-MS.CSV”，转换源码与逐点保留说明见该目录。
21 个独立谱图由 qitest.qrc 编入 Mac 与 Windows 主程序，不依赖外部路径。
磁盘文件名使用英文 ASCII 路径，避免 Windows 代码页导致 JOM/NMake 资源路径解析失败。

入口：main.cpp 在用户进入工作站后调用 AppController::loadBundledCustomerSamples。
导入工作线程按归档哈希与分析器版本去重，保留 IMPORTED_UNVALIDATED 标签。
最后载入第 13 谱供查看；没有确认时间单位，因此不伪造 TIC/EIC。
该行为不执行采集、不切换硬件、不调用模型。取消导入后，下次启动可继续。
