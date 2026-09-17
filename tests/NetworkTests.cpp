#include "device/NetworkInstrument.h"
#include "NetworkTestFrames.h"
#include "Rs485TestDevice.h"
#include "core/MethodDraft.h"
#include "core/ChromatogramEngine.h"
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
    void legacyCommandsAndFixedMethodFields() {
        QCOMPARE(NetworkProtocol::heartbeatCommand(),QByteArray::fromHex("5510300003010123fc1eaa"));
        QCOMPARE(NetworkProtocol::legacyMethodFollowupCommand(),QByteArray::fromHex("55105000030101008556aa"));
        const auto wire=NetworkProtocol::fullscanMethodCommand(MethodDraft::defaultParameters());
        const int expected[]{1,1,10000,50,3000,92,122,892,5000,325,549,579,590,380,0,
            20,20,500,500,500,1,3000,1000,30,50,10,100,100,0,0,802,446,207,651,674,0,0,9,1000,600,500,219,10,10,10};
        int offset=7;
        for(int i=0;i<45;++i) {
            int actual=quint8(wire[offset++]);
            if(i!=0 && i!=1 && i!=14 && i!=20) actual=(actual<<8)|quint8(wire[offset++]);
            QCOMPARE(actual,expected[i]);
        }
        QCOMPARE(offset,93);
    }
    void legacyMethodFollowupsRequireAllAcks_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("success")<<0;
        QTest::newRow("reject-second")<<1;
        QTest::newRow("timeout-second")<<2;
        QTest::newRow("disconnect-before-followup")<<3;
    }
    void legacyMethodFollowupsRequireAllAcks() {
        QFETCH(int,mode);
        test::FakeSerial port;
        port.responder=[](const QByteArray &r){const quint8 c=quint8(r[2]);return c==0x30
            ?test::frame(test::statusPayload()):test::frame(QByteArray::fromHex("1100"),c);};
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1",0,30000));QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails()["port"].toUInt());
        QTRY_VERIFY(adapter.statusDetails()["tcpConnected"].toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;client.write(test::networkFrame(status));
        QTRY_VERIFY(adapter.statusDetails()["connected"].toBool());
        QSignalSpy done(&adapter,&IInstrumentAdapter::methodParametersFinished);
        adapter.requestMethodParameters("legacy",MethodDraft::defaultParameters());
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        if(mode==3) {
            client.disconnectFromHost();QTRY_COMPARE(done.size(),1);QVERIFY(!done[0][1].toBool());
            QTest::qWait(200);QCOMPARE(adapter.statusDetails()["methodFollowupsSent"].toInt(),0);return;
        }
        for(int i=0;i<3;++i) {
            QTRY_VERIFY(client.bytesAvailable()>0);
            QCOMPARE(client.readAll(),QByteArray::fromHex("55105000030101008556aa"));
            QVERIFY(done.isEmpty());QVERIFY(adapter.confirmedMethodParameters().isEmpty());
            QVERIFY(!adapter.statusDetails()["heartbeatActive"].toBool());
            QString error;QVERIFY(!adapter.startAcquisition(1,&error));
            if(i==1 && mode!=0) {
                if(mode==1) client.write(test::networkFrame(QByteArray::fromHex("12"),0x10,0x50));
                QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,4000);QVERIFY(!done[0][1].toBool());
                QTRY_VERIFY(!adapter.statusDetails()["tcpConnected"].toBool());
                QCOMPARE(adapter.statusDetails()["methodFollowupsSent"].toInt(),2);return;
            }
            // Unrelated/repeated method ACK must not complete the followup stage.
            client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x30)
                +test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
            QTest::qWait(30);QVERIFY(done.isEmpty());QCOMPARE(client.bytesAvailable(),qint64(0));
            client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x50));
        }
        QTRY_COMPARE(done.size(),1);QVERIFY(done[0][1].toBool());
        QCOMPARE(adapter.statusDetails()["methodFollowupsSent"].toInt(),3);
        QVERIFY(adapter.statusDetails()["heartbeatActive"].toBool());
    }
    void stopRetriesOnceWithoutExtendingDeadline_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("first-ack-cancels-retry")<<0;
        QTest::newRow("second-stop-succeeds")<<1;
        QTest::newRow("both-stops-unanswered")<<2;
        QTest::newRow("stop-rejected")<<3;
        QTest::newRow("disconnect-cancels-retry")<<4;
    }
    void stopRetriesOnceWithoutExtendingDeadline() {
        QFETCH(int,mode);
        test::FakeSerial port;
        port.responder=[](const QByteArray &r){const quint8 c=quint8(r[2]);return c==0x30
            ?test::frame(test::statusPayload()):test::frame(QByteArray::fromHex("1100"),c);};
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1",0,30000));QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails()["port"].toUInt());
        QTRY_VERIFY(adapter.statusDetails()["tcpConnected"].toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;client.write(test::networkFrame(status));
        QTRY_VERIFY(adapter.statusDetails()["connected"].toBool());
        adapter.requestMethodParameters("retry",MethodDraft::defaultParameters());
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_VERIFY(adapter.statusDetails()["methodConfirmed"].toBool());
        QSignalSpy done(&adapter,&NetworkInstrument::acquisitionFinished),started(&adapter,&NetworkInstrument::acquisitionStarted);
        QString error;QVERIFY(adapter.startAcquisition(1,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        const auto ack=test::networkFrame(QByteArray::fromHex("11"),0x10,0x15);
        client.write(ack);QTRY_COMPARE(started.size(),1);
        client.write(test::networkFrame(QByteArray(3250,0),0x20,0x81,0,0));
        QTRY_COMPARE(adapter.statusDetails()["acquiredScans"].toInt(),1);
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,1500);
        QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));QElapsedTimer elapsed;elapsed.start();
        if(mode==0) client.write(ack);
        else if(mode==3) client.write(test::networkFrame(QByteArray::fromHex("12"),0x10,0x15));
        else if(mode==4) client.disconnectFromHost();
        else {
            QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,800);
            QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
            QVERIFY(elapsed.elapsed()>=400);QVERIFY(done.isEmpty());
            QCOMPARE(adapter.statusDetails()["stopCommandsSent"].toInt(),2);
            if(mode==1) client.write(ack+test::networkFrame(status));
        }
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3500);
        QCOMPARE(done[0][0].toBool(),mode<2);
        if(mode==2) {QVERIFY(elapsed.elapsed()>=2700);QVERIFY(elapsed.elapsed()<3400);}
        QTest::qWait(600);
        QCOMPARE(adapter.statusDetails()["stopCommandsSent"].toInt(),(mode==1||mode==2)?2:1);
        if(mode==1) {
            QVERIFY(!adapter.startAcquisition(1,&error));QVERIFY(error.contains("收尾"));
            client.write(ack);QTest::qWait(30);QCOMPARE(started.size(),1);QCOMPARE(done.size(),1);
            QTRY_VERIFY_WITH_TIMEOUT(!adapter.statusDetails()["stopReplyGuardActive"].toBool(),3500);
            client.readAll();QVERIFY(adapter.startAcquisition(1,&error));
            QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(true));
            QCOMPARE(adapter.statusDetails()["stopCommandsSent"].toInt(),0);
            client.write(ack);QTRY_COMPARE(started.size(),2);
            client.write(test::networkFrame(QByteArray(3250,0),0x20,0x81,0,0));
            QTRY_COMPARE(adapter.statusDetails()["acquiredScans"].toInt(),1);
            QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,1500);
            QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));client.write(ack);
            QTRY_COMPARE(done.size(),2);QVERIFY(done[1][0].toBool());
        }
        if(mode>=2) QVERIFY(!adapter.statusDetails()["tcpConnected"].toBool());
    }
    void cycleCounterCrosses255AndRejectsDuplicateAndInvalidPressure() {
        test::FakeSerial port;
        port.responder=[](const QByteArray &r){const quint8 c=quint8(r[2]);return c==0x30
            ?test::frame(test::statusPayload()):test::frame(QByteArray::fromHex("1100"),c);};
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1",0,30000));QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails()["port"].toUInt());
        QTRY_VERIFY(adapter.statusDetails()["tcpConnected"].toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;client.write(test::networkFrame(status));
        QTRY_VERIFY(adapter.statusDetails()["connected"].toBool());
        const auto values=MethodDraft::defaultParameters();adapter.requestMethodParameters("cycles",values);
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_COMPARE(adapter.confirmedMethodParameters(),values);
        int scans=0;double lastTime=-1;
        connect(&adapter,&NetworkInstrument::acquisitionScan,this,[&](const SpectrumScan &scan){++scans;lastTime=scan.timeSeconds;});
        QSignalSpy done(&adapter,&NetworkInstrument::acquisitionFinished),started(&adapter,&NetworkInstrument::acquisitionStarted);
        QByteArray payload(3250,0);payload[1]=8;QString error;
        QVERIFY(adapter.startAcquisition(30,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(started.size(),1);
        QByteArray cycles;for(int i=0;i<=256;++i)cycles+=test::networkFrame(payload,0x20,0x81,i>>8,i&255);
        client.write(cycles);QTRY_COMPARE_WITH_TIMEOUT(scans,257,4000);QCOMPARE(lastTime,256.0);
        QVERIFY(adapter.statusDetails()["acquisitionError"].toString().isEmpty());client.readAll();
        client.write(test::networkFrame(payload,0x20,0x81,1,0)); // Duplicate cycle 256.
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(done.size(),1);
        QVERIFY(!done.last()[0].toBool());QVERIFY(done.last()[2].toString().contains("应为257，收到256"));
        const QVector<QByteArray> invalid{
            test::networkFrame(QByteArray(498,0),0x20,0x82,0,1), // Missing pressure cycle zero.
            test::networkFrame(payload.left(1000),0x20,0x81,0,0), // Wrong method point count.
            test::networkFrame(payload.left(3249),0x20,0x81,0,0)}; // Odd sample byte count.
        for(int i=0;i<invalid.size();++i) {
            QVERIFY(adapter.startAcquisition(30,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
            client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(started.size(),i+2);
            client.write(invalid[i]);QTRY_VERIFY(client.bytesAvailable()>0);
            QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
            client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(done.size(),i+2);
            QVERIFY(!done.last()[0].toBool());QCOMPARE(scans,257);
        }
    }
    void waveformCrcIsRecordedWhileControlCrcRemainsRequired() {
        QByteArray payload(3250,0);payload[1]=8;
        for(int cycle:{0,1,255,256,65535}) {
            NetworkProtocol decoder;
            const auto wire=test::networkFrame(payload,0x20,0x81,cycle>>8,cycle&255);
            QVERIFY(decoder.feed(wire.left(1460)).isEmpty());
            QVERIFY(decoder.feed(wire.mid(1460,1460)).isEmpty());
            const auto frames=decoder.feed(wire.mid(2920)+test::networkStatusWire());
            QCOMPARE(frames.size(),2);QCOMPARE(frames[0].cycleIndex(),quint16(cycle));
            QCOMPARE(frames[0].payload,payload);QCOMPARE(decoder.rejectedBytes(),quint64(0));
        }
        NetworkProtocol decoder;
        const auto maximum=test::networkFrame(QByteArray(NetworkProtocol::MaximumWaveformPayload,0),0x20,0x81,0,0);
        QCOMPARE(decoder.feed(maximum).size(),1);QCOMPARE(decoder.bufferedBytes(),0);
        decoder.feed(QByteArray::fromHex("552081ffff"));
        QCOMPARE(decoder.lastFeedRejection()["reason"].toString(),QString("length_out_of_range"));
        QVERIFY(decoder.bufferedBytes()<=NetworkProtocol::MaximumWaveformPayload+10);
        decoder.reset();const auto pressure=test::networkFrame(QByteArray(3250,0),0x20,0x82,1,0);
        const auto frames=decoder.feed(pressure);QCOMPARE(frames.size(),1);
        QVector<double> volts;QVERIFY(NetworkProtocol::decodePressure(frames[0],&volts));QCOMPARE(volts.size(),1625);
        auto bad=test::networkFrame(payload,0x20,0x81,0,0);bad[bad.size()-3]=0;bad[bad.size()-2]=0;
        const auto accepted=decoder.feed(bad);QCOMPARE(accepted.size(),1);
        QVERIFY(!accepted[0].crcRequired);QCOMPARE(accepted[0].receivedCrc,quint16(0));
        QVERIFY(accepted[0].calculatedCrc!=0);QCOMPARE(accepted[0].payload,payload);
        QVERIFY(decoder.lastFeedRejection().isEmpty());
        // Compatibility applies to both waveform commands, and to nonzero mismatched CRC too.
        auto other=test::networkFrame(payload,0x20,0x82,0,0);
        other[other.size()-3]=char(quint8(other[other.size()-3])^1);
        const auto pressureAccepted=decoder.feed(other);QCOMPARE(pressureAccepted.size(),1);
        QVERIFY(!pressureAccepted[0].crcRequired);
        // Never disable checksum enforcement for method ACKs just because they also use 0x81.
        for(int command:{0x81,0x15,0x30,0x20}) {
            auto reply=test::networkFrame(QByteArray::fromHex("11"),0x10,command);
            reply[reply.size()-3]=char(quint8(reply[reply.size()-3])^1);decoder.reset();
            QVERIFY(decoder.feed(reply).isEmpty());
            QCOMPARE(decoder.lastFeedRejection()["reason"].toString(),QString("crc_mismatch"));
        }
        decoder.reset();bad[bad.size()-1]=0;
        QVERIFY(decoder.feed(bad).isEmpty());
        QCOMPARE(decoder.lastFeedRejection()["reason"].toString(),QString("frame_tail_mismatch"));
    }
    void capturedWholeCyclesAreAcceptedWithoutChangingTheirBytes() {
        const auto directory=qEnvironmentVariable("QITEST_WAVEFORM_CAPTURE_DIR");
        if(directory.isEmpty()) QSKIP("Optional local customer capture; never committed as a fixture");
        const QStringList names{"81-0","81-1","82-0","82-1"};
        const int expectedCrc[]{0x0f8e,0x13b8,0xe283,0x7128};
        for(int i=0;i<names.size();++i) {
            QFile file(directory+"/actual-waveform-"+names[i]+".bin");QVERIFY(file.open(QIODevice::ReadOnly));
            const auto original=file.readAll();NetworkProtocol decoder;
            QVector<NetworkFrame> frames;
            for(int offset=0;offset<original.size();offset+=1460)frames+=decoder.feed(original.mid(offset,1460));
            QCOMPARE(frames.size(),1);QCOMPARE(frames[0].wire,original);QVERIFY(!frames[0].crcRequired);
            QCOMPARE(frames[0].receivedCrc,quint16(0));QCOMPARE(int(frames[0].calculatedCrc),expectedCrc[i]);
            QCOMPARE(frames[0].cycleIndex(),quint16(i%2));QCOMPARE(frames[0].payload.size(),i<2?3250:498);
            QCOMPARE(decoder.rejectedBytes(),quint64(0));
            if(i<2) {
                double sum=0;for(int j=0;j<frames[0].payload.size();j+=2)
                    sum+=(quint16(quint8(frames[0].payload[j]))<<8)|quint8(frames[0].payload[j+1]);
                QCOMPARE(sum,i==0?19578.0:19794.0);
            }
        }
    }
    void parserRejectionExplainsFirstCandidateWithoutAcceptingIt() {
        NetworkProtocol codec;
        auto crcBad=test::networkStatusWire();crcBad[crcBad.size()-3]=char(quint8(crcBad[crcBad.size()-3])^1);
        QCOMPARE(codec.feed(crcBad).size(),0);
        QCOMPARE(codec.lastFeedRejection()["reason"].toString(),QString("crc_mismatch"));
        QCOMPARE(QByteArray::fromHex(codec.lastFeedRejection()["candidateHex"].toString().toLatin1()),crcBad);
        QVERIFY(codec.lastFeedRejection()["receivedCrc"]!=codec.lastFeedRejection()["calculatedCrc"]);
        codec.reset();auto tailBad=test::networkStatusWire();tailBad[tailBad.size()-1]=0;
        QCOMPARE(codec.feed(tailBad).size(),0);
        QCOMPARE(codec.lastFeedRejection()["reason"].toString(),QString("frame_tail_mismatch"));
        codec.reset();codec.feed(QByteArray::fromHex("5520012000"));
        QCOMPARE(codec.lastFeedRejection()["reason"].toString(),QString("length_out_of_range"));
        QCOMPARE(codec.lastFeedRejection()["declaredLength"].toInt(),8192);
        codec.reset();codec.feed(QByteArray::fromHex("99"));
        QCOMPARE(codec.lastFeedRejection()["reason"].toString(),QString("no_frame_header"));
        QCOMPARE(codec.feed(test::networkStatusWire()).size(),1);
        QVERIFY(codec.lastFeedRejection().isEmpty());
    }
    void heartbeatPausesAcrossSerialAndNetworkMethodThenResumes() {
        test::FakeSerial port;
        bool slow=false;
        port.responder=[&](const QByteArray &r) {
            const quint8 c=quint8(r[2]);
            if(c==0x30) return test::frame(test::statusPayload());
            const auto reply=test::frame(QByteArray::fromHex("1100"),c);
            if(!slow) return reply;
            QTimer::singleShot(500,&port,[&port,reply]{port.deliver(reply);});
            return QByteArray{};
        };
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);
        QVERIFY(serial->openPort("TEST_ONLY"));QTRY_VERIFY(serial->health().connected);
        NetworkInstrument adapter(std::move(serial));
        QVERIFY(adapter.startListening("127.0.0.1",0,30000));
        QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails()["port"].toUInt());
        QTRY_VERIFY(adapter.statusDetails()["tcpConnected"].toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;
        client.write(test::networkFrame(status));QTRY_VERIFY(adapter.statusDetails()["connected"].toBool());
        const auto heartbeat=test::networkFrame(QByteArray::fromHex("23"),0x10,0x30);
        QCOMPARE(NetworkProtocol::heartbeatCommand(),heartbeat);
        QTest::qWait(1700);QCOMPARE(client.bytesAvailable(),qint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,700);QCOMPARE(client.readAll(),heartbeat);
        QElapsedTimer elapsed;elapsed.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,2400);QCOMPARE(client.readAll(),heartbeat);
        QVERIFY(elapsed.elapsed()>=1850);
        const auto values=MethodDraft::defaultParameters();QSignalSpy done(&adapter,&IInstrumentAdapter::methodParametersFinished);
        slow=true;adapter.requestMethodParameters("heartbeat-method",values);
        QVERIFY(adapter.statusDetails()["heartbeatPausedForMethod"].toBool());
        QVERIFY(!adapter.statusDetails()["heartbeatActive"].toBool());
        // An old method reply during the five serial settings is not a new confirmation.
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QTest::qWait(2100);QCOMPARE(done.size(),0);QCOMPARE(client.bytesAvailable(),qint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,1500);
        QCOMPARE(client.readAll(),NetworkProtocol::fullscanMethodCommand(values));
        // Heartbeat ACK cannot finish the method or restart its heartbeat timer.
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x30));
        QTest::qWait(2100);QCOMPARE(done.size(),0);QCOMPARE(client.bytesAvailable(),qint64(0));
        QCOMPARE(adapter.statusDetails()["heartbeatReplies"].toInt(),1);
        QCOMPARE(adapter.statusDetails()["unparsedFrames"].toInt(),0);
        bool pausedAtSuccess=false;
        connect(&adapter,&IInstrumentAdapter::methodParametersFinished,this,[&](const QString &,bool ok,const QJsonObject &,const QString &){
            if(ok) pausedAtSuccess=!adapter.statusDetails()["heartbeatActive"].toBool();
        });
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_COMPARE(done.size(),1);QVERIFY(done[0][1].toBool());QVERIFY(pausedAtSuccess);
        QVERIFY(adapter.statusDetails()["heartbeatActive"].toBool());
        QTest::qWait(1700);QCOMPARE(client.bytesAvailable(),qint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,700);QCOMPARE(client.readAll(),heartbeat);
        slow=false;adapter.requestMethodParameters("heartbeat-rejected",values);
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::fullscanMethodCommand(values));
        client.write(test::networkFrame(QByteArray::fromHex("29"),0x10,0x81));
        QTRY_COMPARE(done.size(),2);QVERIFY(!done[1][1].toBool());
        QTest::qWait(2100);QCOMPARE(client.bytesAvailable(),qint64(0));
        QVERIFY(adapter.statusDetails()["heartbeatPausedForMethod"].toBool());
        adapter.requestMethodParameters("heartbeat-timeout",values);
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,4000);QVERIFY(!done[2][1].toBool());
        QVERIFY(!adapter.statusDetails()["heartbeatActive"].toBool());
        QTRY_COMPARE(client.state(),QAbstractSocket::UnconnectedState);
        client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails()["port"].toUInt());
        QTRY_VERIFY(adapter.statusDetails()["tcpConnected"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,2400);QCOMPARE(client.readAll(),heartbeat);
        adapter.stopListening();QVERIFY(!adapter.statusDetails()["heartbeatActive"].toBool());
        QTemporaryDir dir;QString error;QVERIFY(adapter.exportFrames(dir.filePath("heartbeat.txt"),&error));
        QFile file(dir.filePath("heartbeat.txt"));QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains(heartbeat.toHex(' ').toUpper()));
    }
    void suppliedDatafitCalibratesBothMethodAndMassAxis() {
        const auto values=MethodDraft::defaultParameters();QString error;
        const auto wire=NetworkProtocol::fullscanMethodCommand(values,&error);
        QVERIFY2(!wire.isEmpty(),qPrintable(error));
        // Independent fixed values from the supplied file: polynomial / 2,
        // truncated to the U16 transmitted to the instrument.
        QCOMPARE(wire.mid(15,2),QByteArray::fromHex("005c")); // storage m/z 30 -> 92
        QCOMPARE(wire.mid(17,2),QByteArray::fromHex("007a")); // low m/z 40 -> 122
        QCOMPARE(wire.mid(19,2),QByteArray::fromHex("037c")); // high m/z 300 -> 892
        const auto axis=NetworkProtocol::fullscanMassAxis(values,&error);
        QCOMPARE(axis.size(),1625);
        QVERIFY(std::abs(axis.first()-39.98490795943708)<1e-8);
        QVERIFY(std::abs(axis.last()-299.7529579296157)<1e-8);
        const auto profile=NetworkProtocol::fullscanCalibrationProfile();
        QCOMPARE(profile["calibrate_a"].toDouble(),0.00013517703930585604);
        QCOMPARE(profile["calibrate_b"].toDouble(),5.882440951161457);
        QCOMPARE(profile["calibrate_c"].toDouble(),8.575019905095814);
        auto extended=values;extended["high_mass"]=801;
        QVERIFY2(!NetworkProtocol::fullscanMassAxis(extended,&error).isEmpty(),qPrintable(error));
        extended["high_mass"]=802;
        QVERIFY(NetworkProtocol::fullscanMethodCommand(extended,&error).isEmpty());
        QVERIFY(error.contains("datafit_1000.json"));
        extended["high_mass"]=1002;
        QVERIFY(NetworkProtocol::fullscanMassAxis(extended,&error).isEmpty());
        QVERIFY(error.contains("datafit_2000.json"));
    }
    void detectionAssemblesRawSpectrumAndWaitsForStopAck() {
        test::FakeSerial port;
        port.responder=[](const QByteArray &request){
            const quint8 cmd=quint8(request[2]);return cmd==0x30?test::frame(test::statusPayload())
                :test::frame(QByteArray::fromHex("1100"),cmd);
        };
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);
        NetworkInstrument adapter(std::move(serial));QVERIFY(adapter.startListening("127.0.0.1",0,10000));
        QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;
        client.write(test::networkFrame(status));QTRY_VERIFY(adapter.statusDetails().value("connected").toBool());
        const auto values=MethodDraft::defaultParameters();QString error;
        adapter.requestMethodParameters("capture-method",values);
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_COMPARE(adapter.confirmedMethodParameters(),values);
        const auto axis=NetworkProtocol::fullscanMassAxis(values,&error);
        QCOMPARE(axis.size(),1625);QVERIFY(std::abs(axis.first()-40)<1);QVERIFY(std::abs(axis.last()-300)<1);
        QSignalSpy done(&adapter,&NetworkInstrument::acquisitionFinished);
        QSignalSpy started(&adapter,&NetworkInstrument::acquisitionStarted);
        QVector<SpectrumScan> scans;connect(&adapter,&NetworkInstrument::acquisitionScan,this,[&](const SpectrumScan &s){scans.append(s);});
        QVERIFY(!adapter.startAcquisition(0,&error));QVERIFY(!adapter.startAcquisition(5000,&error));
        QVERIFY(adapter.startAcquisition(1,&error));QVERIFY(!adapter.startAcquisition(1,&error));
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(true));
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(started.size(),1);
        QByteArray payload;for(int i=0;i<axis.size();++i) {payload.append(char(0x80));payload.append(char(i&255));}
        auto first=test::networkFrame(payload,0x20,0x81,0,0);
        first[first.size()-3]=0;first[first.size()-2]=0;
        auto pressureZero=test::networkFrame(QByteArray(498,0),0x20,0x82,0,0);
        pressureZero[pressureZero.size()-3]=0;pressureZero[pressureZero.size()-2]=0;
        client.write(first.left(9));client.flush();QTest::qWait(10);QVERIFY(scans.isEmpty());
        client.write(first.mid(9,1451));
        client.write(first.mid(1460,1460));
        QTest::qWait(20);QVERIFY(scans.isEmpty());
        client.write(first.mid(2920)+pressureZero);
        QTRY_COMPARE(scans.size(),1);QCOMPARE(scans[0].timeSeconds,0.0);
        QCOMPARE(adapter.pressureVolts().size(),249);QCOMPARE(adapter.statusDetails()["pressureCycle"].toInt(),0);
        QCOMPARE(adapter.statusDetails()["waveformCrcMismatches"].toInt(),2);
        QCOMPARE(scans[0].points[0].intensity,32768.0);QCOMPARE(scans[0].points[255].intensity,33023.0);
        client.write(test::networkFrame(payload,0x20,0x81,0,1)+test::networkFrame(QByteArray(498,0),0x20,0x82,0,1));
        QTRY_COMPARE(scans.size(),2);QCOMPARE(scans[1].timeSeconds,1.0);
        QCOMPARE(adapter.statusDetails()["pressureCycle"].toInt(),1);
        const auto tic=ChromatogramEngine::trace(scans,ChromatogramEngine::Kind::Tic);
        double expectedTic=0;for(int i=0;i<axis.size();++i)expectedTic+=32768+(i&255);
        QCOMPARE(tic.size(),2);QCOMPARE(tic[0].mz,0.0);QCOMPARE(tic[1].mz,1.0);
        QCOMPARE(tic[0].intensity,expectedTic);QCOMPARE(tic[1].intensity,expectedTic);
        const auto eic=ChromatogramEngine::trace(scans,ChromatogramEngine::Kind::Eic,1,axis[255],0.00001);
        QCOMPARE(eic[0].intensity,33023.0);QCOMPARE(eic[1].intensity,33023.0);
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,1800);
        QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));QVERIFY(done.isEmpty());
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));
        QTRY_COMPARE(done.size(),1);QVERIFY(done.last()[0].toBool());QVERIFY(!adapter.acquisitionBusy());
        QVERIFY(adapter.statusDetails()["pressureAcquisitionCompleted"].toBool());

        // A missing cycle cannot be silently renumbered or accepted as a successful run.
        QVERIFY(adapter.startAcquisition(1,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(started.size(),2);
        client.write(test::networkFrame(payload,0x20,0x81,0,1));
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));
        QTRY_COMPARE(done.size(),2);QVERIFY(!done.last()[0].toBool());QCOMPARE(scans.size(),2);
        QVERIFY(!adapter.statusDetails()["pressureAcquisitionCompleted"].toBool());
        QVERIFY(done.last()[2].toString().contains("周期编号不连续"));

        // Corrupt status/control data still stops acquisition and remains in raw diagnostics.
        QVERIFY(adapter.startAcquisition(1,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTRY_COMPARE(started.size(),3);
        auto corrupt=test::networkFrame(status);corrupt[8]=char(quint8(corrupt[8])^1);client.write(corrupt);
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));
        QTRY_COMPARE(done.size(),3);QVERIFY(!done.last()[0].toBool());QVERIFY(done.last()[2].toString().contains("校验"));
        const auto failure=adapter.statusDetails()["acquisitionParseFailure"].toJsonObject();
        QCOMPARE(failure["reason"].toString(),QString("crc_mismatch"));
        QTemporaryDir rawDir;const auto jsonPath=rawDir.filePath("raw.json");
        QVERIFY(adapter.exportFrames(jsonPath,&error));
        QFile rawFile(jsonPath);QVERIFY(rawFile.open(QIODevice::ReadOnly));
        const auto rawDoc=QJsonDocument::fromJson(rawFile.readAll()).object();
        QByteArray rawEvidence;
        for(const auto &v:rawDoc["acquisitionErrorRawReceives"].toArray())
            rawEvidence+=QByteArray::fromHex(v.toObject()["hex"].toString().toLatin1());
        QVERIFY(rawEvidence.contains(corrupt));
        for(const auto &v:rawDoc["frames"].toArray())
            QVERIFY(QByteArray::fromHex(v.toObject()["hex"].toString().toLatin1())!=corrupt);
        // Later idle traffic may evict the rolling buffer, never the frozen error evidence.
        for(int i=0;i<140;++i) {
            const auto previous=adapter.statusDetails()["validFrames"].toInt();
            client.write(test::networkFrame(status));
            QTRY_COMPARE(adapter.statusDetails()["validFrames"].toInt(),previous+1);
        }
        const auto textPath=rawDir.filePath("raw.txt");QVERIFY(adapter.exportFrames(textPath,&error));
        QFile rawText(textPath);QVERIFY(rawText.open(QIODevice::ReadOnly));const auto rawTextBytes=rawText.readAll();
        QVERIFY(rawTextBytes.contains("RX_RAW"));QVERIFY(rawTextBytes.contains(corrupt.toHex(' ').toUpper()));
        QVERIFY(QString::fromUtf8(rawTextBytes).contains("波形CRC仅记录"));
        QVERIFY(adapter.statusDetails()["retainedRawReceiveBytes"].toInt()<=1024*1024);
        client.readAll(); // Discard elapsed heartbeats before the next explicit request.

        // An unanswered start cannot finish successfully or accept a late ACK on the old socket.
        QVERIFY(adapter.startAcquisition(1,&error));QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,4000);QVERIFY(!done.last()[0].toBool());
        QVERIFY(!adapter.statusDetails().value("tcpConnected").toBool());
    }
    void fullscanMethodFrameMatchesDocumentedLayout() {
        const auto values=MethodDraft::defaultParameters();QString error;
        const auto wire=NetworkProtocol::fullscanMethodCommand(values,&error);
        QVERIFY2(!wire.isEmpty(),qPrintable(error));QCOMPARE(wire.size(),96);
        NetworkProtocol codec;const auto frames=codec.feed(wire);QCOMPARE(frames.size(),1);
        const auto frame=frames.first();QCOMPARE(frame.action,quint8(0x10));QCOMPARE(frame.command,quint8(0x81));
        QCOMPARE(frame.count,quint8(1));QCOMPARE(frame.index,quint8(1));QCOMPARE(frame.payload.size(),86);
        QCOMPARE(quint8(frame.payload[0]),quint8(1));QCOMPARE(quint8(frame.payload[1]),quint8(1));
        QCOMPARE(QByteArray(frame.payload.mid(2,2)),QByteArray::fromHex("2710"));
        QCOMPARE(QByteArray(frame.payload.mid(4,2)),QByteArray::fromHex("0032"));
        bool success=false;
        const auto ack=codec.feed(test::networkFrame(QByteArray(1,char(0x11)),0x10,0x81));
        QCOMPARE(ack.size(),1);QVERIFY(NetworkProtocol::decodeCommandAcknowledgement(ack[0],0x81,&success));QVERIFY(success);
        auto invalid=values;invalid.insert("scan_mode","SIM");QVERIFY(NetworkProtocol::fullscanMethodCommand(invalid,&error).isEmpty());
        invalid=values;invalid.insert("period",1000);QVERIFY(NetworkProtocol::fullscanMethodCommand(invalid,&error).isEmpty());
    }
    void fullscanConfirmedTimingSlotsRemainIndependent() {
        auto values=MethodDraft::defaultParameters(); QString error;
        auto wire=NetworkProtocol::fullscanMethodCommand(values,&error);
        QVERIFY2(!wire.isEmpty(),qPrintable(error));
        // Absolute wire offsets include the seven-byte header. Slots 20/21 have
        // mixed U8/U16 widths; slot indices are not byte offsets.
        QCOMPARE(wire.mid(21,2),QByteArray::fromHex("1388")); // data[8]: cooling 5000
        QCOMPARE(wire.mid(31,2),QByteArray::fromHex("017c")); // data[13]: injection 380
        QCOMPARE(wire.mid(44,3),QByteArray::fromHex("010bb8")); // old capture: data[20]=1, data[21]=3000
        const auto baseline=wire;
        values.insert("cooling",5100);
        wire=NetworkProtocol::fullscanMethodCommand(values,&error);
        QCOMPARE(wire.mid(21,2),QByteArray::fromHex("13ec"));
        QCOMPARE(wire.mid(31,2),baseline.mid(31,2));
        QCOMPARE(wire.mid(44,3),baseline.mid(44,3));
        values.insert("injection",400);
        wire=NetworkProtocol::fullscanMethodCommand(values,&error);
        QCOMPARE(wire.mid(31,2),QByteArray::fromHex("0190"));
        QCOMPARE(wire.mid(21,2),QByteArray::fromHex("13ec"));
        QCOMPARE(wire.mid(44,3),baseline.mid(44,3));
        NetworkProtocol codec; QCOMPARE(codec.feed(wire).size(),1); // new CRC remains valid
        // sum_time = cooling + scan(325) + RF(30+50) + guard(1000).
        values=MethodDraft::defaultParameters(); values.insert("period",6405);
        QVERIFY2(!NetworkProtocol::fullscanMethodCommand(values,&error).isEmpty(),qPrintable(error));
        values.insert("period",6404);
        QVERIFY(NetworkProtocol::fullscanMethodCommand(values,&error).isEmpty());
        QVERIFY(error.contains("总周期"));
        values.insert("period",6405); values.insert("injection",600);
        QVERIFY2(!NetworkProtocol::fullscanMethodCommand(values,&error).isEmpty(),qPrintable(error));
        values.insert("injection",380.5); // no silent truncation or invented decimal scaling
        QVERIFY(NetworkProtocol::fullscanMethodCommand(values,&error).isEmpty());
    }
    void fullscanWaitsForFiveRs485AcksThenNetworkAck() {
        test::FakeSerial port;
        port.responder=[](const QByteArray &request){
            if(request.size()<3)return QByteArray{};const quint8 command=quint8(request[2]);
            return command==0x30?test::frame(test::statusPayload()):test::frame(QByteArray::fromHex("1100"),command);
        };
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);
        NetworkInstrument adapter(std::move(serial));QVERIFY(adapter.startListening("127.0.0.1",0,3000));
        QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,adapter.statusDetails().value("port").toUInt());
        QTRY_VERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        auto statusPayload=test::networkStatusWire().mid(7,21);statusPayload[9]=0;
        client.write(test::networkFrame(statusPayload));QTRY_VERIFY(adapter.statusDetails().value("connected").toBool());
        const auto values=MethodDraft::defaultParameters();QVERIFY(adapter.validateMethodParameters(values).allowed);
        QSignalSpy finished(&adapter,&IInstrumentAdapter::methodParametersFinished);
        adapter.requestMethodParameters("method-1",values);
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,2000);
        QString error; const auto actualWire=client.readAll();
        QCOMPARE(actualWire,NetworkProtocol::fullscanMethodCommand(values,&error));
        QCOMPARE(actualWire.mid(21,2),QByteArray::fromHex("1388"));
        QCOMPARE(actualWire.mid(31,2),QByteArray::fromHex("017c"));
        QCOMPARE(actualWire.mid(44,3),QByteArray::fromHex("010bb8"));
        QVector<int> controls;for(const auto &wire:port.writes)if(wire.size()>2&&quint8(wire[2])!=0x30)controls<<quint8(wire[2]);
        QCOMPARE(controls,QVector<int>({0x02,0x13,0x12,0x04,0x14}));
        QVERIFY(adapter.statusDetails().value("methodPending").toBool());
        const auto unknownReply=test::networkFrame(QByteArray(1,char(0x7f)),0x10,0x81);
        client.write(unknownReply);
        QTRY_COMPARE(adapter.statusDetails().value("unparsedFrames").toInt(),1);
        QCOMPARE(finished.size(),0); // Export must not turn an unknown response into success.
        QTemporaryDir exportDirectory;
        const auto textPath=exportDirectory.filePath("method.TXT");
        QVERIFY(adapter.exportFrames(textPath,&error));
        QFile textFile(textPath); QVERIFY(textFile.open(QIODevice::ReadOnly));
        const auto exported=textFile.readAll();
        QVERIFY(exported.startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(exported.contains(actualWire.toHex(' ').toUpper()+"\r\n"));
        QVERIFY(exported.contains(unknownReply.toHex(' ').toUpper()+"\r\n"));
        QVERIFY(exported.contains(" TX peer=127.0.0.1 action=0x10 command=0x81 bytes=96"));
        QVERIFY(exported.contains(" RX peer=127.0.0.1 action=0x10 command=0x81 bytes=11"));
        QVERIFY(QString::fromUtf8(exported).contains("方法返回值：0x7f；含义未确认"));
        QVERIFY(!adapter.exportFrames(exportDirectory.filePath("missing/method.txt"),&error));
        client.write(test::networkFrame(QByteArray(1,char(0x11)),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_COMPARE(finished.size(),1);QVERIFY(finished[0][1].toBool());
        QCOMPARE(finished[0][2].toJsonObject(),values);QCOMPARE(adapter.confirmedMethodParameters(),values);
        QVERIFY(!adapter.statusDetails().value("methodPending").toBool());
        auto unsafe=values;unsafe.insert("source",1);QVERIFY(!adapter.validateMethodParameters(unsafe).allowed);
        // Replayed real 0x29 response: fail promptly, preserve connection and confirmed method.
        auto changed=values;changed.insert("cooling",5100);
        adapter.requestMethodParameters("method-cooling-error",changed);
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable()>0,2000); client.readAll();
        client.write(QByteArray::fromHex("55108100030101291a85aa"));
        QTRY_COMPARE(finished.size(),2);
        QVERIFY(!finished[1][1].toBool());
        QVERIFY(finished[1][3].toString().contains("冷却时间错误"));
        QVERIFY(!adapter.statusDetails().value("methodPending").toBool());
        QVERIFY(adapter.statusDetails().value("tcpConnected").toBool());
        QCOMPARE(adapter.confirmedMethodParameters(),values);
        QVERIFY(adapter.exportFrames(exportDirectory.filePath("cooling-error.txt"),&error));
        QFile coolingFile(exportDirectory.filePath("cooling-error.txt")); QVERIFY(coolingFile.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(coolingFile.readAll()).contains("方法返回值：0x29；冷却时间错误，设置失败"));
    }
    void pressureConversionUsesUnsignedBigEndianAnd65535() {
        NetworkProtocol decoder; const auto frames=decoder.feed(test::networkFrame(QByteArray::fromHex("00008000ffff"),0x20,0x82));
        QCOMPARE(frames.size(),1);QVector<double> values;
        QVERIFY(NetworkProtocol::decodePressure(frames[0],&values)); QCOMPARE(values.size(),3);
        QCOMPARE(values[0],0.0);QCOMPARE(values[1],32768.0/65535*2.5*5.7);QCOMPARE(values[2],14.25);
        auto bad=frames[0];bad.payload.append('x');QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
        bad=frames[0];bad.command=0x81;QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
        bad=frames[0];bad.payload=QByteArray(NetworkProtocol::MaximumWaveformPayload+2,0);
        QVERIFY(!NetworkProtocol::decodePressure(bad,&values));
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
        QTRY_COMPARE(adapter.statusDetails().value("validFrames").toInt(),2);
        QVERIFY(adapter.acquireSpectrum().isEmpty()); // Not acquiring: no spectrum is published.
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
        NetworkProtocol txDecoder;const auto sent=txDecoder.feed(client.readAll());
        QVERIFY(sent.size()>=5);
        for(const auto &frame:sent) QCOMPARE(frame.wire,NetworkProtocol::heartbeatCommand());
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
