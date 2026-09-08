#include "device/IInstrumentPlugin.h"

// 厂家接入模板：故意拒绝操作，不是可用硬件驱动。
// 在自己的适配器中补充缓存回读、校验、工作线程和带请求号的异步回执。
// 不能只把 connected/ready 改成 true 就宣布接通；详见本目录 使用说明.md。
class VendorAdapter final : public qitest::IInstrumentAdapter {
public:
    qitest::InstrumentDescriptor descriptor() const override {
        return {"未配置的厂家驱动", {}, "unconfigured", false};
    }
    qitest::InstrumentHealth health() const override { return {}; }
    qitest::InstrumentTelemetry telemetry() const override { return {}; }
    qitest::CommandValidation validate(const qitest::InstrumentCommand &) const override {
        return {false, "请先接入厂家协议并验证互锁"};
    }
    QVector<qitest::SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override {}
    // Implement validateSetting, requestSetting, confirmedSettings and
    // cancelSetting using the documented SDK. Never block the GUI thread.
};

class VendorPlugin final : public QObject, public qitest::IInstrumentPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "cn.feimiao.InstrumentPlugin/1.1")
    Q_INTERFACES(qitest::IInstrumentPlugin)
public:
    qitest::IInstrumentAdapter *createAdapter() override { return new VendorAdapter; }
};
#include "VendorPlugin.moc"
