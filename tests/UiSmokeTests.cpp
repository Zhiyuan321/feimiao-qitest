#include "app/AppController.h"
#include "device/SimulatedInstrument.h"
#include "device/Rs485Instrument.h"
#include "Rs485TestDevice.h"
#include "NetworkTestFrames.h"
#include "PumpTestDevice.h"
#include "device/NetworkInstrument.h"
#include <cmath>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTabWidget>
#include <QTabBar>
#include "ui/MainWindow.h"
#include "ui/ChatTranscript.h"
#include "ui/ChromatogramDialog.h"
#include "ui/scientz/theme/ScientzTheme.h"
#include "ui/CalibrationPage.h"
#include "ui/UserStandardsPage.h"
#include "ui/MethodEditorDialog.h"
#include "ui/InstrumentWorkbench.h"
#include "ui/DeviceWaveformPanel.h"
#include "core/MethodDraft.h"
#include "core/MassAxisCalibration.h"
#include <QMessageBox>
#include <QTimer>
#include <QPlainTextEdit>
#include "storage/CalibrationDocument.h"
#include "storage/IntegrationDocument.h"
#include "ui/SpectrumPlot.h"
#include "storage/RunArchiveCodec.h"
#include "core/AnalysisEngine.h"
#include "core/ChromatogramEngine.h"
#include <QCryptographicHash>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QAction>
#include <QAccessible>
#include <QFile>
#include <QDir>
#include <QDialog>
#include <QComboBox>
#include <QScreen>
#include <QFontDatabase>
#include <QSplitter>
#include <QStackedWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QStyleOptionSpinBox>
#include <QScrollBar>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace qitest;

namespace {

template <typename T>
T *visibleWidgetWithText(QWidget &root, const QString &text) {
    for (auto *widget : root.findChildren<T *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}

QToolButton *commandButton(QWidget &root, const QString &actionId) {
    for (auto *button : root.findChildren<QToolButton *>())
        if (button->property("actionId").toString() == actionId) return button;
    return nullptr;
}

} // namespace

class UiSmokeTests final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settingsDirectory_;
private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        const auto font = qEnvironmentVariable("QITEST_UI_FONT");
        if (!font.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(font);
            QVERIFY(id >= 0);
            QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first()));
        }
        // Tests must not read/write the operator's registry or saved COM port.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory_.path());
    }
    void restrictedMethodPageAndPressureView() {
        const QJsonObject base{{"scan_mode","Fullscan"},{"injection",12.34},{"source",4.9}};QJsonObject saved;
        auto *editor=new MethodEditorDialog("管理员方法",base,[&](const QString &,const QJsonObject &p){saved=p;return true;},nullptr,false);
        editor->show();QTest::qWait(20);
        QVERIFY(!editor->findChild<QLineEdit *>("method_source"));
        auto *injection=editor->findChild<QLineEdit *>("method_injection");QVERIFY(injection);injection->setText("0.01");
        QVERIFY(editor->findChild<QPushButton *>("loadMethodDraft")->isEnabled());
        QVERIFY(editor->findChild<QPushButton *>("exportMethodDraft")->isEnabled());
        editor->findChild<QPushButton *>("saveMethodDraft")->click();
        QCOMPARE(saved.value("source").toDouble(),4.9);QCOMPARE(saved.value("injection").toDouble(),0.01);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTemporaryDir dir;qputenv("QITEST_WORKSPACE_DB",dir.filePath("pressure.sqlite").toUtf8());
        AppController controller(std::make_unique<SimulatedInstrument>());
        std::unique_ptr<QWidget> pressure(createDeviceWaveformPanel(&controller,false));pressure->resize(720,480);pressure->show();
        QVERIFY(controller.startNetworkListening("127.0.0.1",0));QTcpSocket client;
        QVERIFY(controller.fullMethodAccess());
        auto *connectedEditor=new MethodEditorDialog("联网方法",MethodDraft::defaultParameters(),
            [](const QString &,const QJsonObject &){return true;},nullptr,controller.fullMethodAccess());
        connectedEditor->show();QTest::qWait(20);
        for (const auto &field : MethodDraft::fields()) {
            if (field.group != "基本" && field.group != "扫描") continue;
            auto *edit=connectedEditor->findChild<QLineEdit *>("method_"+field.key);
            QVERIFY2(edit && edit->isVisibleTo(connectedEditor),qPrintable(field.key));
        }
        QVERIFY(!visibleWidgetWithText<QLabel>(*connectedEditor,
            "普通账号只可另存扫描模式和进样时间；其他参数沿用管理员方法。"));
        connectedEditor->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        client.connectToHost(QHostAddress::LocalHost,controller.networkStatus().value("port").toUInt());
        QTRY_VERIFY(controller.networkStatus().value("tcpConnected").toBool());
        client.write(test::networkFrame(QByteArray::fromHex("0000200040006000ffff"),0x20,0x82));
        auto *plot=pressure->findChild<QWidget *>("pressureVoltagePlot");QVERIFY(plot);
        QTRY_COMPARE(plot->property("sampleCount").toInt(),5);
        QVERIFY(pressure->findChild<QLabel *>("pressureWaveformStatus")->text().contains("14.25"));
        const auto capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
        if(!capture.isEmpty())QVERIFY(pressure->grab().save(capture+"/pressure-waveform.png"));
        std::unique_ptr<QWidget> rf(createDeviceWaveformPanel(&controller,true));rf->resize(720,480);rf->show();
        QCOMPARE(rf->findChildren<QPushButton *>().size(),2);
        QVERIFY(!rf->findChild<QPushButton *>("rfTuningStart")->isEnabled());
        if(!capture.isEmpty())QVERIFY(rf->grab().save(capture+"/rf-panel.png"));
        controller.stopNetworkListening();QTRY_COMPARE(plot->property("sampleCount").toInt(),0);
        qunsetenv("QITEST_WORKSPACE_DB");
    }
    void rs485StatusPanelReadsAndInvalidates();
    void networkPanelConnectsAlongside485();
    void bundledSamplesImportWithoutDuplicates();
    void externalArchivePreview();
    void customerResultReviewWorkflow();
    void fixedLandscapeNavigation();
    void professionalOfflineToolsValidateAndRemainUsable();
    void instrumentPowerButtonsReflectPartialState();
    void bundledExampleLoadsThreePlotsWithoutAi();
    void navigationAndAcquisitionRemainStable();
    void conversationIsBoundedAndSelectable();
    void traceAnalysisUsesImportedScans();
    void calibrationPageLoadsSavesAndCalculatesWithoutExtraNavigation();
    void densePlotsKeepFullDataButBoundPaintingAndExportOffThread();
    void calibrationEditsPreservePrecisionAndRejectStaleWrites();
    void userStandardsPageCreatesImportsAndExports();
    void userStandardComparisonExportsFrozenEvidence();
    void foreignSavedPathFallsBackToLocalDocuments();
};

void UiSmokeTests::networkPanelConnectsAlongside485() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    qputenv("QITEST_WORKSPACE_DB", directory.filePath("network-ui.sqlite").toUtf8());
    test::FakeSharedBus port;
    auto instrument = std::make_unique<Rs485Instrument>(&port, nullptr);
    AppController controller(std::move(instrument));
    MainWindow window(&controller); window.resize(1024, 768); window.show();
    auto *enter = visibleWidgetWithText<QPushButton>(window, "进入工作站"); QVERIFY(enter); enter->click();
    auto *workspace = window.findChild<QStackedWidget *>("centralWorkspace"); QVERIFY(workspace);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->isVisibleTo(&window), 4000);
    auto *settings = window.findChild<QAction *>("OpenSettings"); QVERIFY(settings); settings->trigger();
    auto *tree = window.findChild<QTreeWidget *>("settingsTree"); QVERIFY(tree);
    bool opened = false;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        auto *item = *it;
        if (item->data(0, Qt::UserRole + 1).toString() == "运行状态") {
            opened = QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0)); break;
        }
        ++it;
    }
    QVERIFY(opened);
    auto *sharedPort = window.findChild<QComboBox *>("rs485Port");
    auto *sharedConnect = window.findChild<QPushButton *>("rs485Connect");
    auto *includePump = window.findChild<QCheckBox *>("rs485IncludePump");
    QVERIFY(sharedPort && sharedConnect && includePump); includePump->setChecked(true);
    sharedPort->setCurrentText("TEST_ONLY"); sharedConnect->click();
    QTRY_VERIFY(controller.rs485Status().value("connected").toBool());
    auto *tabs = window.findChild<QTabWidget *>("communicationTabs"); QVERIFY(tabs); tabs->setCurrentIndex(1);
    auto *panel = window.findChild<QWidget *>("networkConnectionPanel");
    auto *table = window.findChild<QTableWidget *>("networkReadings");
    auto *address = window.findChild<QComboBox *>("networkAddress");
    auto *tcpPort = window.findChild<QSpinBox *>("networkPort");
    auto *listen = window.findChild<QPushButton *>("networkListen");
    auto *stop = window.findChild<QPushButton *>("networkStop");
    QVERIFY(panel && table && address && tcpPort && listen && stop); QVERIFY(panel->isVisibleTo(&window));
    QTcpServer reservation; QVERIFY(reservation.listen(QHostAddress::LocalHost));
    const auto portNumber = reservation.serverPort(); reservation.close();
    address->setCurrentText("127.0.0.1"); tcpPort->setValue(portNumber); listen->click();
    QVERIFY(controller.networkStatus().value("listening").toBool());
    QVERIFY(controller.rs485Status().value("connected").toBool());
    QVERIFY(controller.instrumentReadOnly()); QVERIFY(!controller.instrumentDescriptor().simulation);
    QCOMPARE(table->item(0, 1)->text(), QString("—"));
    QTcpSocket client; client.connectToHost(QHostAddress::LocalHost, portNumber);
    QTRY_VERIFY(controller.networkStatus().value("tcpConnected").toBool());
    client.write(test::networkStatusWire());
    QTRY_COMPARE(table->item(0, 1)->text(), QString("3000 V"));
    QCOMPARE(table->item(1, 1)->text(), QString("1234"));
    QCOMPARE(table->item(2, 1)->text(), QString("开启"));
    QCOMPARE(table->item(3, 1)->text(), QString("2.70E-05 mbar"));
    // The revised status uses byte 9 = 00 for OFF, regardless of the reserved tail.
    auto offPayload = test::networkStatusWire().mid(7, 21);
    offPayload[9] = 0x00; offPayload[20] = 0x11;
    client.write(test::networkFrame(offPayload));
    QTRY_COMPARE(table->item(2, 1)->text(), QString("关闭"));
    QCOMPARE(table->item(1, 1)->text(), QString("1234"));
    QTcpSocket replacement;
    replacement.connectToHost(QHostAddress::LocalHost, portNumber);
    QTRY_COMPARE(controller.networkStatus().value("replacedConnections").toInt(), 1);
    QTRY_COMPARE(table->item(1, 1)->text(), QString("—"));
    QCOMPARE(table->item(3, 1)->text(), QString("—"));
    replacement.write(test::networkFrame(offPayload));
    QTRY_COMPARE(table->item(1, 1)->text(), QString("1234"));
    QCOMPARE(table->item(2, 1)->text(), QString("关闭"));
    QCOMPARE(controller.telemetry().tdTemperatureC, 245.6);
    QCOMPARE(table->item(3, 1)->text(), QString("2.70E-05 mbar"));
    QVERIFY(!controller.updateInstrumentSetting("powerOn", true, true));
    controller.startDetection(); QVERIFY(controller.phase() != AppController::Phase::Acquiring);
    QCoreApplication::processEvents();
    QVERIFY(window.rect().contains(QRect(listen->mapTo(&window, QPoint()), listen->size())));
    QVERIFY(window.rect().contains(QRect(table->mapTo(&window, QPoint()), table->size())));
    const auto capture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(window.grab().save(capture + "/network-readback.png"));
    QVERIFY(controller.exportNetworkFrames(directory.filePath("tcp.json")));
    QCOMPARE(tabs->count(), 2);
    tabs->setCurrentIndex(0);
    auto *pumpTable = window.findChild<QTableWidget *>("rs485Readings");
    QVERIFY(pumpTable);
    QTRY_COMPARE_WITH_TIMEOUT(pumpTable->item(7, 1)->text(), QString("45.0 ℃"), 2500);
    QCOMPARE(pumpTable->item(4, 1)->text(), QString("1200 RPM"));
    QCOMPARE(pumpTable->item(5, 1)->text(), QString("0.80 A"));
    QCOMPARE(pumpTable->item(6, 1)->text(), QString("2.20 V"));
    QVERIFY(pumpTable->item(4, 2)->text().contains("001200"));
    QCOMPARE(pumpTable->item(3, 1)->text(), QString("801.0 Torr"));
    QCOMPARE(controller.telemetry().molecularPumpRpm, 1200.0);
    QCOMPARE(controller.health().ionSourceKv, 0.32); // Preserved when TCP is also connected.
    QCOMPARE(controller.telemetry().molecularPumpTemperatureC, 45.0);
    bool rpmFound = false, tempFound = false;
    for (auto *label : window.findChildren<QLabel *>()) {
        if (label->property("telemetryKey") == "pump") { QCOMPARE(label->text(), QString("1200")); rpmFound = true; }
        if (label->property("telemetryKey") == "pumpTemp") { QCOMPARE(label->text(), QString("45.0")); tempFound = true; }
    }
    QVERIFY(rpmFound && tempFound);
    QVERIFY(controller.networkStatus().value("connected").toBool());
    QCOMPARE(port.openCount, 1); QCOMPARE(port.closeCount, 0); QVERIFY(!port.overlap);
    QVERIFY(controller.exportPumpFrames(directory.filePath("bus.json")));
    QVERIFY(window.rect().contains(QRect(pumpTable->mapTo(&window, QPoint()), pumpTable->size())));
    QVERIFY(pumpTable->viewport()->rect().contains(pumpTable->visualItemRect(pumpTable->item(7, 1))));
    if (!capture.isEmpty()) QVERIFY(window.grab().save(capture + "/shared-485-readback.png"));
    auto *pageScroll = window.findChild<QScrollArea *>("rs485PageScroll");
    auto *note = window.findChild<QLabel *>("rs485ReadbackNote");
    QVERIFY(pageScroll && note);
    // Less available height must scroll, never paint the note over table rows.
    pageScroll->setFixedHeight(250);
    QCoreApplication::processEvents();
    QVERIFY(pageScroll->verticalScrollBar()->maximum() > 0);
    QVERIFY(note->geometry().top() > pumpTable->geometry().bottom());
    pageScroll->setMinimumHeight(0); pageScroll->setMaximumHeight(QWIDGETSIZE_MAX);
    tabs->setCurrentIndex(1);
    stop->click(); QCOMPARE(table->item(0, 1)->text(), QString("—"));
    QCOMPARE(table->item(3, 1)->text(), QString("—"));
    QVERIFY(controller.rs485Status().value("connected").toBool());
    QCOMPARE(controller.telemetry().tdTemperatureC, 245.6);
    controller.disconnectRs485(); QVERIFY(!controller.health().connected);
    // The same 485 adapter remains available after adding/stopping TCP.
    QVERIFY(controller.connectRs485("TEST_ONLY")); QTRY_VERIFY(controller.rs485Status().value("connected").toBool());
    controller.disconnectRs485();
    qunsetenv("QITEST_WORKSPACE_DB");
}

