# 公共谱库与我的谱库

参考谱库的“我的谱库”使用 LibraryFile / LibraryFileCatalog 和 ui/LibraryFilesPage。
按用户提供的旧软件读取代码，.lib 是 UTF-8 JSON 数组，每个物质保留完整对象。显示字段依次为 name、cas、parent_ion、qualitify_ion（沿用旧拼写）、quantify_ion、sample_category、internal_flag、son_area、msms_son_area；新字段写为字符串。定性离子、定量离子、一级阈值和二级阈值均兼容旧文件的逗号分隔多值格式，例如阈值 `0,25000`、`0,0,1500`；母离子仍为单值。internal/external 标定对象和未知字段不丢弃，不据此启动自动筛查或推断定量结果。

列表按文件登记，日期为文件修改时间；索引 library-files.json 与工作区数据库同目录。新建为空数组，导入验证文件后登记路径，不复制或合并物质；重复路径不新增。列表删除只移除登记，不删除用户原文件。编辑页的物质修改需保存后写入；保存采用 QSaveFile，拒绝覆盖外部已修改的当前文件，另存为切换并登记新库，导出仅保存副本。支持最多16 MiB/10000物质；格式不符明确报错，不猜测二进制 .lib。

实机筛查另由“参数预设 → 库路径”指定单个 .lib。当前规则只使用 qualitify_ion 定性离子和 son_area 一级阈值（各至少一项，数量相同），一一对应并要求全部严格超阈值；不使用 internal/external、定量离子或二级阈值。离子数量不固定，1个、2个、3个及更多均支持；数量不一致、空值或无效数值在筛查详情标记未筛查。检测开始冻结库内容，编辑库不改变已保存记录。

对应 tests/LibraryTests.cpp::legacyLibRoundtripAndCatalog 及 UiSmokeTests 中 libraryFiles 两项，布局验证1024×768和1024×700。未提供实际旧版 .lib 样本时，兼容依据为截图字段与合成回归，不能称为旧软件双向验收。

以下原用户标准模块保留以兼容历史数据与证据，当前“我的谱库”入口不再写入其数据库：

SpectralLibraryRepository 查询随软件提供的参考库；UserStandardRepository 管理用户标准与导入导出。
公共库保持只读，用户编辑存入独立数据，不要直接覆盖公共库。
新增字段需同步校验、存储、编辑界面与导入导出；无实证的匹配不能写成确定鉴定结论。
对应 tests/LibraryTests.cpp；界面在 ui/UserStandardsPage。
