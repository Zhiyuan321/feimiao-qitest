#pragma once
#include <QIODevice>
#include <QTimer>
#include <QList>
#include <cstring>

namespace qitest::test {
inline QByteArray statusPayload() {
    QByteArray data = QByteArray::fromHex("eeffeefeeeffee");
    data[3] = char(0xff); // Internal carrier, not gas OFF.
    for (quint16 n : {3200, 123, 1200, 801, 2456, 853, 5680, 42}) {
        data.append(char(n >> 8)); data.append(char(n & 0xff));
    }
    return data;
}
inline QByteArray frame(const QByteArray &payload, quint8 command = 0x30) {
    QByteArray data = QByteArray::fromHex("5588");
    data.append(char(command));
    data.append(char(payload.size() >> 8)); data.append(char(payload.size() & 0xff));
    data += payload; data.append(char(0xaa));
    return data;
}

class FakeSerial final : public QIODevice {
public:
    QByteArray input, reply;
    QList<QByteArray> writes;
    bool failWrite = false;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return input.size() + QIODevice::bytesAvailable(); }
    void deliver(const QByteArray &bytes) { input += bytes; emit readyRead(); }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        const int length = qMin(int(maximum), int(input.size()));
        std::memcpy(data, input.constData(), size_t(length)); input.remove(0, length);
        return length;
    }
    qint64 writeData(const char *data, qint64 size) override {
        writes << QByteArray(data, int(size));
        if (failWrite) return -1;
        if (!reply.isEmpty()) {
            const auto captured = reply;
            QTimer::singleShot(0, this, [this, captured] { if (isOpen()) deliver(captured); });
        }
        return size;
    }
};
}
