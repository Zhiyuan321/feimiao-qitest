#include "device/Rs485Instrument.h"
#include "domain/DisplayLabels.h"
#include <QtTest>
#include "Rs485TestDevice.h"
#include <cmath>

using namespace qitest;
using namespace qitest::test;

class Rs485Tests final : public QObject {
    Q_OBJECT
private slots:
    void documentedQueryAndStateOffsets() {
        QCOMPARE(Rs485Protocol::statusQuery(), QByteArray::fromHex("558830000101aa"));
        const auto bytes = statusPayload();
        QCOMPARE(bytes.size(), 23);
        Rs485Status state;
        QVERIFY(Rs485Protocol::decodeStatus(bytes, &state));
        QCOMPARE(state.highVoltageV, quint16(3200));
        QCOMPARE(state.highVoltageCurrentUa, quint16(123));
        QCOMPARE(state.vacuumGaugeMv, quint16(1200));
        QCOMPARE(state.gasPressureTorr, quint16(801));
        QCOMPARE(state.tdTemperatureC, 245.6);
        QCOMPARE(state.trapTemperatureC, 85.3);
        QCOMPARE(state.efcMlMin, 28.4);
        QCOMPARE(state.gasPumpPwmPercent, quint16(42));
        QVERIFY(state.observationLightOn); QVERIFY(!state.heatingOn);
        QVERIFY(!state.externalCarrierGas); QVERIFY(state.diaphragmPumpOn);
        auto external = bytes; external[3] = char(0xee);
        QVERIFY(Rs485Protocol::decodeStatus(external, &state));
        QVERIFY(state.externalCarrierGas);
    }
    void splitCoalescedAndEmbeddedDelimiters() {
        auto payload = statusPayload(); payload[13] = char(0x55); payload[14] = char(0xaa);
        const auto wire = frame(payload);
        for (int split = 0; split <= wire.size(); ++split) {
            Rs485Protocol codec;
            auto frames = codec.feed(wire.left(split));
            frames += codec.feed(wire.mid(split));
            QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].payload, payload);
            QCOMPARE(codec.bufferedBytes(), 0);
        }
        Rs485Protocol codec;
        const auto frames = codec.feed(Rs485Protocol::statusQuery() + wire + wire);
        QCOMPARE(frames.size(), 3); // Echo can be framed, but is not a status payload.
        Rs485Status status;
        QVERIFY(!Rs485Protocol::decodeStatus(frames[0].payload, &status));
    }
    void malformedFramesAndInvalidStatusNeverBecomeReadings() {
        Rs485Protocol codec;
        auto badTail = frame(statusPayload()); badTail[badTail.size()-1] = 0;
        const auto frames = codec.feed(QByteArray::fromHex("0011559900558830ffff")
            + badTail + frame(statusPayload()));
        QCOMPARE(frames.size(), 1);
        codec.feed(QByteArray::fromHex("5588300400") + QByteArray(800, 'x'));
        QVERIFY(codec.bufferedBytes() <= 1030);
        codec.reset(); QCOMPARE(codec.bufferedBytes(), 0);
        Rs485Status state; state.highVoltageV = 777;
        auto data = statusPayload(); data[0] = 0x01;
        QVERIFY(!Rs485Protocol::decodeStatus(data, &state));
        QCOMPARE(state.highVoltageV, quint16(777));
        QVERIFY(!Rs485Protocol::decodeStatus(statusPayload().left(22), &state));
        QVERIFY(!Rs485Protocol::decodeStatus(statusPayload() + 'x', &state));
        data = statusPayload(); data[21] = 0; data[22] = 101;
        QVERIFY(!Rs485Protocol::decodeStatus(data, &state));
    }
    void adapterReadsOnlyAndKeepsUnsupportedValuesUnknown() {
        FakeSerial device; device.reply = Rs485Protocol::statusQuery() + frame(statusPayload());
        Rs485Instrument adapter(&device, nullptr);
        QVERIFY(adapter.openPort("fixture"));
        QVERIFY(!adapter.health().connected);
        QTRY_VERIFY_WITH_TIMEOUT(adapter.health().connected, 1000);
        QVERIFY(!adapter.descriptor().simulation); QVERIFY(!adapter.health().ready);
        QCOMPARE(adapter.telemetry().carrierGasMode, QString("内载气"));
        QCOMPARE(adapter.telemetry().carrierGasFlowMlMin, 28.4);
        QVERIFY(std::isnan(adapter.telemetry().molecularPumpRpm));
        QVERIFY(std::isnan(adapter.health().vacuumMbar));
        QCOMPARE(adapter.health().ionSourceKv, 0.32);
        QCOMPARE(adapter.telemetry().ionSourceVoltageV, 320.0);
        QCOMPARE(measurementText(adapter.telemetry().multiplierVoltageV), QString("未提供"));
        QCOMPARE(measurementText(5.03e-5, 'f', 2), QString("5.03E-05"));
        QCOMPARE(measurementText(1.0e20, 'f', 1), QString("1.0E+20"));
        QVERIFY(!adapter.confirmedSettings().contains("trapTemperatureC"));
        QVERIFY(!adapter.confirmedSettings().contains("internalCarrierGasOn"));
        QCOMPARE(adapter.statusDetails().value("highVoltageCurrentUa").toUInt(), 123u);
        QVERIFY(!adapter.validateSetting("powerOn", true).allowed);
        const int before = device.writes.size();
        QSignalSpy rejected(&adapter, &IInstrumentAdapter::settingFinished);
        adapter.requestSetting("r", "powerOn", true);
        QCOMPARE(rejected.size(), 1); QVERIFY(!rejected[0][2].toBool());
        QCOMPARE(device.writes.size(), before);
        QVERIFY(adapter.acquireSpectrum().isEmpty());
        for (const auto &request : device.writes) QCOMPARE(request, Rs485Protocol::statusQuery());
        adapter.closePort();
        QVERIFY(!adapter.health().connected); QVERIFY(!adapter.portOpen());
        QVERIFY(adapter.confirmedSettings().isEmpty());
        QVERIFY(!adapter.statusDetails().contains("highVoltageV"));
        QVERIFY(std::isnan(adapter.telemetry().tdTemperatureC));
        QVERIFY(std::isnan(adapter.health().ionSourceKv));
        QVERIFY(std::isnan(adapter.telemetry().ionSourceVoltageV));
    }
    void confirmedIonSourceVoltageAndZero() {
        FakeSerial device;
        auto payload = statusPayload(); payload[7] = 0; payload[8] = 49;
        device.reply = frame(payload);
        Rs485Instrument adapter(&device, nullptr);
        QVERIFY(adapter.openPort("fixture"));
        QVERIFY(std::isnan(adapter.health().ionSourceKv));
        QTRY_COMPARE(adapter.health().ionSourceKv, 0.0049);
        QCOMPARE(adapter.telemetry().ionSourceVoltageV, 4.9);
        QCOMPARE(adapter.statusDetails().value("highVoltageV").toUInt(), 49u);
        QCOMPARE(adapter.statusDetails().value("ionSourceKv").toDouble(), 0.0049);
        QCOMPARE(adapter.statusDetails().value("ionSourceVoltageV").toDouble(), 4.9);
        payload[8] = 0; device.reply = frame(payload);
        QTRY_COMPARE_WITH_TIMEOUT(adapter.health().ionSourceKv, 0.0, 1500);
        QCOMPARE(adapter.telemetry().ionSourceVoltageV, 0.0);
        adapter.closePort();
        QVERIFY(std::isnan(adapter.health().ionSourceKv));
        QVERIFY(std::isnan(adapter.telemetry().ionSourceVoltageV));
    }
    void timeoutInvalidatesOldStateAndRejectsLateReply() {
        FakeSerial device; device.reply = frame(statusPayload());
        Rs485Instrument adapter(&device, nullptr);
        QVERIFY(adapter.openPort("fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(adapter.health().connected, 1000);
        device.reply.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.portOpen(), 3500);
        QVERIFY(!adapter.health().connected);
        QVERIFY(std::isnan(adapter.telemetry().tdTemperatureC));
        QVERIFY(std::isnan(adapter.health().ionSourceKv));
        QVERIFY(std::isnan(adapter.telemetry().ionSourceVoltageV));
        device.deliver(frame(statusPayload()));
        QVERIFY(!adapter.health().connected);
        QVERIFY(adapter.statusDetails().value("message").toString().contains("超时"));
        device.input.clear(); device.reply = frame(statusPayload());
        QVERIFY(adapter.openPort("fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(adapter.health().connected, 1000);
    }
    void echoForeignReplyAndWriteFailureCannotConnect() {
        FakeSerial device;
        device.reply = Rs485Protocol::statusQuery() + frame(statusPayload(), 0x01);
        Rs485Instrument adapter(&device, nullptr);
        QVERIFY(adapter.openPort("fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(!device.writes.isEmpty(), 500);
        QTest::qWait(20);
        QVERIFY(!adapter.health().connected);
        adapter.closePort(); device.failWrite = true; device.reply.clear();
        QVERIFY(adapter.openPort("fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.portOpen(), 500);
        QVERIFY(!adapter.health().connected);
    }
};
QTEST_GUILESS_MAIN(Rs485Tests)
#include "Rs485Tests.moc"
