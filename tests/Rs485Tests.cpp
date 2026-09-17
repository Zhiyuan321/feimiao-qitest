#include "device/Rs485Instrument.h"
#include "domain/DisplayLabels.h"
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include "Rs485TestDevice.h"
#include "core/MethodDraft.h"
#include <cmath>

using namespace qitest;
using namespace qitest::test;

class Rs485Tests final : public QObject {
    Q_OBJECT
private slots:
    void highVoltageRawUsesFullUnsignedWord() {
        for (quint16 raw : {0, 5000, 5001, 37887, 37888, 37889, 65535}) {
            auto data=statusPayload();data[7]=char(raw>>8);data[8]=char(raw&0xff);
            Rs485Status decoded;
            QVERIFY(Rs485Protocol::decodeStatus(data,&decoded));
            QCOMPARE(decoded.highVoltageV,raw);
            QCOMPARE(decoded.trapTemperatureC,85.3);
            // Relaxing the raw-HV limit must not disable unrelated checks.
            auto bad=data;bad[9]=char(0x03);bad[10]=char(0xe9);
            QVERIFY(!Rs485Protocol::decodeStatus(bad,&decoded));
            bad=data;bad[11]=char(0x0c);bad[12]=char(0xe5);
            QVERIFY(!Rs485Protocol::decodeStatus(bad,&decoded));
        }
    }
    void capturedStatusChunksAndRepeatedPolling() {
        const auto dir=qEnvironmentVariable("QITEST_RS485_CAPTURE_DIR");
        if(dir.isEmpty())QSKIP("Optional captures stay outside the repository");
        for(const auto &name:QStringList{"old","new"}) {
            QFile input(dir+"/"+name+"-events.json");QVERIFY(input.open(QIODevice::ReadOnly));
            const auto doc=QJsonDocument::fromJson(input.readAll());QVERIFY(doc.isArray());
            Rs485Protocol decoder;QVector<QByteArray> replies;
            for(const auto &event:doc.array()) {
                const auto row=event.toObject();
                if(row["kind"].toInt()!=3 || row["phase"].toInt()!=1)continue;
                for(const auto &decoded:decoder.feed(QByteArray::fromHex(row["hex"].toString().toLatin1()))) {
                    if(decoded.command!=0x30)continue;
                    Rs485Status status;QVERIFY(Rs485Protocol::decodeStatus(decoded.payload,&status));
                    const auto raw=(quint16(quint8(decoded.payload[7]))<<8)|quint8(decoded.payload[8]);
                    QCOMPARE(status.highVoltageV,quint16(raw));
                    replies.append(frame(decoded.payload));
                }
            }
            QVERIFY(!replies.isEmpty());
            FakeSerial device;int delivered=0;
            device.responder=[&](const QByteArray &request) {
                if(request!=Rs485Protocol::statusQuery())return QByteArray{};
                return replies[(delivered++)%replies.size()];
            };
            Rs485Instrument adapter(&device,nullptr);QVERIFY(adapter.openPort("CAPTURE_REPLAY"));
            QTRY_VERIFY(adapter.health().connected);
            // Reach a third query beyond the former 1.5s disconnect deadline.
            QTRY_VERIFY_WITH_TIMEOUT(delivered>=3,3500);
            QVERIFY(adapter.health().connected);QVERIFY(adapter.portOpen());
            qInfo()<<name<<replies.size()<<"captured status frames accepted; repeated polling stays connected";
            adapter.closePort();
        }
    }
    void documentedQueryAndStateOffsets() {
        QCOMPARE(Rs485Protocol::statusQuery(), QByteArray::fromHex("558830000101aa"));
        QCOMPARE(Rs485Protocol::controlCommand(0x02,QByteArray::fromHex("0038")),
                 QByteArray::fromHex("55880200020038aa"));
        QCOMPARE(Rs485Protocol::controlCommand(0x12,QByteArray::fromHex("03e8")),
                 QByteArray::fromHex("558812000203e8aa"));
        bool acknowledged=false;
        QVERIFY(Rs485Protocol::decodeAcknowledgement({0x02,QByteArray::fromHex("1100")},0x02,&acknowledged));
        QVERIFY(acknowledged);
        QVERIFY(Rs485Protocol::decodeAcknowledgement({0x02,QByteArray::fromHex("12")},0x02,&acknowledged));
        QVERIFY(!acknowledged);
        QVERIFY(!Rs485Protocol::decodeAcknowledgement({0x13,QByteArray::fromHex("11")},0x02,&acknowledged));
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
    void basicMethodStopsAtFirstRejectedAcknowledgement() {
        FakeSerial device;
        device.responder=[](const QByteArray &request){
            if(request.size()<3)return QByteArray{};const quint8 command=quint8(request[2]);
            if(command==0x30)return frame(statusPayload());
            return frame(QByteArray::fromHex(command==0x13?"1200":"1100"),command);
        };
        Rs485Instrument adapter(&device,nullptr);QVERIFY(adapter.openPort("fixture"));
        QTRY_VERIFY(adapter.health().connected);
        const auto parameters=MethodDraft::defaultParameters();
        QVERIFY(adapter.validateBasicMethodParameters(parameters).allowed);
        QSignalSpy finished(&adapter,&Rs485Instrument::basicMethodParametersFinished);QString error;
        QVERIFY(adapter.requestBasicMethodParameters("basic-1",parameters,&error));
        QTRY_COMPARE(finished.size(),1);QVERIFY(!finished[0][1].toBool());
        QVector<int> controls;for(const auto &wire:device.writes)if(wire.size()>2&&quint8(wire[2])!=0x30)controls<<quint8(wire[2]);
        QCOMPARE(controls,QVector<int>({0x02,0x13}));
        auto unsafe=parameters;unsafe.insert("source",1);QVERIFY(!adapter.validateBasicMethodParameters(unsafe).allowed);
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
