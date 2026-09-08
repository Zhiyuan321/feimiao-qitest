#include "ai/LocalAiBridge.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace qitest;

// A tiny child process tests lifecycle/HTTP behavior without loading model weights.
static int fakeServer(QCoreApplication &app) {
    const auto args = app.arguments();
    QFile model(args.value(args.indexOf("--model") + 1));
    if (!model.open(QIODevice::ReadOnly)) return 2;
    const auto mode = model.readAll();
    QFile launches(model.fileName() + ".launches");
    if (launches.open(QIODevice::Append)) { launches.write("start\n"); launches.close(); }
    QTcpServer server;
    if (mode != "startup-hang") {
        if (!server.listen(QHostAddress::LocalHost, args.value(args.indexOf("--port") + 1).toUShort())) return 3;
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, mode] {
                    auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    if (!bytes.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                    if (!bytes.startsWith("GET /health") && mode == "request-hang") return;
                    socket->setProperty("sent", true);
                    QByteArray body = bytes.startsWith("GET /health") ? QByteArray("{\"status\":\"ok\"}")
                        : QByteArray("{\"choices\":[{\"message\":{\"content\":\"请查看仪器状态。\"}}]}");
                    if (!bytes.startsWith("GET /health") && mode == "tool-proposal")
                        body = R"({"choices":[{"message":{"content":"请查看仪器状态。","tool_calls":[{"type":"function","function":{"name":"open_home","arguments":"{}"}}]}}]})";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    return app.exec();
}

class LocalAiBridgeTests final : public QObject {
    Q_OBJECT
private:
    LocalAiConfig config(const QString &path, const QByteArray &mode) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(mode);
        QTcpServer reservation;
        reservation.listen(QHostAddress::LocalHost);
        LocalAiConfig value;
        value.serverExecutable = QCoreApplication::applicationFilePath();
        value.modelPath = path;
        value.port = reservation.serverPort();
        value.preloadOnWarmUp = false;
        value.startupTimeoutMs = 2500;
        value.requestTimeoutMs = 2000;
        value.idleUnloadMs = 120;
        value.maxQueuedRequests = 2;
        return value;
    }
private slots:
    void lightweightAssistantCannotProposeTools() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "tool-proposal");
        settings.allowToolProposals = false;
        LocalAiBridge bridge(settings);
        QSignalSpy proposals(&bridge, &LocalAiBridge::operationProposed);
        QSignalSpy answers(&bridge, &LocalAiBridge::answerReady);
        bridge.askQuestion("解释当前状态", "{}");
        QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 1, 6000);
        QCOMPARE(proposals.count(), 0);
    }
    void progressAndCancelKeepEventLoopResponsive() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "request-hang");
        settings.requestTimeoutMs = 5000;
        LocalAiBridge bridge(settings);
        QSignalSpy busy(&bridge, &LocalAiBridge::busyChanged);
        QSignalSpy states(&bridge, &LocalAiBridge::stateChanged);
        QSignalSpy failures(&bridge, &LocalAiBridge::failed);
        QSignalSpy answers(&bridge, &LocalAiBridge::answerReady);
        int ticks = 0;
        QTimer uiTimer;
        QObject::connect(&uiTimer, &QTimer::timeout, [&] { ++ticks; });
        uiTimer.start(25);
        bridge.askQuestion("请解释", "{}");
        QCOMPARE(busy.count(), 1);
        QCOMPARE(busy.first().first().toBool(), true);
        QTest::qWait(1200);
        QVERIFY(ticks >= 20);
        QVERIFY(std::any_of(states.cbegin(), states.cend(), [](const QList<QVariant> &row) {
            return row.first().toString().contains("秒");
        }));
        bridge.cancelQuestion();
        QCOMPARE(busy.last().first().toBool(), false);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(answers.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
        const int stateCount = states.count();
        QTest::qWait(1050);
        QCOMPARE(states.count(), stateCount);
    }
    void noWarmupAndIdleReload() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto settings = config(dir.filePath("fake-model"), "normal");
        LocalAiBridge bridge(settings);
        QSignalSpy answers(&bridge, &LocalAiBridge::answerReady);
        QSignalSpy failures(&bridge, &LocalAiBridge::failed);
        bridge.warmUp();
        QTest::qWait(180);
        QVERIFY(!QFile::exists(settings.modelPath + ".launches"));
        bridge.askQuestion("查看状态", "{}");
        QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 1, 6000);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
        bridge.askQuestion("再次查看状态", "{}");
        QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 2, 6000);
        QCOMPARE(failures.count(), 0);
        QFile launches(settings.modelPath + ".launches");
        QVERIFY(launches.open(QIODevice::ReadOnly));
        QCOMPARE(launches.readAll().count("start"), 2);
    }
    void keepAlivePreloadsAndDefersIdleUnload() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "normal");
        LocalAiBridge bridge(settings);
        bridge.setKeepAlive(true);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.statusSummary().contains("本地运行"), 6000);
        QTest::qWait(settings.idleUnloadMs * 2);
        QVERIFY(bridge.statusSummary().contains("本地运行"));
        bridge.setKeepAlive(false);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
    }
    void manualUnloadStopsActiveWorkAndAllowsReuse() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "request-hang");
        LocalAiBridge bridge(settings);
        QSignalSpy failures(&bridge, &LocalAiBridge::failed);
        QSignalSpy answers(&bridge, &LocalAiBridge::answerReady);
        bridge.askQuestion("stop", "{}");
        QTest::qWait(800);
        bridge.unload();
        QCOMPARE(failures.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
        QFile file(settings.modelPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("normal"); file.close();
        bridge.askQuestion("after-stop", "{}");
        QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 1, 6000);
    }
    void startupTimeoutRecoversAndQueueIsBounded() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "startup-hang");
        settings.startupTimeoutMs = 600;
        LocalAiBridge bridge(settings);
        QSignalSpy failures(&bridge, &LocalAiBridge::failed);
        QSignalSpy answers(&bridge, &LocalAiBridge::answerReady);
        bridge.askQuestion("one", "{}");
        bridge.askQuestion("two", "{}");
        bridge.askQuestion("three", "{}");
        QCOMPARE(failures.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(failures.count() >= 2, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
        QFile file(settings.modelPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("normal"); file.close();
        bridge.askQuestion("recovered", "{}");
        QTRY_COMPARE_WITH_TIMEOUT(answers.count(), 1, 6000);
        QCOMPARE(answers.first().first().toString(), QString("recovered"));
    }
    void requestTimeoutReleasesProcess() {
        QTemporaryDir dir;
        auto settings = config(dir.filePath("fake-model"), "request-hang");
        settings.requestTimeoutMs = 120;
        LocalAiBridge bridge(settings);
        QSignalSpy failures(&bridge, &LocalAiBridge::failed);
        bridge.askQuestion("question", "{}");
        QTRY_VERIFY_WITH_TIMEOUT(!failures.isEmpty(), 6000);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.statusSummary().contains("本地运行"), 4000);
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--model")) return fakeServer(app);
    LocalAiBridgeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "LocalAiBridgeTests.moc"
