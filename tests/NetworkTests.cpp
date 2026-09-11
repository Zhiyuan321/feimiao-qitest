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
    void pressureConversionUsesUnsignedBigEndianAnd65535() {
        NetworkProtocol decoder; const auto frames=decoder.feed(test::networkFrame(QByteArray::fromHex("00008000ffff"),0x20,0x82));
        QCOMPARE(frames.size(),1);QVector<double> values;
        QVERIFY(NetworkProtocol::decodePressure(frames[0],&values)); QCOMPARE(values.size(),3);
        QCOMPARE(values[0],0.0);QCOMPARE(values[1],32768.0/65535*2.5*5.7);QCOMPARE(values[2],14.25);
        auto bad=frames[0];bad.payload.append('x');QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
        bad=frames[0];bad.command=0x81;QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
        bad=frames[0];bad.payload=QByteArray(1002,0);QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
    }
    void pressureReadbackAndTuningAckAreSeparateFromPhysicalState() {
        NetworkInstrument adapter;QString error;QVERIFY(!adapter.requestTuning(true,&error));
        QVERIFY(adapter.startListening("127.0.0.1",0,1000));QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        QVERIFY(!adapter.requestTuning(true,&error));
        auto payload=test::networkStatusWire().mid(7,21);payload[9]=0;
        client.write(test::networkFrame(payload));QTRY_VERIFY(adapter.statusDetails().value("connected").toBool());
        client.write(test::networkFrame(QByteArray::fromHex("0000ffff"),0x20,0x82));
        QTRY_COMPARE(adapter.pressureVolts().size(),2);QCOMPARE(adapter.pressureVolts()[1],14.25);
        QVERIFY(adapter.requestTuning(true,&error));QVERIFY(!adapter.requestTuning(false,&error));
        QTRY_VERIFY(client.bytesAvailable()>0);
        QCOMPARE(client.readAll(),test::networkFrame(QByteArray(1,char(0x22)),0x10,0x20));
        client.write(test::networkFrame(QByteArray(1,char(0x11)),0x10,0x20));
        QTRY_VERIFY(!adapter.statusDetails().value("tuningPending").toBool());
        QVERIFY(adapter.statusDetails().value("tuningMessage").toString().contains("应答"));
        QVERIFY(!adapter.confirmedSettings().contains("tuningEnabled"));
        QVERIFY(!adapter.validateSetting("powerOn",true).allowed);
        QTRY_VERIFY_WITH_TIMEOUT(adapter.pressureVolts().isEmpty(),1500);
        QVERIFY(adapter.requestTuning(false,&error)); // STOP allowed even after status expires.
        QTRY_VERIFY(client.bytesAvailable()>0);
        QCOMPARE(client.readAll(),test::networkFrame(QByteArray(1,char(0x23)),0x10,0x20));
        client.write(test::networkFrame(QByteArray(1,char(0x12)),0x10,0x20));
        QTRY_VERIFY(!adapter.statusDetails().value("tuningPending").toBool());
        QVERIFY(adapter.statusDetails().value("tuningMessage").toString().contains("拒绝"));
        QVERIFY(adapter.requestTuning(false,&error));
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.statusDetails().value("tcpConnected").toBool(),3500);
        QVERIFY(!adapter.statusDetails().value("tuningPending").toBool());
        QVERIFY(adapter.pressureVolts().isEmpty());
    }

    void vacuumConversion() {
        // Independently evaluated decimal reference values, including unsigned 16-bit endpoints.
        const QVector<QPair<quint16, double>> cases{
            {0, 1.671754990426998e-5}, {1234, 2.702826557052938e-5},
            {2822, 5.015546770129972e-5}, {2826, 5.023363518557833e-5},
            {2831, 5.033151587861558e-5}, {65535, 2013142.257836125}};
        for (const auto &entry : cases)
            QVERIFY(std::abs(NetworkProtocol::vacuumMbarFromRaw(entry.first) / entry.second - 1.0) < 1e-12);
    }
    void crcAndDocumentedStatus() {
        QCOMPARE(NetworkProtocol::crc16("123456789"), quint16(0x4b37));
        NetworkProtocol codec;
        const auto frames = codec.feed(test::networkStatusWire());
        QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].payload.size(), 21);
        NetworkStatus status;
        QVERIFY(NetworkProtocol::decodeStatus(frames[0], &status));
        QCOMPARE(status.multiplierVoltageV, quint16(3000)); QCOMPARE(status.vacuumRaw, quint16(1234));
        QVERIFY(status.experimentRunning);
        auto off = frames[0]; off.payload[9] = 0x00;
        // Reserved bytes are not interpreted as power states.
        off.payload[4] = char(0xfe);
        off.payload[16] = 0x01;
        off.payload[20] = 0x11; // Trailing reserved data must not become experiment state.
        QVERIFY(NetworkProtocol::decodeStatus(off, &status)); QVERIFY(!status.experimentRunning);
        for (int kind = 0; kind < 10; ++kind) {
            auto invalid = frames[0];
            if (kind == 0) invalid.action = 0x10;
            if (kind == 1) invalid.command = 0x81;
            if (kind == 2) invalid.count = 2;
            if (kind == 3) invalid.index = 0;
            if (kind == 4) invalid.payload.chop(1);
            if (kind == 5) invalid.payload[9] = 0x11;
            if (kind == 6) invalid.payload[0] = char(0xff);
            if (kind == 7) invalid.payload[9] = 0x12;
            if (kind == 8) invalid.payload.resize(14); // Superseded layout is not this firmware.
            if (kind == 9) invalid.payload.append(QByteArray(2, char(0))); // LEN is not payload size.
            QVERIFY(!NetworkProtocol::decodeStatus(invalid, &status));
        }
    }
    void capturedStatusReplay() {
        // Optional local evidence; customer JSON, IPs and timestamps stay outside the repository.
        const auto path = qEnvironmentVariable("QITEST_NETWORK_REPLAY_JSON");
        if (path.isEmpty()) QSKIP("Set QITEST_NETWORK_REPLAY_JSON to the 2026-09-09 four-frame capture");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto captured = QJsonDocument::fromJson(file.readAll()).object().value("frames").toArray();
        QCOMPARE(captured.size(), 4);
        const QVector<int> expected{2827, 2823, 2828, 2826};
        NetworkInstrument adapter;
        QVERIFY(adapter.startListening("127.0.0.1", 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        for (int i = 0; i < captured.size(); ++i) {
            const auto wire = QByteArray::fromHex(captured[i].toObject().value("hex").toString().toLatin1());
            QCOMPARE(wire.size(), 31);
            NetworkProtocol codec;
            const auto frames = codec.feed(wire);
            QCOMPARE(frames.size(), 1); QCOMPARE(codec.rejectedBytes(), quint64(0));
            NetworkStatus status;
            QVERIFY(NetworkProtocol::decodeStatus(frames[0], &status));
            QCOMPARE(status.vacuumRaw, quint16(expected[i]));
            QCOMPARE(status.multiplierVoltageV, quint16(0));
            QVERIFY(!status.experimentRunning);
            client.write(wire);
            QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(), i + 1);
            QVERIFY(adapter.statusDetails().value("connected").toBool());
            QCOMPARE(adapter.statusDetails().value("vacuumRaw").toInt(), expected[i]);
            QCOMPARE(adapter.statusDetails().value("unparsedFrames").toInt(), 0);
            QVERIFY(!adapter.statusDetails().value("experimentRunning").toBool());
            QCOMPARE(adapter.telemetry().vacuumMbar, NetworkProtocol::vacuumMbarFromRaw(quint16(expected[i])));
        }
        QTemporaryDir directory; QString error;
        QVERIFY(adapter.exportFrames(directory.filePath("replay.json"), &error));
        QFile exported(directory.filePath("replay.json")); QVERIFY(exported.open(QIODevice::ReadOnly));
        const auto replay = QJsonDocument::fromJson(exported.readAll()).object();
        QVERIFY(replay.value("protocol").toString().contains("21"));
        for (const auto &frame : replay.value("frames").toArray())
            QVERIFY(frame.toObject().value("decodedStatus").toBool());
    }
    void splitMergedNoiseAndCorruption() {
        const auto wire = test::networkStatusWire();
        for (int split = 0; split <= wire.size(); ++split) {
            NetworkProtocol codec;
            auto frames = codec.feed(wire.left(split)); frames += codec.feed(wire.mid(split));
            QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].wire, wire); QCOMPARE(codec.bufferedBytes(), 0);
        }
        NetworkProtocol codec;
        auto badCrc = wire; badCrc[wire.size() - 3] = char(quint8(badCrc[wire.size() - 3]) ^ 0xff);
        auto badTail = wire; badTail[wire.size() - 1] = 0;
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
        QVERIFY(std::abs(adapter.telemetry().vacuumMbar / 2.702826557052938e-5 - 1.0) < 1e-12);
        QCOMPARE(adapter.health().vacuumMbar, adapter.telemetry().vacuumMbar);
        QCOMPARE(adapter.statusDetails().value("vacuumMbar").toDouble(), adapter.health().vacuumMbar);
        QVERIFY(!adapter.health().ready);
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
        QVERIFY(!adapter.statusDetails().contains("vacuumMbar"));
        QVERIFY(std::isnan(adapter.health().vacuumMbar));
        QVERIFY(std::isnan(adapter.telemetry().vacuumMbar));
        client.write(wire); QTRY_VERIFY(adapter.health().connected);
        QTcpSocket second;
        // Wine may expose only 127.0.0.1 even though real Windows supports the
        // loopback block. Exercise foreign-peer rejection wherever a second
        // loopback address can actually be bound, without skipping the rest of
        // the reconnect/staleness test on that emulator.
        if (second.bind(QHostAddress("127.0.0.2"), 0)) {
            second.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
            QTRY_COMPARE(adapter.statusDetails().value("rejectedConnections").toInt(), 1);
            QTRY_VERIFY(second.state() == QAbstractSocket::UnconnectedState);
            QVERIFY(adapter.health().connected);
        } else {
            qInfo("Second loopback address unavailable; foreign-peer branch not exercised");
        }
        client.abort(); QTRY_VERIFY(!adapter.statusDetails().value("tcpConnected").toBool());
        QVERIFY(std::isnan(adapter.health().vacuumMbar));
        QVERIFY(std::isnan(adapter.telemetry().vacuumMbar));
        QVERIFY(!adapter.health().connected); QVERIFY(adapter.statusDetails().value("listening").toBool());
        client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        client.write(wire); QTRY_VERIFY(adapter.health().connected);
        adapter.stopListening(); QVERIFY(!adapter.health().connected);
        QVERIFY(!adapter.statusDetails().value("listening").toBool());
    }
    void sameIpReconnectReplacesUnclosedSession() {
        test::FakeSerial port; port.reply = test::frame(test::statusPayload());
        auto serial = std::make_unique<Rs485Instrument>(&port, nullptr);
        QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);
        NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1", 0));
        const auto serverPort = quint16(adapter.statusDetails().value("port").toUInt());
        auto client = std::make_unique<QTcpSocket>();
        client->connectToHost(QHostAddress::LocalHost, serverPort);
        QTRY_COMPARE(adapter.statusDetails().value("acceptedConnections").toInt(), 1);
        client->write(test::networkStatusWire());
        QTRY_VERIFY(adapter.statusDetails().value("connected").toBool());
        for (int i = 1; i <= 24; ++i) {
            // Leave an incomplete packet and an open socket, then reconnect from the same IP.
            const auto before = adapter.statusDetails().value("receivedBytes").toLongLong();
            client->write(test::networkStatusWire().left(12));
            QTRY_VERIFY(adapter.statusDetails().value("receivedBytes").toLongLong() >= before + 12);
            auto replacement = std::make_unique<QTcpSocket>();
            replacement->connectToHost(QHostAddress::LocalHost, serverPort);
            QTRY_COMPARE(adapter.statusDetails().value("replacedConnections").toInt(), i);
            QTRY_VERIFY(replacement->state() == QAbstractSocket::ConnectedState);
            QTRY_VERIFY(client->state() == QAbstractSocket::UnconnectedState);
            QVERIFY(!adapter.statusDetails().value("connected").toBool());
            QVERIFY(!adapter.statusDetails().contains("vacuumRaw"));
            QVERIFY(!adapter.statusDetails().contains("vacuumMbar"));
            QVERIFY(std::isnan(adapter.health().vacuumMbar));
            QVERIFY(std::isnan(adapter.telemetry().vacuumMbar));
            QCOMPARE(adapter.statusDetails().value("peerPort").toInt(), int(replacement->localPort()));
            QCOMPARE(adapter.telemetry().tdTemperatureC, 245.6); // 485 stays live.
            auto payload = test::networkStatusWire().mid(7, 21);
            payload[2] = char(i); payload[3] = 42; payload[9] = char(i % 2);
            replacement->write(test::networkFrame(payload));
            QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(), i + 1);
            QVERIFY(adapter.statusDetails().value("connected").toBool());
            QCOMPARE(adapter.statusDetails().value("vacuumRaw").toInt(), i * 256 + 42);
            QCOMPARE(adapter.health().vacuumMbar, NetworkProtocol::vacuumMbarFromRaw(quint16(i * 256 + 42)));
            QCOMPARE(adapter.telemetry().vacuumMbar, adapter.health().vacuumMbar);
            QCOMPARE(adapter.statusDetails().value("experimentRunning").toBool(), bool(i % 2));
            QCOMPARE(adapter.statusDetails().value("unparsedFrames").toInt(), 0);
            QTest::qWait(10); // Late notifications from the old socket must not invalidate the new one.
            QVERIFY(adapter.statusDetails().value("connected").toBool());
            client = std::move(replacement);
        }
        QCOMPARE(adapter.statusDetails().value("acceptedConnections").toInt(), 25);
        QCOMPARE(adapter.statusDetails().value("rejectedConnections").toInt(), 0);
        QTemporaryDir directory; QString error;
        QVERIFY(adapter.exportFrames(directory.filePath("reconnect.json"), &error));
        QFile file(directory.filePath("reconnect.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto events = QJsonDocument::fromJson(file.readAll()).object().value("connectionEvents").toArray();
        QCOMPARE(events.size(), 64);
        QCOMPARE(events.last().toObject().value("event").toString(), QString("accepted"));
        adapter.stopListening();
        QVERIFY(adapter.serial()->health().connected);
        QVERIFY(adapter.startListening("127.0.0.1", 0));
        QCOMPARE(adapter.statusDetails().value("acceptedConnections").toInt(), 0);
        QCOMPARE(adapter.statusDetails().value("replacedConnections").toInt(), 0);
    }
    void continuousReadbackBeyondDefaultTimeout() {
        NetworkInstrument adapter;
        QVERIFY(adapter.startListening("127.0.0.1", 0)); // Real default: 5 seconds.
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        const auto wire = test::networkStatusWire();
        for (int i = 0; i < 12; ++i) {
            QCOMPARE(client.write(wire), qint64(wire.size()));
            QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(), i + 1);
            QVERIFY(adapter.statusDetails().value("connected").toBool());
            QCOMPARE(adapter.statusDetails().value("receivedBytes").toLongLong(), qint64((i + 1) * wire.size()));
            QTest::qWait(1000);
        }
        QVERIFY(adapter.statusDetails().value("connected").toBool());
        QCOMPARE(client.bytesAvailable(), qint64(0)); // Application-layer TX remains empty.
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.statusDetails().value("connected").toBool(), 5500);
        QVERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        client.write(wire);
        QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(), 13);
        QVERIFY(adapter.statusDetails().value("connected").toBool());
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
        QCOMPARE(adapter.health().vacuumMbar, NetworkProtocol::vacuumMbarFromRaw(1234));
        QCOMPARE(adapter.telemetry().vacuumMbar, adapter.health().vacuumMbar);
        QVERIFY(adapter.serial()->openPort("TEST_ONLY")); QTRY_VERIFY(adapter.serial()->health().connected);
        adapter.stopListening(); QVERIFY(adapter.health().connected);
        QVERIFY(std::isnan(adapter.health().vacuumMbar));
        QVERIFY(std::isnan(adapter.telemetry().vacuumMbar));
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
