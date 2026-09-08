#pragma once
#include <QByteArray>
#include <QVector>

namespace qitest {
struct NetworkFrame {
    quint8 action = 0, command = 0, count = 0, index = 0;
    QByteArray payload, wire;
};
struct NetworkStatus {
    quint16 multiplierVoltageV = 0, vacuumRaw = 0;
    bool experimentRunning = false;
};
// 质谱网口通讯协议(3).pdf, pp.4-5, 13-14. Length = payload + 2;
// CRC-16/MODBUS arithmetic, transmitted HIGH byte first (custom wire order).
class NetworkProtocol {
public:
    static quint16 crc16(const QByteArray &bytes);
    static bool decodeStatus(const NetworkFrame &frame, NetworkStatus *status);
    QVector<NetworkFrame> feed(const QByteArray &bytes);
    void reset() { buffer_.clear(); }
    int bufferedBytes() const { return buffer_.size(); }
    quint64 rejectedBytes() const { return rejectedBytes_; }
private:
    QByteArray buffer_;
    quint64 rejectedBytes_ = 0;
};
}
