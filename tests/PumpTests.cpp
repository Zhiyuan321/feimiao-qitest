#include "device/Rs485Instrument.h"
#include "device/StartupSequence.h"
#include <cmath>
#include <limits>
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
    void zeroCurrentTimeoutKeepsConnectionAndPendingReply() {
        test::FakeSerial port;QElapsedTimer sinceStart;
        bool delayed=false,inFlight=false,overlap=false,currentOn=false;
        port.responder=[&](const QByteArray &r) {
            if(inFlight)overlap=true;
            if(r==PumpProtocol::powerCommand(true)) {sinceStart.start();return r;} // Echo cannot confirm startup.
            if(r==Rs485Protocol::statusQuery()) {
                const auto reply=test::frame(test::statusPayload());
                if(sinceStart.isValid() && sinceStart.elapsed()>9200 && !delayed) {
                    delayed=true;inFlight=true;
                    QTimer::singleShot(1000,&port,[&,reply]{inFlight=false;if(port.isOpen())port.deliver(reply.mid(8));});
                    return reply.left(8);
                }
                return reply;
            }
            for(int i=0;i<4;++i)if(r==PumpProtocol::query(i)) {
                auto reply=test::pumpReply(i);if(i==1 && !currentOn)reply.replace(10,6,"000000");return reply;
            }
            return QByteArray();
        };
        QTemporaryDir diagnostics;qputenv("QITEST_DIAGNOSTICS_DIR",diagnostics.path().toUtf8());
        Rs485Instrument adapter(&port,nullptr);QVERIFY(adapter.openPort("TEST_ONLY",true));
        QTRY_VERIFY(adapter.health().connected);QSignalSpy done(&adapter,&IInstrumentAdapter::settingFinished);
        QElapsedTimer deadline;deadline.start();
        adapter.requestSetting("zero-current","molecularPumpOn",true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,11500);
        QVERIFY(deadline.elapsed()>=10000);
        QVERIFY(!done[0][2].toBool());QVERIFY(done[0][4].toString().contains("电流仍为0"));
        QVERIFY(adapter.portOpen());QVERIFY(adapter.health().connected);
        QVERIFY(adapter.statusDetails()["lastFailure"].toMap()["pendingQuery"].toBool());
        QFile saved(adapter.statusDetails()["diagnosticPath"].toString());QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto snapshot=QJsonDocument::fromJson(saved.readAll()).object();
        QVERIFY(snapshot["busStatus"].toObject()["connected"].toBool());
        QCOMPARE(snapshot["pumpStatus"].toObject()["molecularPumpCurrentA"].toDouble(),0.0);
        QVERIFY(inFlight);QTRY_VERIFY(!inFlight);QTest::qWait(300);QVERIFY(!overlap);
        QVERIFY(adapter.portOpen());QVERIFY(adapter.health().connected);
        currentOn=true;QTRY_VERIFY(adapter.confirmedSettings().value("molecularPumpOn").toBool());
        QCOMPARE(done.size(),1); // A later live current must not revive the failed operation.
        QCOMPARE(port.writes.count(PumpProtocol::powerCommand(true)),1);
        QVERIFY(adapter.pumpStatusDetails().contains("molecularPumpRpm"));
        qunsetenv("QITEST_DIAGNOSTICS_DIR");
    }
    void powerTrafficDrainsBeforeNextQuery_data() {
        QTest::addColumn<bool>("turnOn");QTest::newRow("start")<<true;QTest::newRow("stop")<<false;
    }
    void powerTrafficDrainsBeforeNextQuery() {
        QFETCH(bool,turnOn);test::FakeSerial port;QElapsedTimer elapsed;bool sent=false,overlap=false;
        port.responder=[&](const QByteArray &r) {
            if(r==PumpProtocol::powerCommand(turnOn)) {
                sent=true;elapsed.start();
                QTimer::singleShot(50,&port,[&]{port.deliver("00110");});
                QTimer::singleShot(90,&port,[&]{port.deliver("01006000000009\r");});
                return QByteArray();
            }
            if(sent && elapsed.elapsed()<270) {overlap=true;return QByteArray::fromHex("1333536afe");}
            if(r==Rs485Protocol::statusQuery())return test::frame(test::statusPayload());
            for(int i=0;i<4;++i)if(r==PumpProtocol::query(i)) {
                auto reply=test::pumpReply(i);
                if(i==1)reply.replace(10,6,sent && turnOn?"000080":"000000");
                if(i==0 && sent && !turnOn)reply.replace(10,6,"000000");
                return reply;
            }
            return QByteArray();
        };
        Rs485Instrument adapter(&port,nullptr);QVERIFY(adapter.openPort("TEST_ONLY",true));
        QTRY_VERIFY(adapter.health().connected);QSignalSpy done(&adapter,&IInstrumentAdapter::settingFinished);
        if(turnOn)adapter.requestSetting("power","molecularPumpOn",true);else adapter.requestPumpShutdown("power");
        QTRY_COMPARE(done.size(),1);QVERIFY(done[0][2].toBool());QVERIFY(!overlap);QVERIFY(adapter.portOpen());
        QCOMPARE(port.writes.count(PumpProtocol::powerCommand(turnOn)),1);
    }
    void powerUsesFreshCurrentAndNeverResends() {
        test::FakeSerial port; bool hold=false; QByteArray current="000080";
        port.responder=[&](const QByteArray &r) {
            if(r==Rs485Protocol::statusQuery()) return test::frame(test::statusPayload());
            for(int i=0;i<4;++i) if(r==PumpProtocol::query(i)) {
                if(i==1 && hold) return QByteArray();
                auto reply=test::pumpReply(i); if(i==1) reply.replace(10,6,current); return reply;
            }
            return QByteArray(); // Power commands have no dedicated ACK.
        };
        Rs485Instrument adapter(&port,nullptr); QVERIFY(adapter.openPort("TEST_ONLY",true));
        QTRY_VERIFY(adapter.confirmedSettings().value("molecularPumpOn").toBool());
        QSignalSpy done(&adapter,&IInstrumentAdapter::settingFinished);
        hold=true;
        adapter.requestSetting("on","molecularPumpOn",true);
        QTRY_VERIFY(port.writes.contains(PumpProtocol::powerCommand(true)));
        QTest::qWait(100); QVERIFY(done.isEmpty());
        QVERIFY(!adapter.confirmedSettings().contains("molecularPumpOn"));
        port.deliver(PumpProtocol::powerCommand(true)+test::pumpReply(0));
        QVERIFY(done.isEmpty()); // An echo and a speed reply cannot confirm current.
        hold=false; port.deliver(test::pumpReply(1));
        QTRY_COMPARE(done.size(),1); QVERIFY(done[0][2].toBool());
        QCOMPARE(port.writes.count(PumpProtocol::powerCommand(true)),1);
        current="000000";
        adapter.requestSetting("off","molecularPumpOn",false);
        QTRY_COMPARE(done.size(),2); QVERIFY(done[1][2].toBool());
        QCOMPARE(done[1][3].toBool(),false);
        QCOMPARE(port.writes.count(PumpProtocol::powerCommand(false)),1);
        QVERIFY(!adapter.confirmedSettings().value("molecularPumpOn").toBool());
    }
    void missingCurrentFailsAndLateReplyCannotConfirm() {
        test::FakeSerial port;
        port.responder=[](const QByteArray &r) { return r==Rs485Protocol::statusQuery()
            ?test::frame(test::statusPayload()):QByteArray(); };
        Rs485Instrument adapter(&port,nullptr); QVERIFY(adapter.openPort("TEST_ONLY",true));
        QTRY_VERIFY(adapter.health().connected);
        QSignalSpy done(&adapter,&IInstrumentAdapter::settingFinished);
        adapter.requestSetting("on","molecularPumpOn",true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2500);
        QVERIFY(!done[0][2].toBool()); QVERIFY(!adapter.portOpen());
        port.deliver(test::pumpReply(1)); QCOMPARE(done.size(),1);
        QVERIFY(!adapter.confirmedSettings().contains("molecularPumpOn"));
        QCOMPARE(port.writes.count(PumpProtocol::powerCommand(true)),1);
    }
    void startupSequenceRequiresConfirmedStagesAndFreshVacuum() {
        StartupSequence sequence;
        using Stage = StartupSequence::Stage;
        QVERIFY(sequence.begin());
        sequence.observeCarrierFlow(1.0, true);
        QCOMPARE(sequence.pendingActions().size(), 2);
        QCOMPARE(sequence.pendingActions()[1].value.toInt(), 250);
        QVERIFY(!sequence.begin());
        sequence.observeVacuum(1e-5, true);
        QCOMPARE(sequence.stage(), Stage::Preparing);
        sequence.confirmed("diaphragmPumpOn", true, true);
        sequence.confirmed("tdTemperatureC", 250, true);
        QCOMPARE(sequence.stage(), Stage::WaitingForRoughVacuum);
        sequence.observeVacuum(8.0, true); // Equality must not start the molecular pump.
        QCOMPARE(sequence.stage(), Stage::WaitingForRoughVacuum);
        sequence.observeVacuum(7.99, true);
        QCOMPARE(sequence.stage(), Stage::StartingPump);
        QCOMPARE(sequence.pendingActions().first().key, QString("molecularPumpOn"));
        sequence.observeVacuum(1e-5, true);
        sequence.confirmed("tdTemperatureC", 250, true);
        QCOMPARE(sequence.stage(), Stage::StartingPump); // Pressure/old ACK is insufficient.
        sequence.confirmed("molecularPumpOn", true, true);
        QCOMPARE(sequence.stage(), Stage::WaitingForVacuum);
        QVERIFY(sequence.pendingActions().isEmpty());
        sequence.observeVacuum(1e-2, true);
        QCOMPARE(sequence.stage(), Stage::WaitingForVacuum);
        sequence.observeVacuum(9.99e-3, true);
        QCOMPARE(sequence.stage(), Stage::StartingTrapHeating);
        QCOMPARE(sequence.pendingActions().first().value.toInt(), 85);
        sequence.confirmed("trapTemperatureC", 85, true);
        QCOMPARE(sequence.stage(), Stage::Complete);
    }
    void carrierFlowChangesVacuumCriteriaIncludingAfterCompletion() {
        using Stage = StartupSequence::Stage;
        StartupSequence sequence; QVERIFY(sequence.begin());
        sequence.confirmed("diaphragmPumpOn", true, true);
        sequence.confirmed("tdTemperatureC", 250, true);
        sequence.observeVacuum(5e-4, true);
        sequence.confirmed("molecularPumpOn", true, true);
        sequence.observeVacuum(5e-5, true);
        QCOMPARE(sequence.stage(), Stage::WaitingForVacuum); // Flow is still unknown.
        sequence.observeCarrierFlow(0, false);
        QVERIFY(!sequence.vacuumReady());
        sequence.observeVacuum(1e-4, true);
        sequence.observeCarrierFlow(0, true);
        QCOMPARE(sequence.stage(), Stage::WaitingForVacuum); // E-04 fails with no gas.
        sequence.observeVacuum(9.99e-5, true);
        QCOMPARE(sequence.stage(), Stage::StartingTrapHeating);
        QVERIFY(sequence.vacuumReady());
        sequence.confirmed("trapTemperatureC", 85, true);
        QCOMPARE(sequence.stage(), Stage::Complete);
        sequence.observeCarrierFlow(0.1, true); // Gas enabled after startup completion.
        sequence.observeVacuum(9.99e-3, true);
        QVERIFY(sequence.vacuumReady());
        QVERIFY(sequence.pendingActions().isEmpty()); // Never repeat the heating command.
        sequence.observeVacuum(1e-2, true); QVERIFY(!sequence.vacuumReady());
        sequence.observeVacuum(5e-3, true); QVERIFY(sequence.vacuumReady());
        sequence.observeCarrierFlow(0, true); QVERIFY(!sequence.vacuumReady());
        sequence.observeVacuum(5e-5, true); QVERIFY(sequence.vacuumReady());
        sequence.observeCarrierFlow(-1, true); QVERIFY(!sequence.vacuumReady());
        sequence.observeCarrierFlow(std::numeric_limits<double>::quiet_NaN(), true);
        QVERIFY(!sequence.vacuumReady());
        sequence.observeCarrierFlow(1, true); QVERIFY(sequence.vacuumReady());
        sequence.observeVacuum(5e-5, false); QVERIFY(!sequence.vacuumReady());

        StartupSequence flowing; QVERIFY(flowing.begin());
        flowing.confirmed("diaphragmPumpOn", true, true);
        flowing.confirmed("tdTemperatureC", 250, true);
        flowing.observeVacuum(5e-4, true);
        flowing.confirmed("molecularPumpOn", true, true);
        flowing.observeCarrierFlow(0, true);
        flowing.observeVacuum(5e-3, true);
        QCOMPARE(flowing.stage(), Stage::WaitingForVacuum);
        flowing.observeCarrierFlow(1, true); // Gas enabled while waiting for E-05.
        QCOMPARE(flowing.stage(), Stage::StartingTrapHeating);
    }
    void molecularPumpEightMbarBoundary() {
        using Stage=StartupSequence::Stage;
        for(double p:{std::nextafter(8.0,0.0),7.99,1.0,5e-4,5e-5}) {
            StartupSequence sequence;QVERIFY(sequence.begin());
            sequence.confirmed("diaphragmPumpOn",true,true);
            sequence.confirmed("tdTemperatureC",250,true);
            sequence.observeVacuum(p,true);
            QCOMPARE(sequence.stage(),Stage::StartingPump);
        }
        for(double p:{8.0,std::nextafter(8.0,9.0),8.01,10.0,1e4,0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity()})
            QVERIFY(!StartupSequence::molecularPumpStartPressureAllowed(p));
        for(double p:{8.0,8.01,10.0,1e4}) {
            StartupSequence sequence;QVERIFY(sequence.begin());
            sequence.confirmed("diaphragmPumpOn",true,true);
            sequence.confirmed("tdTemperatureC",250,true);
            sequence.observeVacuum(p,true);
            QCOMPARE(sequence.stage(),Stage::WaitingForRoughVacuum);
            QVERIFY(sequence.pendingActions().isEmpty());
        }
    }
    void startupFailureNeverContinuesHeating() {
        using Stage = StartupSequence::Stage;
        for (double pressure : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
            StartupSequence sequence; QVERIFY(sequence.begin());
            sequence.observeVacuum(pressure, true);
            QCOMPARE(sequence.stage(), Stage::Failed);
            sequence.confirmed("tdTemperatureC", 250, true);
            sequence.observeVacuum(1e-5, true);
            QVERIFY(sequence.pendingActions().isEmpty());
        }
        StartupSequence stale; QVERIFY(stale.begin());
        stale.observeVacuum(1e-5, false); QCOMPARE(stale.stage(), Stage::Failed);
        StartupSequence rejected; QVERIFY(rejected.begin());
        rejected.confirmed("diaphragmPumpOn", true, false);
        QCOMPARE(rejected.stage(), Stage::Failed);
        StartupSequence mismatch; QVERIFY(mismatch.begin());
        mismatch.confirmed("tdTemperatureC", 200, true);
        QCOMPARE(mismatch.stage(), Stage::Failed);
    }
    void literalPowerCommandsAreNotQueryReplies() {
        QCOMPARE(PumpProtocol::powerCommand(true).toHex(), QByteArray("303031313030313030363131313131313031350d"));
        QCOMPARE(PumpProtocol::powerCommand(false).toHex(), QByteArray("303031313030313030363030303030303030390d"));
        PumpReply reply;
        QVERIFY(!PumpProtocol::extract(PumpProtocol::powerCommand(true), &reply));
        QVERIFY(!PumpProtocol::extract(PumpProtocol::powerCommand(false), &reply));
    }
    void highRawVoltageKeepsSharedPollingConnected() {
        test::FakeSharedBus transport;
        transport.mainPayload[7]=char(0x94);transport.mainPayload[8]=char(0x01);
        Rs485Instrument reader(&transport,nullptr);
        QVERIFY(reader.openPort("TEST_ONLY",true));
        QTRY_VERIFY_WITH_TIMEOUT(transport.writes.size()>=11,3500);
        QVERIFY(reader.health().connected);QVERIFY(reader.portOpen());
        QCOMPARE(reader.statusDetails().value("highVoltageV").toUInt(),37889u);
        QCOMPARE(reader.telemetry().ionSourceVoltageV,3788.9);
        QCOMPARE(reader.pumpStatusDetails().value("326").toString(),QString("000045"));
        for(int i=0;i<transport.writes.size();++i)
            QCOMPARE(transport.writes[i],i%5==0?Rs485Protocol::statusQuery():PumpProtocol::query(i%5-1));
        QCOMPARE(transport.openCount,1);QCOMPARE(transport.closeCount,0);QVERIFY(!transport.overlap);
        reader.closePort();
    }
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
