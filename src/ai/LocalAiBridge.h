#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <QElapsedTimer>

namespace qitest {

// 这是桥接类的通用默认值；发布版低配参数由 AppController 加载 manifest 后传入。
// 不要仅看这里的默认 GPU/队列值推断最终包配置，也不要在此加入硬件控制逻辑。
struct LocalAiConfig {
    QString serverExecutable;
    QString modelPath;
    quint16 port = 18081;
    int contextTokens = 4096;
    int maxOutputTokens = 512;
    int maxQueuedRequests = 12;
    int gpuLayers = 99;
    int parallelRequests = 1;
    int cpuThreads = 0;
    int batchTokens = 256;
    int microBatchTokens = 128;
    int idleUnloadMs = 300000;
    int startupTimeoutMs = 180000;
    int requestTimeoutMs = 180000;
    bool preloadOnWarmUp = true;
    bool allowToolProposals = true;
};

// 只管理模型子进程和问答通信。基础操作路由在 AiCommandRouter，不依赖模型权重。
class LocalAiBridge final : public QObject {
    Q_OBJECT
public:
    explicit LocalAiBridge(LocalAiConfig config, QObject *parent = nullptr);
    ~LocalAiBridge() override;
    bool componentsAvailable() const;
    QString statusSummary() const;
    QString modelName() const;

public slots:
    void warmUp();
    void setKeepAlive(bool keepAlive);
    void unload();
    void cancelQuestion();
    void explainEvidence(const QString &structuredEvidence);
    void askQuestion(const QString &question, const QString &structuredEvidence);

signals:
    void busyChanged(bool busy);
    void stateChanged(const QString &state);
    void explanationReady(const QString &text);
    void answerReady(const QString &question, const QString &text);
    void operationProposed(const QString &question, const QString &toolEnvelope);
    void failed(const QString &error);

private:
    struct PendingRequest {
        QString question;
        QString evidence;
    };
    void ensureServer();
    void cleanupStaleServer();
    void writeServerPid();
    void clearServerPid();
    QString serverPidFilePath() const;
    void pollHealth();
    void sendPendingRequest();
    void scheduleIdleUnload();
    void stopServer();
    void failRequests(const QString &message);
    void cancelReplies();
    void setBusy(bool busy);

    LocalAiConfig config_;
    QProcess server_;
    QNetworkAccessManager network_;
    QTimer healthTimer_;
    QTimer startupTimer_;
    QTimer idleTimer_;
    QTimer shutdownTimer_;
    QTimer progressTimer_;
    QElapsedTimer questionClock_;
    bool busy_ = false;
    QPointer<QNetworkReply> healthReply_;
    QPointer<QNetworkReply> activeReply_;
    QQueue<PendingRequest> pendingRequests_;
    QString apiToken_;
    bool serverReady_ = false;
    bool requestInFlight_ = false;
    bool stoppingServer_ = false;
    bool destroying_ = false;
    bool keepAlive_ = false;
    qint64 ownedServerPid_ = 0;
};

} // namespace qitest
