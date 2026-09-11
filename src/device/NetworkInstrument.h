#pragma once
#include "device/Rs485Instrument.h"
#include "device/NetworkProtocol.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <memory>

namespace qitest {
// Built-in dual transport: preserve an existing 485 connection when TCP starts.
// Only explicit tuning commands are supported; all other hardware settings remain blocked.
class NetworkInstrument final : public IInstrumentAdapter {
    Q_OBJECT
public:
    explicit NetworkInstrument(std::unique_ptr<Rs485Instrument> serial = {}, QObject *parent = nullptr);
    ~NetworkInstrument() override;
    bool startListening(const QString &address, quint16 port = 11000, int staleMs = 5000);
    void stopListening();
    Rs485Instrument *serial() const { return serial_.get(); }
    QVariantMap statusDetails() const;
    bool exportFrames(const QString &path, QString *error) const;
    QVector<double> pressureVolts() const { return pressureVolts_; }
    // Reports command ACK only, never claims physical RF output has been verified.
    bool requestTuning(bool enabled, QString *error);
    InstrumentDescriptor descriptor() const override;
    InstrumentHealth health() const override;
    InstrumentTelemetry telemetry() const override;
    QVariantMap confirmedSettings() const override { return serial_->confirmedSettings(); }
    bool readOnly() const override { return true; }
    QString connectionSummary() const override;
    CommandValidation validate(const InstrumentCommand &command) const override;
    CommandValidation validateSetting(const QString &, const QVariant &) const override;
    void requestSetting(const QString &id, const QString &key, const QVariant &) override;
    QVector<SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override {}
private:
    void acceptConnection();
    void receive();
    void closePeer(const QString &message);
    void notify();
    void recordConnectionEvent(const QString &event, const QTcpSocket *socket);
    std::unique_ptr<Rs485Instrument> serial_;
    QTcpServer server_;
    QTcpSocket *peer_ = nullptr;
    QTimer staleTimer_, updateTimer_, pressureTimer_, tuningTimer_;
    QVector<double> pressureVolts_;
    quint64 pressureFrameCount_ = 0;
    int pressureFrameIndex_ = 0, pressureFrameTotal_ = 0;
    bool tuningPending_ = false, tuningTarget_ = false;
    QString tuningMessage_ = "调谐尚未操作";
    NetworkProtocol decoder_;
    NetworkStatus status_;
    bool fresh_ = false;
    QString message_ = "网口未监听 · 只读状态", peerAddress_;
    QDateTime lastReadback_;
    quint64 receivedBytes_ = 0, validFrames_ = 0, unparsedFrames_ = 0;
    quint64 acceptedConnections_ = 0, replacedConnections_ = 0, rejectedConnections_ = 0;
    QList<QJsonObject> recentFrames_;
    QList<QJsonObject> connectionEvents_;
};
}
