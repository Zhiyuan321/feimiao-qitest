#include "ai/LocalAiBridge.h"
#include "ai/AiSafetyGuard.h"
#include "ai/AiToolProtocol.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QThread>
#include <QUrl>
#include <QUuid>

#if defined(Q_OS_WIN)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace qitest {

LocalAiBridge::LocalAiBridge(LocalAiConfig config, QObject *parent)
    : QObject(parent), config_(std::move(config)),
      apiToken_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
#ifdef QITEST_WIN7
    config_.gpuLayers = 0;
    config_.parallelRequests = 1;
    const int availableThreads = qMax(1, QThread::idealThreadCount() - 1);
    config_.cpuThreads = qBound(1, config_.cpuThreads ? config_.cpuThreads : 2, qMin(2, availableThreads));
    config_.preloadOnWarmUp = false;
    server_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    startupTimer_.setSingleShot(true);
    idleTimer_.setSingleShot(true);
    shutdownTimer_.setSingleShot(true);
    connect(&startupTimer_, &QTimer::timeout, this, [this] {
        failRequests("本地 AI 加载超时，可稍后重试；其他功能仍可使用");
        stopServer();
    });
    connect(&idleTimer_, &QTimer::timeout, this, [this] {
        if (!requestInFlight_ && pendingRequests_.isEmpty()) stopServer();
    });
    connect(&shutdownTimer_, &QTimer::timeout, this, [this] {
        if (server_.state() != QProcess::NotRunning) server_.kill();
    });
    healthTimer_.setInterval(500);
    progressTimer_.setInterval(1000);
    connect(&progressTimer_, &QTimer::timeout, this, [this] {
        const auto seconds = questionClock_.elapsed() / 1000;
        emit stateChanged(QString("%1 · %2 秒%3")
            .arg(serverReady_ ? "正在生成回答" : "正在加载模型").arg(seconds)
            .arg(seconds >= 10 ? "\n仍在处理，可停止；其他功能不受影响。" : ""));
    });
    connect(&healthTimer_, &QTimer::timeout, this, &LocalAiBridge::pollHealth);
    connect(&server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (destroying_ || stoppingServer_) return;
        if (error == QProcess::FailedToStart) {
            failRequests("本地 AI 服务无法启动：" + server_.errorString());
            stopServer();
        }
    });
    connect(&server_, &QProcess::started, this, &LocalAiBridge::writeServerPid);
    connect(&server_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus) {
            serverReady_ = false;
            healthTimer_.stop();
            startupTimer_.stop();
            shutdownTimer_.stop();
            clearServerPid();
            if (destroying_) return;
            if (stoppingServer_) {
                stoppingServer_ = false;
                if (!pendingRequests_.isEmpty()) ensureServer();
            } else {
                failRequests("本地 AI 服务已退出，可重新提问；其他功能仍可使用");
            }
        });
    // Drain runtime output instead of accumulating an unbounded QProcess buffer.
    connect(&server_, &QProcess::readyReadStandardOutput, this, [this] {
        server_.readAllStandardOutput();
    });
}

LocalAiBridge::~LocalAiBridge() {
    destroying_ = true;
    cancelReplies();
    if (server_.state() != QProcess::NotRunning) {
        server_.terminate();
        if (!server_.waitForFinished(1500)) {
            server_.kill();
            server_.waitForFinished(1500);
        }
    }
    clearServerPid();
}

bool LocalAiBridge::componentsAvailable() const {
    return QFileInfo::exists(config_.serverExecutable) && QFileInfo::exists(config_.modelPath);
}

QString LocalAiBridge::modelName() const { return QFileInfo(config_.modelPath).completeBaseName(); }

QString LocalAiBridge::statusSummary() const {
    if (!componentsAvailable()) return "本地 AI 组件未安装";
    if (server_.state() == QProcess::Running) return modelName() + " · 本地运行";
    return modelName() + " · 按需启动";
}

void LocalAiBridge::warmUp() {
    if (config_.preloadOnWarmUp && componentsAvailable() && !serverReady_) ensureServer();
}

