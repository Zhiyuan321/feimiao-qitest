#pragma once

#include "device/IInstrumentAdapter.h"
#include "device/Rs485Protocol.h"
#include <QDateTime>
#include <QIODevice>
#include <QTimer>

class QSerialPort;

namespace qitest {
// First hardware integration is deliberately read-only: the only wire command
// emitted by this adapter is 0x30. All IO is asynchronous, on its owning thread.
class Rs485Instrument final : public IInstrumentAdapter {
    Q_OBJECT
public:
    explicit Rs485Instrument(QObject *parent = nullptr);
    // Test seam: borrowed device must outlive this adapter; no physical port used.
    Rs485Instrument(QIODevice *transport, QObject *parent);
    ~Rs485Instrument() override;
    bool openPort(const QString &name);
    void closePort();
    static QStringList availablePorts();
    InstrumentDescriptor descriptor() const override;
    InstrumentHealth health() const override { return health_; }
    InstrumentTelemetry telemetry() const override { return telemetry_; }
    QVariantMap confirmedSettings() const override;
    CommandValidation validate(const InstrumentCommand &command) const override;
    CommandValidation validateSetting(const QString &, const QVariant &) const override;
    void requestSetting(const QString &id, const QString &key, const QVariant &) override;
    QVector<SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override {}
    bool readOnly() const override { return true; }
    QString connectionSummary() const override { return message_; }
    QVariantMap statusDetails() const;
    QString portName() const { return portName_; }
    bool portOpen() const { return transport_->isOpen(); }
private:
    void query();
    void receive();
    void fail(const QString &message);
    void clearReadings();
    QIODevice *transport_;
    QSerialPort *serial_ = nullptr;
    QTimer pollTimer_, timeout_;
    Rs485Protocol decoder_;
    Rs485Status status_;
    InstrumentHealth health_ = unavailableRs485Health();
    InstrumentTelemetry telemetry_ = unavailableRs485Telemetry();
    QString portName_, message_ = "485未连接 · 只读状态";
    QDateTime lastReadback_;
    bool pending_ = false, closing_ = false;
};
} // namespace qitest
