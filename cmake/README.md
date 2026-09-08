# CMake 工具链

根目录 CMakeLists.txt 才是主程序入口。toolchains/ 用于跨平台编译。
Windows 本机开发选择 Qt 5.12.12 / MinGW Kit，不要误用 Mac 交叉工具链。
工具链变更后用独立构建目录验证，不共用其他平台 CMakeCache。
