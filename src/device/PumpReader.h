#pragma once
#include "device/PumpProtocol.h"
#include <QJsonObject>
#include <QVariantMap>

namespace qitest {
// Raw/converted pump readback and bounded bus evidence only. Rs485Instrument owns all IO/timers.
class PumpReader {
public:
    void resetSession(bool enabled);
    void invalidate(const QString &message);
    void resetDecoder() { decoder_.reset(); }
    void record(const QString &direction, const QByteArray &bytes, const QString &note);
    bool consume(const QByteArray &bytes, int expectedIndex);
    QVariantMap statusDetails() const;
    bool exportFrames(const QString &path, const QVariantMap &busStatus, QString *error) const;
private:
    PumpProtocol decoder_;
    QVariantMap readings_;
    QList<QJsonObject> records_;
    QString message_ = "分子泵未启用";
    quint64 receivedBytes_ = 0, parsed_ = 0, ignored_ = 0;
};
}