void LocalAiBridge::setKeepAlive(bool keepAlive) {
    keepAlive_ = keepAlive;
    if (keepAlive_) {
        idleTimer_.stop();
        if (componentsAvailable() && !serverReady_) ensureServer();
    } else {
        scheduleIdleUnload();
    }
}

void LocalAiBridge::unload() {
    const bool hadWork = busy_ || requestInFlight_ || !pendingRequests_.isEmpty();
    if (hadWork) failRequests("本次回答已停止。");
    stopServer();
    if (!hadWork) emit stateChanged("本地模型未运行");
}

void LocalAiBridge::setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    if (busy) { questionClock_.start(); progressTimer_.start(); }
    else progressTimer_.stop();
    emit busyChanged(busy);
}

void LocalAiBridge::cancelQuestion() {
    if (!busy_) return;
    failRequests("本次回答已停止，可继续提问；基础助手仍可使用。");
    stopServer();
}

void LocalAiBridge::explainEvidence(const QString &structuredEvidence) {
    askQuestion("请解释当前仪器状态与检测证据，并给出需要人工确认的下一步。", structuredEvidence);
}

void LocalAiBridge::askQuestion(const QString &question, const QString &structuredEvidence) {
    if (!componentsAvailable()) {
        emit failed("本地模型或 llama-server 不存在");
        return;
    }
    const QString cleanQuestion = question.trimmed().left(1000);
    if (cleanQuestion.isEmpty()) {
        emit failed("请输入要询问的问题");
        return;
    }
    if (pendingRequests_.size() >= config_.maxQueuedRequests) {
        emit failed("智能台正在处理，请等待当前问题完成后再发送");
        return;
    }
    idleTimer_.stop();
    pendingRequests_.enqueue({cleanQuestion, structuredEvidence.left(16000)});
    setBusy(true);
    if (serverReady_) {
        sendPendingRequest();
        return;
    }
    ensureServer();
}

void LocalAiBridge::ensureServer() {
    if (stoppingServer_) return;
    if (server_.state() != QProcess::NotRunning) {
        if (!healthTimer_.isActive()) healthTimer_.start();
        pollHealth();
        return;
    }
    cleanupStaleServer();
#ifdef QITEST_WIN7
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    // Leave working space for Windows, the acquisition process and Qt. Do not
    // force a large model into swap when other applications consume the RAM.
    const quint64 requiredBytes = qMax<quint64>(1536ULL * 1024 * 1024,
        static_cast<quint64>(qMax<qint64>(0, QFileInfo(config_.modelPath).size())) + 768ULL * 1024 * 1024);
    if (GlobalMemoryStatusEx(&memory) && memory.ullAvailPhys < requiredBytes) {
        failRequests("可用内存不足，暂未加载 AI；请关闭其他程序后重试，软件其他操作仍可使用");
        return;
    }
#endif
    emit stateChanged("正在加载本地千问模型…");
    server_.setProgram(config_.serverExecutable);
    QStringList arguments{"--model", config_.modelPath, "--host", "127.0.0.1", "--port",
        QString::number(config_.port), "--ctx-size", QString::number(config_.contextTokens),
        "--n-gpu-layers", QString::number(config_.gpuLayers), "--parallel", QString::number(config_.parallelRequests),
        "--batch-size", QString::number(config_.batchTokens),
        "--ubatch-size", QString::number(config_.microBatchTokens),
        "--jinja", "--reasoning", "off", "--no-webui", "--api-key", apiToken_};
    if (config_.cpuThreads > 0) arguments << "--threads" << QString::number(config_.cpuThreads)
        << "--threads-batch" << QString::number(config_.cpuThreads);
    server_.setArguments(arguments);
    server_.setProcessChannelMode(QProcess::MergedChannels);
    server_.start();
    startupTimer_.start(config_.startupTimeoutMs);
    healthTimer_.start();
}

QString LocalAiBridge::serverPidFilePath() const {
    return QDir::tempPath() + "/cn.feimiao.qitest.llama-server.pid";
}