void UiSmokeTests::rs485StatusPanelReadsAndInvalidates() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    qputenv("QITEST_WORKSPACE_DB", directory.filePath("rs485-ui.sqlite").toUtf8());
    test::FakeSerial port;
    auto instrument = std::make_unique<Rs485Instrument>(&port, nullptr);
    auto *adapter = instrument.get();
    AppController controller(std::move(instrument));
    MainWindow window(&controller);
    window.resize(1024, 768);
    window.show();
    auto *enter = visibleWidgetWithText<QPushButton>(window, "进入工作站");
    QVERIFY(enter);
    QVERIFY(enter->isEnabled());
    enter->click();
    auto *workspace = window.findChild<QStackedWidget *>("centralWorkspace");
    QVERIFY(workspace);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->isVisibleTo(&window), 4000);
    auto *settings = window.findChild<QAction *>("OpenSettings");
    QVERIFY(settings);
    settings->trigger();
    auto *tree = window.findChild<QTreeWidget *>("settingsTree");
    QVERIFY(tree);
    bool opened = false;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        auto *item = *it;
        if (item->data(0, Qt::UserRole + 1).toString() == "运行状态") {
            opened = QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0));
            break;
        }
        ++it;
    }
    QVERIFY(opened);
    auto *panel = window.findChild<QWidget *>("rs485ConnectionPanel");
    auto *table = window.findChild<QTableWidget *>("rs485Readings");
    auto *connectButton = window.findChild<QPushButton *>("rs485Connect");
    QVERIFY(panel && table && connectButton);
    QVERIFY(panel->isVisibleTo(&window));
    QCOMPARE(table->item(0, 1)->text(), QString("—"));
    auto ionPayload = test::statusPayload(); ionPayload[7] = 0; ionPayload[8] = 49;
    port.reply = test::frame(ionPayload);
    QVERIFY(adapter->openPort("TEST_ONLY"));
    QTRY_COMPARE(table->item(0, 1)->text(), QString("245.6 ℃"));
    QCOMPARE(table->item(2, 1)->text(), QString("28.4 mL/min"));
    QCOMPARE(table->item(9, 1)->text(), QString("49"));
    QLabel *ionReading = nullptr;
    for (auto *label : window.findChildren<QLabel *>())
        if (label->property("telemetryKey") == "ion") ionReading = label;
    QVERIFY(ionReading); QCOMPARE(ionReading->text(), QString("4.9"));
    auto *ionUnit = ionReading->parentWidget()->findChild<QLabel *>("readoutUnit");
    QVERIFY(ionUnit); QCOMPARE(ionUnit->text(), QString("V"));
    QCOMPARE(controller.telemetry().ionSourceVoltageV, 4.9);
    QCOMPARE(table->item(12, 1)->text(), QString("内载气"));
    for (auto *widget : window.findChildren<QWidget *>())
        if (widget->property("instrumentControl").toBool()) QVERIFY(!widget->isEnabled());
    QCoreApplication::processEvents();
    QVERIFY(window.rect().contains(QRect(connectButton->mapTo(&window, QPoint()), connectButton->size())));
    QVERIFY(window.rect().contains(QRect(table->mapTo(&window, QPoint()), table->size())));
    const auto capture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(window.grab().save(capture + "/rs485-readback.png"));
    // A new poll must not navigate away from the user's selected control page.
    QTreeWidgetItemIterator controls(tree);
    while (*controls) {
        auto *item = *controls;
        if (item->data(0, Qt::UserRole + 1).toString() == "常用部件") {
            QVERIFY(QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0)));
            break;
        }
        ++controls;
    }
    auto *controlPages = window.findChild<QStackedWidget *>("instrumentControlPages");
    QVERIFY(controlPages && controlPages->isVisibleTo(&window));
    controller.disconnectRs485();
    QVERIFY(controlPages->isVisibleTo(&window));
    QCOMPARE(table->item(0, 1)->text(), QString("—"));
    QCOMPARE(table->item(9, 1)->text(), QString("—"));
    QCOMPARE(ionReading->text(), QString("—"));
    qunsetenv("QITEST_WORKSPACE_DB");
}

void UiSmokeTests::foreignSavedPathFallsBackToLocalDocuments() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("QITEST_WORKSPACE_DB", directory.filePath("path-fallback.sqlite").toUtf8());
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    settings.setValue("sampleSaveFolder", "/Users/other-computer/Documents/飞秒检测数据");

    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);
    window.show();
    auto *enter = visibleWidgetWithText<QPushButton>(window, "进入工作站");
    QVERIFY(enter);
    enter->click();
    auto *runAction = window.findChild<QAction *>("StartRun");
    QVERIFY(runAction);
    runAction->trigger();
    auto *dialog = window.findChild<QDialog *>("sampleSaveDialog");
    QVERIFY(dialog);
    const QString actual = QDir::fromNativeSeparators(
        dialog->findChild<QLineEdit *>("sampleSaveFolder")->text());
    const QString expected = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath("飞秒检测数据");
    QCOMPARE(QDir::cleanPath(actual), QDir::cleanPath(expected));
    QVERIFY(!actual.startsWith("/Users/other-computer/"));
    settings.remove("sampleSaveFolder");
}

void UiSmokeTests::customerResultReviewWorkflow() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("QITEST_WORKSPACE_DB", directory.filePath("customer-review.sqlite").toUtf8());

    auto *editor = new MethodEditorDialog("客户方法", {}, [](const QString &, const QJsonObject &) { return true; });
    editor->show();
    QCoreApplication::processEvents();
    QVERIFY(editor->findChildren<QScrollArea *>().isEmpty());
    for (const QString key : {QString("carrier"), QString("extraction"), QString("inlet"), QString("td"),
             QString("source"), QString("trap"), QString("period"), QString("speed"),
             QString("rf_frequency"), QString("storage_mass"), QString("low_mass"),
             QString("high_mass"), QString("cooling"), QString("ac_frequency"),
             QString("injection"), QString("multiplier")}) {
        auto *field = editor->findChild<QLineEdit *>("method_" + key);
        QVERIFY2(field, qPrintable(key));
        QVERIFY2(field->isVisibleTo(editor), qPrintable(key + " is hidden"));
        QVERIFY2(editor->rect().contains(QRect(field->mapTo(editor, QPoint()), field->size())),
                 qPrintable(key + " is outside the dialog"));
    }
    QCOMPARE(editor->findChild<QLineEdit *>("method_carrier")->text(), QString("1"));
    QCOMPARE(editor->findChild<QLineEdit *>("method_speed")->text(), QString("8000"));
    QCOMPARE(editor->findChild<QLineEdit *>("method_rf_frequency")->text(), QString("50"));
    QCOMPARE(editor->findChild<QLineEdit *>("method_multiplier")->text(), QString("1000"));
    for (auto *label : editor->findChildren<QLabel *>()) {
        if (!label->isVisibleTo(editor)) continue;
        QVERIFY(!label->text().contains("模拟"));
        QVERIFY(!label->text().contains("测试版"));
        QVERIFY(!label->text().contains("演示"));
    }
    QVERIFY(!editor->findChild<QPushButton *>("loadLegacyReference"));
    for (const QString text : {QString("打开文件"), QString("导出文件"), QString("保存新版本"), QString("取消")}) {
        auto *button = visibleWidgetWithText<QPushButton>(*editor, text);
        QVERIFY(button);
        QVERIFY(editor->rect().contains(QRect(button->mapTo(editor, QPoint()), button->size())));
    }
    const QImage editorImage = editor->grab().toImage();
    QVERIFY(editorImage.pixelColor(editorImage.width() / 2, editorImage.height() / 2).lightness() > 100);
    const QString captureDirectory = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) QVERIFY(editorImage.save(captureDirectory + "/method-editor-customer-review.png"));
    editor->findChild<QPushButton *>("saveMethodDraft")->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);
    window.show();
    if (!captureDirectory.isEmpty())
        QVERIFY(window.grab().save(captureDirectory + "/welcome-customer-review.png"));
    auto *enter = visibleWidgetWithText<QPushButton>(window, "进入工作站");
    QVERIFY(enter);
    enter->click();
    QTRY_VERIFY_WITH_TIMEOUT(commandButton(window, "OpenHome")->isVisibleTo(&window), 5000);
    auto *monitor = window.findChild<QScrollArea *>("monitorScroll");
    QVERIFY(monitor && monitor->isVisibleTo(&window));
    QVERIFY(monitor->width() >= 280);
    for (auto *readout : monitor->findChildren<QLabel *>()) {
        if (!readout->property("telemetryKey").isValid()) continue;
        QVERIFY(!readout->text().contains('\n'));
    }
    if (!captureDirectory.isEmpty())
        QVERIFY(window.grab().save(captureDirectory + "/workspace-monitor-customer-review.png"));
    for (auto *label : window.findChildren<QLabel *>()) {
        QVERIFY(!label->text().contains("当前使用模拟仪器"));
        QVERIFY(!label->text().contains("专业能力留在系统内部"));
    }

    window.findChild<QAction *>("OpenReport")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "结果复核与报告"), 1000);
    auto *openSaved = window.findChild<QPushButton *>("openSavedResult");
    auto *details = window.findChild<QPushButton *>("screeningDetails");
    auto *reviewView = window.findChild<QPushButton *>("reportReviewView");
    auto *previewView = window.findChild<QPushButton *>("reportPreviewView");
    QVERIFY(openSaved && openSaved->isVisibleTo(&window));
    QVERIFY(details && details->isVisibleTo(&window));
    QVERIFY(reviewView && previewView);
    QCOMPARE(reviewView->parentWidget(), openSaved->parentWidget());
    QCOMPARE(previewView->parentWidget(), openSaved->parentWidget());
    QVERIFY(!window.findChild<QWidget *>("embeddedReportNavigation"));
    details->click();
    auto *detailsDialog = window.findChild<QDialog *>("screeningDetailsDialog");
    QVERIFY(detailsDialog);
    auto *screeningTable = detailsDialog->findChild<QTableWidget *>("screeningDetailsTable");
    QVERIFY(screeningTable);
    auto *statusFilter = detailsDialog->findChild<QComboBox *>("screeningStatusFilter");
    auto *screeningSearch = detailsDialog->findChild<QLineEdit *>("screeningSearch");
    QVERIFY(statusFilter && screeningSearch);
    QCOMPARE(statusFilter->count(), 3);
    QCOMPARE(screeningTable->columnCount(), 6);
    QVERIFY(!visibleWidgetWithText<QLabel>(*detailsDialog,
        "当前为演示筛查面板；正式判定前需由客户确认离子列表、阈值和仪器适配结果。"));
    QVERIFY(!window.findChild<QPushButton *>("resultColumnOptions"));
    detailsDialog->close();

    auto *runAction = window.findChild<QAction *>("StartRun");
    QVERIFY(runAction);
    runAction->trigger();
    auto *sampleDialog = window.findChild<QDialog *>("sampleSaveDialog");
    QVERIFY(sampleDialog);
    sampleDialog->findChild<QLineEdit *>("sampleNumber")->setText("CUSTOMER-RESULT");
    sampleDialog->findChild<QLineEdit *>("sampleSaveFolder")->setText(directory.path());
    sampleDialog->findChild<QLineEdit *>("sampleFileName")->setText("customer-result");
    sampleDialog->findChild<QPushButton *>("confirmSampleStart")->click();
    QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "结果复核与报告"), 1000);
    auto *completed=window.findChild<QDialog *>("detectionCompletedDialog");QVERIFY(completed);
    completed->findChild<QPushButton *>("confirmDetectionCompleted")->click();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto *results = window.findChild<QTableWidget *>("reportScreeningResults");
    QVERIFY(results);
    auto *candidateSearch = window.findChild<QLineEdit *>("reportCandidateSearch");
    QVERIFY(candidateSearch);
    for (int row = 0; row < results->rowCount(); ++row)
        QCOMPARE(results->item(row, 3)->text(), QString("可疑"));
    details->click();
    detailsDialog = window.findChild<QDialog *>("screeningDetailsDialog");
    QVERIFY(detailsDialog);
    screeningTable = detailsDialog->findChild<QTableWidget *>("screeningDetailsTable");
    statusFilter = detailsDialog->findChild<QComboBox *>("screeningStatusFilter");
    screeningSearch = detailsDialog->findChild<QLineEdit *>("screeningSearch");
    QVERIFY(statusFilter && screeningSearch);
    QCOMPARE(screeningTable->rowCount(), demoReferences().size());
    QCOMPARE(screeningTable->horizontalHeaderItem(0)->text(), QString("序号"));
    QCOMPARE(screeningTable->horizontalHeaderItem(5)->text(), QString("是否检出"));
    QVERIFY(detailsDialog->findChild<QPushButton *>("screeningNextPage")->isEnabled());
    QCOMPARE(screeningTable->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    statusFilter->setCurrentText("未检出");
    screeningSearch->setText("甲基苯丙胺");
    QCoreApplication::processEvents();
    int visibleRows = 0;
    for (int row = 0; row < screeningTable->rowCount(); ++row)
        if (!screeningTable->isRowHidden(row)) ++visibleRows;
    QVERIFY(visibleRows <= 1);
    if (!captureDirectory.isEmpty())
        QVERIFY(detailsDialog->grab().save(captureDirectory + "/screening-details-customer-review.png"));
    detailsDialog->close();
    if (!captureDirectory.isEmpty()) QVERIFY(window.grab().save(captureDirectory + "/report-customer-review.png"));
    qunsetenv("QITEST_WORKSPACE_DB");
}

