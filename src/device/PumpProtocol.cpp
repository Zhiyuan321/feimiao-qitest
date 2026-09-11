#include "device/PumpProtocol.h"

namespace qitest {
QByteArray PumpProtocol::query(int index) {
    const QByteArray queries[]{"0010039802=?115\r", "0010031002=?099\r",
        "0010031302=?102\r", "0010032602=?106\r"};
    return index >= 0 && index < 4 ? queries[index] : QByteArray();
}
QByteArray PumpProtocol::parameter(int index) { return query(index).mid(5, 3); }
bool PumpProtocol::extract(const QByteArray &wire, PumpReply *reply) {
    if (!reply || wire.size() < 17 || wire.size() > 256 || !wire.endsWith('\r')
        || !wire.startsWith("001")) return false;
    const auto code = wire.mid(5, 3);
    bool known = false;
    for (int i = 0; i < 4; ++i) if (code == parameter(i)) known = true;
    if (!known) return false;
    const auto raw = wire.mid(10, 6);
    for (char c : raw) if (c < '0' || c > '9') return false;
    *reply = {code, raw, wire};
    return true;
}
QVector<QByteArray> PumpProtocol::feed(const QByteArray &bytes) {
    QVector<QByteArray> frames;
    for (char c : bytes) {
        if (c == '\n' && buffer_.isEmpty() && !overflow_) continue; // Optional LF after CR.
        if (!overflow_) {
            buffer_.append(c);
            if (buffer_.size() > 256) { buffer_.clear(); overflow_ = true; }
        }
        if (c == '\r') {
            if (!overflow_) frames.append(buffer_);
            reset();
        }
    }
    return frames;
}
}
