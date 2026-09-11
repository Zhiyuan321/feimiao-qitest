#include "device/NetworkProtocol.h"
#include <cmath>

namespace qitest {
namespace {
quint16 u16(const QByteArray &bytes, int offset) {
    return (quint16(quint8(bytes[offset])) << 8) | quint8(bytes[offset + 1]);
}
}
double NetworkProtocol::vacuumMbarFromRaw(quint16 value) {
    const double result = (value * 1.0 / 65536) * 2.5 * 5.7;
    return std::pow(10.0, (result - 6.143) / 1.286);
}
bool NetworkProtocol::decodePressure(const NetworkFrame &frame, QVector<double> *volts) {
    if (!volts || frame.action != 0x20 || frame.command != 0x82
        || frame.payload.isEmpty() || frame.payload.size() > 1000 || frame.payload.size() % 2) return false;
    QVector<double> values; values.reserve(frame.payload.size()/2);
    // Exact conversion supplied by the user on 2026-09-10; not the vacuum formula.
    for(int i=0;i<frame.payload.size();i+=2) values.append(u16(frame.payload,i) / 65535.0 * 2.5 * 5.7);
    *volts=values; return true;
}
QByteArray NetworkProtocol::tuningCommand(bool enabled) {
    QByteArray body=QByteArray::fromHex("102000030101");
    body.append(enabled ? char(0x22) : char(0x23));
    const auto crc=crc16(body);
    QByteArray wire(1,char(0x55)); wire+=body;
    wire.append(char(crc>>8)); wire.append(char(crc&0xff)); wire.append(char(0xaa));
    return wire;
}
quint16 NetworkProtocol::crc16(const QByteArray &bytes) {
    quint16 crc = 0xffff;
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
QVector<NetworkFrame> NetworkProtocol::feed(const QByteArray &bytes) {
    QVector<NetworkFrame> frames;
    // Bytewise accumulation bounds the cache even for hostile or corrupt streams.
    for (char byte : bytes) {
        buffer_.append(byte);
        for (;;) {
            const int start = buffer_.indexOf(char(0x55));
            if (start < 0) { rejectedBytes_ += buffer_.size(); buffer_.clear(); break; }
            if (start > 0) { rejectedBytes_ += start; buffer_.remove(0, start); }
            if (buffer_.size() < 5) break;
            const int length = u16(buffer_, 3);
            const int size = length + 8;
            if (length < 3 || length > 1026) {
                ++rejectedBytes_; buffer_.remove(0, 1); continue;
            }
            if (buffer_.size() < size) break;
            if (quint8(buffer_[size - 1]) != 0xaa
                || u16(buffer_, size - 3) != crc16(buffer_.mid(1, size - 4))) {
                ++rejectedBytes_; buffer_.remove(0, 1); continue;
            }
            frames.push_back({quint8(buffer_[1]), quint8(buffer_[2]),
                quint8(buffer_[5]), quint8(buffer_[6]), buffer_.mid(7, length - 2), buffer_.left(size)});
            buffer_.remove(0, size);
        }
    }
    return frames;
}
bool NetworkProtocol::decodeStatus(const NetworkFrame &frame, NetworkStatus *status) {
    // 2026-09-09 revised protocol: 21 payload bytes, LEN = 21 + 2.
    // Page 14's "data length 23" includes COUNT/INDEX; the table and capture agree on 21.
    if (!status || frame.action != 0x20 || frame.command != 0x01
        || frame.count != 1 || frame.index != 1 || frame.payload.size() != 21) return false;
    const auto &data = frame.payload;
    const auto experiment = quint8(data[9]);
    if ((experiment != 0x01 && experiment != 0x00) || u16(data, 0) > 3000) return false;
    *status = {u16(data, 0), u16(data, 2), experiment == 0x01};
    return true;
}
}