void UiSmokeTests::bundledSamplesImportWithoutDuplicates() {
    QTemporaryDir database;
    qputenv("QITEST_WORKSPACE_DB", database.filePath("bundled.sqlite").toUtf8());
    AppController controller(std::make_unique<SimulatedInstrument>());
    controller.setOfflineDemoSession();
    QSignalSpy done(&controller, &AppController::importFinished);
    controller.loadBundledCustomerSamples();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
    QCOMPARE(controller.recentRuns().size(), 21);
    const auto trend = controller.bundledIntensityTrend();
    QCOMPARE(trend.size(), 21);
    const auto first = RunArchiveCodec::read(":/qitest/resources/customer_samples/biscuit-ms-spectrum-01.qit.json");
    double sum = 0, extracted = 0;
    for (const auto &point : first.rawSpectrum) {
        sum += point.intensity;
        if (std::abs(point.mz - 304.0) <= 0.5) extracted += point.intensity;
    }
    QCOMPARE(trend.first().mz, 1.0);
    QCOMPARE(trend.first().intensity, sum);
    QCOMPARE(controller.bundledIntensityTrend(304.0, 0.5).first().intensity, extracted);
    QCOMPARE(controller.currentRun().dataScope, QString("IMPORTED_UNVALIDATED"));
    QCOMPARE(controller.result().processedSpectrum.points.size(), 4600);
    QVERIFY(controller.scans().isEmpty());
    const auto id = controller.currentRun().id;
    controller.loadBundledCustomerSamples();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
    QCOMPARE(controller.recentRuns().size(), 21);
    QCOMPARE(controller.currentRun().id, id);
    QSignalSpy saved(&controller, &AppController::archiveGenerated);
    controller.startSampleDetection({{"sample_id", "repeat-import"}}, database.filePath("repeat.qit.json"));
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 5000);
    QCOMPARE(controller.currentRun().dataScope, QString("IMPORTED_UNVALIDATED"));
    QCOMPARE(controller.bundledIntensityTrend().size(), 21);
    QCOMPARE(controller.bundledIntensityTrend().first().intensity, sum);
    const auto exported = RunArchiveCodec::read(database.filePath("repeat.qit.json"));
    QVERIFY(exported.valid);
    QCOMPARE(exported.rawSpectrum.size(), 4600);
}

void UiSmokeTests::externalArchivePreview() {
    const auto path=qEnvironmentVariable("QITEST_PREVIEW_ARCHIVE");
    if(path.isEmpty())QSKIP("No external archive requested");
    QTemporaryDir database;
    qputenv("QITEST_WORKSPACE_DB",database.filePath("preview.sqlite").toUtf8());
    const QDir directory(QFileInfo(path).absolutePath());
    for(const auto &name:directory.entryList({"*.qit.json"},QDir::Files))
        QVERIFY2(RunArchiveCodec::read(directory.filePath(name)).valid,qPrintable(name));
    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);window.show();window.resize(1024,768);
    auto *enter=visibleWidgetWithText<QPushButton>(window,"进入工作站");QVERIFY(enter);enter->click();
    QTRY_VERIFY_WITH_TIMEOUT(commandButton(window,"OpenHome")->isVisibleTo(&window),5000);
    controller.importRunArchive(path);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.currentRun().id.isEmpty(),5000);
    QCOMPARE(controller.currentRun().dataScope,QString("IMPORTED_UNVALIDATED"));
    QCOMPARE(controller.result().processedSpectrum.points.size(),4600);
    QVERIFY(controller.scans().isEmpty());
    commandButton(window,"OpenHome")->click();
    QTest::qWait(100);
    const auto capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grab().save(capture+"/customer-spectrum.png"));
}

void UiSmokeTests::fixedLandscapeNavigation() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir dir;
    qputenv("QITEST_WORKSPACE_DB", dir.filePath("landscape.sqlite").toUtf8());
    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller); window.show();
    QCOMPARE(qApp->font().pixelSize(), 15);
    auto *enter = visibleWidgetWithText<QPushButton>(window, "进入工作站");
    QVERIFY(enter); enter->click();
    QTRY_VERIFY_WITH_TIMEOUT(commandButton(window, "OpenHome")->isVisibleTo(&window),4000);
    window.resize(800, 480); QTest::qWait(50);
    QVERIFY(window.width() >= 1024); QVERIFY(window.height() >= 700);
    window.resize(1024,768);
    window.findChild<QAction *>("OpenHome")->trigger();
    window.findChild<QAction *>("OpenSettings")->trigger();
    auto *tree = window.findChild<QTreeWidget *>("settingsTree");
    auto *section = window.findChild<QComboBox *>("settingsSection");
    QVERIFY(tree); QVERIFY(section); QVERIFY(tree->isVisibleTo(&window));
    section->setCurrentIndex(0);
    QWheelEvent wheel(QPointF(section->rect().center()),
        QPointF(section->mapToGlobal(section->rect().center())), QPoint(), QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(section, &wheel);
    QCOMPARE(section->currentIndex(), 0); // 页面滚动不得意外切换设置分类。
    // A windowed Win7 desktop may reserve title/taskbar pixels. Navigation must
    // preserve the established client area; full-screen switching is tested below.
    QTest::qWait(150);
    const QSize clientArea = window.size();
    QCOMPARE(clientArea.width(), 1024);
    QVERIFY(clientArea.height() >= 700 && clientArea.height() <= 768);
    for (int s = 0; s < 3; ++s) {
        section->setCurrentIndex(s); QTest::qWait(30);
        QCOMPARE(tree->verticalScrollBar()->maximum(), 0);
        for (int i=0; i<tree->topLevelItemCount(); ++i) {
            auto *item = tree->topLevelItem(i);
            QVERIFY(item->text(0) != "硬件接入说明");
            if(item->isHidden()) continue;
            QMetaObject::invokeMethod(tree,"itemClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,item),Q_ARG(int,0));
            QTest::qWait(30);
            QVERIFY(tree->isVisibleTo(&window));
            QCOMPARE(window.size(), clientArea);
            QVERIFY(window.findChild<QWidget *>("centralWorkspace")->isVisibleTo(&window));
            if (item->text(0) == "质量轴校准") {
                auto *page = window.findChild<QWidget *>("workbench_质量轴校准");
                QVERIFY(page);
                for (const auto &id : {"calculateWorkbench", "loadWorkbench", "saveWorkbench"}) {
                    auto *action = page->findChild<QPushButton *>(id);
                    QVERIFY(action && action->isVisibleTo(&window));
                    QVERIFY(page->rect().contains(QRect(action->mapTo(page,QPoint(0,0)),action->size())));
                }
                const auto capture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
                if (!capture.isEmpty()) QVERIFY(window.grab().save(capture + "/mass-workbench.png"));
            }
        }
    }
    section->setCurrentIndex(0);
    auto *item=tree->topLevelItem(1);
    QMetaObject::invokeMethod(tree,"itemClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,item),Q_ARG(int,0));
    for(const QSize size:{QSize(1024,700),QSize(1024,768),QSize(1280,800),QSize(1024,768)}) {
        window.resize(size); QTest::qWait(60);
        QVERIFY(tree->isVisibleTo(&window));
        QVERIFY(window.findChild<QWidget *>("centralWorkspace")->isVisibleTo(&window));
        QCOMPARE(tree->verticalScrollBar()->maximum(), 0);
        auto *monitor = window.findChild<QScrollArea *>("monitorScroll");
        QVERIFY(monitor);
        QCOMPARE(monitor->verticalScrollBar()->maximum(), 0);
    }
    const auto capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) QVERIFY(window.grab().save(capture+"/fixed-landscape.png"));
    section->setCurrentIndex(2);
    for (int i=0; i<tree->topLevelItemCount(); ++i) {
        auto *view = tree->topLevelItem(i);
        if (view->data(0,Qt::UserRole).toString() != "视图") continue;
        QMetaObject::invokeMethod(tree,"itemClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,view),Q_ARG(int,0));
        auto *screenAction = window.findChild<QPushButton *>("fullScreenAction");
        QVERIFY(screenAction && screenAction->isVisibleTo(&window));
        screenAction->click();
        QTRY_VERIFY(window.isFullScreen());
        QCOMPARE(screenAction->text(), QString("退出全屏"));
        screenAction->click();
        QTRY_VERIFY(!window.isFullScreen());
    }
    window.findChild<QToolButton *>("closeSettingsButton")->click();
    QVERIFY(!tree->isVisibleTo(&window));
    qunsetenv("QITEST_WORKSPACE_DB");
}


void UiSmokeTests::instrumentPowerButtonsReflectPartialState() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir dir;
    qputenv("QITEST_WORKSPACE_DB", dir.filePath("power-ui.sqlite").toUtf8());
    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);
    auto *on = window.findChild<QPushButton *>("instrumentStartupOn");
    auto *off = window.findChild<QPushButton *>("instrumentStartupOff");
    auto *state = window.findChild<QLabel *>("instrumentStartupState");
    auto *ready = window.findChild<QLabel *>("instrumentReadiness3");
    QVERIFY(on && off && state && ready);
    QVERIFY(on->isChecked());
    off->click();
    QVERIFY(off->isChecked()); QVERIFY(!on->isChecked());
    QCOMPARE(state->text(), QString("已关闭"));
    QCOMPARE(ready->text(), QString("未就绪"));
    auto *deviceState = window.findChild<QLabel *>("runDeviceState");
    auto *detectionTime = window.findChild<QLabel *>("runDetectionTime");
    auto *softwareTime = window.findChild<QLabel *>("runSoftwareTime");
    QVERIFY(deviceState && detectionTime && softwareTime);
    QVERIFY(deviceState->text().contains("系统 · 未就绪"));
    QCOMPARE(detectionTime->text(), QString("检测用时 —"));
    QVERIFY(softwareTime->text().startsWith("软件运行 "));
    QVERIFY(deviceState->toolTip().isEmpty());
    QVERIFY(detectionTime->toolTip().isEmpty());
    QVERIFY(softwareTime->toolTip().isEmpty());
    const QStringList keys{"rfOn", "ionHighVoltageOn", "diaphragmPumpOn",
        "molecularPumpOn", "pinchValveOn", "internalCarrierGasOn"};
    for (const auto &key : keys) {
        auto *button = window.findChild<QToolButton *>("instrumentControl_" + key);
        QVERIFY(button); QVERIFY(!button->isChecked());
        QVERIFY(!button->icon().isNull());
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
        QVERIFY(!button->icon().pixmap(72, 72).isNull());
        QVERIFY(button->text().endsWith("已关闭"));
    }
    window.findChild<QToolButton *>("instrumentControl_rfOn")->click();
    QVERIFY(!on->isChecked()); QVERIFY(!off->isChecked());
    QCOMPARE(state->text(), QString("部分开启"));
    on->click();
    QVERIFY(on->isChecked()); QVERIFY(!off->isChecked());
    QCOMPARE(state->text(), QString("已开启"));
    QCOMPARE(ready->text(), QString("✓ 允许采集"));
    for (const auto &key : keys)
        QVERIFY(window.findChild<QToolButton *>("instrumentControl_" + key)->isChecked());
    off->toggle(); // Also used by native accessibility checkbox activation.
    QVERIFY(off->isChecked()); QVERIFY(!on->isChecked());
    QVERIFY(!controller.health().ready);
    window.findChild<QToolButton *>("instrumentControl_rfOn")->toggle();
    QCOMPARE(state->text(), QString("部分开启"));
    on->toggle();
    QVERIFY(controller.health().ready);
    qunsetenv("QITEST_WORKSPACE_DB");
}

void UiSmokeTests::bundledExampleLoadsThreePlotsWithoutAi() {
    QStandardPaths::setTestModeEnabled(true);
    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);
    window.show();
    QVERIFY(controller.scans().isEmpty()); // Never insert a sample on startup.
    auto *enter=visibleWidgetWithText<QPushButton>(window,"进入工作站"); QVERIFY(enter); enter->click();
    auto *home=commandButton(window,"OpenHome"); QVERIFY(home);
    QTRY_VERIFY_WITH_TIMEOUT(home->isVisibleTo(&window),5000);
    home->click();
    auto *analysisTabs=window.findChild<QTabWidget *>("analysisViewTabs");QVERIFY(analysisTabs);
    auto *analysisTabBar=analysisTabs->tabBar();QVERIFY(analysisTabBar);QVERIFY(analysisTabBar->isVisibleTo(&window));
    const QImage tabImage=analysisTabBar->grab().toImage();QVERIFY(!tabImage.isNull());
    for(int index=0;index<analysisTabBar->count();++index) {
        const QRect rect=analysisTabBar->tabRect(index);
        QVERIFY2(tabImage.pixelColor(rect.left()+4,rect.center().y()).lightness()>140,
            qPrintable(analysisTabBar->tabText(index)));
    }
    auto *example=window.findChild<QPushButton *>("loadPublicExample"); QVERIFY(example);
    QVERIFY(example->isVisibleTo(&window));
    example->click();
    QTRY_COMPARE_WITH_TIMEOUT(controller.scans().size(),64,5000);
    QCOMPARE(controller.currentRun().dataScope,QString("PUBLIC_EXAMPLE"));
    QVERIFY(controller.deepAiEnabled());
    auto *mode=window.findChild<QComboBox *>("aiModeSelector"); QVERIFY(mode);
    QCOMPARE(mode->count(),4);
    QCOMPARE(mode->currentText(),QString("自动"));
    mode->setCurrentText("关闭"); QVERIFY(!controller.deepAiEnabled());
    mode->setCurrentText("自动"); QVERIFY(controller.deepAiEnabled());
    for (const auto &name:{"runPrimaryPlot","runMsPlot","runEicPlot"}) {
        auto *plot=window.findChild<SpectrumPlot *>(name); QVERIFY(plot);
        QTRY_VERIFY_WITH_TIMEOUT(plot->points().size()>1,1000);
    }
    QVERIFY(!example->isVisible());
    const auto id=controller.currentRun().id;
    QSignalSpy finished(&controller,&AppController::importFinished);
    controller.loadPublicExample();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
    QCOMPARE(controller.currentRun().id,id); // The same sample reuses its record.
    const auto capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        QVERIFY(window.grab().save(capture+"/bundled-example.png"));
    }
}

