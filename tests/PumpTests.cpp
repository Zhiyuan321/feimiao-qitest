#include "device/Rs485Instrument.h"
#include <cmath>
#include "PumpTestDevice.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
using namespace qitest;
class PumpTests final : public QObject {
    Q_OBJECT
private slots:
    void literalQueriesAndOffsets() {
        QCOMPARE(PumpProtocol::query(0), QByteArray("0010039802=?115\r"));
        QCOMPARE(PumpProtocol::query(1), QByteArray("0010031002=?099\r"));
        QCOMPARE(PumpProtocol::query(2), QByteArray("0010031302=?102\r"));
        QCOMPARE(PumpProtocol::query(3), QByteArray("0010032602=?106\r"));
        QVERIFY(PumpProtocol::query(4).isEmpty());
        PumpReply result;
        QVERIFY(PumpProtocol::extract(test::pumpReply(0), &result));
        QCOMPARE(result.raw, QByteArray("001200")); // Do not remove trailing zeroes.
        QVERIFY(!PumpProtocol::extract("0010039812=?001200\r", &result)); // Contradictory Word example.
        for (int i = 0; i < 4; ++i) QVERIFY(!PumpProtocol::extract(PumpProtocol::query(i), &result));
        auto wrongAddress = test::pumpReply(0); wrongAddress[2] = '2';
        QVERIFY(!PumpProtocol::extract(wrongAddress, &result));
        auto badNumber = test::pumpReply(1); badNumber[12] = '.';
        QVERIFY(!PumpProtocol::extract(badNumber, &result));
        QVERIFY(!PumpProtocol::extract(test::pumpReply(1).left(15), &result));
    }
    void fragmentedMergedAndOversizedReplies() {
        const auto wire = test::pumpReply(0);
        for (int split = 0; split <= wire.size(); ++split) {
            PumpProtocol codec;
            auto frames = codec.feed(wire.left(split)); frames += codec.feed(wire.mid(split));
            QCOMPARE(frames.size(), 1); QCOMPARE(frames[0], wire);
        }
        PumpProtocol codec;
        auto frames = codec.feed(QByteArray(5000, 'x') + '\r' + wire + '\n' + test::pumpReply(1));
        QCOMPARE(frames.size(), 2); QCOMPARE(frames[1], test::pumpReply(1));
    }
    void confirmedUnitsKeepRawValuesAndInvalidate() {
        PumpReader reader;
        reader.resetSession(true);
        const QByteArray raw[]{"090002", "000014", "002364", "000032"};
        const char *keys[]{"molecularPumpRpm", "molecularPumpCurrentA", "molecularPumpVoltageV", "molecularPumpTemperatureC"};
        const double expected[]{90002.0, 0.14, 23.64, 32.0};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(!reader.statusDetails().contains(keys[i]));
            auto reply = test::pumpReply(i); reply.replace(10, 6, raw[i]);
            QVERIFY(reader.consume(reply, i));
            QCOMPARE(reader.statusDetails().value(keys[i]).toDouble(), expected[i]);
            QCOMPARE(reader.statusDetails().value(QString::fromLatin1(PumpProtocol::parameter(i))).toString(), QString::fromLatin1(raw[i]));
        }
        reader.invalidate("timeout");
        for (const auto key : keys) QVERIFY(!reader.statusDetails().contains(key));
        for (int i = 0; i < 4; ++i) {
            auto reply = test::pumpReply(i); reply.replace(10, 6, "000000");
            QVERIFY(reader.consume(reply, i));
            QVERIFY(reader.statusDetails().contains(keys[i]));
            QCOMPARE(reader.statusDetails().value(keys[i]).toDouble(), 0.0);
        }
        auto invalid = test::pumpReply(0); invalid[10] = 'x';
        QVERIFY(!reader.consume(invalid, 0));
        QCOMPARE(reader.statusDetails().value(keys[0]).toDouble(), 0.0);
        reader.resetSession(false);
        for (const auto key : keys) QVERIFY(!reader.statusDetails().contains(key));
    }
    void sharedPortReadsBothSequentiallyAndExports() {
        test::FakeSharedBus transport;
        Rs485Instrument reader(&transport, nullptr);
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_COMPARE_WITH_TIMEOUT(reader.pumpStatusDetails().value("326").toString(), QString("000045"), 2500);
        QCOMPARE(reader.pumpStatusDetails().value("398").toString(), QString("001200"));
        QCOMPARE(reader.pumpStatusDetails().value("310").toString(), QString("000080"));
        QCOMPARE(reader.pumpStatusDetails().value("313").toString(), QString("000220"));
        QCOMPARE(reader.telemetry().carrierGasPressureTorr, 801.0);
        QCOMPARE(reader.telemetry().molecularPumpRpm, 1200.0);
        QCOMPARE(reader.telemetry().molecularPumpCurrentA, 0.8);
        QCOMPARE(reader.telemetry().molecularPumpVoltageV, 2.2);
        QCOMPARE(reader.telemetry().molecularPumpTemperatureC, 45.0);
        QTRY_VERIFY_WITH_TIMEOUT(transport.writes.size() >= 11, 3500);
        QCOMPARE(reader.telemetry().molecularPumpRpm, 1200.0); // Survives the next main-board status.
        QCOMPARE(reader.telemetry().molecularPumpTemperatureC, 45.0);
        for (int i = 0; i < transport.writes.size(); ++i)
            QCOMPARE(transport.writes[i], i % 5 == 0 ? Rs485Protocol::statusQuery() : PumpProtocol::query(i % 5 - 1));
        QCOMPARE(transport.openCount, 1); QCOMPARE(transport.closeCount, 0); QVERIFY(!transport.overlap);
        QTemporaryDir dir; QString error;
        QVERIFY(reader.exportFrames(dir.filePath("bus.json"), &error));
        QFile file(dir.filePath("bus.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(doc.value("mode").toString().contains("未校验"));
        QVERIFY(doc.value("busStatus").toObject().value("connected").toBool());
        QCOMPARE(doc.value("pumpStatus").toObject().value("310").toString(), QString("000080"));
        QCOMPARE(doc.value("pumpStatus").toObject().value("molecularPumpCurrentA").toDouble(), 0.8);
        QCOMPARE(doc.value("records").toArray()[0].toObject().value("hex").toString(), QString("55 88 30 00 01 01 aa"));
        for (int i = 0; i < 300; ++i) transport.deliver("junk\r");
        QCOMPARE(reader.pumpStatusDetails().value("retainedRecords").toInt(), 256);
        reader.closePort(); QVERIFY(!reader.portOpen()); QVERIFY(!reader.pumpStatusDetails().contains("398"));
        QCOMPARE(transport.closeCount, 1);
        QVERIFY(std::isnan(reader.telemetry().molecularPumpRpm));
        QVERIFY(std::isnan(reader.telemetry().molecularPumpTemperatureC));
    }
    void pumpTimeoutKeepsMainBoardLiveAndRejectsLateReply() {
        test::FakeSharedBus transport;
        Rs485Instrument reader(&transport, nullptr);
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_VERIFY(reader.pumpStatusDetails().contains("398"));
        transport.respond = false;
        QTRY_VERIFY_WITH_TIMEOUT(!reader.pumpStatusDetails().value("enabled").toBool(), 2500);
        QVERIFY(reader.portOpen()); QVERIFY(!reader.pumpStatusDetails().contains("398"));
        QVERIFY(std::isnan(reader.telemetry().molecularPumpRpm));
        QVERIFY(std::isnan(reader.telemetry().molecularPumpCurrentA));
        QVERIFY(std::isnan(reader.telemetry().molecularPumpVoltageV));
        QVERIFY(std::isnan(reader.telemetry().molecularPumpTemperatureC));
        const int before = transport.writes.size();
        transport.deliver(test::pumpReply(0)); QVERIFY(!reader.pumpStatusDetails().contains("398"));
        QTRY_VERIFY_WITH_TIMEOUT(transport.writes.size() >= before + 2, 2000);
        QVERIFY(reader.health().connected); QCOMPARE(reader.telemetry().tdTemperatureC, 245.6);
        for (int i = before; i < transport.writes.size(); ++i) QCOMPARE(transport.writes[i], Rs485Protocol::statusQuery());
        QCOMPARE(transport.openCount, 1); QCOMPARE(transport.closeCount, 0);
        reader.closePort(); transport.respond = true;
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_VERIFY(reader.pumpStatusDetails().contains("398"));
        reader.closePort(); QVERIFY(!reader.pumpStatusDetails().contains("398"));
    }
    void wrongParameterAndWordExampleNeverAdvanceQuery() {
        test::FakeSharedBus transport; transport.respond = false;
        Rs485Instrument reader(&transport, nullptr);
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_VERIFY(transport.writes.size() >= 2);
        QCOMPARE(transport.writes.last(), PumpProtocol::query(0));
        transport.deliver(test::pumpReply(1));
        transport.deliver("0010039812=?001200\r");
        QVERIFY(!reader.pumpStatusDetails().contains("310")); QVERIFY(!reader.pumpStatusDetails().contains("398"));
        QTest::qWait(100); QCOMPARE(transport.writes.last(), PumpProtocol::query(0));
        transport.deliver(test::pumpReply(0));
        QTRY_COMPARE(reader.pumpStatusDetails().value("398").toString(), QString("001200"));
        reader.closePort();
    }
    void slowPumpDoesNotKeepMainBoardReadingsFresh() {
        test::FakeSharedBus transport; transport.pumpDelayMs = 1200;
        Rs485Instrument reader(&transport, nullptr);
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_VERIFY(reader.health().connected);
        QTRY_VERIFY_WITH_TIMEOUT(!reader.health().connected, 5400);
        QVERIFY(std::isnan(reader.telemetry().carrierGasPressureTorr)); QVERIFY(reader.portOpen());
        QTRY_VERIFY_WITH_TIMEOUT(reader.health().connected, 2000);
        QVERIFY(!transport.overlap);
    }
    void mainTimeoutAndWriteFailureCloseSharedPort() {
        test::FakeSharedBus transport; transport.mainRespond = false;
        Rs485Instrument reader(&transport, nullptr);
        QVERIFY(reader.openPort("TEST_ONLY", true));
        QTRY_VERIFY_WITH_TIMEOUT(!reader.portOpen(), 2000);
        QVERIFY(!reader.pumpStatusDetails().contains("398"));
        QCOMPARE(transport.writes.size(), 1);
        transport.mainRespond = true; transport.failWrite = true;
        QVERIFY(reader.openPort("TEST_ONLY", true)); QTRY_VERIFY(!reader.portOpen());
    }
};
QTEST_GUILESS_MAIN(PumpTests)
#include "PumpTests.moc"
