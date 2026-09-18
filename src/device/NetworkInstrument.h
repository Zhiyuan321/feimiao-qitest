#pragma once
#include "device/Rs485Instrument.h"
#include "device/NetworkProtocol.h"
#include "device/StartupSequence.h"
#include "device/ShutdownSequence.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <memory>

namespace qitest {
// Built-in dual transport: preserve an existing 485 connection when TCP starts.
// Explicit method/tuning commands and timed Fullscan acquisition; no generic hardware writes.
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
    bool startAcquisition(int seconds, QString *error);
    void stopAcquisition(bool cancelled = true);
    bool acquisitionBusy() const { return acquisitionState_ != 0; }
    bool calibrationBusy() const { return settingBusy() || acquisitionBusy() || tuningPending_ || !methodRequestId_.isEmpty() || (fresh_ && status_.experimentRunning); }
    bool setCalibrationProfile(const QJsonObject &profile,QString *error=nullptr);
    QJsonObject calibrationProfile() const { return calibrationProfile_; }
    InstrumentDescriptor descriptor() const override;
    InstrumentHealth health() const override;
    InstrumentTelemetry telemetry() const override;
    QVariantMap confirmedSettings() const override;
    bool startupBusy() const { return !startupId_.isEmpty(); }
    bool supportsSetting(const QString &key) const { return key == "powerOn" || key == "pinchValveOn" || Rs485Instrument::supportsSetting(key); }
    bool shutdownBusy() const { return !shutdownId_.isEmpty(); }
    bool settingBusy() const { return shutdownBusy() || startupBusy() || !pinchRequestId_.isEmpty() || serial_->settingBusy(); }
    void cancelSetting(const QString &id) override;
    bool readOnly() const override { return true; }
    QString connectionSummary() const override;
    CommandValidation validate(const InstrumentCommand &command) const override;
    CommandValidation validateSetting(const QString &, const QVariant &) const override;
    void requestSetting(const QString &id, const QString &key, const QVariant &) override;
    QJsonObject confirmedMethodParameters() const override { return confirmedMethodParameters_; }
    CommandValidation validateMethodParameters(const QJsonObject &parameters) const override;
    void requestMethodParameters(const QString &requestId, const QJsonObject &parameters) override;
    QVector<SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override { stopAcquisition(); }
signals:
    void acquisitionStarted();
    void acquisitionScan(const qitest::SpectrumScan &scan);
    void acquisitionFinished(bool success, bool cancelled, const QString &error);
private:
    void finishPinchValve(bool success, const QString &error = {});
    QString pinchRequestId_;
    bool pinchTarget_ = false;
    QVariant pinchConfirmed_;
    QTimer pinchTimer_;
    void advanceShutdown();
    void finishShutdown(bool success,const QString &error = {});
    ShutdownSequence shutdown_;
    QString shutdownId_,shutdownStepId_;
    void advanceStartup();
    void stopStartup(const QString &reason);
    QString startupMessage() const;
    QVariant observedPowerState() const;
    bool observedVacuumReady() const;
    bool operatingConditionsReady() const;
    bool startupTemperaturesConfirmed() const;
    bool runningStartupNeedsHeating() const;
    StartupSequence startup_;
    QString startupId_, startupStepId_, startupStepKey_;
    bool startupPreparingMode_ = false;
    bool sendDetection(bool enabled);
    void receiveAcquisition(const NetworkFrame &frame);
    void finishNetworkAcquisition(bool success, const QString &error);
    void failAcquisition(const QString &error);
    void acceptConnection();
    void receive();
    void closePeer(const QString &message);
    void notify();
    void sendFullscanMethod();
    void sendLegacyMethodFollowup();
    void finishMethod(bool success, const QString &error = {});
    void sendHeartbeat();
    void recordConnectionEvent(const QString &event, const QTcpSocket *socket);
    std::unique_ptr<Rs485Instrument> serial_;
    QTcpServer server_;
    QTcpSocket *peer_ = nullptr;
    QTimer staleTimer_, updateTimer_, pressureTimer_, tuningTimer_, methodTimer_;
    QTimer heartbeatTimer_;
    quint64 heartbeatSent_ = 0;
    bool heartbeatPausedForMethod_ = false;
    QVector<double> pressureVolts_;
    quint64 pressureFrameCount_ = 0;
    int pressureCycle_ = -1;
    bool pressureAcquisitionCompleted_ = false;
    bool pressureRunTrace_ = false, pressureDisplayLimit_ = false;
    double pressureSampleIntervalMinutes_ = 0;
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
    struct RawReceive {
        QString time, peer;
        int port = 0;
        quint64 offset = 0;
        QByteArray bytes;
    };
    QList<RawReceive> recentRawReceives_, errorRawReceives_;
    int recentRawBytes_ = 0;
    QJsonObject acquisitionParseFailure_;
    quint64 heartbeatReplies_ = 0;
    quint64 waveformCrcMismatches_ = 0;
    QString methodRequestId_;
    QJsonObject pendingMethodParameters_, confirmedMethodParameters_;
    QJsonObject calibrationProfile_=NetworkProtocol::fullscanCalibrationProfile();
    QString methodConfirmationReason_ = "本次连接尚未设置方法";
    QByteArray pendingMethodWire_;
    QTimer methodFollowupDelay_;
    int methodStage_ = 0, methodFollowupsSent_ = 0; // 0 serial, 1 method ACK, 2 delay, 3 followup ACK
    QTimer acquisitionAckTimer_, acquisitionDurationTimer_;
    QTimer stopRetryTimer_, stopReplyGuardTimer_;
    int stopCommandsSent_ = 0;
    int acquisitionState_ = 0; // 0 idle, 1 start ACK, 2 collecting, 3 stop ACK
    int acquisitionSeconds_ = 0, acquiredScans_ = 0, nextPressureCycle_ = 0;
    bool acquisitionCancelled_ = false;
    QString acquisitionError_;
    QVector<double> acquisitionMassAxis_;
};
}