void UiSmokeTests::userStandardComparisonExportsFrozenEvidence() {
    Scientz::Ui::ThemeManager::apply(*qApp, Scientz::Ui::Density::Standard);
    QTemporaryDir dir; QString error;
    UserStandard standard; standard.name="比对测试标准"; standard.ionization="EI";
    standard.provenance="synthetic test fixture, not an identified substance";
    standard.peaks={{10,3},{20,4}};
    const QString inputFile=dir.filePath("test.qstd.json");
    QVERIFY(UserStandardRepository::writeFile(inputFile,standard,&error));
    UserStandardsPage page(dir.filePath("user.sqlite")); page.resize(800,640); page.show();
    QVERIFY(page.importFile(inputFile,&error));
    page.setComparisonProvider([] { return StandardComparisonInput{{{10,6},{20,8}},"fixture-record","合成峰表"}; });
    page.findChild<QTableWidget *>("userStandardsTable")->selectRow(0);
    auto *compare=page.findChild<QPushButton *>("compareUserStandard"); QVERIFY(compare->isEnabled()); compare->click();
    auto *dialog=page.findChild<StandardComparisonDialog *>(); QVERIFY(dialog);
    auto *calculate=dialog->findChild<QPushButton *>("calculateStandardComparison");
    auto *conditions=dialog->findChild<QCheckBox *>("comparisonConditions");
    auto *tolerance=dialog->findChild<QDoubleSpinBox *>("comparisonTolerance");
    QVERIFY(!calculate->isEnabled()); QVERIFY(!dialog->exportEvidence(dir.filePath("no.json"),&error));
    conditions->setChecked(true); calculate->click();
    QCOMPARE(dialog->findChild<QTableWidget *>("standardComparisonPairs")->rowCount(),2);
    QVERIFY(dialog->findChild<QLabel *>("standardComparisonSummary")->text().contains("100.00"));
    const QString output=dir.filePath("comparison.qcompare.json"); QVERIFY(dialog->exportEvidence(output,&error));
    QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto envelope=QJsonDocument::fromJson(file.readAll()).object();
    const auto bytes=QByteArray::fromBase64(envelope["payload_base64"].toString().toLatin1());
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()),envelope["sha256"].toString());
    const auto payload=QJsonDocument::fromJson(bytes).object();
    QCOMPARE(payload["record_id"].toString(),QString("fixture-record"));
    QCOMPARE(payload["reference_peaks"].toArray().size(),2);
    QCOMPARE(payload["query_peaks"].toArray().size(),2);
    QCOMPARE(payload["pairs"].toArray().size(),2);
    tolerance->setValue(0.1); QVERIFY(!dialog->exportEvidence(dir.filePath("stale.json"),&error));
    calculate->click();
    const QString capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    QCoreApplication::processEvents();
    if(!capture.isEmpty()) QVERIFY(dialog->grab().save(capture+"/standard-comparison.png"));
    conditions->setChecked(false); QVERIFY(!dialog->exportEvidence(dir.filePath("unconfirmed.json"),&error));
    dialog->close();
}

void UiSmokeTests::userStandardsPageCreatesImportsAndExports() {
    QTemporaryDir dir; QString error;
    UserStandardsPage page(dir.filePath("standard.sqlite")); page.resize(780,650); page.show();
    page.findChild<QPushButton *>("newUserStandard")->click();
    auto *dialog=page.findChild<QDialog *>("userStandardEditor"); QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("standardName")->setText("用户验证标准");
    dialog->findChild<QLineEdit *>("standardIonization")->setText("EI");
    dialog->findChild<QLineEdit *>("standardProvenance")->setText("synthetic UI test only");
    dialog->findChild<QPlainTextEdit *>("standardPeaks")->setPlainText("31,500\n46,1000");
    dialog->findChild<QPushButton *>("saveUserStandard")->click();
    QPointer<QDialog> guard(dialog); QTRY_VERIFY(guard.isNull());
    auto *table=page.findChild<QTableWidget *>("userStandardsTable"); QCOMPARE(table->rowCount(),1);
    table->selectRow(0); QVERIFY(page.findChild<QPushButton *>("editUserStandard")->isEnabled());
    const auto path=dir.filePath("saved.qstd.json"); QVERIFY2(page.exportSelected(path,&error),qPrintable(error));
    QVERIFY(page.importFile(path,&error)); QCOMPARE(table->rowCount(),1);
    table->selectRow(0); page.findChild<QPushButton *>("editUserStandard")->click();
    dialog=page.findChild<QDialog *>("userStandardEditor"); QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("standardName")->setText("修订后的标准");
    dialog->findChild<QPushButton *>("saveUserStandard")->click();
    guard=dialog; QTRY_VERIFY(guard.isNull()); QCOMPARE(table->item(0,2)->text(),QString("2"));
    table->selectRow(0); page.findChild<QPushButton *>("editUserStandard")->click();
    dialog=page.findChild<QDialog *>("userStandardEditor"); QVERIFY(dialog);
    auto *saveAs=dialog->findChild<QPushButton *>("saveAsUserStandard"); QVERIFY(saveAs);
    saveAs->click(); QVERIFY(dialog->isVisible()); QCOMPARE(table->rowCount(),1);
    dialog->findChild<QLineEdit *>("standardName")->setText("另存标准");
    saveAs->click(); guard=dialog; QTRY_VERIFY(guard.isNull()); QCOMPARE(table->rowCount(),2);
    UserStandardRepository repository(dir.filePath("standard.sqlite")); QVERIFY(repository.open(&error));
    const auto originals=repository.search("修订后的标准",0,&error);
    QCOMPARE(originals.size(),1); QCOMPARE(originals[0].revision,2);
    const auto copies=repository.search("另存标准",0,&error);
    QCOMPARE(copies.size(),1); QCOMPARE(copies[0].revision,1);
    QVERIFY(originals[0].id != copies[0].id);
    table->selectRow(0); page.findChild<QPushButton *>("editUserStandard")->click();
    dialog=page.findChild<QDialog *>("userStandardEditor"); QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("standardName")->setText("不保存的修改");
    QTimer::singleShot(0, [] {
        if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Cancel)->click();
    });
    dialog->reject(); QVERIFY(dialog->isVisible());
    QTimer::singleShot(0, [] {
        if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Discard)->click();
    });
    guard=dialog; dialog->close(); QTRY_VERIFY(guard.isNull());
    QCOMPARE(repository.search("不保存的修改",0,&error).size(),0);
    table->selectRow(0);
    const auto capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    QVERIFY(page.grab().toImage().pixelColor(2,2).lightness() > 160);
    if(!capture.isEmpty()) { QCoreApplication::processEvents(); QVERIFY(page.grab().save(capture+"/user-standards.png")); }
}

void UiSmokeTests::calibrationEditsPreservePrecisionAndRejectStaleWrites() {
    QTemporaryDir dir; QString error;
    CalibrationModel model; model.name="精度回归";
    model.observations={{1,2.1234567891234567,0,0,true},{2,4,0,0,true},{3,6,0,0,true}};
    const auto path=dir.filePath("precision.qcal.json");
    QVERIFY(CalibrationDocument::save(path,model,&error));
    CalibrationPage page; page.show(); QVERIFY(page.loadFile(path,&error));
    auto *table=page.findChild<QTableWidget *>("calibrationPoints");
    table->cellDoubleClicked(0,1);
    auto *edit=page.findChild<QDialog *>("calibrationPointDialog"); QVERIFY(edit);
    auto *response=edit->findChild<QLineEdit *>("pointResponse"); QVERIFY(response);
    QCOMPARE(response->text().toDouble(),model.observations[0].response);
    auto *apply=edit->findChild<QPushButton *>("applyCalibrationPoint");
    response->setText("nan"); apply->click(); QCOMPARE(page.model().observations[0].response,model.observations[0].response);
    response->setText(QString::number(model.observations[0].response,'g',17)); apply->click();
    QCOMPARE(page.model().observations[0].response,model.observations[0].response);
    QPointer<QDialog> guard(edit); QTRY_VERIFY(guard.isNull());
    table->cellDoubleClicked(0,1); edit=page.findChild<QDialog *>("calibrationPointDialog"); QVERIFY(edit);
    QVERIFY(page.loadFile(path,&error)); // Programmatic change while an editor is open.
    edit->findChild<QLineEdit *>("pointResponse")->setText("3");
    edit->findChild<QPushButton *>("applyCalibrationPoint")->click();
    QCOMPARE(page.model().observations[0].response,model.observations[0].response);
    QVERIFY(edit->findChild<QLabel *>("pointEditFeedback")->text().contains("已变化"));
    guard=edit; edit->close(); QTRY_VERIFY(guard.isNull());
    QAction *add=nullptr;
    for(auto *action:page.findChildren<QAction *>()) if(action->text()=="添加校准点") add=action;
    QVERIFY(add); add->trigger();
    edit=page.findChild<QDialog *>("calibrationPointDialog"); QVERIFY(edit);
    edit->findChild<QLineEdit *>("pointConcentration")->setText("4");
    edit->findChild<QLineEdit *>("pointResponse")->setText("8.1234567891234567");
    edit->findChild<QPushButton *>("applyCalibrationPoint")->click();
    QCOMPARE(page.model().observations.size(),size_t(4));
    QCOMPARE(page.model().observations.back().response,8.1234567891234567);
}

void UiSmokeTests::densePlotsKeepFullDataButBoundPaintingAndExportOffThread() {
    SpectrumPlot plot(SpectrumPlot::Mode::Line); plot.resize(600,300); plot.show();
    QVector<SpectrumPoint> points;
    for(int i=0;i<100000;++i) points.append({i*0.001, i==43210 ? 10000.0:double(i%31)});
    plot.setPoints(points); QVERIFY(!plot.grab().isNull());
    QCOMPARE(plot.points().size(),100000);
    QVERIFY(plot.renderedPointCount()<2400);
    const int count=plot.cacheBuildCount();
    for(int i=0;i<20;++i) { QTest::mouseMove(&plot,QPoint(100+i*10,100)); plot.grab(); }
    QCOMPARE(plot.cacheBuildCount(),count);
    plot.zoomAt(43.21,0.2); QVERIFY(plot.isZoomed()); plot.grab();
    QCOMPARE(plot.cacheBuildCount(),count+1); QCOMPARE(plot.points()[43210].intensity,10000.0);
    plot.resetView(); QVERIFY(!plot.isZoomed());
    QTemporaryDir dir; const auto path=dir.filePath("full.csv");
    QSignalSpy completed(&plot,&SpectrumPlot::exportFinished);
    QVERIFY(plot.exportCsv(path)); QVERIFY(!plot.exportCsv(dir.filePath("duplicate.csv")));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(),1,10000); QVERIFY(completed.first()[0].toBool());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    int rows=0; while(!file.atEnd()) { const auto line=file.readLine(); if(!line.startsWith('#') && !line.startsWith("x,y")) ++rows; }
    QCOMPARE(rows,100000); QVERIFY(!QFile::exists(dir.filePath("duplicate.csv")));
}

void UiSmokeTests::calibrationPageLoadsSavesAndCalculatesWithoutExtraNavigation() {
    Scientz::Ui::ThemeManager::apply(*qApp, Scientz::Ui::Density::Standard);
    QTemporaryDir dir; QString error;
    CalibrationModel model; model.name="内标校准"; model.standard=CalibrationModel::Standard::Internal;
    model.internalStandard="IS-A"; model.observations={{2,21,2,10,true},{8,82,4,20,true},{30,202,6,20,true}};
    const auto path=dir.filePath("input.qcal.json"); QVERIFY(CalibrationDocument::save(path,model,&error));
    CalibrationPage page; page.resize(760,570); page.show();
    QCoreApplication::processEvents();
    auto *contentPanel = page.findChild<QFrame *>("calibrationContentPanel");
    QVERIFY(contentPanel);
    QCOMPARE(contentPanel->property("sciRole").toString(), QString("workspaceSection"));
    for (auto *section : page.findChildren<QWidget *>()) {
        if (section == contentPanel) continue;
        QVERIFY2(section->property("sciRole").toString() != "workspaceSection",
            "calibration subdivisions must not add touching rounded cards");
    }
    auto *split = page.findChild<QSplitter *>("calibrationSplit"); QVERIFY(split);
    QVERIFY(split->sizes().at(1) >= split->sizes().at(0));
    for (const QString key : {QString("calibrationName"), QString("calibrationUnit"), QString("calibrationResponseUnit")}) {
        auto *edit = page.findChild<QLineEdit *>(key); QVERIFY(edit);
        QVERIFY(edit->height() <= 34);
        QVERIFY(page.rect().contains(QRect(edit->mapTo(&page,QPoint()),edit->size())));
    }
    QVERIFY2(page.loadFile(path,&error),qPrintable(error));
    auto *table=page.findChild<QTableWidget *>("calibrationPoints"); QVERIFY(table); QCOMPARE(table->rowCount(),3);
    auto *calculate=page.findChild<QPushButton *>("calculateCalibrationSample"); QVERIFY(calculate && calculate->isEnabled());
    calculate->click();
    auto *dialog=page.findChild<QDialog *>("calibrationSampleDialog"); QVERIFY(dialog);
    dialog->findChild<QDoubleSpinBox *>("sampleResponse")->setValue(61);
    dialog->findChild<QDoubleSpinBox *>("sampleInternalResponse")->setValue(10);
    dialog->findChild<QDoubleSpinBox *>("sampleInternalConcentration")->setValue(2);
    dialog->findChild<QPushButton *>("calculateSample")->click();
    QVERIFY(dialog->findChild<QLabel *>("sampleConcentrationResult")->text().startsWith("浓度 6 "));
    dialog->findChild<QDoubleSpinBox *>("sampleInternalResponse")->setValue(0);
    QVERIFY(dialog->findChild<QLabel *>("sampleConcentrationResult")->text().contains("重新计算"));
    dialog->findChild<QPushButton *>("calculateSample")->click();
    QVERIFY(!dialog->findChild<QLabel *>("sampleConcentrationResult")->text().startsWith("浓度"));
    QPointer<QDialog> guard(dialog); dialog->close(); QTRY_VERIFY(guard.isNull());
    QVERIFY(page.saveFile(dir.filePath("saved.qcal.json"),&error));
    table->item(0,0)->setCheckState(Qt::Unchecked); QVERIFY(!calculate->isEnabled());
    table->item(0,0)->setCheckState(Qt::Checked); QVERIFY(calculate->isEnabled());
    const auto previous=page.model().name;
    QVERIFY(!page.loadFile(dir.filePath("missing.json"),&error)); QCOMPARE(page.model().name,previous);
    const QString capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    QVERIFY(page.grab().toImage().pixelColor(2,2).lightness() > 160);
    if(!capture.isEmpty()) { QCoreApplication::processEvents(); QVERIFY(page.grab().save(capture+"/calibration.png")); }
}

