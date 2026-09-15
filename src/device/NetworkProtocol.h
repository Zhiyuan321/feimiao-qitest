#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace qitest {
struct NetworkFrame {
    quint8 action = 0, command = 0, count = 0, index = 0;
    QByteArray payload, wire;
    quint16 receivedCrc = 0, calculatedCrc = 0;
    bool crcRequired = true;
    // For action 0x20 / commands 0x81,0x82 these bytes are a BE cycle number,
    // not a fragment count/index (user confirmed 2026-09-15).
    quint16 cycleIndex() const { return (quint16(count)<<8)|index; }
};
struct NetworkStatus {
    quint16 multiplierVoltageV = 0, vacuumRaw = 0;
    bool experimentRunning = false;
};
// 2026-09-09 质谱网口通讯协议(2).pdf, pp.4-5, 13-14. Status payload = 21;
// length field = payload + 2. Experiment state at offset 9: 01 = on, 00 = off.
// CRC-16/MODBUS arithmetic, transmitted HIGH byte first (custom wire order).
class NetworkProtocol {
public:
    static constexpr int MaximumWaveformPayload = 65532; // even samples; U16 LEN includes two cycle bytes
    // Formula and mbar output unit confirmed by the instrument team on 2026-09-09.
    // Input is the TCP unsigned 16-bit raw value, not the RS485 millivolt field.
    static double vacuumMbarFromRaw(quint16 value);
    static bool decodePressure(const NetworkFrame &frame, QVector<double> *volts);
    static QByteArray tuningCommand(bool enabled);
    static QByteArray detectionCommand(bool enabled);
    static QByteArray heartbeatCommand();
    static QJsonObject fullscanCalibrationProfile();
    static QVector<double> fullscanMassAxis(const QJsonObject &parameters, QString *error = nullptr);
    // Fullscan 0x81 frame from the vendor V1.4 protocol and the supplied
    // workstation parameter mapping. Returns empty on any unsafe conversion.
    static QByteArray fullscanMethodCommand(const QJsonObject &parameters, QString *error = nullptr);
    static bool decodeCommandAcknowledgement(const NetworkFrame &frame, quint8 expectedCommand,
                                             bool *success);
    static quint16 crc16(const QByteArray &bytes);
    static bool decodeStatus(const NetworkFrame &frame, NetworkStatus *status);
    QVector<NetworkFrame> feed(const QByteArray &bytes);
    void reset() { buffer_.clear(); }
    int bufferedBytes() const { return buffer_.size(); }
    quint64 rejectedBytes() const { return rejectedBytes_; }
    QJsonObject lastFeedRejection() const { return lastFeedRejection_; }
private:
    QJsonObject lastFeedRejection_;
    QByteArray buffer_;
    quint64 rejectedBytes_ = 0;
};
}
