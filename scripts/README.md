# 维护脚本导航

首次开发只按根目录 WINDOWS7_BUILD_GUIDE.md 编译，不需要执行这里全部脚本。

| 任务 | 脚本 |
| --- | --- |
| 整理源码 ZIP | package_source.py |
| Mac 交叉构建 Win7 | package_windows_qt512_cross.sh |
| 校验 Win7 运行目录 | verify_windows_qt512.py |
| 生成一体安装 EXE | package_windows_installer.py |
| 安装/启动/关闭/卸载检查 | test_windows_installer.py |
| 运行模型冒烟检查 | smoke_windows_ai.py |
| 更新交付模型 | refresh_light_model.py |
| 刷新已构建主程序 | refresh_local_windows_executable.py |

其他脚本是构建辅助、旧平台路线或本机整理工具，不要见到脚本就逐一运行。
执行前看脚本参数和目标路径；本机工具链路径不能原样照搬到其他电脑。
`test_windows_installer.py` 不再包含开发者个人电脑路径。可用 `--installer`
指定安装程序，用 `--wine` 或环境变量 `WINE_BIN` 指定 Wine。Wine 只用于
快速检查，不能代替 Windows 7 目标机验收。