void UiSmokeTests::navigationAndAcquisitionRemainStable() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir workspaceDir;
    struct RestoreWorkspaceEnv {
        QByteArray before=qgetenv("QITEST_WORKSPACE_DB");
        ~RestoreWorkspaceEnv(){if(before.isNull())qunsetenv("QITEST_WORKSPACE_DB");else qputenv("QITEST_WORKSPACE_DB",before);}
    } restoreWorkspaceEnv;
    qputenv("QITEST_WORKSPACE_DB",workspaceDir.filePath("navigation.sqlite").toUtf8());
    AppController controller(std::make_unique<SimulatedInstrument>());
    MainWindow window(&controller);
    window.show();
    QSignalSpy localAnswers(&controller, &AppController::aiAssistantAnswerReady);
    controller.askAiAssistant("我怎么生成报告？");
    QCOMPARE(localAnswers.count(), 1);
    QVERIFY(localAnswers.first().at(1).toString().contains("复核"));
    controller.askAiAssistant("解释当前状态");
    QCOMPARE(localAnswers.count(), 2);
    QVERIFY(controller.deepAiEnabled());
    auto *aiMode = window.findChild<QComboBox *>("aiModeSelector");
    QVERIFY(aiMode);
    QCOMPARE(aiMode->count(), 4);
    aiMode->setCurrentText("关闭");
    QVERIFY(!controller.deepAiEnabled());
    aiMode->setCurrentText("自动");
    QVERIFY(controller.deepAiEnabled());
    QCOMPARE(aiMode->currentText(), QString("自动"));
    QVERIFY(!window.findChild<QWidget *>("assistantSuggestions"));
    QVERIFY(!window.findChild<QLabel *>("deepAiPurpose"));
    auto *offline = visibleWidgetWithText<QPushButton>(window, "进入工作站");
    QVERIFY(offline);
    QTest::mouseClick(offline, Qt::LeftButton);
    auto *homeButton = commandButton(window, "OpenHome");
    QVERIFY(homeButton);
    QCOMPARE(homeButton->text(), QString("样品分析"));
    QVERIFY(homeButton->width() >= homeButton->fontMetrics().horizontalAdvance(homeButton->text()) + 24);
    QVERIFY(!commandButton(window, "StartRun"));
    QVERIFY(!commandButton(window, "DataMenu"));
    QVERIFY(!commandButton(window, "OpenRecords"));
    for (const QString id : {QString("OpenMethod"), QString("OpenReport")}) {
        auto *direct = commandButton(window, id);
        QVERIFY(direct && !direct->menu());
    }
    QVERIFY(!commandButton(window, "OpenLibrary"));
    QVERIFY(!commandButton(window, "OpenQuantitation"));
    QVERIFY(!visibleWidgetWithText<QPushButton>(window, "运行当前方法"));
    QTRY_VERIFY_WITH_TIMEOUT(homeButton->isVisibleTo(&window), 5000);
    const auto phaseBeforeNavigation = controller.phase();
    homeButton->click();
    QCOMPARE(controller.phase(), phaseBeforeNavigation);
    QVERIFY(!commandButton(window, "OpenLockSession"));
    auto *topSettingsButton = commandButton(window, "OpenSettings");
    QVERIFY(topSettingsButton);
    QCOMPARE(topSettingsButton->text(), QString("设置"));
    auto *topMonitorButton = commandButton(window, "OpenInstrumentStatus");
    QVERIFY(topMonitorButton);
    QVERIFY(topSettingsButton->mapTo(&window, QPoint()).x() < topMonitorButton->mapTo(&window, QPoint()).x());
    auto *methodButton = commandButton(window, "OpenMethod");
    QVERIFY(methodButton);
    QCOMPARE(methodButton->text(), QString("方法选择"));
    QVERIFY(methodButton->x() < homeButton->x());
    QCOMPARE(commandButton(window, "OpenReport")->text(), QString("报告查看"));
    QVERIFY(window.findChild<QLabel *>("batteryStatus"));
    QVERIFY(!window.findChild<QAction *>("OpenPowerSession"));

    auto *assistantAction = window.findChild<QAction *>("OpenAssistant");
    QVERIFY(assistantAction);
    assistantAction->trigger();
    auto *assistantRail = window.findChild<QWidget *>("assistantRail");
    QVERIFY(assistantRail);
    QTRY_VERIFY_WITH_TIMEOUT(assistantRail->isVisibleTo(&window), 1000);

    QLineEdit *commandInput = nullptr;
    for (auto *edit : assistantRail->findChildren<QLineEdit *>())
        if (edit->property("sciRole").toString() == "assistantInput") commandInput = edit;
    QVERIFY(commandInput);
    window.resize(1152, 768);
    QTest::qWait(240);
    const QPoint inputOrigin = commandInput->mapTo(assistantRail, QPoint(0, 0));
    QVERIFY(inputOrigin.x() >= 0);
    QVERIFY(inputOrigin.y() >= 0);
    QVERIFY(inputOrigin.x() + commandInput->width() <= assistantRail->width());
    QVERIFY(inputOrigin.y() + commandInput->height() <= assistantRail->height());
    commandInput->setText("我要生成报告");
    auto *send = visibleWidgetWithText<QPushButton>(*assistantRail, "发送");
    QVERIFY(send);
    QTest::mouseClick(send, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "结果复核与报告"), 1000);

    auto *monitorScroll = window.findChild<QWidget *>("monitorScroll");
    QVERIFY(monitorScroll);
    auto *instrumentAction = window.findChild<QAction *>("OpenInstrumentStatus");
    QVERIFY(instrumentAction);
    if (!monitorScroll->isVisibleTo(&window)) instrumentAction->trigger();
    QVERIFY(!assistantRail->isVisibleTo(&window));
    QVERIFY(monitorScroll->isVisibleTo(&window));
    QCOMPARE(monitorScroll->width(), 300);
    auto *centralWorkspace = window.findChild<QWidget *>("centralWorkspace");
    QVERIFY(centralWorkspace);
    for (const int viewportWidth : {1152, 1280, 1366, 1440, 1600}) {
        const int availableWidth = QGuiApplication::primaryScreen()->availableGeometry().width() - 16;
        // Desktop rails apply above the 1024-pixel embedded breakpoint.
        const int requestedWidth = qMax(1152, qMin(viewportWidth, availableWidth));
        window.resize(requestedWidth, 768);
        QTest::qWait(80);
        QCOMPARE(window.width(), requestedWidth);
        QVERIFY(centralWorkspace->width() >= 640);
        QVERIFY(assistantRail->width() <= 320);
        QVERIFY(centralWorkspace->width() > assistantRail->width());
        auto *settingsNavigation = window.findChild<QWidget *>("settingsSidebarRight");
        QVERIFY(settingsNavigation);
        QVERIFY(centralWorkspace->width() - settingsNavigation->width() >= 500);
        // The outer rail fitting is insufficient: its scroll-area child and
        // every reading/control must also fit, otherwise AlwaysOff hides clips.
        auto *monitorArea = qobject_cast<QScrollArea *>(monitorScroll);
        QVERIFY(monitorArea);
        QCOMPARE(monitorArea->horizontalScrollBar()->maximum(), 0);
        QVERIFY(monitorArea->widget()->width() <= monitorArea->viewport()->width());
        for (auto *child : monitorArea->widget()->findChildren<QWidget *>()) {
            if (!child->isVisibleTo(monitorArea->widget())) continue;
            const int right = child->mapTo(monitorArea->widget(), child->rect().topRight()).x();
            QVERIFY2(right < monitorArea->widget()->width(), qPrintable(child->objectName()));
            if (auto *label = qobject_cast<QLabel *>(child)) {
                if (label->objectName().startsWith("readout"))
                    QVERIFY2(label->contentsRect().width() >= label->fontMetrics().horizontalAdvance(label->text()),
                        qPrintable(label->text()));
            }
        }
        const QRect centerRect(centralWorkspace->mapTo(&window, QPoint(0, 0)), centralWorkspace->size());
        for (auto *rail : {assistantRail, monitorScroll}) {
            if (!rail->isVisibleTo(&window)) continue;
            const QRect railRect(rail->mapTo(&window, QPoint(0, 0)), rail->size());
            QVERIFY(window.rect().contains(railRect));
            QVERIFY(!centerRect.intersects(railRect));
        }
        if (viewportWidth == 1024) {
            QVERIFY(monitorScroll->height() >= 180);
            QVERIFY(assistantRail->height() >= 320);
            QCOMPARE(assistantRail->mapTo(&window, QPoint(0, 0)).x(), monitorScroll->mapTo(&window, QPoint(0, 0)).x());
            QVERIFY(assistantRail->mapTo(&window, QPoint(0, 0)).y() < monitorScroll->mapTo(&window, QPoint(0, 0)).y());
        }
    }
    QVERIFY(!window.findChild<QWidget *>("monitorFooter"));
    for (auto *label : monitorScroll->findChildren<QLabel *>()) {
        QVERIFY(label->text() != "●");
        QVERIFY(label->text() != "◆");
    }
    QVERIFY(!visibleWidgetWithText<QPushButton>(*monitorScroll, "设置与维护"));
    auto *pressureValue = window.findChild<QLabel *>("carrierPressureValue");
    QVERIFY(pressureValue);
    QCOMPARE(pressureValue->text(), QString("801.0 Torr"));
    QVERIFY(pressureValue->isVisibleTo(&window));
    QVERIFY(!window.findChild<QToolButton *>("pressureDetailsToggle"));
    QVERIFY(!window.findChild<QLabel *>("pressureDetails"));

    // Exercise the real Qt step hit targets, including bounds and keyboard input.
    auto *transcript = window.findChild<ChatTranscript *>("assistantTranscript");
    QVERIFY(transcript);
    const int messagesBeforeNavigation = transcript->messageCount();
    commandInput->setText("打开参数预设");
    QTest::keyClick(commandInput, Qt::Key_Return);
    QVERIFY(visibleWidgetWithText<QLabel>(window, "参数预设"));
    QCOMPARE(transcript->messageCount(), messagesBeforeNavigation + 2);
    auto *presetTree = window.findChild<QTreeWidget *>("settingsTree");
    QVERIFY(presetTree);
    QCOMPARE(presetTree->currentItem()->data(0, Qt::UserRole + 1).toString(), QString("参数预设"));
    auto *temperature = window.findChild<QSpinBox *>("presetTrapTemperature");
    QVERIFY(temperature && temperature->isVisibleTo(&window));
    QTest::qWait(80);
    QStyleOptionSpinBox stepOption;
    stepOption.initFrom(temperature);
    stepOption.frame = true;
    stepOption.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    stepOption.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
    const auto stepRect = [&](QStyle::SubControl control) {
        return temperature->style()->subControlRect(QStyle::CC_SpinBox, &stepOption, control, temperature);
    };
    const QRect plus = stepRect(QStyle::SC_SpinBoxUp);
    const QRect minus = stepRect(QStyle::SC_SpinBoxDown);
    QVERIFY(plus.width() >= 32 && plus.height() >= 30);
    QVERIFY(minus.width() >= 32 && minus.height() >= 30);
    QVERIFY(!plus.intersects(minus));
    QVERIFY(temperature->rect().contains(plus) && temperature->rect().contains(minus));
    const int savedTemperature = temperature->value();
    temperature->setValue(50);
    QTest::mouseClick(temperature, Qt::LeftButton, Qt::NoModifier, plus.center());
    QCOMPARE(temperature->value(), 51);
    QTest::mouseClick(temperature, Qt::LeftButton, Qt::NoModifier, minus.center());
    QCOMPARE(temperature->value(), 50);
    QTest::keyClick(temperature, Qt::Key_Up);
    QCOMPARE(temperature->value(), 51);
    temperature->setValue(temperature->maximum());
    QTest::mouseClick(temperature, Qt::LeftButton, Qt::NoModifier, plus.center());
    QCOMPARE(temperature->value(), temperature->maximum());
    temperature->setValue(temperature->minimum());
    QTest::mouseClick(temperature, Qt::LeftButton, Qt::NoModifier, minus.center());
    QCOMPARE(temperature->value(), temperature->minimum());
    temperature->setValue(savedTemperature);
    const QString presetCapture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!presetCapture.isEmpty()) {
        QVERIFY(QDir().mkpath(presetCapture));
        QVERIFY(window.grab().save(presetCapture + "/presets.png"));
    }

    auto *methodAction = window.findChild<QAction *>("OpenMethod");
    QVERIFY(methodAction);
    methodAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "编辑方法"), 1000);
    auto *methodTable = window.findChild<QTableWidget *>("methodTable");
    QVERIFY(methodTable);
    QVERIFY(methodTable->columnWidth(5) >= 170);
    QLineEdit *methodName = nullptr;
    for (auto *edit : window.findChildren<QLineEdit *>())
        if (edit->placeholderText() == "方法名称") methodName = edit;
    QVERIFY(methodName);
    QCOMPARE(methodName->maxLength(), 16);
    auto *editMethodParameters = window.findChild<QPushButton *>("editMethodParameters");
    QVERIFY(editMethodParameters);
    QCOMPARE(editMethodParameters->text(), QString("编辑参数"));
    auto *activateMethod = visibleWidgetWithText<QPushButton>(window, "设为当前方法");
    QVERIFY(activateMethod);
    QVERIFY(!activateMethod->isEnabled());
    controller.createDemoMethodVersion("激活回归方法", "UI 点击回归");
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (int row = 0; row < methodTable->rowCount(); ++row) {
            const auto *state = methodTable->item(row, 0);
            const auto *name = methodTable->item(row, 1);
            if (!methodTable->isRowHidden(row) && state && name
                && name->text() == "激活回归方法"
                && !state->data(Qt::UserRole + 1).toBool()) return true;
        }
        return false;
    }(), 1000);
    int activationRow = -1;
    for (int row = 0; row < methodTable->rowCount(); ++row) {
        if (!methodTable->isRowHidden(row) && methodTable->item(row, 0)
            && methodTable->item(row, 1)
            && methodTable->item(row, 1)->text() == "激活回归方法"
            && !methodTable->item(row, 0)->data(Qt::UserRole + 1).toBool()) {
            activationRow = row;
            break;
        }
    }
    QVERIFY(activationRow >= 0);
    methodTable->selectRow(activationRow);
    QTRY_VERIFY_WITH_TIMEOUT(activateMethod->isEnabled(), 1000);
    QSignalSpy methodsChanged(&controller, &AppController::methodsChanged);
    QTest::mouseClick(activateMethod, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(methodsChanged.count() >= 1, 1000);
    QCOMPARE(controller.activeMethod().name, QString("激活回归方法"));
    QCOMPARE(methodTable->currentRow(), -1);
    QTest::qWait(220);

    // Exercise the macOS crash path repeatedly while Accessibility traverses
    // the same table. Activation must not synchronously rewrite selected items.
    for (int iteration = 0; iteration < 12; ++iteration) {
        int inactiveRow = -1;
        for (int row = 0; row < methodTable->rowCount(); ++row) {
            const auto *state = methodTable->item(row, 0);
            if (!methodTable->isRowHidden(row) && state
                && !state->data(Qt::UserRole + 1).toBool()) {
                inactiveRow = row;
                break;
            }
        }
        QVERIFY(inactiveRow >= 0);
        methodTable->selectRow(inactiveRow);
        QTRY_VERIFY_WITH_TIMEOUT(activateMethod->isEnabled(), 500);
        QTest::mouseClick(activateMethod, Qt::LeftButton);
        QCOMPARE(methodTable->currentRow(), -1);
        if (auto *accessible = QAccessible::queryAccessibleInterface(methodTable)) {
            (void)accessible->childCount();
            (void)accessible->text(QAccessible::Name);
        }
        for (int row = 0; row < methodTable->rowCount(); ++row)
            for (int column = 0; column < methodTable->columnCount(); ++column)
                if (const auto *cell = methodTable->item(row, column))
                    (void)cell->text();
        QTest::qWait(220);
    }

    QVERIFY(!window.findChild<QAction *>("OpenRecords"));
    QVERIFY(!window.findChild<QTableWidget *>("recordsTable"));

    auto *libraryAction = window.findChild<QAction *>("OpenLibrary");
    QVERIFY(libraryAction);
    libraryAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "参考谱库"), 1000);
    auto *librarySurface = window.findChild<QWidget *>("standardLibraryTabs"); QVERIFY(librarySurface);
    auto *libraryPages = window.findChild<QStackedWidget *>("standardLibraryPages"); QVERIFY(libraryPages);
    auto *privateLibrary = window.findChild<QPushButton *>("userLibrarySelector"); QVERIFY(privateLibrary);
    auto *publicLibrary = window.findChild<QPushButton *>("publicLibrarySelector"); QVERIFY(publicLibrary);
    privateLibrary->click(); QCOMPARE(libraryPages->currentIndex(),1);
    publicLibrary->click(); QCOMPARE(libraryPages->currentIndex(),0);
    QCoreApplication::processEvents();
    auto *publicTable = window.findChild<QTableWidget *>("publicLibraryTable");
    QVERIFY(publicTable);
    QVERIFY(publicTable->columnWidth(0) >= 100);
    for (int column : {1, 4, 5, 6}) QVERIFY(publicTable->isColumnHidden(column));
    QCOMPARE(publicTable->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    const auto libraryImage = librarySurface->grab().toImage();
    QVERIFY(libraryImage.pixelColor(4,4).lightness() > 160);
    const auto libraryCapture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!libraryCapture.isEmpty()) QVERIFY(libraryImage.save(libraryCapture + "/library-surface.png"));

    auto *quantitationAction = window.findChild<QAction *>("OpenQuantitation");
    QVERIFY(quantitationAction);
    quantitationAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(visibleWidgetWithText<QLabel>(window, "定量曲线"), 1000);

    auto *settingsAction = window.findChild<QAction *>("OpenSettings");
    QVERIFY(settingsAction);
    // The quantitation/library routes already opened settings. A second press
    // must close it and restore the preceding records page without losing state.
    settingsAction->trigger();
    auto *settingsSidebar = window.findChild<QWidget *>("settingsSidebarRight");
    QVERIFY(settingsSidebar);
    QVERIFY(!settingsSidebar->isVisibleTo(&window));
    auto *settingsWorkspace = window.findChild<QStackedWidget *>("centralWorkspace");
    QVERIFY(settingsWorkspace);
    QCOMPARE(settingsWorkspace->currentIndex(), 3);
    auto *closeSettingsButton = window.findChild<QToolButton *>("closeSettingsButton");
    QVERIFY(closeSettingsButton);
    for (int repeat = 0; repeat < 8; ++repeat) {
        settingsAction->trigger();
        QCOMPARE(settingsWorkspace->currentIndex(), 1);
        QVERIFY(settingsSidebar->isVisibleTo(&window));
        if (repeat % 2) closeSettingsButton->click();
        else settingsAction->trigger();
        QCOMPARE(settingsWorkspace->currentIndex(), 3);
        QVERIFY(!settingsSidebar->isVisibleTo(&window));
    }
    settingsAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(settingsSidebar->isVisibleTo(&window), 1000);
    auto *settingsTree = window.findChild<QTreeWidget *>("settingsTree");
    QVERIFY(settingsTree);
    QCOMPARE(settingsTree->indentation(), 0);
    QVERIFY(!settingsTree->itemsExpandable());
    QVERIFY(!window.findChild<QListWidget *>("settingsList"));
    const auto openModule = [&](const QString &module) {
        for (int i = 0; i < settingsTree->topLevelItemCount(); ++i) {
            auto *item = settingsTree->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toString() == module)
                return QMetaObject::invokeMethod(settingsTree, "itemClicked", Qt::DirectConnection,
                    Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0));
        }
        return false;
    };
    auto *settingsPrimary = window.findChild<QPushButton *>("settingsPrimaryAction");
    QVERIFY(settingsPrimary);
    int hardwareInfoCount = 0;
    QTreeWidgetItem *hardwareInfo = nullptr;
    QTreeWidgetItemIterator uniquePages(settingsTree);
    while (*uniquePages) {
        auto *item = *uniquePages;
        const auto label = item->text(0).trimmed();
        QVERIFY(label != "调谐与校准" && label != "进样与注射泵" && label != "抽取清洗液");
        if (label == "硬件接入说明") { ++hardwareInfoCount; hardwareInfo = item; }
        ++uniquePages;
    }
    QCOMPARE(hardwareInfoCount, 0);
    QVERIFY(openModule("仪器配置"));
    bool openedControls = false;
    QTreeWidgetItemIterator controlsIterator(settingsTree);
    while (*controlsIterator) {
        auto *item = *controlsIterator;
        if (item->data(0, Qt::UserRole + 1).toString() == "常用部件") {
            openedControls = QMetaObject::invokeMethod(settingsTree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0));
            break;
        }
        ++controlsIterator;
    }
    QVERIFY(openedControls);
    QVERIFY(!window.findChild<QComboBox *>("instrumentControlGroup"));
    auto *controlPages = window.findChild<QStackedWidget *>("instrumentControlPages");
    auto *auxiliaryKey = window.findChild<QComboBox *>("auxiliaryControlKey");
    auto *auxiliarySwitch = window.findChild<QComboBox *>("auxiliarySwitchValue");
    auto *auxiliaryNumber = window.findChild<QSpinBox *>("auxiliaryNumericValue");
    QVERIFY(controlPages && auxiliaryKey && auxiliarySwitch && auxiliaryNumber);
    const auto openControlPage = [&](const QString &name) {
        QTreeWidgetItemIterator it(settingsTree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole + 1).toString() == name)
                return QMetaObject::invokeMethod(settingsTree, "itemClicked", Qt::DirectConnection,
                    Q_ARG(QTreeWidgetItem *, *it), Q_ARG(int, 0));
            ++it;
        }
        return false;
    };
    QVERIFY(openControlPage("辅助部件"));
    QCOMPARE(controlPages->currentIndex(), 1);
    QVERIFY(auxiliaryKey->isVisibleTo(&window));
    QCOMPARE(auxiliaryKey->count(), 6);
    auxiliaryKey->setCurrentIndex(auxiliaryKey->findData("coolingFanOn"));
    QCOMPARE(auxiliaryKey->currentText(), QString("风扇（FAN）"));
    QVERIFY(auxiliaryKey->toolTip().contains("0x05"));
    QVERIFY(auxiliarySwitch->isVisibleTo(&window));
    QVERIFY(!auxiliaryNumber->isVisibleTo(&window));
    const auto beforeEdit = controller.instrumentSettings();
    auxiliarySwitch->setCurrentIndex(1);
    QCOMPARE(controller.instrumentSettings(), beforeEdit); // Editing never sends a command.
    auxiliaryKey->setCurrentIndex(auxiliaryKey->findData("tdTemperatureC"));
    QVERIFY(auxiliaryNumber->isVisibleTo(&window));
    QVERIFY(!auxiliarySwitch->isVisibleTo(&window));
    QVERIFY(openControlPage("常用部件"));
    QCOMPARE(controlPages->currentIndex(), 0);
    QVERIFY(openModule("参考谱库"));
    QVERIFY(openModule("定量曲线"));
    QVERIFY(!openModule("标准与校准"));
    for (const QString module : {QString("帮助"), QString("用户及参数设置"), QString("锁屏")}) {
        QVERIFY(openModule(module));
        QVERIFY(settingsPrimary->isVisibleTo(&window));
        settingsPrimary->click();
        const QString dialogName = module == "锁屏" ? "operationGuard" : "settingsInformation";
        auto *dialog = window.findChild<QDialog *>(dialogName);
        QVERIFY(dialog && dialog->isVisible());
        if (module == "锁屏") {
            QTest::keyClick(dialog, Qt::Key_Escape);
            QVERIFY(dialog->isVisible());
        }
        auto *done = dialog->findChild<QPushButton *>("settingsDialogDone");
        QVERIFY(done);
        done->click();
        QTRY_VERIFY(!window.findChild<QDialog *>(dialogName));
    }
    QVERIFY(openModule("视图"));
    QCOMPARE(settingsPrimary->text(), QString("恢复默认布局"));
    settingsPrimary->click();
    QVERIFY(!assistantRail->isVisibleTo(&window));
    QVERIFY(monitorScroll->isVisibleTo(&window));
    for (const QSize size : {QSize(1024, 680), QSize(1280, 800)}) {
        window.resize(size);
        for (int groupIndex = 0; groupIndex < settingsTree->topLevelItemCount(); ++groupIndex) {
            auto *group = settingsTree->topLevelItem(groupIndex);
            QCOMPARE(group->childCount(), 0);
            if (group->data(0, Qt::UserRole).toString().isEmpty()) {
                QVERIFY(!(group->flags() & Qt::ItemIsSelectable));
                continue;
            }
            const QString originalLabel = group->text(0);
            const int originalRow = settingsTree->indexOfTopLevelItem(group);
            QVERIFY(QMetaObject::invokeMethod(settingsTree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, group), Q_ARG(int, 0)));
            QTest::qWait(40);
            settingsTree->scrollToItem(group);
            if(size.width()>1024) {
                QVERIFY(settingsTree->viewport()->rect().intersects(settingsTree->visualItemRect(group)));
                QCOMPARE(settingsTree->horizontalScrollBar()->maximum(), 0);
                QVERIFY(settingsTree->mapTo(&window, settingsTree->rect().bottomRight()).y() < window.height());
            } else QVERIFY(settingsTree->isVisibleTo(&window));
            int expanded = 0;
            for (int index = 0; index < settingsTree->topLevelItemCount(); ++index)
                if (settingsTree->topLevelItem(index)->isExpanded()) ++expanded;
            QCOMPARE(expanded, 0);
            QCOMPARE(settingsTree->currentItem(), group);
            QCOMPARE(group->text(0), originalLabel);
            QCOMPARE(settingsTree->indexOfTopLevelItem(group), originalRow);
            // Clicking the active entry again must not hide or collapse it.
            QVERIFY(QMetaObject::invokeMethod(settingsTree, "itemClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem *, group), Q_ARG(int, 0)));
            QCOMPARE(settingsTree->currentItem(), group);
            QVERIFY(!group->isHidden());
        }
    }
    for (auto *label : window.findChildren<QLabel *>()) {
        if (!label->isVisibleTo(&window)) continue;
        QVERIFY2(!label->text().contains("Word 2.3.5.1"),
            "development requirement text must not appear in the product UI");
        QVERIFY2(!label->text().contains("实时监控中"),
            "the removed monitor footer must not return");
    }

    const qsizetype widgetCountBeforeNavigation = window.findChildren<QWidget *>().size();
    for (int iteration = 0; iteration < 80; ++iteration) {
        homeButton->click();
        methodAction->trigger();
        settingsAction->trigger();
        assistantAction->trigger();
        instrumentAction->trigger();
    }
    QCoreApplication::processEvents();
    QCOMPARE(window.findChildren<QWidget *>().size(), widgetCountBeforeNavigation);

    auto *runAction = window.findChild<QAction *>("StartRun");
    QVERIFY(runAction);
    QTemporaryDir sampleDirectory;
    QVERIFY(sampleDirectory.isValid());
    auto confirmSample = [&window,&sampleDirectory] {
        auto *dialog = window.findChild<QDialog *>("sampleSaveDialog");
        QVERIFY(dialog);
        const auto capture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(dialog->grab().save(capture + "/sample-save-dialog.png"));
        dialog->findChild<QLineEdit *>("sampleNumber")->setText("SAMPLE-TEST");
        dialog->findChild<QLineEdit *>("samplePersonName")->setText("张三");
        dialog->findChild<QLineEdit *>("sampleSaveFolder")->setText(sampleDirectory.path());
        dialog->findChild<QLineEdit *>("sampleFileName")->setText("test-sample");
        dialog->findChild<QPushButton *>("confirmSampleStart")->click();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    };
    runAction->trigger();
    auto *cancelSample = window.findChild<QDialog *>("sampleSaveDialog");
    QVERIFY(cancelSample); cancelSample->reject();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QVERIFY(controller.phase() != AppController::Phase::Acquiring);
    runAction->trigger();
    confirmSample();
    QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::Acquiring, 1000);
    QCOMPARE(runAction->text(), QString("检测中"));
    QVERIFY(!runAction->isEnabled());
    auto *runningButton=window.findChild<QPushButton *>("runAcquisitionButton");
    QVERIFY(runningButton && !runningButton->isEnabled());
    runningButton->click();runAction->trigger();
    QCOMPARE(controller.phase(), AppController::Phase::Acquiring);
    controller.cancelDetection(); // Failure/cancellation is not successful completion.
    QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::Ready, 1000);
    QVERIFY(!window.findChild<QDialog *>("detectionCompletedDialog"));
    QCOMPARE(runAction->text(), QString("开始检测"));
    runAction->trigger();
    confirmSample();
    QTRY_COMPARE_WITH_TIMEOUT(controller.phase(), AppController::Phase::ResultReady, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(sampleDirectory.filePath("test-sample.qit.json")),5000);
    auto *completed=window.findChild<QDialog *>("detectionCompletedDialog");
    QVERIFY(completed && completed->isVisible());
    QVERIFY(visibleWidgetWithText<QLabel>(*completed,"检测已完成"));
    QCOMPARE(completed->findChildren<QPushButton *>().size(),1);
    QVERIFY(!runAction->isEnabled());QCOMPARE(runningButton->text(),QString("检测中"));
    completed->reject();QVERIFY(completed->isVisible());QVERIFY(!runningButton->isEnabled());
    const auto completionCapture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if(!completionCapture.isEmpty())QVERIFY(completed->grab().save(completionCapture+"/detection-completed.png"));
    completed->findChild<QPushButton *>("confirmDetectionCompleted")->click();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QVERIFY(!window.findChild<QDialog *>("detectionCompletedDialog"));
    QVERIFY(runAction->isEnabled());QVERIFY(runningButton->isEnabled());
    QCOMPARE(runningButton->text(),QString("开始检测"));
    const auto savedSample = RunArchiveCodec::read(sampleDirectory.filePath("test-sample.qit.json"));
    QVERIFY(savedSample.valid);
    QCOMPARE(savedSample.sampleInfo.value("sample_id").toString(),QString("SAMPLE-TEST"));
    QVERIFY(!controller.liveSpectrum().isEmpty());
    QVERIFY(!controller.result().processedSpectrum.points.isEmpty());
    auto *sourceNotice = window.findChild<QLabel *>("resultDataSource");
    QVERIFY(sourceNotice && sourceNotice->text().contains("本机检测数据"));
    auto *screeningResults = window.findChild<QTableWidget *>("reportScreeningResults");
    QVERIFY(screeningResults && screeningResults->rowCount() > 0);
    QCOMPARE(screeningResults->item(0, 3)->text(), QString("可疑"));
    QVERIFY(!window.findChild<QTableWidget *>("screeningResults"));
    // 检测完成会自动进入报告页；返回样品分析后再检查三图固定布局。
    window.findChild<QAction *>("OpenHome")->trigger();
    auto *runCanvas = window.findChild<QWidget *>("analysisCanvas");
    auto *primaryPlot = window.findChild<SpectrumPlot *>("runPrimaryPlot");
    auto *msPlot = window.findChild<SpectrumPlot *>("runMsPlot");
    auto *eicPlot = window.findChild<SpectrumPlot *>("runEicPlot");
    QVERIFY(runCanvas && primaryPlot && msPlot && eicPlot);
    QVERIFY(primaryPlot->points().isEmpty()); QVERIFY(eicPlot->points().isEmpty());
    auto *acquisitionButton = window.findChild<QPushButton *>("runAcquisitionButton");
    QVERIFY(acquisitionButton && acquisitionButton->height() == 36);
    // Check true child bounds, not merely absent scroll bars. Test every panel
    // combination at the supported minimum window as well as normal sizes.
    for (const QSize requested : {QSize(1024, 680), QSize(1280, 800), QSize(1366, 768)}) {
        window.resize(requested);
        for (int panels = 0; panels < 4; ++panels) {
            if (assistantRail->isVisibleTo(&window) != bool(panels & 1)) assistantAction->trigger();
            if (monitorScroll->isVisibleTo(&window) != bool(panels & 2)) instrumentAction->trigger();
            QTest::qWait(60);
            QVERIFY(runCanvas->isVisibleTo(&window));
            for (QWidget *part : {static_cast<QWidget *>(primaryPlot), static_cast<QWidget *>(msPlot), static_cast<QWidget *>(eicPlot)}) {
                QVERIFY(part->isVisibleTo(&window));
                const QRect bounds(part->mapTo(runCanvas, QPoint()), part->size());
                QVERIFY2(runCanvas->rect().contains(bounds), qPrintable(part->objectName()));
                QVERIFY(window.rect().contains(QRect(part->mapTo(&window, QPoint()), part->size())));
            }
            QVERIFY(msPlot->height() >= 100);
            QVERIFY(msPlot->width() > runCanvas->width() * 0.8);
            QVERIFY(primaryPlot->mapTo(runCanvas, QPoint()).y() < msPlot->mapTo(runCanvas, QPoint()).y());
            QVERIFY(msPlot->mapTo(runCanvas, QPoint()).y() < eicPlot->mapTo(runCanvas, QPoint()).y());
            QVERIFY(!screeningResults->isVisibleTo(&window));
        }
    }
    QVERIFY(runCanvas->findChildren<QScrollArea *>().isEmpty());
    const auto plots = window.findChildren<SpectrumPlot *>();
    QVERIFY(!plots.isEmpty());
    SpectrumPlot *interactivePlot = nullptr;
    for (auto *plot : plots)
        if (plot == msPlot) interactivePlot = plot;
    QVERIFY(interactivePlot);
    // Offscreen Wine cannot reliably map the macOS cursor into a hidden HWND.
    // Exercise the widget's actual event handler without depending on that mapping.
    const QPointF position(interactivePlot->rect().center());
    QMouseEvent hover(QEvent::MouseMove, position,
        QPointF(interactivePlot->mapToGlobal(position.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(interactivePlot, &hover);
    QTRY_VERIFY_WITH_TIMEOUT(interactivePlot->toolTip().contains("m/z"), 1000);
    const QString captureDirectory = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        window.resize(1280, 800);
        QTest::qWait(180);
        QVERIFY(window.grab().save(captureDirectory + "/spectrum.png"));
        window.resize(1024, 768);
        QTest::qWait(180);
        QVERIFY(window.grab().save(captureDirectory + "/spectrum-compact.png"));
        window.resize(1280, 800);
        methodAction->trigger();
        QTest::qWait(180);
        QVERIFY(window.grab().save(captureDirectory + "/methods.png"));
    }
    auto *reportAction = window.findChild<QAction *>("OpenReport");
    QVERIFY(reportAction);
    reportAction->trigger();
    QVERIFY(screeningResults->isVisibleTo(&window));
    QCOMPARE(screeningResults->horizontalHeaderItem(1)->text(), QString("浓度\nμg/mL"));
    auto *reportSummary = window.findChild<QWidget *>("reportSummaryStrip");
    QVERIFY(reportSummary);
    QVERIFY(reportSummary->isVisibleTo(&window));
    auto *review = visibleWidgetWithText<QPushButton>(window, "完成复核");
    QVERIFY(review);
    if (review->isEnabled()) QTest::mouseClick(review, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentRun().reviewStatus, QString("REVIEWED"), 1000);
    auto *generatePdf = visibleWidgetWithText<QPushButton>(window, "生成 PDF");
    QVERIFY(generatePdf);
    QTRY_VERIFY_WITH_TIMEOUT(generatePdf->isEnabled(), 1000);
    QSignalSpy generated(&controller, &AppController::reportGenerated);
    QTest::mouseClick(generatePdf, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 3000);
    const QString reportPath = generated.constFirst().constFirst().toString();
    QVERIFY(QFileInfo(reportPath).fileName().startsWith(QStringLiteral("张三-检测报告-")));
    QFile report(reportPath);
    QVERIFY(report.open(QIODevice::ReadOnly));
    QCOMPARE(report.read(4), QByteArray("%PDF"));
    if (!captureDirectory.isEmpty()) {
        QTest::qWait(180);
        QVERIFY(window.grab().save(captureDirectory + "/report.png"));
        const QSize previousSize = window.size();
        window.resize(1024, 768);
        QTest::qWait(100);
        QVERIFY(window.grab().save(captureDirectory + "/report-1024.png"));
        window.resize(previousSize);
    }
    // 页面已删除，底层归档导入导出仍须保持可用。
    QVERIFY(!window.findChild<QTableWidget *>("recordsTable"));
    QTemporaryDir transferDirectory;
    const QString exportPath = transferDirectory.filePath("roundtrip.qit.json");
    QSignalSpy exported(&controller, &AppController::archiveGenerated);
    controller.exportRunArchive(controller.currentRun().id, exportPath);
    QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 5000);
    QVERIFY(RunArchiveCodec::read(exportPath).valid);
    QSignalSpy importedBatch(&controller, &AppController::importFinished);
    controller.importRunArchives({exportPath, exportPath});
    QVERIFY(controller.importInProgress());
    // Reentrant import must be rejected, not start another worker.
    controller.importRunArchives({exportPath});
    QTRY_COMPARE_WITH_TIMEOUT(importedBatch.count(), 1, 10000);
    QVERIFY(!controller.importInProgress());
    QVERIFY(importedBatch.first().first().toString().contains("重复跳过 1"));
    if (!captureDirectory.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(window.grab().save(captureDirectory + "/data.png"));
    }
    // Public real scan series: TIC must come from scan sums, not a second copy
    // of the mass spectrum. Selection must change only the displayed MS scan.
    const QString scanPath = transferDirectory.filePath("public.scan.csv");
    QVERIFY(QFile::copy(":/public-ms/openms_bsa.scan.csv", scanPath));
    const auto scanArchive = RunArchiveCodec::read(scanPath);
    QVERIFY2(scanArchive.valid, qPrintable(scanArchive.error));
    const QString scanId = "import-" + QString::fromLatin1(QCryptographicHash::hash(
        (scanArchive.payloadHash + ":" + AnalysisEngine::Version).toUtf8(), QCryptographicHash::Sha256).toHex());
    importedBatch.clear();
    controller.importRunArchives({scanPath});
    QTRY_COMPARE_WITH_TIMEOUT(importedBatch.count(), 1, 10000);
    controller.loadStoredRun(scanId);
    QCOMPARE(controller.scans().size(), scanArchive.scans.size());
    window.findChild<QAction *>("OpenHome")->trigger();
    for (const QSize requested : {QSize(1024,680), QSize(1366,768)}) {
        window.resize(requested);
        for (int panels=0; panels<4; ++panels) {
            if (assistantRail->isVisibleTo(&window) != bool(panels & 1)) assistantAction->trigger();
            if (monitorScroll->isVisibleTo(&window) != bool(panels & 2)) instrumentAction->trigger();
            QTest::qWait(60);
            QVERIFY(runCanvas->isVisibleTo(&window));
            for (QWidget *part : {static_cast<QWidget *>(primaryPlot), static_cast<QWidget *>(msPlot), static_cast<QWidget *>(eicPlot)}) {
                QVERIFY(part->isVisibleTo(&window));
                QVERIFY(runCanvas->rect().contains(QRect(part->mapTo(runCanvas, QPoint()),part->size())));
            }
        }
    }
    QVERIFY(primaryPlot->points().size() != msPlot->points().size());
    const auto &selectedScan = controller.scans().last();
    primaryPlot->pointActivated(selectedScan.timeSeconds);
    QCOMPARE(msPlot->points().size(), selectedScan.points.size());
    QCOMPARE(msPlot->points().first().mz, selectedScan.points.first().mz);
    QCOMPARE(msPlot->points().first().intensity, selectedScan.points.first().intensity);
    auto *eicMz = window.findChild<QDoubleSpinBox *>("runEicMz");
    auto *eicTolerance = window.findChild<QDoubleSpinBox *>("runEicTolerance");
    QVERIFY(eicMz && eicTolerance);
    auto *exampleAction = window.findChild<QPushButton *>("loadPublicExample");
    auto *traceAction = window.findChild<QPushButton *>("openTraceAnalysis");
    QVERIFY(exampleAction && traceAction);
    for (const QSize viewport : {QSize(1024,768), QSize(1280,800), QSize(1024,768)}) {
        window.resize(viewport);
        QCoreApplication::processEvents();
        QCOMPARE(exampleAction->size(), QSize(120,30));
        QCOMPARE(traceAction->size(), exampleAction->size());
        QCOMPARE(eicMz->size(), QSize(112,30));
        QCOMPARE(eicTolerance->size(), QSize(90,30));
    }
    msPlot->pointActivated(selectedScan.points.first().mz);
    QTRY_COMPARE(eicPlot->points().size(), primaryPlot->points().size());
    for (auto *input : {eicMz, eicTolerance}) {
        const auto *editor = input->findChild<QLineEdit *>(); QVERIFY(editor);
        QVERIFY2(editor->width() >= editor->fontMetrics().horizontalAdvance(editor->text()) + 4,
            qPrintable(input->objectName() + " numeric text clipped"));
        QCOMPARE(input->height(), 30);
    }
    const auto expectedEic = ChromatogramEngine::trace(controller.scans(), ChromatogramEngine::Kind::Eic,
        1, eicMz->value(), eicTolerance->value());
    for (int i=0; i<expectedEic.size(); ++i) {
        QCOMPARE(eicPlot->points()[i].mz, expectedEic[i].mz);
        QCOMPARE(eicPlot->points()[i].intensity, expectedEic[i].intensity);
    }
    eicPlot->pointActivated(controller.scans().first().timeSeconds);
    QCOMPARE(msPlot->points().first().intensity, controller.scans().first().points.first().intensity);
    if (!captureDirectory.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(window.grab().save(captureDirectory + "/tic-ms-linked.png"));
    }
}