void LocalAiBridge::writeServerPid() {
    ownedServerPid_ = server_.processId();
    QFile file(serverPidFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    file.write(QByteArray::number(ownedServerPid_));
}

void LocalAiBridge::clearServerPid() {
    QFile file(serverPidFilePath());
    if (!file.exists()) return;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const qint64 recorded = file.readAll().trimmed().toLongLong();
        if (recorded != 0 && recorded != ownedServerPid_) return;
    }
    file.remove();
    ownedServerPid_ = 0;
}

void LocalAiBridge::cleanupStaleServer() {
    QFile file(serverPidFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const qint64 pid = file.readAll().trimmed().toLongLong();
    file.close();
    if (pid <= 1 || pid == server_.processId()) return;

#if defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
        FALSE, static_cast<DWORD>(pid));
    if (!process) {
        file.remove();
        return;
    }
    wchar_t imagePath[32768]{};
    DWORD imagePathLength = static_cast<DWORD>(sizeof(imagePath) / sizeof(imagePath[0]));
    const bool sameExecutable = QueryFullProcessImageNameW(process, 0, imagePath,
        &imagePathLength)
        && QDir::cleanPath(QString::fromWCharArray(imagePath, imagePathLength)).compare(
            QDir::cleanPath(QFileInfo(config_.serverExecutable).absoluteFilePath()),
            Qt::CaseInsensitive) == 0;
    if (sameExecutable) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 500);
    }
    CloseHandle(process);
#else
    QProcess inspect;
    inspect.start("/bin/ps", {"-p", QString::number(pid), "-o", "command="});
    if (!inspect.waitForFinished(700)) return;
    const QString command = QString::fromUtf8(inspect.readAllStandardOutput()).trimmed();
    if (!command.contains(config_.serverExecutable) || !command.contains(config_.modelPath)) {
        file.remove();
        return;
    }

    ::kill(static_cast<pid_t>(pid), SIGTERM);
    for (int attempt = 0; attempt < 20 && ::kill(static_cast<pid_t>(pid), 0) == 0; ++attempt)
        QThread::msleep(25);
    if (::kill(static_cast<pid_t>(pid), 0) == 0) ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
    file.remove();
}

void LocalAiBridge::pollHealth() {
    if (healthReply_ || stoppingServer_ || serverReady_) return;
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/health").arg(config_.port)));
    request.setRawHeader("Authorization", "Bearer " + apiToken_.toUtf8());
    auto *reply = network_.get(request);
    healthReply_ = reply;
    QTimer::singleShot(2000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        healthReply_ = nullptr;
        const bool ready = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (!ready) return;
        healthTimer_.stop();
        startupTimer_.stop();
        serverReady_ = true;
        emit stateChanged(modelName() + " · 本地就绪");
        sendPendingRequest();
        scheduleIdleUnload();
    });
}

