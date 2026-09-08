#include "device/NetworkProtocol.h"

namespace qitest {
namespace {
quint16 u16(const QByteArray &bytes, int offset) {
    return (quint16(quint8(bytes[offset])) << 8) | quint8(bytes[offset + 1]);
}
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
    // Only this complete single-frame layout has confirmed semantics.
    if (!status || frame.action != 0x20 || frame.command != 0x01
        || frame.count != 1 || frame.index != 1 || frame.payload.size() != 14) return false;
    const auto &data = frame.payload;
    const auto experiment = quint8(data[9]);
    if ((experiment != 0x11 && experiment != 0x12) || u16(data, 0) > 3000) return false;
    *status = {u16(data, 0), u16(data, 2), experiment == 0x11};
    return true;
}
}