void UiSmokeTests::conversationIsBoundedAndSelectable() {
    ChatTranscript chat;
    chat.resize(280, 360);
    chat.show();
    chat.appendMessage(ChatTranscript::Role::Assistant, "可以查看状态。");
    chat.appendMessage(ChatTranscript::Role::User, "打开参数预设");
    QTest::qWait(50);
    auto *left = chat.findChild<QLabel *>("assistantMessage");
    auto *right = chat.findChild<QLabel *>("userMessage");
    QVERIFY(left && right);
    QVERIFY(left->mapTo(&chat, QPoint()).x() < right->mapTo(&chat, QPoint()).x());
    QVERIFY(left->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    QCOMPARE(right->textFormat(), Qt::PlainText);
    chat.appendMessage(ChatTranscript::Role::User, "<img src='file:///private/test'>");
    for (int i = 0; i < 240; ++i)
        chat.appendMessage(ChatTranscript::Role::Assistant, QString("状态记录 %1").arg(i));
    QCOMPARE(chat.messageCount(), ChatTranscript::MaximumMessages);
    const int boundedWidgetCount = chat.findChildren<QWidget *>().size();
    for (int i = 0; i < 240; ++i)
        chat.appendMessage(ChatTranscript::Role::User, QString("查询记录 %1").arg(i));
    QCOMPARE(chat.findChildren<QWidget *>().size(), boundedWidgetCount);
    QTest::qWait(80);
    QCOMPARE(chat.verticalScrollBar()->value(), chat.verticalScrollBar()->maximum());
    chat.verticalScrollBar()->setValue(0);
    chat.appendMessage(ChatTranscript::Role::Assistant, "保留阅读位置");
    QTest::qWait(40);
    QVERIFY(chat.verticalScrollBar()->value() < chat.verticalScrollBar()->maximum());
    for (int i = 0; i < 8; ++i)
        chat.appendMessage(ChatTranscript::Role::Assistant, QString(20000, QChar('X')));
    int total = 0;
    for (auto *label : chat.findChildren<QLabel *>()) total += label->text().size();
    QVERIFY(total <= ChatTranscript::MaximumCharacters);
    QTest::qWait(80);
    QCOMPARE(chat.horizontalScrollBar()->maximum(), 0);
    chat.clear();
    QCOMPARE(chat.messageCount(), 0);
    QVERIFY(chat.findChildren<QLabel *>().isEmpty());
}

void UiSmokeTests::traceAnalysisUsesImportedScans() {
    Scientz::Ui::ThemeManager::apply(*qApp, Scientz::Ui::Density::Standard);
    auto *dialog = new ChromatogramDialog({{0,1,{{100,1},{101,2}}}, {1,1,{{100,3},{101,4}}}, {3,1,{{100,5},{101,6}}}});
    QPointer<QDialog> guard(dialog);
    dialog->show();
    auto *calculate = dialog->findChild<QPushButton *>("calculateIntegral");
    auto *result = dialog->findChild<QLabel *>("integrationResult");
    auto *kind = dialog->findChild<QComboBox *>("traceKind");
    QVERIFY(calculate && result && kind);
    auto *extraction = dialog->findChild<QWidget *>("extractionParameters");
    QVERIFY(extraction && !extraction->isVisible());
    calculate->click(); QVERIFY(result->text().contains("23"));
    kind->setCurrentIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(calculate->isEnabled(), 2000);
    QVERIFY(extraction->isVisible());
    calculate->click(); QVERIFY(result->text().contains("10"));
    auto *plot = dialog->findChild<SpectrumPlot *>(); QVERIFY(plot);
    QSignalSpy selected(plot, &SpectrumPlot::pointActivated);
    QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, plot->rect().center());
    QCOMPARE(selected.count(), 1);
    const QString capture = qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    dialog->resize(760, 560); QCoreApplication::processEvents();
    for (auto *input : dialog->findChildren<QDoubleSpinBox *>()) {
        QCOMPARE(input->buttonSymbols(), QAbstractSpinBox::NoButtons);
        QVERIFY(input->height() <= 34);
        auto *editor = input->findChild<QLineEdit *>(); QVERIFY(editor);
        QVERIFY(editor->width() >= editor->fontMetrics().horizontalAdvance(editor->text()) + 4);
        QVERIFY(dialog->rect().contains(QRect(input->mapTo(dialog, QPoint()), input->size())));
    }
    QVERIFY(dialog->grab().toImage().pixelColor(4, 4).alpha() == 255);
    if (!capture.isEmpty()) QVERIFY(dialog->grab().save(capture + "/trace-analysis.png"));
    dialog->close();
    QTRY_VERIFY(guard.isNull());
    // Real mzML timestamps have more decimals than the input displays. They
    // must not make the default full-range integral fail after rounding.
    auto *precise = new ChromatogramDialog({{1501.41394042969,1,{{100,1},{101,2}}},
        {1503.03125,1,{{100,3},{101,4}}}, {1504.31518554688,1,{{100,5},{101,6}}}});
    QPointer<QDialog> preciseGuard(precise);
    precise->findChild<QPushButton *>("calculateIntegral")->click();
    QVERIFY(precise->findChild<QLabel *>("integrationResult")->text().startsWith("面积"));
    QTemporaryDir directory; QString error;
    const auto path=directory.filePath("area.qint.json");
    QVERIFY2(precise->saveFile(path,&error),qPrintable(error));
    precise->findChild<QDoubleSpinBox *>("integrationFrom")->setValue(1502);
    QVERIFY(!precise->saveFile(path,&error)); // Stale area must not overwrite the saved record.
    QVERIFY2(precise->loadFile(path,&error),qPrintable(error));
    precise->findChild<QPushButton *>("calculateIntegral")->click();
    const auto second=directory.filePath("restored.qint.json");
    QVERIFY(precise->saveFile(second,&error));
    IntegrationSnapshot firstRecord, restoredRecord;
    QVERIFY(IntegrationDocument::load(path,&firstRecord,&error));
    QVERIFY(IntegrationDocument::load(second,&restoredRecord,&error));
    QCOMPARE(firstRecord.fromSeconds,restoredRecord.fromSeconds); QCOMPARE(firstRecord.area,restoredRecord.area);
    auto *baselineMode=precise->findChild<QComboBox *>("integrationBaseline"); QVERIFY(baselineMode);
    baselineMode->setCurrentIndex(1); QVERIFY(!precise->saveFile(second,&error));
    precise->findChild<QPushButton *>("calculateIntegral")->click();
    QVERIFY(precise->saveFile(second,&error));
    QVERIFY(IntegrationDocument::load(second,&restoredRecord,&error)); QVERIFY(restoredRecord.endpointBaseline);
    QVERIFY(precise->loadFile(path,&error)); QCOMPARE(baselineMode->currentIndex(),0);
    auto *unrelated=new ChromatogramDialog({{0,1,{{100,2},{101,3}}},{1,1,{{100,4},{101,5}}}});
    QVERIFY(!unrelated->loadFile(path,&error)); QVERIFY(error.contains("不匹配")); unrelated->close();
    precise->close(); QTRY_VERIFY(preciseGuard.isNull());
    auto *shortTrace=new ChromatogramDialog({{1.0,1,{{100,2},{101,3}}},{1.0000001,1,{{100,4},{101,5}}}});
    shortTrace->findChild<QPushButton *>("calculateIntegral")->click();
    QVERIFY(shortTrace->findChild<QLabel *>("integrationResult")->text().startsWith("面积"));
    shortTrace->close();
}

