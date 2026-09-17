#include "app/AppController.h"
#include "device/VendorControlCatalog.h"
#include "device/SimulatedInstrument.h"
#include "device/Rs485Instrument.h"
#include "device/NetworkInstrument.h"
#include "Rs485TestDevice.h"
#include "NetworkTestFrames.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <cmath>
#include <limits>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>
#include "core/MethodDraft.h"
#include "storage/RunArchiveCodec.h"
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
    CommandValidation validateMethodParameters(const QJsonObject &) const override { return {true, {}}; }
    void requestMethodParameters(const QString &request, const QJsonObject &parameters) override {
        methodId = request; requestedMethod = parameters;
    }
    void acknowledgeMethod(bool success, const QJsonObject &readback) {
        emit methodParametersFinished(methodId, success, readback, success ? QString() : QString("fixture"));
    }
    QString methodId;
    QJsonObject requestedMethod;
};

class InstrumentControlTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settingsDirectory_;
private slots:
    void analysisStorageWaitKeepsGuiResponsive() {
        QTemporaryDir dir;
        const QString path = dir.filePath("responsive-save.sqlite");
        qputenv("QITEST_WORKSPACE_DB", path.toUtf8());
        {
            AppController controller(std::make_unique<SimulatedInstrument>());
            auto db = QSqlDatabase::addDatabase("QSQLITE", "slow-storage-fixture");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            bool released = false;
            int heartbeats = 0;
            QTimer heartbeat;
            heartbeat.setInterval(10);
            connect(&heartbeat, &QTimer::timeout, &controller, [&] { ++heartbeats; });
            connect(&controller, &AppController::phaseChanged, &controller,
                [&](AppController::Phase phase) {
                    if (phase != AppController::Phase::Analyzing) return;
                    QSqlQuery lock(db);
                    QVERIFY(lock.exec("BEGIN IMMEDIATE"));
                    heartbeat.start();
                    QTimer::singleShot(250, &controller, [&] {
                        QSqlQuery unlock(db);
                        QVERIFY(unlock.exec("ROLLBACK"));
                        released = true;
                    });
                });
            controller.startDetection();
            QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 7000);
            QVERIFY(released);
            QVERIFY(heartbeats >= 3);
            QVERIFY(!controller.currentRun().id.isEmpty());
            QCOMPARE(controller.recentRuns().size(), 1);
            db.close();
        }
        QSqlDatabase::removeDatabase("slow-storage-fixture");
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void failedPersistenceDoesNotPublishSuccess() {
        QTemporaryDir dir;
        const QString path = dir.filePath("failed-save.sqlite");
        qputenv("QITEST_WORKSPACE_DB", path.toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "failed-save-fixture");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_run BEFORE INSERT ON runs "
                               "BEGIN SELECT RAISE(ABORT, 'test save failure'); END"));
            db.close();
        }
        QSqlDatabase::removeDatabase("failed-save-fixture");
        QSignalSpy saved(&controller, &AppController::runSaved);
        QSignalSpy completed(&controller, &AppController::analysisCompleted);
        controller.startDetection();
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::Failed, 5000);
        QCOMPARE(saved.count(), 0);
        QCOMPARE(completed.count(), 0);
        QVERIFY(controller.currentRun().id.isEmpty());
        QVERIFY(controller.recentRuns().isEmpty());
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void acquisitionKeepsMethodAndSettingsStable() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("stable-method.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        qputenv("QITEST_OPERATOR_ROLE", "admin");
        controller.setSessionOperator("admin");
        const auto before = controller.activeMethod();
        const QJsonObject parameters{{"scan_mode", "Fullscan"}, {"injection", 12.34},
                                     {"source", 4.9}, {"td", 40.0}};
        QVERIFY(controller.createMethodDraft("下一次检测", parameters));
        QString nextId;
        for (const auto &method : controller.methods())
            if (method.name == "下一次检测") nextId = method.id;
        QVERIFY(!nextId.isEmpty());
        controller.startDetection();
        QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
        controller.activateMethod(nextId);
        QCOMPARE(controller.activeMethod().id, before.id);
        const auto settings = controller.instrumentSettings();
        QVERIFY(!controller.updateInstrumentSetting("ionSourceSetpointKv", 5.0, true));
        QCOMPARE(controller.instrumentSettings(), settings);
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 5000);
        QCOMPARE(controller.activeMethod().id, before.id);
        controller.activateMethod(nextId);
        QCOMPARE(controller.activeMethod().id, nextId);
        QVERIFY(controller.updateInstrumentSetting("ionSourceSetpointKv", 5.0, true));
        qunsetenv("QITEST_OPERATOR_ROLE");
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void realReplyDuringAcquisitionWaitsForCompletion() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("source-boundary.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        QVERIFY(controller.startNetworkListening("127.0.0.1", 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, controller.networkStatus().value("port").toUInt());
        QTRY_VERIFY(controller.networkStatus().value("tcpConnected").toBool());
        controller.startDetection();
        QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
        client.write(test::networkStatusWire());
        QTRY_VERIFY(controller.networkStatus().value("connected").toBool());
        QVERIFY(controller.instrumentDescriptor().simulation);
        QCOMPARE(controller.telemetry().multiplierVoltageV, 1450.0);
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 5000);
        QCOMPARE(controller.currentRun().dataScope, QString("DEMO_SIMULATION"));
        QTRY_VERIFY(!controller.instrumentDescriptor().simulation);
        QCOMPARE(controller.telemetry().multiplierVoltageV, 3000.0);
        client.abort();
        QTRY_VERIFY(!controller.health().connected);
        QVERIFY(!controller.instrumentDescriptor().simulation);
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void completeMethodEditingSurvivesDeviceConnection() {
        QTemporaryDir dir;qputenv("QITEST_WORKSPACE_DB",dir.filePath("methods.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        qputenv("QITEST_OPERATOR_ROLE","admin");controller.setSessionOperator("admin");
        const QJsonObject base{{"scan_mode","Fullscan"},{"injection",12.34},{"source",4.9},{"td",40.0}};
        QVERIFY(controller.createMethodDraft("管理员方法",base));QString id;
        for(const auto &method:controller.methods()) if(method.name=="管理员方法") id=method.id;
        QVERIFY(!id.isEmpty());
        controller.activateMethod(id);
        QCOMPARE(controller.confirmedMethodParameters(), base);
        qputenv("QITEST_OPERATOR_ROLE","operator");controller.setSessionOperator("operator");
        QVERIFY(controller.fullMethodAccess());
        auto next=base;next.insert("scan_mode","SIM");next.insert("injection",600.0);
        next.insert("source",6.0);
        QVERIFY(controller.createMethodDraft("现场方法",next));
        QVERIFY(controller.startNetworkListening("127.0.0.1",0));
        QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(controller.realConnectionPending());
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, controller.networkStatus().value("port").toUInt());
        QTRY_VERIFY(controller.networkStatus().value("tcpConnected").toBool());
        client.write(test::networkStatusWire());
        QTRY_VERIFY(!controller.instrumentDescriptor().simulation);
        QVERIFY(!controller.realConnectionPending());
        QVERIFY(controller.fullMethodAccess());
        next.insert("source",7.0);
        QVERIFY(controller.createMethodDraft("联网方法",next));
        controller.stopNetworkListening();
        QVERIFY(!controller.requestRfTuning(true,true));
        QVERIFY(controller.useSimulatedInstrument());
        QCOMPARE(controller.confirmedMethodParameters(), base);
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
        QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(controller.realConnectionPending());
        controller.stopNetworkListening(); QVERIFY(controller.instrumentDescriptor().simulation);
        QVERIFY(!controller.realConnectionPending());
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
        auto instrument = std::make_unique<TestInstrument>();
        auto *device = instrument.get();
        AppController controller(std::move(instrument));
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
        QSignalSpy notices(&controller, &AppController::notice);
        controller.activateMethod(created.id);
        QCOMPARE(controller.activeMethod().id, created.id);
        QCOMPARE(controller.confirmedMethodParameters(), parameters);
        QVERIFY(!notices.isEmpty());
        QCOMPARE(notices.last().at(0).toString(), QString("当前方法已更新"));
        auto updatedParameters=parameters;updatedParameters.insert("injection",381.0);
        QVERIFY(controller.updateMethodDraft(created.id,created.name,updatedParameters));
        QVERIFY(controller.activeMethod().id.isEmpty());
        controller.activateMethod(created.id);
        QCOMPARE(controller.activeMethod().id,created.id);
        QCOMPARE(controller.confirmedMethodParameters(),updatedParameters);
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
    void realMethodSuccessRequiresMatchingDeviceReadback() {
        QTemporaryDir dir;
        qputenv("QITEST_WORKSPACE_DB", dir.filePath("method-real-ack.sqlite").toUtf8());
        qputenv("QITEST_OPERATOR_ROLE", "admin");
        auto fixture = std::make_unique<TestInstrument>();
        auto *device = fixture.get();
        AppController controller(std::move(fixture));
        controller.setSessionOperator("admin");
        const QJsonObject parameters{{"scan_mode","Fullscan"},{"carrier",1.0},{"extraction",0.0},
            {"inlet",50.0},{"td",0.0},{"source",0.0},{"trap",85.0},{"period",10000.0},
            {"speed",8000.0},{"rf_frequency",50.0},{"storage_mass",30.0},{"low_mass",40.0},
            {"high_mass",300.0},{"cooling",5000.0},{"ac_frequency",590.0},
            {"injection",380.0},{"multiplier",1000.0}};
        QVERIFY(controller.createMethodDraft("实机回传方法", parameters));
        QString createdId;
        for (const auto &method : controller.methods())
            if (method.name == "实机回传方法") createdId = method.id;
        QVERIFY(!createdId.isEmpty());
        QSignalSpy notices(&controller, &AppController::notice);
        controller.activateMethod(createdId);
        QCOMPARE(device->requestedMethod, parameters);
        QVERIFY(controller.activeMethod().id != createdId);
        QVERIFY(notices.isEmpty());
        device->acknowledgeMethod(true, parameters);
        QCOMPARE(controller.activeMethod().id, createdId);
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().at(0).toString(), QString("方法设置成功"));
        qunsetenv("QITEST_OPERATOR_ROLE");
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void networkDetectionUsesPresetTimeAndPersistsRawScans_data() {
        QTest::addColumn<bool>("withLibrary");
        QTest::newRow("raw-only")<<false;
        QTest::newRow("specified-lib-variable-ion-screening")<<true;
    }
    void networkDetectionUsesPresetTimeAndPersistsRawScans() {
        QFETCH(bool,withLibrary);
        QTemporaryDir dir;qputenv("QITEST_WORKSPACE_DB",dir.filePath("network-run.sqlite").toUtf8());
        test::FakeSerial port;
        int trapTenths=853;
        port.responder=[&](const QByteArray &r){
            const quint8 c=quint8(r[2]);auto payload=test::statusPayload();
            payload[17]=char(trapTenths>>8);payload[18]=char(trapTenths);
            return c==0x30?test::frame(payload):test::frame(QByteArray::fromHex("1100"),c);
        };
        auto serial=std::make_unique<Rs485Instrument>(&port,nullptr);QVERIFY(serial->openPort("TEST_ONLY"));
        QTRY_VERIFY(serial->health().connected);
        auto adapter=std::make_unique<NetworkInstrument>(std::move(serial));auto *network=adapter.get();
        AppController controller(std::move(adapter));
        QSignalSpy captureNotices(&controller,&AppController::notice);
        bool heartbeatPausedAtMethodNotice=false;
        connect(&controller,&AppController::notice,this,[&](const QString &notice){
            if(notice=="方法设置成功") heartbeatPausedAtMethodNotice=
                !network->statusDetails()["heartbeatActive"].toBool();
        });
        QVERIFY(network->startListening("127.0.0.1",0,10000));
        QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,network->statusDetails().value("port").toUInt());
        QTRY_VERIFY(network->statusDetails().value("tcpConnected").toBool());
        auto status=test::networkStatusWire().mid(7,21);status[9]=0;client.write(test::networkFrame(status));
        QTRY_VERIFY(network->statusDetails().value("connected").toBool());
        QVERIFY(controller.createMethodDraft("网口采集回归",MethodDraft::defaultParameters()));
        QString id;for(const auto &m:controller.methods())if(m.name=="网口采集回归")id=m.id;
        QVERIFY(!id.isEmpty());controller.activateMethod(id);
        QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_COMPARE(controller.activeMethod().id,id);
        QVERIFY(heartbeatPausedAtMethodNotice);
        QVERIFY(network->statusDetails()["heartbeatActive"].toBool());
        // Local active method remains after reconnect, but a fresh hardware ACK is required.
        client.disconnectFromHost();
        QTRY_VERIFY(!network->statusDetails().value("tcpConnected").toBool());
        client.connectToHost(QHostAddress::LocalHost,network->statusDetails().value("port").toUInt());
        QTRY_VERIFY(network->statusDetails().value("tcpConnected").toBool());
        client.write(test::networkFrame(status));QTRY_VERIFY(network->statusDetails().value("connected").toBool());
        QCOMPARE(controller.activeMethod().id,id);
        QVERIFY(!network->statusDetails().value("methodConfirmed").toBool());
        controller.startDetection();QVERIFY(controller.phase()!=AppController::Phase::Acquiring);
        QVERIFY(captureNotices.last()[0].toString().contains("网口连接变化"));
        QCOMPARE(client.bytesAvailable(),qint64(0));
        controller.activateMethod(id); // Reapply exactly the same active version.
        QTRY_VERIFY(client.bytesAvailable()>0);
        QCOMPARE(client.readAll(),NetworkProtocol::fullscanMethodCommand(MethodDraft::defaultParameters()));
        QVERIFY(!network->statusDetails().value("methodConfirmed").toBool());
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_VERIFY(network->statusDetails().value("methodConfirmed").toBool());
        QSignalSpy rejected(&controller,&AppController::detectionStartRejected);
        controller.activateMethod(id);QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        QVERIFY(!controller.checkDetectionStart()); // A previous ACK cannot approve a pending reapply.
        client.write(test::networkFrame(QByteArray::fromHex("12"),0x10,0x81));
        QTRY_VERIFY(!network->statusDetails().value("methodPending").toBool());
        QVERIFY(!controller.checkDetectionStart()); // Failed reapply also blocks the prior confirmation.
        controller.activateMethod(id);QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x81));
        QVERIFY(test::acknowledgeLegacyMethodFollowups(client));
        QTRY_VERIFY(!network->statusDetails().value("heartbeatPausedForMethod").toBool());
        // Check measured temperature, not the method setpoint. Equality must fail.
        for(int t:{849,850}) {
            trapTenths=t;auto payload=test::statusPayload();payload[17]=char(t>>8);payload[18]=char(t);
            port.deliver(test::frame(payload));QTRY_COMPARE(controller.telemetry().ionTrapTemperatureC,t/10.0);
            const int beforeReject=rejected.size();controller.startDetection();
            QCOMPARE(rejected.size(),beforeReject+1);
            QVERIFY(rejected.last()[0].toStringList().join(" ").contains("离子阱温度"));
            QVERIFY(controller.phase()!=AppController::Phase::Acquiring);QVERIFY(controller.recentRuns().isEmpty());
        }
        trapTenths=851;
        auto readyPayload=test::statusPayload();readyPayload[17]=char(trapTenths>>8);readyPayload[18]=char(trapTenths);
        port.deliver(test::frame(readyPayload));QTRY_COMPARE(controller.telemetry().ionTrapTemperatureC,85.1);
        for(int raw:{20000,17000,16000,12000,1234}) {
            status[2]=char(raw>>8);status[3]=char(raw);client.write(test::networkFrame(status));
            QTRY_COMPARE(network->statusDetails().value("vacuumRaw").toInt(),raw);
            const double pressure=NetworkProtocol::vacuumMbarFromRaw(raw);
            const int previousRejects=rejected.size();
            const bool allowed=controller.checkDetectionStart();
            QCOMPARE(allowed,pressure<0.01);
            if(!allowed) {
                QCOMPARE(rejected.size(),previousRejects+1);
                QCOMPARE(rejected.last()[0].toStringList().size(),1);
                QVERIFY(rejected.last()[0].toStringList()[0].contains("真空度"));
                client.readAll();controller.startDetection();QTest::qWait(5);
                QVERIFY(!client.readAll().contains(NetworkProtocol::detectionCommand(true)));
                QVERIFY(controller.phase()!=AppController::Phase::Acquiring);
            }
        }
        auto preset=controller.instrumentPreset();const auto before=preset;
        preset["libraryPath"]="";
        if(withLibrary) {
            QFile lib(dir.filePath("specified.lib"));QVERIFY(lib.open(QIODevice::WriteOnly));
            lib.write("[{\"name\":\"one-ion\",\"qualitify_ion\":\"100\",\"son_area\":\"1\"},"
                "{\"name\":\"two-ions\",\"qualitify_ion\":\"200,238\",\"son_area\":\"1,1\"},"
                "{\"name\":\"three-ions\",\"qualitify_ion\":\"100,200,238\",\"son_area\":\"1,1,1\"}]");lib.close();
            preset["libraryPath"]=lib.fileName();
        }
        preset["detectionTimeSeconds"]=1;QVERIFY(controller.saveInstrumentPreset(preset));
        controller.startDetection();QCOMPARE(controller.phase(),AppController::Phase::Acquiring);
        if(withLibrary)QVERIFY(QFile::remove(preset["libraryPath"].toString())); // Frozen at start, no later file dependency.
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(true));
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));
        QTest::qWait(20);
        const auto axis=NetworkProtocol::fullscanMassAxis(MethodDraft::defaultParameters());
        QByteArray bytes;for(int i=0;i<axis.size();++i)bytes.append(QByteArray::fromHex("0102"));
        auto wholeCycle=test::networkFrame(bytes,0x20,0x81,0,0);
        wholeCycle[wholeCycle.size()-3]=0;wholeCycle[wholeCycle.size()-2]=0;
        client.write(wholeCycle.left(1460));client.write(wholeCycle.mid(1460));
        QTRY_COMPARE(controller.scans().size(),1);
        QCOMPARE(controller.liveSpectrum()[0].intensity,258.0);
        QTRY_VERIFY_WITH_TIMEOUT(client.peek(client.bytesAvailable()).contains(NetworkProtocol::detectionCommand(false)),1800);
        QVERIFY(client.readAll().contains(NetworkProtocol::detectionCommand(false))); // Heartbeats may precede the timed stop.
        QCOMPARE(controller.phase(),AppController::Phase::Acquiring);QVERIFY(controller.currentRun().id.isEmpty());
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));
        QTRY_VERIFY_WITH_TIMEOUT(controller.phase()!=AppController::Phase::Acquiring && controller.phase()!=AppController::Phase::Analyzing,3000);
        QVERIFY2(controller.phase()==AppController::Phase::ResultReady,
            captureNotices.isEmpty()?"no notice":qPrintable(captureNotices.last()[0].toString()));
        QCOMPARE(controller.currentRun().dataScope,QString("DEVICE_UNVALIDATED"));
        QCOMPARE(controller.currentRun().sampleInfo.value("detection_time_seconds").toInt(),1);
        QCOMPARE(controller.currentRun().sampleInfo.value("mass_axis_profile").toObject(),NetworkProtocol::fullscanCalibrationProfile());
        QCOMPARE(controller.currentRun().sampleInfo.value("waveform_crc_policy").toString(),QString("record_only"));
        QCOMPARE(network->statusDetails()["waveformCrcMismatches"].toInt(),1);
        QCOMPARE(controller.result().candidates.size(),withLibrary?3:0);
        QCOMPARE(controller.result().screeningItems.size(),withLibrary?3:0);
        QCOMPARE(controller.currentRun().sampleInfo.value("screening_status").toString(),withLibrary?QString("COMPLETE"):QString("NOT_CONFIGURED"));
        const auto evidence=withLibrary?controller.result().candidates[0].evidence:QString();
        QCOMPARE(controller.result().processedSpectrum.points[0].intensity,258.0);
        QCOMPARE(controller.result().processedSpectrum.totalIonCurrent,axis.size()*258.0);
        const auto runId=controller.currentRun().id;controller.loadStoredRun(runId);
        QCOMPARE(controller.currentRun().sampleInfo.value("waveform_crc_policy").toString(),QString("record_only"));
        QCOMPARE(controller.scans().size(),1);QCOMPARE(controller.scans()[0].points[0].intensity,258.0);
        QCOMPARE(controller.result().candidates.size(),withLibrary?3:0);
        if(withLibrary)QCOMPARE(controller.result().candidates[0].evidence,evidence);
        // Cancel waits for a real stop reply; an unanswered stop is a failure.
        controller.startDetection();QTRY_VERIFY(client.bytesAvailable()>0);client.readAll();
        client.write(test::networkFrame(QByteArray::fromHex("11"),0x10,0x15));QTest::qWait(60);
        controller.cancelDetection();QCOMPARE(controller.phase(),AppController::Phase::Acquiring);
        QTRY_VERIFY(client.bytesAvailable()>0);QCOMPARE(client.readAll(),NetworkProtocol::detectionCommand(false));
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(),AppController::Phase::Failed,4000);
        QCOMPARE(controller.recentRuns().size(),1);
        const auto exportedPath=dir.filePath("raw-detection.qit.json");
        controller.exportRunArchive(runId,exportedPath);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(exportedPath),3000);
        const auto archive=RunArchiveCodec::read(exportedPath);QVERIFY2(archive.valid,qPrintable(archive.error));
        QCOMPARE(archive.sampleInfo.value("mass_axis_profile").toObject(),NetworkProtocol::fullscanCalibrationProfile());
        QCOMPARE(archive.scans.size(),1);QCOMPARE(archive.rawSpectrum[0].intensity,258.0);
        QSignalSpy imported(&controller,&AppController::importFinished);
        controller.importRunArchives({exportedPath});QTRY_COMPARE_WITH_TIMEOUT(imported.size(),1,3000);
        QCOMPARE(controller.result().processedSpectrum.points[0].intensity,258.0);
        QCOMPARE(controller.result().candidates.size(),withLibrary?3:0);
        QCOMPARE(controller.result().screeningItems.size(),withLibrary?3:0);
        if(withLibrary)QCOMPARE(controller.result().candidates[0].evidence,evidence);
        QCOMPARE(controller.scans().size(),1);
        QVERIFY(controller.saveInstrumentPreset(before));qunsetenv("QITEST_WORKSPACE_DB");
    }
};
QTEST_GUILESS_MAIN(InstrumentControlTests)
#include "InstrumentControlTests.moc"
