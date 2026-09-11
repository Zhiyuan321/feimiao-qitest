#pragma once
#include "device/IInstrumentAdapter.h"
#include <QtPlugin>

namespace qitest {
// 插件与主程序必须使用匹配的 Qt、编译器、位数和接口头文件。
// 工厂返回的适配器由宿主接管销毁，不能返回栈对象或在插件内提前释放。
class IInstrumentPlugin {
public:
    virtual ~IInstrumentPlugin() = default;
    virtual IInstrumentAdapter *createAdapter() = 0;
};
}
Q_DECLARE_INTERFACE(qitest::IInstrumentPlugin, "cn.feimiao.InstrumentPlugin/1.2")
