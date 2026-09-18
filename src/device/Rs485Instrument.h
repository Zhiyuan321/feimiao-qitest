#pragma once

#include "device/IInstrumentAdapter.h"
#include "device/Rs485Protocol.h"
#include "device/PumpReader.h"
#include <QDateTime>
#include <QIODevice>
#include <QTimer>

class QSerialPort;

namespace qitest {
// One serial owner schedules main-board status and optional pump queries sequentially.
class Rs485Instrument final : public IInstrumentAdapter {
    Q_OBJECT
public:
    explicit Rs485Instrument(QObject *parent = nullptr);
    // Test seam: borrowed device must outlive this adapter; no physical port used.
    Rs485Instrument(QIODevice *transport, QObject *parent);
    ~Rs485Instrument() override;
    bool openPort(const QString &name, bool includePump = false);
    void closePort();
    static QStringList availablePorts();
    InstrumentDescriptor descriptor() const override;
    InstrumentHealth health() const override { return health_; }
    InstrumentTelemetry telemetry() const override;
    QVariantMap confirmedSettings() const override;
    CommandValidation validate(const InstrumentCommand &command) const override;
    CommandValidation validateSetting(const QString &, const QVariant &) const override;
    void requestSetting(const QString &id, const QString &key, const QVariant &) override;
    void cancelSetting(const QString &id) override;
    void cancelSetting(const QString &id,const QString &reason);
    void requestPumpShutdown(const QString &id);
    static bool supportsSetting(const QString &key);
    bool settingBusy() const { return !controlId_.isEmpty(); }
    QVector<SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override {}
    bool readOnly() const override { return true; }
    QString connectionSummary() const override { return message_; }
    QVariantMap statusDetails() const;
    QVariantMap pumpStatusDetails() const;
    bool exportFrames(const QString &path, QString *error) const { return pump_.exportFrames(path, statusDetails(), error); }
    QString portName() const { return portName_; }
    bool portOpen() const { return transport_->isOpen(); }
    CommandValidation validateBasicMethodParameters(const QJsonObject &parameters) const;
    bool requestBasicMethodParameters(const QString &requestId, const QJsonObject &parameters,
                                      QString *error = nullptr);
    void cancelBasicMethodParameters(const QString &requestId);
signals:
    void basicMethodParametersFinished(const QString &requestId, bool success,
                                       const QJsonObject &parameters, const QString &error);
private:
    void query();
    void receive();
    void sendNextBasicParameter();
    void finishBasicParameters(bool success, const QString &error = {});
    void fail(const QString &message, bool abnormal = true);
    void recordFailure(const QString &message);
    void saveFailureDiagnostics();
    void pumpConfirmationExpired();
    void clearReadings();
    void clearMainReadings();
    void sendControl();
    void queueSetting(const QString &id,const QString &key,const QVariant &value,bool waitForRest);
    bool controlWaitForRest_ = false;
    bool pumpTurnaround_ = false;
    void finishControl(bool success, const QString &error = {});
    void recordControl(const QString &event, const QVariantMap &details = {});
    bool controlReadbackMatches() const;
    QIODevice *transport_;
    QSerialPort *serial_ = nullptr;
    QTimer pollTimer_, timeout_, mainFreshTimer_;
    PumpReader pump_;
    bool pumpEnabled_ = false;
    int nextQuery_ = -1, activeQuery_ = -1; // -1 = main board; 0..3 = pump.
    Rs485Protocol decoder_;
    Rs485Status status_;
    InstrumentHealth health_ = unavailableRs485Health();
    InstrumentTelemetry telemetry_ = unavailableRs485Telemetry();
    QString portName_, message_ = "485未连接 · 只读状态";
    QDateTime lastReadback_;
    bool pending_ = false, closing_ = false;
    QVector<QPair<quint8,QByteArray>> basicQueue_;
    int basicIndex_ = -1;
    QString basicRequestId_;
    QJsonObject basicParameters_;
    QString controlId_, controlKey_;
    QVariant controlValue_;
    QByteArray controlWire_;
    quint8 controlCommand_ = 0;
    int controlStage_ = 0; // 0 idle, 1 queued, 2 board ACK, 3 board status, 4 pump current
    QTimer controlDeadline_, pumpFreshTimer_;
    QVariantMap confirmedSetpoints_;
    QVariantList controlHistory_;
    QVariantMap lastFailure_;
    QString diagnosticPath_, diagnosticError_;
};
} // namespace qitest