void UiSmokeTests::professionalOfflineToolsValidateAndRemainUsable() {
    Scientz::Ui::ThemeManager::apply(*qApp, Scientz::Ui::Density::Standard);
    QString error;QJsonObject parameters{{"scan_mode","Fullscan"},{"low_mass",40},{"high_mass",500},{"inlet",50}};
    QVERIFY(MethodDraft::validate(parameters,&error));parameters.insert("high_mass",20);QVERIFY(!MethodDraft::validate(parameters,&error));parameters.insert("high_mass",500);
    parameters.insert("inlet",101);QVERIFY(!MethodDraft::validate(parameters,&error));parameters.insert("inlet",50);
    const auto linear=MassAxisCalibration::fit({{100,101},{200,201},{300,301}},1);QVERIFY2(linear.valid,qPrintable(linear.error));double result=0;QVERIFY(linear.map(150,&result));QVERIFY(std::abs(result-151)<1e-8);QVERIFY(!linear.map(400,&result));
    const auto quadratic=MassAxisCalibration::fit({{100,102},{200,205},{300,310},{400,417}},2);QVERIFY(quadratic.valid);QVERIFY(quadratic.rms<1e-8);
    QVERIFY(!MassAxisCalibration::fit({{100,101},{100,102},{200,201}},2).valid);
    QVERIFY(!MassAxisCalibration::fit({{100,300},{200,200},{300,100}},1).valid);
    QTemporaryDir dir;UserStandardRepository repo(dir.filePath("standards.sqlite"));QVERIFY(repo.open(&error));
    UserStandard standard;standard.name="多离子标准";standard.ionization="EI";standard.provenance="测试数据";standard.category="测试类别";standard.qualifierMz=31;standard.additionalQualifierMzs={45,46};standard.peaks={{31,100},{45,60},{46,50}};
    QVERIFY2(repo.save(&standard,&error),qPrintable(error));const auto id=standard.id;
    QVERIFY(UserStandardRepository::writeFile(dir.filePath("multi.qstd.json"),standard,&error));UserStandard roundtrip;QVERIFY(UserStandardRepository::readFile(dir.filePath("multi.qstd.json"),&roundtrip,&error));QCOMPARE(roundtrip.additionalQualifierMzs,standard.additionalQualifierMzs);
    roundtrip.additionalQualifierMzs={31};QVERIFY(!UserStandardRepository::validate(roundtrip,&error));
    QVERIFY(repo.categories(&error).contains("测试类别"));QVERIFY(repo.addCategory("空类别",&error));QVERIFY(repo.removeCategory("空类别",&error));QVERIFY(!repo.removeCategory("测试类别",&error));
    QVERIFY(!repo.archive(id,standard.revision+1,&error));QVERIFY(repo.archive(id,standard.revision,&error));QCOMPARE(repo.count(&error),0);QVERIFY(repo.search("",0,&error).isEmpty());QVERIFY(repo.load(id,&roundtrip,&error));QCOMPARE(roundtrip.additionalQualifierMzs,standard.additionalQualifierMzs);QVERIFY(!repo.save(&standard,&error));
    bool saved=false;auto *editor=new MethodEditorDialog("离线方法",parameters,[&](const QString &,const QJsonObject &p){saved=MethodDraft::validate(p,&error);return saved;});editor->show();QTest::qWait(30);
    auto *scanMode=editor->findChild<QComboBox *>("methodScanMode");QVERIFY(scanMode);QCOMPARE(scanMode->count(),3);QCOMPARE(scanMode->currentText(),QString("Fullscan"));
    QVERIFY(editor->findChild<QLineEdit *>("method_low_mass")->height()>=44);
    QCOMPARE(editor->findChild<QLineEdit *>("method_high_mass")->text(),QString("500"));
    QCOMPARE(editor->findChild<QLineEdit *>("method_multiplier")->text(),QString("1000"));
    editor->findChild<QPushButton *>("saveMethodDraft")->click();QVERIFY(saved);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    const QString capture=qEnvironmentVariable("QITEST_UI_CAPTURE_DIR");
    for(const QString &kind:{"射频调谐","质量轴校准","注射泵"}) {
        std::unique_ptr<QWidget> page(createInstrumentWorkbench(kind));page->resize(960,650);page->show();QTest::qWait(30);
        QVERIFY(!page->findChild<QPushButton *>("hardwareUnavailable")->isEnabled());
        if(kind=="注射泵") {
            auto *start=page->findChild<QPushButton *>("simulateDeliver");start->click();QVERIFY(!start->isEnabled());
            auto *stop=page->findChild<QPushButton *>("stopSyringe");QVERIFY(stop->isEnabled());stop->click();QVERIFY(start->isEnabled());QVERIFY(!stop->isEnabled());
            start->click();page->hide();QVERIFY(start->isEnabled());page->show();
        } else {
            auto *table=page->findChild<QTableWidget *>("workbenchPoints");table->setRowCount(3);
            for(int r=0;r<3;++r){table->setItem(r,0,new QTableWidgetItem(QString::number(100*(r+1))));table->setItem(r,1,new QTableWidgetItem(QString::number(100*(r+1)+1)));}
            page->findChild<QPushButton *>("calculateWorkbench")->click();QVERIFY(!page->findChild<QLabel *>("workbenchResult")->text().isEmpty());
            table->item(0,0)->setText("bad");QVERIFY(page->findChild<QLabel *>("workbenchResult")->text().isEmpty());
            page->findChild<QPushButton *>("calculateWorkbench")->click();QVERIFY(page->findChild<QLabel *>("workbenchStatus")->text().contains("有效"));
            table->item(0,0)->setText("100");page->findChild<QPushButton *>("calculateWorkbench")->click();
        }
        if(!capture.isEmpty())QVERIFY(page->grab().save(capture+"/professional-"+kind+".png"));
    }
}

QTEST_MAIN(UiSmokeTests)
#include "UiSmokeTests.moc"
