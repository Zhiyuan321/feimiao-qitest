#include "device/NetworkInstrument.h"
#include "NetworkTestFrames.h"
#include "Rs485TestDevice.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace qitest;
class NetworkTests final : public QObject {
    Q_OBJECT
private slots:
    void crcAndDocumentedStatus() {
        QCOMPARE(NetworkProtocol::crc16("123456789"), quint16(0x4b37));
        NetworkProtocol codec;
        const auto frames = codec.feed(test::networkStatusWire());
        QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].payload.size(), 14);
        NetworkStatus status;
        QVERIFY(NetworkProtocol::decodeStatus(frames[0], &status));
        QCOMPARE(status.multiplierVoltageV, quint16(3000)); QCOMPARE(status.vacuumRaw, quint16(1234));
        QVERIFY(status.experimentRunning);
        auto off = frames[0]; off.payload[9] = 0x12;
        // Reserved bytes are not interpreted as power states.
        off.payload[4] = char(0xfe);
        QVERIFY(NetworkProtocol::decodeStatus(off, &status)); QVERIFY(!status.experimentRunning);
        for (int kind = 0; kind < 7; ++kind) {
            auto invalid = frames[0];
            if (kind == 0) invalid.action = 0x10;
            if (kind == 1) invalid.command = 0x81;
            if (kind == 2) invalid.count = 2;
            if (kind == 3) invalid.index = 0;
            if (kind == 4) invalid.payload.chop(1);
            if (kind == 5) invalid.payload[9] = 0;
            if (kind == 6) invalid.payload[0] = char(0xff);
            QVERIFY(!NetworkProtocol::decodeStatus(invalid, &status));
        }
    }
    void splitMergedNoiseAndCorruption() {
        const auto wire = test::networkStatusWire();
        for (int split = 0; split <= wire.size(); ++split) {
            NetworkProtocol codec;
            auto frames = codec.feed(wire.left(split)); frames += codec.feed(wire.mid(split));
            QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].wire, wire); QCOMPARE(codec.bufferedBytes(), 0);
        }
        NetworkProtocol codec;
        auto badCrc = wire; badCrc[21] = 0;
        auto badTail = wire; badTail[23] = 0;
        const auto merged = codec.feed(QByteArray::fromHex("ffff552001ffff") + badCrc + badTail + wire + wire);
        QCOMPARE(merged.size(), 2); QVERIFY(codec.rejectedBytes() > 0);
        const auto payload = QByteArray::fromHex("55aa55aa");
        const auto unknown = codec.feed(test::networkFrame(payload, 0x20, 0x81, 5, 3));
        QCOMPARE(unknown.size(), 1); QCOMPARE(unknown[0].payload, payload);
        QCOMPARE(unknown[0].count, quint8(5)); QCOMPARE(unknown[0].index, quint8(3));
        codec.feed(QByteArray::fromHex("55200104020101") + QByteArray(800, 'x'));
        QVERIFY(codec.bufferedBytes() <= 1034);
        codec.reset(); QCOMPARE(codec.bufferedBytes(), 0);
        QCOMPARE(codec.feed(test::networkFrame(QByteArray(1024, 'x'), 0x20, 0x81)).size(), 1);
    }
    void tcpReadbackStalenessAndReconnect() {
        NetworkInstrument adapter;
        QVERIFY(adapter.startListening("127.0.0.1", 0, 1000));
        QTcpSocket client; client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        QVERIFY(!adapter.health().connected); QVERIFY(!adapter.descriptor().simulation);
        auto wire = test::networkStatusWire();
        client.write(wire.left(8)); QTest::qWait(30); QVERIFY(!adapter.health().connected);
        client.write(wire.mid(8)); QTRY_VERIFY(adapter.health().connected);
        QCOMPARE(adapter.telemetry().multiplierVoltageV, 3000.0);
        QVERIFY(std::isnan(adapter.telemetry().vacuumMbar)); QVERIFY(!adapter.health().ready);
        QCOMPARE(adapter.statusDetails().value("vacuumRaw").toInt(), 1234);
        QVERIFY(adapter.confirmedSettings().isEmpty()); QVERIFY(adapter.acquireSpectrum().isEmpty());
        QVERIFY(!adapter.validateSetting("powerOn", true).allowed);
        adapter.requestSetting("r", "powerOn", true);
        QTest::qWait(20); QCOMPARE(client.bytesAvailable(), qint64(0)); // No hidden commands/ACKs.
        // Spectrum frames must not refresh the status deadline.
        client.write(test::networkFrame(QByteArray(1000, 'x'), 0x20, 0x81));
        QTRY_VERIFY(adapter.statusDetails().value("unparsedFrames").toInt() == 1);
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.health().connected, 1800);
        QVERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        QVERIFY(std::isnan(adapter.telemetry().multiplierVoltageV));
        QVERIFY(!adapter.statusDetails().contains("vacuumRaw"));
        client.write(wire); QTRY_VERIFY(adapter.health().connected);
        QTcpSocket second; second.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(second.state() == QAbstractSocket::UnconnectedState);
        QVERIFY(adapter.health().connected);
        client.abort(); QTRY_VERIFY(!adapter.statusDetails().value("tcpConnected").toBool());
        QVERIFY(!adapter.health().connected); QVERIFY(adapter.statusDetails().value("listening").toBool());
        client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        client.write(wire); QTRY_VERIFY(adapter.health().connected);
        adapter.stopListening(); QVERIFY(!adapter.health().connected);
        QVERIFY(!adapter.statusDetails().value("listening").toBool());
    }
    void channelsRemainIndependentAndRawExportIsBounded() {
        test::FakeSerial port; port.reply = test::frame(test::statusPayload());
        auto serial = std::make_unique<Rs485Instrument>(&port, nullptr);
        QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);
        NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1", 0));
        QTcpSocket client; client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        client.write(test::networkStatusWire()); QTRY_VERIFY(adapter.statusDetails().value("connected").toBool());
        QCOMPARE(adapter.telemetry().tdTemperatureC, 245.6); QCOMPARE(adapter.telemetry().multiplierVoltageV, 3000.0);
        for (int i = 0; i < 300; ++i) client.write(test::networkFrame(QByteArray(1000, 'x'), 0x20, 0x82));
        QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(), 301);
        QCOMPARE(adapter.statusDetails().value("retainedFrames").toInt(), 256);
        QTemporaryDir dir; QString error;
        QVERIFY(adapter.exportFrames(dir.filePath("frames.json"), &error));
        QFile file(dir.filePath("frames.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(doc.value("frames").toArray().size(), 256);
        QCOMPARE(doc.value("frames").toArray()[0].toObject().value("command").toInt(), 0x82);
        adapter.serial()->closePort();
        QVERIFY(adapter.health().connected); QVERIFY(std::isnan(adapter.telemetry().tdTemperatureC));
        QCOMPARE(adapter.telemetry().multiplierVoltageV, 3000.0);
        QVERIFY(adapter.serial()->openPort("TEST_ONLY")); QTRY_VERIFY(adapter.serial()->health().connected);
        adapter.stopListening(); QVERIFY(adapter.health().connected);
        QCOMPARE(adapter.telemetry().tdTemperatureC, 245.6); QVERIFY(std::isnan(adapter.telemetry().multiplierVoltageV));
    }
    void listenFailureDoesNotBecomeSimulation() {
        NetworkInstrument adapter;
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost));
        QVERIFY(!adapter.startListening("127.0.0.1", occupied.serverPort()));
        QVERIFY(!adapter.statusDetails().value("listening").toBool());
        QVERIFY(!adapter.health().connected); QVERIFY(!adapter.descriptor().simulation);
        QVERIFY(!adapter.startListening("not-an-ip", 11000));
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "NetworkTests.moc"
