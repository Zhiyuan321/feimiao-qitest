#include "device/PumpReader.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace qitest {
void PumpReader::resetSession(bool enabled) {
    records_.clear(); receivedBytes_ = parsed_ = ignored_ = 0;
    invalidate(enabled ? "分子泵等待共用485回读" : "分子泵未启用");
}
void PumpReader::invalidate(const QString &message) {
    decoder_.reset(); readings_.clear(); message_ = message;
}
void PumpReader::record(const QString &direction, const QByteArray &bytes, const QString &note) {
    if (direction == "RX") receivedBytes_ += bytes.size();
    records_.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction", direction}, {"ascii", QString::fromLatin1(bytes)},
        {"hex", QString::fromLatin1(bytes.toHex(' '))}, {"note", note}});
    if (records_.size() > 256) records_.removeFirst();
}
bool PumpReader::consume(const QByteArray &bytes, int expectedIndex) {
    bool accepted = false;
    for (const auto &wire : decoder_.feed(bytes)) {
        PumpReply reply;
        if (accepted || !PumpProtocol::extract(wire, &reply) || reply.parameter != PumpProtocol::parameter(expectedIndex)) {
            ++ignored_; continue;
        }
        accepted = true; ++parsed_;
        readings_.insert(QString::fromLatin1(reply.parameter), QString::fromLatin1(reply.raw));
        readings_.insert(QString::fromLatin1(reply.parameter) + "Time", QDateTime::currentDateTime().toString("HH:mm:ss"));
        // Units confirmed by the user on 2026-09-09; retain all six raw digits.
        static const char *keys[]{"molecularPumpRpm", "molecularPumpCurrentA", "molecularPumpVoltageV", "molecularPumpTemperatureC"};
        const double value = reply.raw.toUInt();
        readings_.insert(keys[expectedIndex], (expectedIndex == 1 || expectedIndex == 2) ? value / 100.0 : value);
        message_ = "分子泵回读中 · RPM / A / V / ℃ · 回复校验待确认";
    }
    return accepted;
}
QVariantMap PumpReader::statusDetails() const {
    auto data = readings_; data.insert("message", message_); data.insert("receivedBytes", receivedBytes_);
    data.insert("parsedFrames", parsed_); data.insert("ignoredFrames", ignored_); data.insert("retainedRecords", records_.size());
    return data;
}
bool PumpReader::exportFrames(const QString &path, const QVariantMap &busStatus, QString *error) const {
    QJsonArray records; for (const auto &record : records_) records.append(record);
    const auto bytes = QJsonDocument(QJsonObject{{"mode", "共用485主控板及分子泵顺序查询；泵按mid(5,3)/mid(10,6)取数，398直接RPM、310/313除100为A/V、326直接℃（控制器即泵体）；未校验回复校验码，仅供回读核对，不作控制依据"},
        {"busStatus", QJsonObject::fromVariantMap(busStatus)},
        {"pumpStatus", QJsonObject::fromVariantMap(statusDetails())}, {"records", records}}).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}
}
