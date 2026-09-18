#pragma once
#include <QByteArray>
#include <QVector>

namespace qitest {
struct PumpReply {
    QByteArray parameter, raw, wire;
};
// Trial reader based on the user's four literal queries and mid(5,3)/mid(10,6).
// Response checksum and physical scaling are not established: raw diagnostics only.
class PumpProtocol {
public:
    static QByteArray query(int index);
    // User-supplied literals, 2026-09-17. Building a frame does not send it or
    // establish an acknowledgement contract; startup must still wait for one.
    static QByteArray powerCommand(bool enabled);
    static QByteArray parameter(int index);
    static bool extract(const QByteArray &wire, PumpReply *reply);
    QVector<QByteArray> feed(const QByteArray &bytes);
    void reset() { buffer_.clear(); overflow_ = false; }
private:
    QByteArray buffer_;
    bool overflow_ = false;
};
}
