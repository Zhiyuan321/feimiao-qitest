#pragma once
#include "device/NetworkProtocol.h"
namespace qitest { namespace test {
inline QByteArray networkStatusWire() {
    // Synthetic golden frame: action20/cmd01, length23, payload21, 3000V, raw1234, ON=01.
    return QByteArray::fromHex("552001001701010bb804d200000000000100000000000000000000004cb2aa");
}
inline QByteArray networkFrame(const QByteArray &payload, quint8 action = 0x20,
                               quint8 command = 0x01, quint8 count = 1, quint8 index = 1) {
    const int length = payload.size() + 2;
    QByteArray frame;
    for (int v : {0x55, int(action), int(command), length >> 8, length & 0xff, int(count), int(index)}) frame.append(char(v));
    frame.append(payload);
    const auto crc = NetworkProtocol::crc16(frame.mid(1));
    frame.append(char(crc >> 8)); frame.append(char(crc)); frame.append(char(0xaa));
    return frame;
}
}}
