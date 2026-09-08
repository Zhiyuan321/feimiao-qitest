#pragma once

#include "domain/Models.h"
#include <QObject>

namespace qitest {

// 厂家接入的核心接口。界面不直接调用串口/TCP/SDK，而通过本接口下发。
// 接口不得阻塞调用线程：耗时通信放到厂家自己的工作线程，查询函数返回缓存。
// 只有实际回读确认后才能报告成功；具体联调步骤见本目录 使用说明.md。
class IInstrumentAdapter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~IInstrumentAdapter() = default;
    // 实机必须 simulation=false；不能借模拟标志绕过权限和确认。
    virtual InstrumentDescriptor descriptor() const = 0;
    virtual InstrumentHealth health() const = 0;
    virtual InstrumentTelemetry telemetry() const = 0;
    virtual CommandValidation validate(const InstrumentCommand &command) const = 0;
    virtual QVector<SpectrumPoint> acquireSpectrum() = 0;
    virtual void cancel() = 0;
    // 返回实际已确认状态。缺失项表示未知，不可用上次保存的设定值填充。
    virtual QVariantMap confirmedSettings() const { return {}; }
    // 检查固件支持、连接、单位、安全范围及硬件互锁；这里只校验，不发送。
    virtual CommandValidation validateSetting(const QString &, const QVariant &) const {
        return {false, "厂家尚未接入此控制接口"};
    }
    // 立即返回；保存 requestId/key，通信完成后通过 settingFinished 原样带回。
    virtual void requestSetting(const QString &requestId, const QString &key, const QVariant &) {
        emit settingFinished(requestId, key, false, {}, "厂家尚未接入此控制接口");
    }
    // 超时表示“未知”，不是“关闭”。取消等待也不等于物理动作已经撤销。
    // 禁止盲目重发高压、泵或电源命令。
    virtual void cancelSetting(const QString &) {}
signals:
    // success=true 仍须提供有效 readback；控制器会与原请求目标值比较。
    // 仅“写入串口成功”或“命令已收到”不能冒充物理状态确认。
    void settingFinished(const QString &requestId, const QString &key,
                         bool success, const QVariant &readback, const QString &error);
};

} // namespace qitest
