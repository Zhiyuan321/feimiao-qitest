#pragma once
#include "device/PumpProtocol.h"
#include "device/Rs485Protocol.h"
#include "Rs485TestDevice.h"
#include <QIODevice>
#include <QTimer>
#include <cstring>
namespace qitest::test {
// Synthetic replies for the supplied offsets. Trailer is intentionally not a verified checksum.
inline QByteArray pumpReply(int index) {
    const QByteArray values[]{"001200", "000080", "000220", "000045"};
    return "00110" + PumpProtocol::parameter(index) + "06" + values[index] + "000\r";
}
class FakeSharedBus final : public QIODevice {
public:
    QByteArray input;
    QList<QByteArray> writes;
    bool respond = true, mainRespond = true, failWrite = false, overlap = false, outstanding = false;
    int openCount = 0, closeCount = 0, pumpDelayMs = 5;
    bool open(OpenMode mode) override { ++openCount; return QIODevice::open(mode); }
    void close() override { if (isOpen()) ++closeCount; QIODevice::close(); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return input.size() + QIODevice::bytesAvailable(); }
    void deliver(const QByteArray &bytes) { input += bytes; emit readyRead(); }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        const int n = qMin(int(maximum), input.size()); std::memcpy(data, input.constData(), size_t(n)); input.remove(0, n); return n;
    }
    qint64 writeData(const char *data, qint64 size) override {
        const QByteArray request(data, int(size)); writes.append(request);
        if (failWrite) return -1;
        if (outstanding) overlap = true;
        QByteArray response; int delay = 5;
        if (request == Rs485Protocol::statusQuery() && mainRespond) response = frame(statusPayload());
        if (respond) for (int i = 0; i < 4; ++i) if (request == PumpProtocol::query(i)) {
            response = pumpReply(i); delay = pumpDelayMs;
        }
        if (!response.isEmpty()) {
            outstanding = true;
            QTimer::singleShot(0, this, [this, request, response] { if (isOpen()) deliver(request + response.left(8)); });
            QTimer::singleShot(delay, this, [this, response] { outstanding = false; if (isOpen()) deliver(response.mid(8)); });
        }
        return size;
    }
};
}
