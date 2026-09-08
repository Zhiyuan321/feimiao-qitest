#pragma once

#include "device/IInstrumentAdapter.h"
#include <atomic>

namespace qitest {

class SimulatedInstrument final : public IInstrumentAdapter {
public:
    SimulatedInstrument();
    InstrumentDescriptor descriptor() const override;
    InstrumentHealth health() const override;
    InstrumentTelemetry telemetry() const override;
    CommandValidation validate(const InstrumentCommand &command) const override;
    QVector<SpectrumPoint> acquireSpectrum() override;
    void cancel() override;
    QVariantMap confirmedSettings() const override { return settings_; }
    CommandValidation validateSetting(const QString &key, const QVariant &value) const override;
    void requestSetting(const QString &requestId, const QString &key, const QVariant &value) override;

private:
    std::atomic_bool cancelled_{false};
    QVariantMap settings_;
};

} // namespace qitest