void LocalAiBridge::sendPendingRequest() {
    if (!serverReady_ || requestInFlight_ || pendingRequests_.isEmpty()) return;
    const PendingRequest pending = pendingRequests_.dequeue();
    requestInFlight_ = true;
    emit stateChanged("正在生成回答…");
    const QString systemPrompt =
        "你是海关毒品筛查仪器的本地操作与说明书助手。只能根据提供的结构化证据和本地操作手册回答，"
        "不得生成、修改或推测未提供的科学数值；必须区分本机检测数据、参考库和正式结论；"
        "不得把候选匹配写成物质鉴定结论，不得建议绕过权限、互锁或人工复核；"
        "证据不足时明确说明缺少什么。若用户询问操作，说明界面路径和安全确认点；"
        "若白名单操作已经由界面执行，不要声称自己操作了其他功能。"
        "不得向用户显示内部字段名、英文枚举、工具协议或数据库标识；"
        "只有证据明确提供阈值并给出通过结果时，才能说参数在范围内；"
        "没有已完成分析时，不得把缺失的质量分数表述为 0 分，也不得虚构运行队列或已加载状态；"
        "必须把证据改写成自然、简短、完整的中文。输出不超过三段：结论、依据、下一步。"
        + (config_.allowToolProposals ? AiToolProtocol::toolInstructions()
            : QString("你只能解释，不能执行或输出任何工具调用。只回答当前问题，尽量不超过100字。"));
    const QString userPrompt = QString(
        "<question>\n%1\n</question>\n<deterministic_evidence>\n%2\n"
        "</deterministic_evidence>")
        .arg(pending.question, pending.evidence);
    QJsonObject payload{{"model", modelName()}, {"temperature", 0.1},
        {"max_tokens", config_.maxOutputTokens},
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", systemPrompt}},
            QJsonObject{{"role", "user"}, {"content", userPrompt}}}}};
    if (config_.allowToolProposals) {
        payload.insert("tools", AiToolProtocol::nativeTools());
        payload.insert("tool_choice", "auto");
    }
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(config_.port)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + apiToken_.toUtf8());
    auto *reply = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    activeReply_ = reply;
    QTimer::singleShot(config_.requestTimeoutMs, reply, [reply] {
        if (!reply->isFinished()) { reply->setProperty("timedOut", true); reply->abort(); }
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, question = pending.question, evidence = pending.evidence] {
        const auto bytes = reply->isOpen() ? reply->readAll() : QByteArray{};
        if (reply->error() != QNetworkReply::NoError) emit failed(
            reply->property("timedOut").toBool() ? "本地 AI 回答超时，请缩短问题后重试"
            : "本地 AI 请求失败：" + reply->errorString());
        else {
            const auto choices = QJsonDocument::fromJson(bytes).object().value("choices").toArray();
            const auto message = choices.isEmpty() ? QJsonObject{} : choices.first().toObject()
                .value("message").toObject();
            const QString rawText = message.value("content").toString().trimmed();
            std::optional<AiToolCall> toolCall;
            const auto nativeCalls = message.value("tool_calls").toArray();
            if (config_.allowToolProposals && !nativeCalls.isEmpty() && nativeCalls.first().isObject())
                toolCall = AiToolProtocol::parseNativeToolCall(nativeCalls.first().toObject());
            if (config_.allowToolProposals && !toolCall) toolCall = AiToolProtocol::parseEnvelope(rawText);
            const QString text = AiToolProtocol::removeEnvelope(rawText);
            if (toolCall)
                emit operationProposed(question, QString::fromUtf8(QJsonDocument(QJsonObject{
                    {"tool", toolCall->id}, {"confidence", toolCall->confidence},
                    {"arguments", toolCall->arguments}}).toJson(QJsonDocument::Compact)));
            if (text.isEmpty() && !toolCall) emit failed("本地 AI 未返回可用解释");
            else {
                const QString safeText = text.isEmpty() ? QString("已识别页面操作请求。")
                    : AiSafetyGuard::enforce(text, evidence);
                emit explanationReady(safeText);
                emit answerReady(question, safeText);
            }
        }
        reply->deleteLater();
        activeReply_ = nullptr;
        requestInFlight_ = false;
        if (pendingRequests_.isEmpty()) setBusy(false);
        if (reply->property("timedOut").toBool()) {
            failRequests("等待中的问题已取消，请重新提问");
            stopServer();
        } else {
            sendPendingRequest();
            scheduleIdleUnload();
        }
        });
}

void LocalAiBridge::scheduleIdleUnload() {
    if (!keepAlive_ && config_.idleUnloadMs > 0 && serverReady_
            && !requestInFlight_ && pendingRequests_.isEmpty())
        idleTimer_.start(config_.idleUnloadMs);
}

void LocalAiBridge::cancelReplies() {
    for (auto reply : {healthReply_, activeReply_}) {
        if (!reply) continue;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    healthReply_ = nullptr;
    activeReply_ = nullptr;
    requestInFlight_ = false;
}

void LocalAiBridge::failRequests(const QString &message) {
    setBusy(false);
    healthTimer_.stop();
    startupTimer_.stop();
    serverReady_ = false;
    cancelReplies();
    pendingRequests_.clear();
    emit failed(message);
}

void LocalAiBridge::stopServer() {
    healthTimer_.stop();
    startupTimer_.stop();
    idleTimer_.stop();
    serverReady_ = false;
    cancelReplies();
    if (server_.state() == QProcess::NotRunning) { stoppingServer_ = false; return; }
    stoppingServer_ = true;
    server_.terminate();
    shutdownTimer_.start(1500);
}

} // namespace qitest
