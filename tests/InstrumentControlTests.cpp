#include "app/AppController.h"
#include "device/VendorControlCatalog.h"
#include "device/SimulatedInstrument.h"
#include "device/Rs485Instrument.h"
#include "Rs485TestDevice.h"
#include <QTcpServer>
#include <cmath>
#include <limits>
#include <QTemporaryDir>
#include <QtTest>
#include "core/MethodDraft.h"
using namespace qitest;

// Contract fixture, not a vendor implementation. Intentionally withholds ACK.
class TestInstrument : public IInstrumentAdapter {
public:
    InstrumentDescriptor descriptor() const override { return {"test", "test", "test", false}; }
    InstrumentHealth health() const override { return {true, true}; }
    InstrumentTelemetry telemetry() const override { return {}; }
    CommandValidation validate(const InstrumentCommand &) const override { return {false, "fixture"}; }
    QVector<SpectrumPoint> acquireSpectrum() override { return {}; }
    void cancel() override {}
    CommandValidation validateSetting(const QString &, const QVariant &) const override { return {true, {}}; }
    QString id, key;
    int count = 0;
    void requestSetting(const QString &request, const QString &name, const QVariant &) override {
        id = request; key = name; ++count;
    }
    void acknowledge(bool success, QVariant value) { emit settingFinished(id, key, success, value, "fixture"); }
};

class InstrumentControlTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settingsDirectory_;
private slots:
    void completeMethodEditingSurvivesDeviceConnection() {
        QTemporaryDir dir;qputenv("QITEST_WORKSPACE_DB",dir.filePath("methods.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        qputenv("QITEST_OPERATOR_ROLE","admin");controller.setSessionOperator("admin");
        const QJsonObject base{{"scan_mode","Fullscan"},{"injection",12.34},{"source",4.9},{"td",40.0}};
        QVERIFY(controller.createMethodDraft("管理员方法",base));QString id;
        for(const auto &method:controller.methods()) if(method.name=="管理员方法") id=method.id;
        QVERIFY(!id.isEmpty());
        qputenv("QITEST_OPERATOR_ROLE","operator");controller.setSessionOperator("operator");
        QVERIFY(controller.fullMethodAccess());
        auto next=base;next.insert("scan_mode","SIM");next.insert("injection",600.0);
        next.insert("source",6.0);
        QVERIFY(controller.createMethodDraft("现场方法",next));
        QVERIFY(controller.startNetworkListening("127.0.0.1",0));
        QVERIFY(!controller.instrumentDescriptor().simulation);
        QVERIFY(controller.fullMethodAccess());
        next.insert("source",7.0);
        QVERIFY(controller.createMethodDraft("联网方法",next));
        controller.stopNetworkListening();
        QVERIFY(!controller.requestRfTuning(true,true));
        QString error;next=base;next.insert("injection",600.01);QVERIFY(!MethodDraft::validate(next,&error));
        next.insert("injection",1.001);QVERIFY(!MethodDraft::validate(next,&error));
        next.insert("injection",0.0);QVERIFY(MethodDraft::validate(next,&error));
        qunsetenv("QITEST_OPERATOR_ROLE");qunsetenv("QITEST_WORKSPACE_DB");
    }

    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        // Tests must not read/write the operator's registry or saved COM port.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory_.path());
    }
    void rs485ReadbackClearsUnknownFieldsAndCannotControlOrAcquire() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("rs485.sqlite").toUtf8());
        test::FakeSerial port;
        auto instrument = std::make_unique<Rs485Instrument>(&port, nullptr);
        auto *adapter = instrument.get();
        AppController controller(std::move(instrument));
        QVERIFY(controller.instrumentReadOnly());
        QVERIFY(!controller.instrumentDescriptor().simulation);
        for (const auto &check : controller.startupChecks())
            if (check.name == "仪器接口") QVERIFY(!check.blocking);
        QVERIFY(!controller.instrumentSettings().value("powerOn").isValid());
        QVERIFY(std::isnan(controller.health().tdTemperatureC));
        port.reply = test::frame(test::statusPayload());
        QVERIFY(adapter->openPort("TEST_ONLY"));
        QVERIFY(!controller.health().connected);
        QTRY_VERIFY(controller.health().connected);
        QCOMPARE(controller.telemetry().ionTrapTemperatureC, 85.3);
        QVERIFY(controller.instrumentSettings().value("observationLightOn").toBool());
        QVERIFY(!controller.instrumentSettings().value("trapTemperatureC").isValid());
        const auto count = port.writes.size();
        QVERIFY(!controller.updateInstrumentSetting("wastePumpOn", false, true));
        controller.startDetection();
        QVERIFY(controller.phase() != AppController::Phase::Acquiring);
        QCOMPARE(port.writes.size(), count);
        controller.disconnectRs485();
        QVERIFY(!controller.health().connected);
        QVERIFY(std::isnan(controller.telemetry().ionTrapTemperatureC));
        QVERIFY(!controller.instrumentSettings().value("observationLightOn").isValid());
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void failedConnectionsPreserveSimulationAndCannotReplacePlugin() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("network-controller.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        QVERIFY(!controller.connectRs485("QITEST_PORT_THAT_DOES_NOT_EXIST"));
        QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(controller.health().ready);
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost));
        QVERIFY(!controller.startNetworkListening("127.0.0.1", occupied.serverPort()));
        // A failed real connection attempt preserves the usable offline demo.
        QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(!controller.instrumentReadOnly()); QVERIFY(controller.health().connected);
        QVERIFY(controller.telemetry().multiplierVoltageV > 0.0);
        QVERIFY(controller.instrumentSettings().value("powerOn").toBool());
        QVERIFY(controller.startNetworkListening("127.0.0.1", 0));
        controller.stopNetworkListening(); QVERIFY(!controller.instrumentDescriptor().simulation);
        QVERIFY(controller.useSimulatedInstrument());
        QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(controller.health().ready);
        auto plugin = std::make_unique<TestInstrument>();
        AppController vendor(std::move(plugin));
        QVERIFY(!vendor.startNetworkListening("127.0.0.1", 0));
        QCOMPARE(vendor.instrumentDescriptor().model, QString("test"));
        QVERIFY(vendor.networkStatus().isEmpty());
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void detectionTimingStopsAndHistoryDoesNotReplaceActiveAcquisition() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("timing.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        QCOMPARE(controller.detectionElapsedMs(), qint64(-1));
        QVERIFY(controller.softwareElapsedMs() >= 0);
        controller.startDetection();
        QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
        QTest::qWait(40);
        QVERIFY(controller.detectionElapsedMs() >= 30);
        controller.cancelDetection();
        const auto cancelledTime = controller.detectionElapsedMs();
        QTest::qWait(30);
        QCOMPARE(controller.detectionElapsedMs(), cancelledTime);
        controller.startDetection();
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 5000);
        QVERIFY(controller.detectionElapsedMs() > 0);
        const auto completedTime = controller.detectionElapsedMs();
        const auto id = controller.currentRun().id;
        QVERIFY(!id.isEmpty());
        QTest::qWait(30);
        QCOMPARE(controller.detectionElapsedMs(), completedTime);
        controller.loadStoredRun(id);
        QCOMPARE(controller.detectionElapsedMs(), qint64(-1));
        controller.startDetection();
        controller.loadStoredRun(id);
        QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
        controller.cancelDetection();
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void simulatorPowerAndManualControlsShareReadback() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("power.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        const QStringList keys{"rfOn", "ionHighVoltageOn", "diaphragmPumpOn",
            "molecularPumpOn", "pinchValveOn", "internalCarrierGasOn"};
        QVERIFY(controller.health().ready);
        for (const auto &key : keys) QVERIFY(controller.instrumentSettings().value(key).toBool());
        QVERIFY(controller.updateInstrumentSetting("powerOn", false));
        for (const auto &key : keys) QVERIFY(!controller.instrumentSettings().value(key).toBool());
        QVERIFY(!controller.health().ready);
        QCOMPARE(controller.telemetry().molecularPumpRpm, 0.0);
        QCOMPARE(controller.health().ionSourceKv, 0.0);
        QCOMPARE(controller.health().carrierGasMlMin, 0.0);
        QVERIFY(controller.updateInstrumentSetting("rfOn", true));
        QVERIFY(controller.instrumentSettings().value("powerOn").toBool());
        QVERIFY(!controller.health().ready);
        QVERIFY(controller.updateInstrumentSetting("powerOn", true));
        for (const auto &key : keys) QVERIFY(controller.instrumentSettings().value(key).toBool());
        QVERIFY(controller.health().ready);
        QVERIFY(controller.updateInstrumentSetting("ionSourceEnabled", false));
        QVERIFY(!controller.instrumentSettings().value("ionHighVoltageOn").toBool());
        QVERIFY(!controller.health().ready);
        QVERIFY(controller.updateInstrumentSetting("powerOn", false));
        QVERIFY(!controller.instrumentSettings().value("ionSourceEnabled").toBool());
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void vendorPayloadsStaySeparateFromWireTransport() {
        QByteArray bytes; QString error;
        QVERIFY(VendorControlCatalog::controlPayload("observationLightOn", true, &bytes, &error));
        QCOMPARE(bytes, QByteArray::fromHex("01"));
        QVERIFY(VendorControlCatalog::controlPayload("wastePumpOn", false, &bytes));
        QCOMPARE(bytes, QByteArray::fromHex("02"));
        QVERIFY(VendorControlCatalog::controlPayload("rf48VOn", true, &bytes));
        QCOMPARE(bytes, QByteArray::fromHex("22"));
        QVERIFY(VendorControlCatalog::controlPayload("highVoltageBoardOn", false, &bytes));
        QCOMPARE(bytes, QByteArray::fromHex("23"));
        QVERIFY(VendorControlCatalog::controlPayload("tdTemperatureC", 56, &bytes));
        QCOMPARE(bytes, QByteArray::fromHex("0038"));
        QVERIFY(VendorControlCatalog::controlPayload("efcMlMin", 28.4, &bytes));
        QCOMPARE(bytes, QByteArray::fromHex("6ef0"));
        const auto previous = bytes;
        QVERIFY(!VendorControlCatalog::controlPayload("efcMlMin", 50.001, &bytes));
        QVERIFY(!VendorControlCatalog::controlPayload("tdTemperatureC", 56.1, &bytes));
        QVERIFY(!VendorControlCatalog::controlPayload("tdTemperatureC", std::numeric_limits<double>::quiet_NaN(), &bytes));
        QVERIFY(!VendorControlCatalog::controlPayload("wastePumpOn", "true", &bytes));
        QVERIFY(!VendorControlCatalog::controlPayload("unknown", true, &bytes));
        QCOMPARE(bytes, previous);
        QVERIFY(!VendorControlCatalog::controlPayload("powerOn", true, &bytes));
        QVERIFY(!VendorControlCatalog::controlPayload("coolingFanOn", true, nullptr));
        QCOMPARE(bytes, previous);
        // Check every documented auxiliary switch in both directions. This
        // validates payload bytes only, never an assumed frame/CRC or device.
        for (const auto &spec : VendorControlCatalog::controls()) {
            if (!spec.toggle) continue;
            for (const bool on : {false, true}) {
                QVERIFY(VendorControlCatalog::controlPayload(spec.key, on, &bytes));
                QCOMPARE(bytes.size(), 1);
                QCOMPARE(static_cast<unsigned char>(bytes[0]),
                    static_cast<unsigned char>(spec.transport == "网口" ? (on ? 0x22 : 0x23) : (on ? 0x01 : 0x02)));
            }
        }
    }
    void boundedCommandsRequireMatchingReadback() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("workspace.sqlite").toUtf8());
        qputenv("QITEST_OPERATOR_ROLE", "engineer");
        auto instrument = std::make_unique<TestInstrument>();
        auto *device = instrument.get();
        AppController controller(std::move(instrument));
        QVERIFY(!controller.updateInstrumentSetting("rfOn", true, true)); // offline role
        controller.setSessionOperator("test-engineer");
        QSignalSpy confirmations(&controller, &AppController::instrumentConfirmationRequired);
        QVERIFY(!controller.updateInstrumentSetting("rfOn", true));
        QCOMPARE(confirmations.count(), 1);
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        QVERIFY(controller.updateInstrumentSetting("rfOn", true, true));
        QCOMPARE(device->count, 1);
        QVERIFY(!controller.updateInstrumentSetting("rfOn", false, true)); // no double-submit
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        emit device->settingFinished("old-request", "rfOn", true, true, {});
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        device->acknowledge(true, true);
        QCOMPARE(controller.instrumentSettings().value("rfOn"), QVariant(true));
        QVERIFY(controller.updateInstrumentSetting("rfOn", false, true));
        device->acknowledge(true, true); // mismatched readback is not success
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        QVERIFY(!controller.updateInstrumentSetting("efcMlMin", "nonsense", true));
        QVERIFY(!controller.updateInstrumentSetting("unknown", true, true));
        QVERIFY(controller.updateInstrumentSetting("rfOn", false, true));
        QSignalSpy pending(&controller, &AppController::instrumentCommandPending);
        QTRY_VERIFY_WITH_TIMEOUT(!pending.isEmpty(), 6500);
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        device->acknowledge(true, false); // late reply cannot overwrite unknown state
        QVERIFY(!controller.instrumentSettings().value("rfOn").isValid());
        const int before = device->count;
        QVERIFY(!controller.updateInstrumentSetting("efcMlMin", 50.01, true));
        QVERIFY(!controller.saveInstrumentPreset({{"trapTemperatureC", 50}, {"inletFlowPercent", 20},
                                                 {"pumpFlowPercent", 20}, {"efcMlMin", 999.99}}));
        QCOMPARE(device->count, before);
        QVERIFY(controller.saveInstrumentPreset({{"trapTemperatureC", 50}, {"inletFlowPercent", 20},
                                                 {"pumpFlowPercent", 20}, {"efcMlMin", 28.4}}));
        QCOMPARE(device->count, before); // local preset never masquerades as a hardware ACK
        QVERIFY(!controller.updateInstrumentSetting("tdTemperatureC", 40.5, true));
        QVERIFY(!controller.updateInstrumentSetting("tdTemperatureC", true, true));
        QVERIFY(!controller.updateInstrumentSetting("wastePumpOn", true));
        QCOMPARE(device->count, before); // new controls use the same real-device confirmation gate
        QVERIFY(controller.updateInstrumentSetting("wastePumpOn", true, true));
        QVERIFY(!controller.instrumentSettings().value("wastePumpOn").isValid());
        device->acknowledge(true, true);
        QCOMPARE(controller.instrumentSettings().value("wastePumpOn"), QVariant(true));
        qunsetenv("QITEST_WORKSPACE_DB");
        qunsetenv("QITEST_OPERATOR_ROLE");
    }
    void simulatorAcceptsAndUsesCompleteMethodParameters() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("method-simulator.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        const QJsonObject parameters{{"scan_mode","Fullscan"},{"carrier",1.0},{"extraction",0.0},
            {"inlet",50.0},{"td",0.0},{"source",0.0},{"trap",85.0},{"period",10000.0},
            {"speed",8000.0},{"rf_frequency",50.0},{"storage_mass",30.0},{"low_mass",100.0},
            {"high_mass",102.0},{"cooling",5000.0},{"ac_frequency",590.0},
            {"injection",380.0},{"multiplier",1000.0}};
        QVERIFY(controller.createMethodDraft("旧版字段模拟方法", parameters));
        MethodDefinition created;
        for (const auto &method : controller.methods())
            if (method.name == "旧版字段模拟方法") created = method;
        QVERIFY(!created.id.isEmpty());
        QCOMPARE(created.parameters.value("data_scope").toString(), QString("DEMO_SIMULATION"));
        controller.activateMethod(created.id);
        QCOMPARE(controller.activeMethod().id, created.id);
        QCOMPARE(controller.confirmedMethodParameters(), parameters);
        QCOMPARE(controller.telemetry().carrierGasFlowMlMin, 1.0);
        QCOMPARE(controller.telemetry().extractionFlowPercent, 0.0);
        QCOMPARE(controller.telemetry().ionTrapTemperatureC, 85.0);
        QCOMPARE(controller.telemetry().tdTemperatureC, 0.0);
        QCOMPARE(controller.telemetry().ionSourceVoltageV, 0.0);
        QCOMPARE(controller.telemetry().multiplierVoltageV, 1000.0);
        SimulatedInstrument directSimulator;
        directSimulator.requestMethodParameters("direct-test", parameters);
        const auto spectrum = directSimulator.acquireSpectrum();
        QCOMPARE(spectrum.size(), 5);
        QCOMPARE(spectrum.first().mz, 100.0);
        QCOMPARE(spectrum.last().mz, 102.0);
        controller.startDetection();
        QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
        QCOMPARE(controller.liveSpectrum().size(), 0);
        controller.cancelDetection();
        qunsetenv("QITEST_WORKSPACE_DB");
    }
};
QTEST_GUILESS_MAIN(InstrumentControlTests)
#include "InstrumentControlTests.moc"
