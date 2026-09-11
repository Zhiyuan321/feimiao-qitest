#include "device/Rs485Instrument.h"
#include "device/NetworkInstrument.h"
#include "domain/DisplayLabels.h"
#include "app/AppController.h"
#include "core/MethodDraft.h"
#include "core/PlatformPaths.h"
#include "storage/ArchiveImportWorker.h"
#include "ai/AiEvidenceBuilder.h"
#include "ai/LocalAiBridge.h"
#include "library/SpectralLibraryRepository.h"
#include "report/ReportGenerator.h"
#include "storage/RunArchiveCodec.h"
#include "support/DiagnosticBundle.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSysInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>
#include <cmath>

namespace qitest {
namespace {

QString deployedResourceRoot() {
#if defined(Q_OS_MACOS)
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../Resources");
#else
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/resources");
#endif
}

QString qualityLevelName(QualityLevel level) {
    switch (level) {
    case QualityLevel::Pass: return "PASS";
    case QualityLevel::Review: return "REVIEW";
    case QualityLevel::Fail: return "FAIL";
    }
    return "FAIL";
}

QString safeReportFileStem(QString value) {
    value = value.trimmed();
    for (QChar &character : value) {
        if (character.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(character))
            character = QChar('_');
    }
    while (value.endsWith('.') || value.endsWith(' ')) value.chop(1);
    value = value.simplified().left(60);
    return value.isEmpty() ? QStringLiteral("检测结果") : value;
}

QString reportPathForRun(const QString &directory, const RunSummary &run) {
    const QString personName = run.sampleInfo.value("person_name").toString();
    const QString sampleId = run.sampleInfo.value("sample_id").toString();
    const QString subject = safeReportFileStem(personName.trimmed().isEmpty() ? sampleId : personName);
    const QString base = QStringLiteral("%1-检测报告-%2")
        .arg(subject, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QString path = QDir(directory).absoluteFilePath(base + ".pdf");
    for (int suffix = 2; QFileInfo::exists(path); ++suffix)
        path = QDir(directory).absoluteFilePath(QString("%1-%2.pdf").arg(base).arg(suffix));
    return path;
}

struct AiDeploymentProfile {
    QString defaultModel = "Qwen3.5-0.8B-Q4_0.gguf";
    QStringList fallbacks;
    int contextTokens = 4096;
    int maxOutputTokens = 256;
    int maxQueuedRequests = 1;
    int gpuLayers = 0;
    int parallelRequests = 1;
    int cpuThreads = 2;
    int batchTokens = 128;
    int microBatchTokens = 64;
    int idleUnloadMs = 30000;
    int startupTimeoutMs = 180000;
    int requestTimeoutMs = 120000;
    bool preloadOnWarmUp = false;
    bool allowToolProposals = false;
};

bool shouldUseLowMemoryProfile() {
#ifdef QITEST_WIN7
    return true;
#else
    const auto profile = qEnvironmentVariable("QITEST_AI_PROFILE").toLower();
    if (profile == "low" || profile == "lowmemory" || profile == "low-memory") return true;
    const auto lowMemory = qEnvironmentVariable("QITEST_AI_LOW_MEMORY").toLower();
    return lowMemory == "1" || lowMemory == "true" || lowMemory == "yes" || lowMemory == "on";
#endif
}

void applyProfileJson(const QJsonObject &object, AiDeploymentProfile &profile) {
    profile.contextTokens = qBound(1024, object.value("contextTokens").toInt(profile.contextTokens), 8192);
    profile.maxOutputTokens = qBound(128, object.value("maxOutputTokens").toInt(profile.maxOutputTokens), 2048);
    profile.maxQueuedRequests = qBound(1, object.value("maxQueuedRequests").toInt(profile.maxQueuedRequests), 32);
    profile.gpuLayers = qBound(0, object.value("nGpuLayers").toInt(profile.gpuLayers), 128);
    profile.parallelRequests = qBound(1, object.value("parallelRequests").toInt(profile.parallelRequests), 8);
    profile.cpuThreads = qBound(0, object.value("cpuThreads").toInt(profile.cpuThreads), 16);
    profile.batchTokens = qBound(32, object.value("batchTokens").toInt(profile.batchTokens), 512);
    profile.microBatchTokens = qBound(16, object.value("microBatchTokens").toInt(profile.microBatchTokens), profile.batchTokens);
    profile.idleUnloadMs = qBound(1000, object.value("idleUnloadMs").toInt(profile.idleUnloadMs), 1800000);
    profile.startupTimeoutMs = qBound(1000, object.value("startupTimeoutMs").toInt(profile.startupTimeoutMs), 300000);
    profile.requestTimeoutMs = qBound(1000, object.value("requestTimeoutMs").toInt(profile.requestTimeoutMs), 600000);
    profile.preloadOnWarmUp = object.value("preloadOnWarmUp").toBool(profile.preloadOnWarmUp);
    profile.allowToolProposals = object.value("allowToolProposals").toBool(profile.allowToolProposals);
}

AiDeploymentProfile loadAiDeploymentProfile() {
    AiDeploymentProfile profile;
#ifdef QITEST_WIN7
    // Safe defaults also apply if a deployed manifest is missing or unreadable.
    profile.gpuLayers = 0;
    profile.maxOutputTokens = 256;
    profile.maxQueuedRequests = 1;
    profile.cpuThreads = 2;
    profile.batchTokens = 128;
    profile.microBatchTokens = 64;
    profile.idleUnloadMs = 30000;
    profile.preloadOnWarmUp = false;
#endif
    QStringList candidates{deployedResourceRoot() + "/config/ai-model-manifest.json"};
#ifdef QITEST_SOURCE_DIR
    candidates << QStringLiteral(QITEST_SOURCE_DIR) + "/config/ai-model-manifest.json";
#endif
    for (const auto &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        if (object.isEmpty()) continue;
        profile.defaultModel = object.value("defaultModelFile").toString(profile.defaultModel);
        const auto fallbacks = object.value("fallbackModelFiles").toArray();
        if (!fallbacks.isEmpty()) {
            profile.fallbacks.clear();
            for (const auto &value : fallbacks) profile.fallbacks << value.toString();
        }
        applyProfileJson(object, profile);
        const QJsonObject lowMemoryProfile = object.value("lowMemoryProfile").toObject();
        if (shouldUseLowMemoryProfile() && !lowMemoryProfile.isEmpty()) {
            applyProfileJson(lowMemoryProfile, profile);
        }
        break;
    }
    return profile;
}

} // namespace

AppController::AppController(std::unique_ptr<IInstrumentAdapter> instrument, QObject *parent)
    : QObject(parent),
      instrument_(std::move(instrument)),
      engine_(demoReferences(), "screening-panel-cpp-0.4.0") {
    softwareClock_.start();
    instrumentSettings_ = {
        {"powerOn", false}, {"rfOn", false}, {"ionHighVoltageOn", false},
        {"diaphragmPumpOn", false}, {"molecularPumpOn", false}, {"pinchValveOn", false},
        {"internalCarrierGasOn", false}, {"cleaningModeOn", false}, {"gasSavingOn", false},
        {"coolingModeOn", false},
        {"observationLightOn", false}, {"coolingFanOn", false}, {"wastePumpOn", false},
        {"rf48VOn", false}, {"highVoltageBoardOn", false}, {"tdTemperatureC", 0},
        {"ionSourceEnabled", true}, {"trapTemperatureC", 85}, {"inletFlowPercent", 20},
        {"pumpFlowPercent", 20}, {"efcMlMin", 28.4}, {"ionSourceSetpointKv", 3.2}
    };
    QSettings persistentSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    // Disk preferences are not hardware readback. Never restore an ON indicator
    // from yesterday's process. Real hardware starts unknown until read back.
    const auto readback = instrument_->confirmedSettings();
    for (auto it = instrumentSettings_.begin(); it != instrumentSettings_.end(); ++it) {
        if (readback.contains(it.key()) || !instrument_->descriptor().simulation) it.value() = readback.value(it.key());
        else if (it.value().userType() != QMetaType::Bool)
            it.value() = persistentSettings.value("instrument/" + it.key(), it.value());
    }
    // 每次只允许一个待确认命令；超时清除请求号，后续迟到回执不能覆盖新状态。
    settingTimeout_.setSingleShot(true);
    settingTimeout_.setInterval(5000);
    connect(&settingTimeout_, &QTimer::timeout, this, [this] {
        const QString id = pendingSettingId_, key = pendingSettingKey_;
        pendingSettingId_.clear(); pendingSettingKey_.clear();
        instrument_->cancelSetting(id);
        instrumentSettings_[key] = QVariant();
        emit instrumentSettingsChanged(instrumentSettings_);
        emit instrumentCommandPending(key, false);
        if (workspace_) workspace_->appendAudit(sessionOperator_, "INSTRUMENT_COMMAND_TIMEOUT", key, id);
        emit notice("仪器未及时回执，状态未知；请检查设备，不要反复点击。");
    });
    bindInstrumentSignals();
    QString workspacePath = qEnvironmentVariable("QITEST_WORKSPACE_DB");
    if (workspacePath.isEmpty()) workspacePath = PlatformPaths::appDataFile("workspace.sqlite");
    workspace_ = std::make_unique<WorkspaceRepository>(workspacePath);
    QString workspaceError;
    if (!workspace_->open(&workspaceError)) workspace_.reset();
    else {
        recoveredSessionCount_ = workspace_->recoverInterruptedSessions(&workspaceError);
        if (recoveredSessionCount_ < 0) recoveredSessionCount_ = 0;
    }
    if (workspace_ && workspace_->methods().isEmpty()) {
        const auto method = workspace_->createMethodVersion("痕量筛查",
            {{"data_scope", "DEMO_SIMULATION"}, {"instrument_contract", "sim-contract-1"},
             {"note", "默认筛查方法；硬件参数以仪器配置为准"}},
            "system-bootstrap", &workspaceError);
        if (!method.id.isEmpty()) workspace_->activateMethod(method.id, "system-bootstrap", &workspaceError);
    }
    // 仅恢复模拟器自己的活动方法。真实设备仍须经过显式协议映射、确认和审计。
    if (workspace_ && instrument_->descriptor().simulation) {
        const QJsonObject parameters = workspace_->activeMethod().parameters.value("method_parameters").toObject();
        if (!parameters.isEmpty() && instrument_->validateMethodParameters(parameters).allowed)
            instrument_->requestMethodParameters("startup-simulation-restore", parameters);
    }
    QStringList databaseCandidates;
    const QString configured = qEnvironmentVariable("QITEST_LIBRARY_DB");
    if (!configured.isEmpty()) databaseCandidates << configured;
    databaseCandidates << deployedResourceRoot() + "/data/qitest_spectral_library.sqlite";
#ifdef QITEST_SOURCE_DIR
    databaseCandidates << QStringLiteral(QITEST_SOURCE_DIR) + "/data/library/qitest_spectral_library.sqlite";
#endif
    for (const auto &candidate : databaseCandidates) {
        if (!QFileInfo::exists(candidate)) continue;
        SpectralLibraryRepository repository(candidate);
        QString error;
        if (repository.openReadOnly(&error) && repository.spectrumCount() > 0) {
            librarySummary_ = repository.sourceSummary() + " · 参考库";
            libraryDatabasePath_ = candidate;
            break;
        }
    }
    const AiDeploymentProfile aiProfile = loadAiDeploymentProfile();
    QString serverPath = qEnvironmentVariable("QITEST_AI_SERVER");
    QString modelPath = qEnvironmentVariable("QITEST_AI_MODEL");
    if (serverPath.isEmpty()) {
#if defined(Q_OS_WIN)
        serverPath = deployedResourceRoot() + "/ai/llama-server.exe";
#else
        serverPath = deployedResourceRoot() + "/ai/llama-server";
#endif
    }
    if (modelPath.isEmpty()) {
        const QString root = deployedResourceRoot() + "/ai/";
        for (const auto &file : QStringList{aiProfile.defaultModel} + aiProfile.fallbacks)
            if (QFileInfo::exists(root + file)) { modelPath = root + file; break; }
    }
#ifdef QITEST_SOURCE_DIR
    if (!QFileInfo::exists(serverPath))
        serverPath = QStringLiteral(QITEST_SOURCE_DIR) + "/.tools/llama-runtime/llama-server";
    if (!QFileInfo::exists(modelPath)) {
        const QString root = QStringLiteral(QITEST_SOURCE_DIR) + "/models/qwen/";
        for (const auto &file : QStringList{aiProfile.defaultModel} + aiProfile.fallbacks)
            if (QFileInfo::exists(root + file)) { modelPath = root + file; break; }
    }
#endif
    aiBridge_ = new LocalAiBridge({serverPath, modelPath, 18081, aiProfile.contextTokens,
        aiProfile.maxOutputTokens, aiProfile.maxQueuedRequests, aiProfile.gpuLayers,
        aiProfile.parallelRequests, aiProfile.cpuThreads, aiProfile.batchTokens,
        aiProfile.microBatchTokens, aiProfile.idleUnloadMs, aiProfile.startupTimeoutMs,
        aiProfile.requestTimeoutMs, aiProfile.preloadOnWarmUp, aiProfile.allowToolProposals}, this);
    QString knowledgeError;
    QStringList knowledgeCandidates{
        deployedResourceRoot() + "/knowledge/operator_manual_zh.md"
    };
#ifdef QITEST_SOURCE_DIR
    knowledgeCandidates << QStringLiteral(QITEST_SOURCE_DIR) + "/resources/knowledge/operator_manual_zh.md";
#endif
    for (const auto &candidate : knowledgeCandidates) {
        if (QFileInfo::exists(candidate) && knowledgeStore_.loadMarkdown(candidate, &knowledgeError)) break;
    }
    connect(aiBridge_, &LocalAiBridge::stateChanged, this, &AppController::aiStateChanged);
    connect(aiBridge_, &LocalAiBridge::busyChanged, this, &AppController::aiBusyChanged);
    connect(aiBridge_, &LocalAiBridge::explanationReady, this, &AppController::aiExplanationReady);
    connect(aiBridge_, &LocalAiBridge::answerReady, this, &AppController::aiAssistantAnswerReady);
    connect(aiBridge_, &LocalAiBridge::operationProposed, this, &AppController::aiOperationProposed);
    connect(aiBridge_, &LocalAiBridge::failed, this, [this](const QString &error) {
        emit aiAssistantFailed(error);
        emit aiStateChanged(error);
        emit notice(error);
    });
    acquisitionTimer_.setInterval(55);
    connect(&acquisitionTimer_, &QTimer::timeout, this, [this] {
        progress_ = std::min(100, progress_ + 4);
        const qsizetype visible = pendingSpectrum_.size() * progress_ / 100;
        liveSpectrum_ = pendingSpectrum_.mid(0, visible);
        emit progressChanged(progress_);
        emit spectrumChanged(liveSpectrum_);
        if (progress_ >= 100) finishAcquisition();
    });
}

void AppController::bindInstrumentSignals() {
    connect(instrument_.get(), &IInstrumentAdapter::stateChanged,
            this, &AppController::refreshInstrumentReadback);
    connect(instrument_.get(), &IInstrumentAdapter::settingFinished, this,
        [this](const QString &id, const QString &key, bool success, const QVariant &actual, const QString &error) {
            // 调试第一检查点：请求号和控制键必须匹配，不能把其他命令的 ACK 串进来。
            if (id != pendingSettingId_ || key != pendingSettingKey_ || id.isEmpty()) return;
            settingTimeout_.stop();
            pendingSettingId_.clear(); pendingSettingKey_.clear();
            // 调试第二检查点：成功标志、有效回读、目标值三者缺一不可。
            const bool acknowledged = success && actual.isValid() && actual == pendingSettingValue_;
            if (acknowledged) {
                instrumentSettings_[key] = actual;
                // The simulator may acknowledge a start/stop affecting several
                // parts. Pull its single state snapshot; never invent real-device
                // part states from an aggregate power ACK.
                if (instrument_->descriptor().simulation) {
                    const auto readback = instrument_->confirmedSettings();
                    for (auto it = readback.begin(); it != readback.end(); ++it)
                        if (instrumentSettings_.contains(it.key())) instrumentSettings_[it.key()] = it.value();
                }
                if (actual.userType() != QMetaType::Bool) {
                    QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
                    preferences.setValue("instrument/" + key, actual);
                }
            } else if (!instrument_->descriptor().simulation) instrumentSettings_[key] = QVariant();
            if (workspace_) workspace_->appendAudit(sessionOperator_,
                acknowledged ? "INSTRUMENT_COMMAND_CONFIRMED" : "INSTRUMENT_COMMAND_FAILED", key,
                id + "; " + (instrument_->descriptor().simulation ? "simulation; " : "hardware; ") + error.left(300));
            emit instrumentSettingsChanged(instrumentSettings_);
            emit instrumentCommandPending(key, false);
            emit notice(acknowledged ? "操作已确认"
                : "操作未确认：" + (error.isEmpty() ? QString("设备回读与设定不一致") : error));
        });
    connect(instrument_.get(), &IInstrumentAdapter::methodParametersFinished, this,
        [this](const QString &id, bool success, const QJsonObject &readback, const QString &error) {
            if (id != pendingMethodRequestId_ || id.isEmpty()) return;
            const QString methodId = pendingMethodId_;
            const QJsonObject expected = pendingMethodParameters_;
            pendingMethodRequestId_.clear(); pendingMethodId_.clear(); pendingMethodParameters_ = {};
            const bool acknowledged = success && readback == expected;
            QString storageError;
            const bool activated = acknowledged && workspace_
                && workspace_->activateMethod(methodId, sessionOperator_, &storageError);
            if (workspace_) workspace_->appendAudit(sessionOperator_,
                activated ? "SIM_METHOD_CONFIRMED" : "SIM_METHOD_FAILED", methodId,
                id + "; " + (error.isEmpty() ? storageError : error).left(300));
            if (activated) emit methodsChanged();
            emit notice(activated ? "方法参数已确认，当前方法已更新"
                : "方法未激活：" + (error.isEmpty() ? QString("参数回读不一致") : error));
        });
}

void AppController::refreshInstrumentReadback() {
    const auto values = instrument_->confirmedSettings();
    for (auto it = instrumentSettings_.begin(); it != instrumentSettings_.end(); ++it) {
        if (it.key() == pendingSettingKey_) continue;
        if (!instrument_->descriptor().simulation || values.contains(it.key()))
            it.value() = values.value(it.key());
    }
    emit instrumentSettingsChanged(instrumentSettings_);
}

QStringList AppController::rs485Ports() const { return Rs485Instrument::availablePorts(); }
QVariantMap AppController::rs485Status() const {
    const auto *adapter = qobject_cast<Rs485Instrument *>(instrument_.get());
    if (auto *network = qobject_cast<NetworkInstrument *>(instrument_.get())) adapter = network->serial();
    return adapter ? adapter->statusDetails() : QVariantMap{};
}

bool AppController::connectRs485(const QString &portName, bool includePump) {
    if (portName.trimmed().isEmpty()) { emit notice("请选择485串口"); return false; }
    if (phase_ == Phase::Acquiring || phase_ == Phase::Analyzing || !pendingSettingId_.isEmpty()) {
        emit notice("请先结束采集或待确认操作，再更换设备连接"); return false;
    }
    auto *adapter = qobject_cast<Rs485Instrument *>(instrument_.get());
    if (auto *network = qobject_cast<NetworkInstrument *>(instrument_.get())) adapter = network->serial();
    if (!adapter) {
        // Never replace a loaded vendor plugin through the serial settings UI.
        if (!instrument_->descriptor().simulation) {
            emit notice("当前使用厂家插件，请通过插件配置设备连接"); return false;
        }
        QObject::disconnect(instrument_.get(), nullptr, this, nullptr);
        auto serial = std::make_unique<Rs485Instrument>();
        adapter = serial.get();
        instrument_ = std::move(serial);
        bindInstrumentSignals();
        refreshInstrumentReadback();
    }
    const bool opened = adapter->openPort(portName, includePump);
    if (opened) QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").setValue("rs485/port", portName.trimmed());
    // Opening a COM port is not proof of an instrument reply; stateChanged will
    // report connected only after a validated status frame.
    emit notice(adapter->connectionSummary());
    return opened;
}

void AppController::disconnectRs485() {
    if (auto *adapter = qobject_cast<Rs485Instrument *>(instrument_.get())) adapter->closePort();
    if (auto *network = qobject_cast<NetworkInstrument *>(instrument_.get())) network->serial()->closePort();
}

QVariantMap AppController::networkStatus() const {
    auto *adapter = qobject_cast<NetworkInstrument *>(instrument_.get());
    return adapter ? adapter->statusDetails() : QVariantMap{};
}
QVector<double> AppController::pressureVolts() const {
    const auto *network=qobject_cast<NetworkInstrument *>(instrument_.get());
    return network ? network->pressureVolts() : QVector<double>{};
}
bool AppController::requestRfTuning(bool enabled, bool confirmed) {
    if(!canTune()) {emit notice("调谐需管理员或工程师账号");return false;}
    if(enabled && !confirmed) {emit notice("请先确认开始调谐");return false;}
    if(phase_==Phase::Acquiring || phase_==Phase::Analyzing || !pendingSettingId_.isEmpty()) {
        emit notice("请先结束当前采集或待确认操作");return false;
    }
    auto *network=qobject_cast<NetworkInstrument *>(instrument_.get());
    if(!network) {emit notice("请先连接网口仪器");return false;}
    if(!workspace_ || !workspace_->appendAudit(sessionOperator_,"TUNING_REQUESTED", "rf", enabled?"start":"stop")) {
        emit notice("操作记录保存失败，调谐指令未发送");return false;
    }
    QString error; const bool result=network->requestTuning(enabled,&error);
    emit notice(result ? "调谐指令已提交，等待设备应答" : error);return result;
}
bool AppController::startNetworkListening(const QString &address, quint16 port, int staleMs) {
    if (phase_ == Phase::Acquiring || phase_ == Phase::Analyzing || !pendingSettingId_.isEmpty()) {
        emit notice("请先结束采集或待确认操作，再更换设备连接"); return false;
    }
    auto *network = qobject_cast<NetworkInstrument *>(instrument_.get());
    if (!network) {
        auto *serial = qobject_cast<Rs485Instrument *>(instrument_.get());
        if (!serial && !instrument_->descriptor().simulation) {
            emit notice("当前使用厂家插件，请通过插件配置设备连接"); return false;
        }
        QObject::disconnect(instrument_.get(), nullptr, this, nullptr);
        std::unique_ptr<Rs485Instrument> existingSerial;
        if (serial) { instrument_.release(); existingSerial.reset(serial); }
        auto adapter = std::make_unique<NetworkInstrument>(std::move(existingSerial));
        network = adapter.get(); instrument_ = std::move(adapter);
        bindInstrumentSignals(); refreshInstrumentReadback();
    }
    const bool opened = network->startListening(address, port, staleMs);
    if (opened) {
        QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
        preferences.setValue("network/address", address.trimmed());
        if (port) preferences.setValue("network/port", port);
        preferences.setValue("network/staleMs", staleMs);
    }
    emit notice(network->connectionSummary()); return opened;
}
void AppController::stopNetworkListening() {
    if (auto *adapter = qobject_cast<NetworkInstrument *>(instrument_.get())) adapter->stopListening();
}
bool AppController::exportNetworkFrames(const QString &path) {
    auto *adapter = qobject_cast<NetworkInstrument *>(instrument_.get());
    if (!adapter || path.isEmpty()) return false;
    QString error;
    const bool saved = adapter->exportFrames(path, &error);
    emit notice(saved ? "已导出最近网口报文：" + path : "报文导出失败：" + error);
    return saved;
}

QVariantMap AppController::pumpStatus() const {
    const auto *serial = qobject_cast<Rs485Instrument *>(instrument_.get());
    if (const auto *network = qobject_cast<NetworkInstrument *>(instrument_.get())) serial = network->serial();
    return serial ? serial->pumpStatusDetails() : QVariantMap{};
}
bool AppController::exportPumpFrames(const QString &path) {
    const auto *serial = qobject_cast<Rs485Instrument *>(instrument_.get());
    if (const auto *network = qobject_cast<NetworkInstrument *>(instrument_.get())) serial = network->serial();
    if (!serial || path.isEmpty()) return false;
    QString error;
    const bool saved = serial->exportFrames(path, &error);
    emit notice(saved ? "已导出485收发报文：" + path : "导出失败：" + error); return saved;
}

InstrumentHealth AppController::health() const { return instrument_->health(); }

InstrumentTelemetry AppController::telemetry() const { return instrument_->telemetry(); }

InstrumentDescriptor AppController::instrumentDescriptor() const { return instrument_->descriptor(); }

QString AppController::sessionSummary() const {
    return QString("%1 · %2").arg(sessionOperator_, AuthorizationPolicy::roleName(sessionRole_));
}

bool AppController::updateInstrumentSetting(const QString &key, const QVariant &value, bool confirmed) {
    if (instrument_->readOnly()) { emit notice("当前设备仅提供状态读取，硬件控制未开放"); return false; }
    // 固定顺序：并发限制 → 类型/范围 → 权限/连接 → 厂家校验 → 用户确认 → 审计 → 下发。
    // 下方通用输入范围不是设备的物理安全范围；厂家适配器必须继续收紧校验。
    if (!pendingSettingId_.isEmpty()) { emit notice("请等待仪器确认上一项操作"); return false; }
    if (!instrumentSettings_.contains(key)) {
        emit notice("未知的仪器设置项");
        return false;
    }
    const auto inRange = [](double number, double minimum, double maximum) {
        return number >= minimum && number <= maximum;
    };
    bool numberOk = false;
    const double numeric = value.toDouble(&numberOk);
    bool valid = value.userType() == QMetaType::Bool;
    if (key == "trapTemperatureC" || key == "tdTemperatureC")
        valid = numberOk && value.userType() != QMetaType::Bool
            && inRange(numeric, 0, 999) && std::floor(numeric) == numeric;
    else if (key == "inletFlowPercent" || key == "pumpFlowPercent")
        valid = numberOk && inRange(numeric, 0, 100);
    else if (key == "efcMlMin") valid = numberOk && inRange(numeric, 0, 50.0);
    else if (key == "ionSourceSetpointKv") valid = numberOk && inRange(numeric, 0, 10);
    if (!valid) {
        emit notice("设置值超出允许范围");
        return false;
    }
    if (!instrument_->descriptor().simulation
        && !AuthorizationPolicy::allows(sessionRole_, Permission::HardwareCriticalCommand)) {
        emit notice("当前角色无权修改真实仪器设置");
        return false;
    }
    if (!instrument_->health().connected) { emit notice("仪器未连接"); return false; }
    const auto validation = instrument_->validateSetting(key, value);
    if (!validation.allowed) { emit notice(validation.reason); return false; }
    if (!instrument_->descriptor().simulation && !confirmed) {
        emit instrumentConfirmationRequired(key, value); return false;
    }
    pendingSettingId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pendingSettingKey_ = key; pendingSettingValue_ = value;
    const bool audited = workspace_ && workspace_->appendAudit(sessionOperator_, "INSTRUMENT_COMMAND_REQUESTED", key, pendingSettingId_);
    if (!instrument_->descriptor().simulation && !audited) {
        pendingSettingId_.clear(); pendingSettingKey_.clear();
        emit notice("操作记录无法保存，真实仪器命令未发送。请检查磁盘和数据库。");
        return false;
    }
    emit instrumentCommandPending(key, true);
    settingTimeout_.start();
    // A synchronous simulator ACK may clear pendingSettingId_ inside the slot.
    // Pass an independent ID so signal arguments and audit remain intact.
    const QString requestId = pendingSettingId_;
    instrument_->requestSetting(requestId, key, value);
    return true;
}

QString AppController::aiSummary() const { return aiBridge_->statusSummary(); }

bool AppController::saveInstrumentPreset(const QVariantMap &preset) {
    // A saved preset is a desired configuration, never an acknowledged device state.
    const QVariantMap maxima{{"trapTemperatureC", 999.0}, {"inletFlowPercent", 100.0},
                            {"pumpFlowPercent", 100.0}, {"efcMlMin", 50.0}};
    if (preset.size() != maxima.size()) { emit notice("预设参数不完整"); return false; }
    for (auto it = preset.begin(); it != preset.end(); ++it) {
        bool ok = false;
        const double value = it.value().toDouble(&ok);
        if (!maxima.contains(it.key()) || !ok || !(value >= 0 && value <= maxima.value(it.key()).toDouble())) {
            emit notice("预设参数不合法"); return false;
        }
    }
    QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    for (auto it = preset.begin(); it != preset.end(); ++it) preferences.setValue("preset/" + it.key(), it.value());
    preferences.sync();
    if (preferences.status() != QSettings::NoError) { emit notice("预设保存失败"); return false; }
    if (workspace_) workspace_->appendAudit(sessionOperator_, "PRESET_SAVED", "instrument", "desired settings only");
    emit notice("预设已保存，尚未应用到仪器。");
    return true;
}

QString AppController::aiContextSummary() const {
    return AiEvidenceBuilder::buildSummary(buildAiContext());
}

QString AppController::workspaceSummary() const {
    if (!workspace_) return "检测记录不可用";
    if (recoveredSessionCount_ > 0)
        return QString("已恢复 %1 个上次异常中断的采集会话").arg(recoveredSessionCount_);
    return "检测记录已启用";
}

QVector<RunSummary> AppController::recentRuns(int limit) const {
    return workspace_ ? workspace_->recentRuns(limit) : QVector<RunSummary>{};
}

QVector<LibraryCompound> AppController::searchLibrary(const QString &query, int limit) const {
    if (libraryDatabasePath_.isEmpty()) return {};
    SpectralLibraryRepository repository(libraryDatabasePath_);
    QString error;
    if (!repository.openReadOnly(&error)) return {};
    return repository.searchCompounds(query, limit);
}

QVector<StartupCheck> AppController::startupChecks() const {
    const auto instrumentHealth = instrument_->health();
    return {
        {"本地 C++ 科学引擎", AnalysisEngine::Version, true, true},
        {"正式参考谱库", librarySummary_, !libraryDatabasePath_.isEmpty(), false},
        {"检测记录与审计", workspaceSummary(), workspace_ != nullptr, true},
        {"仪器接口", instrument_->readOnly() ? instrument_->connectionSummary()
                : instrumentHealth.connected ? "接口已就绪" : "仪器未连接",
            instrumentHealth.connected && instrumentHealth.ready, !instrument_->readOnly()},
        {"离线 AI 解释层", aiSummary(), aiBridge_->componentsAvailable(), false}
    };
}

QVector<MethodDefinition> AppController::methods() const {
    return workspace_ ? workspace_->methods() : QVector<MethodDefinition>{};
}

MethodDefinition AppController::activeMethod() const {
    return workspace_ ? workspace_->activeMethod() : MethodDefinition{};
}

void AppController::setSessionOperator(const QString &operatorName) {
    sessionOperator_ = operatorName.trimmed().isEmpty() ? "offline-demo" : operatorName.trimmed();
    sessionRole_ = AuthorizationPolicy::roleFromString(qEnvironmentVariable("QITEST_OPERATOR_ROLE", "operator"));
    if (workspace_) workspace_->appendAudit(sessionOperator_, "SESSION_STARTED", "application", "local session");
    emit sessionChanged(sessionOperator_, AuthorizationPolicy::roleName(sessionRole_));
}

void AppController::setOfflineDemoSession() {
    sessionOperator_ = "offline-demo";
    sessionRole_ = SessionRole::OfflineDemo;
    if (workspace_) workspace_->appendAudit(sessionOperator_, "SESSION_STARTED", "application", "offline demo session");
    emit sessionChanged(sessionOperator_, AuthorizationPolicy::roleName(sessionRole_));
}

void AppController::explainCurrentState() {
    askAiAssistant(AiEvidenceBuilder::defaultQuestionForState(buildAiContext()));
}

void AppController::prepareAiAssistant() {
    // Opening a panel is not consent to load model weights.
}

void AppController::setDeepAiEnabled(bool enabled) {
    setAiMode(enabled ? AiMode::Automatic : AiMode::Off);
}

void AppController::setAiMode(AiMode mode) {
    if (aiMode_ == mode) return;
    const bool wasEnabled = deepAiEnabled();
    aiMode_ = mode;
    aiBridge_->setKeepAlive(mode == AiMode::AlwaysOn);
    if (mode == AiMode::Off) aiBridge_->unload();
    emit aiModeChanged(mode);
    if (wasEnabled != deepAiEnabled()) emit deepAiEnabledChanged(deepAiEnabled());
    emit notice(mode == AiMode::Automatic ? "智能台：自动模式"
        : mode == AiMode::Manual ? "智能台：手动模式"
        : mode == AiMode::AlwaysOn ? "智能台：常开模式"
        : "智能台：已关闭");
}

void AppController::cancelAiQuestion() { aiBridge_->cancelQuestion(); }

void AppController::explainFeature(const QString &feature) {
    const auto hits = knowledgeStore_.search(feature, 1);
    QString answer = "此处用于查看“" + feature + "”。状态与数值以实际数据为准。";
    if (feature == "载气压力")
        answer = "载气压力读数：" + measurementText(instrument_->telemetry().carrierGasPressureTorr, 'f', 1)
            + " Torr。\n厂家允许范围尚未配置，不能据此判断过载或正常。请在“查看状态”中核对连接和实测值，按厂家规范检查气路；不要直接关闭泵或阀。"
            + QString("\n请按厂家规范核对气路和仪器状态。");
    else if (feature == "质量复核") {
        QStringList lines;
        for (const auto &check : result_.quality.checks)
            lines << check.title + (check.passed ? "：通过。" : "：需复核。") + check.detail;
        answer = lines.isEmpty() ? "还没有完成检测，暂不能复核质量。请先运行方法或导入检测归档。" : lines.join('\n');
    } else if (feature == "仪器状态与仪器设置")
        answer = AiEvidenceBuilder::buildSummary(buildAiContext());
    else if (feature.contains("开关") || feature.contains("高压"))
        answer = "仪器操作经过权限、安全校验和设备回执。没有回执不算成功；超时状态未知，不会自动重发。操作前请核实设备连接状态。";
    else if (!hits.isEmpty() && hits.first().score >= 8) answer = hits.first().title + "\n" + hits.first().text;
    else answer = "当前还没有足够的本地说明。请明确功能名称，或开启自动模式进一步解释；没有执行仪器操作。";
    emit aiAssistantAnswerReady(feature, answer);
}

AiContextSnapshot AppController::buildAiContext() const {
    AiContextSnapshot snapshot;
    snapshot.phaseLabel = phaseLabel_;
    snapshot.dataScope = currentRun_.dataScope.isEmpty() ? "DEMO_SIMULATION" : currentRun_.dataScope;
    snapshot.instrumentHealth = instrument_->health();
    snapshot.instrumentTelemetry = instrument_->telemetry();
    snapshot.librarySummary = librarySummary_;
    snapshot.workspaceSummary = workspaceSummary();
    snapshot.aiSummary = aiSummary();
    snapshot.activeMethod = activeMethod();
    snapshot.currentRun = currentRun_;
    snapshot.result = result_;
    return snapshot;
}

QString AppController::currentAiEvidence() const {
    return AiEvidenceBuilder::buildEvidence(buildAiContext());
}

void AppController::askAiAssistant(const QString &question) {
    const QString cleanQuestion = question.trimmed();
    if (cleanQuestion.isEmpty()) {
        emit notice("请输入要询问的问题");
        return;
    }
    if (workspace_)
        workspace_->appendAudit(sessionOperator_, "AI_ASSISTANT_REQUEST",
            currentRun_.id.isEmpty() ? "current-state" : currentRun_.id,
            QString("local bounded evidence; question_chars=%1").arg(cleanQuestion.size()));
    // 自动/关闭模式优先走轻量规则；手动/常开模式将自由问题交给模型。
    QString localAnswer;
    if ((aiMode_ == AiMode::Automatic || aiMode_ == AiMode::Off)
            && (cleanQuestion == "请解释当前仪器状态与检测证据，并指出需要人工确认的项目。"
            || cleanQuestion == "解释当前状态" || cleanQuestion == "当前状态怎么样")) {
        localAnswer = AiEvidenceBuilder::buildSummary(buildAiContext());
    } else if ((aiMode_ == AiMode::Automatic || aiMode_ == AiMode::Off)
            && (cleanQuestion == "请仅根据当前质量检查，说明哪些检查通过、哪些需要复核，以及证据缺口。"
               || cleanQuestion == "复核质量门控")) {
        if (result_.quality.checks.isEmpty()) localAnswer = "还没有完成检测，暂不能复核质量。";
        else {
            QStringList lines;
            for (const auto &check : result_.quality.checks)
                lines << check.title + (check.passed ? "：通过。" : "：需复核。") + check.detail;
            localAnswer = lines.join('\n');
        }
    } else if ((aiMode_ == AiMode::Automatic || aiMode_ == AiMode::Off)
            && (cleanQuestion == "你好" || cleanQuestion == "你能做什么")) {
        localAnswer = "可以帮你打开页面、查看仪器状态、查操作说明。试试“打开报告”或“如何编辑方法”。";
    } else if ((aiMode_ == AiMode::Automatic || aiMode_ == AiMode::Off)
            && (cleanQuestion.contains("怎么") || cleanQuestion.contains("如何")
               || cleanQuestion.contains("用法") || cleanQuestion.contains("步骤"))) {
        const auto hits = knowledgeStore_.search(cleanQuestion, 2);
        if (!hits.isEmpty() && hits.first().score >= 14
                && (hits.size() == 1 || hits.first().score >= hits.at(1).score + 5))
            localAnswer = hits.first().title + "\n" + hits.first().text;
    }
    if (!localAnswer.isEmpty()) {
        emit aiAssistantAnswerReady(cleanQuestion, localAnswer);
        return;
    }
    const auto manualHits = knowledgeStore_.search(cleanQuestion, 2);
    const QString manualContext = !manualHits.isEmpty() && manualHits.first().score >= 14
        ? knowledgeStore_.contextFor(cleanQuestion, 2, 2400) : QString{};
    if (aiMode_ == AiMode::Off) {
        emit aiAssistantAnswerReady(cleanQuestion, manualContext.isEmpty()
            ? QString("当前问题需要模型分析，请切换为自动、手动或常开模式。")
            : QString("相关操作说明：\n") + manualContext);
        return;
    }
    const QString groundedContext = currentAiEvidence()
        + (manualContext.isEmpty() ? QString{} : "\n\n<local_operator_manual>\n"
            + manualContext + "\n</local_operator_manual>");
    aiBridge_->askQuestion(cleanQuestion, groundedContext);
}

void AppController::exportCurrentReport() {
    QVector<int> candidateRows;
    candidateRows.reserve(result_.candidates.size());
    for (int row = 0; row < result_.candidates.size(); ++row) candidateRows.append(row);
    exportSelectedReport(candidateRows);
}

void AppController::exportSelectedReport(const QVector<int> &candidateRows) {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ExportData)) {
        emit notice("当前角色无权导出报告"); return;
    }
    if (currentRun_.id.isEmpty() || result_.processedSpectrum.points.isEmpty()) {
        emit notice("尚无可导出的检测结果");
        return;
    }
    const QString directory = PlatformPaths::documentsSubdirectory("飞秒质谱报告");
    if (!QDir().mkpath(directory)) { emit notice("无法创建报告目录"); return; }
    // 姓名优先、样本编号兜底；过滤 Windows 禁止字符并保留既有报告。
    const QString path = reportPathForRun(directory, currentRun_);
    QString error;
    if (!ReportGenerator::writePdf(path, currentRun_, result_, candidateRows, &error)) {
        emit notice("报告生成失败：" + error);
        return;
    }
    currentRun_.reportPath = path;
    const bool tracked = workspace_ && workspace_->setReportPath(currentRun_.id, path, sessionOperator_, &error);
    emit reportGenerated(path);
    emit recordsChanged();
    emit notice(tracked ? "报告已生成" : "PDF 已生成，但记录关联保存失败：" + error);
}

void AppController::markCurrentRunReviewed() {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ReviewResult)) {
        emit notice("当前角色无权记录人工复核"); return;
    }
    if (currentRun_.id.isEmpty()) { emit notice("尚无可复核记录"); return; }
    QString error;
    if (!workspace_ || !workspace_->setReviewStatus(currentRun_.id, "REVIEWED", sessionOperator_, &error)) {
        emit notice("复核状态保存失败：" + error);
        return;
    }
    currentRun_.reviewStatus = "REVIEWED";
    emit runSaved(currentRun_);
    emit recordsChanged();
    emit notice("已记录人工复核");
}

void AppController::createDemoMethodVersion(const QString &name, const QString &revisionNote) {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ManageMethods)) {
        emit notice("当前角色无权创建方法版本"); return;
    }
    if (!workspace_) { emit notice("方法仓库不可用"); return; }
    QString error;
    const auto method = workspace_->createMethodVersion(name,
        {{"data_scope", "DEMO_SIMULATION"}, {"instrument_contract", "sim-contract-1"},
         {"revision_note", revisionNote.trimmed()}}, sessionOperator_, &error);
    if (method.id.isEmpty()) { emit notice("方法版本保存失败：" + error); return; }
    emit methodsChanged();
    emit notice(QString("已创建 %1 v%2；需明确激活后才用于新检测").arg(method.name).arg(method.version));
}

bool AppController::createMethodDraft(const QString &name, const QJsonObject &parameters, const QString &baseMethodId) {
    if (!workspace_) {
        emit notice("无方法编辑权限或仓库不可用"); return false;
    }
    if (!fullMethodAccess()) {
        MethodDefinition base;
        for(const auto &candidate: methods()) if(candidate.id == baseMethodId) {base=candidate;break;}
        if(base.id.isEmpty() || !base.parameters.value("method_parameters").isObject() || name != base.name) {
            emit notice("请先选择管理员建立的方法；普通账号仅能另存参数版本"); return false;
        }
        auto expected=base.parameters.value("method_parameters").toObject();
        for(const auto &key: QStringList{"scan_mode","injection"}) {
            if(parameters.contains(key)) expected.insert(key,parameters.value(key)); else expected.remove(key);
        }
        if(expected != parameters) {emit notice("普通账号只能修改扫描模式和进样时间");return false;}
    }
    QString error;
    if (!MethodDraft::validate(parameters,&error)) {emit notice(error);return false;}
    const bool simulation = instrument_->descriptor().simulation;
    const auto method=workspace_->createMethodVersion(name,
        {{"data_scope", simulation ? "DEMO_SIMULATION" : "OFFLINE_DRAFT"},
         {"instrument_contract", simulation ? "sim-contract-1" : "UNMAPPED"},
         {"method_parameters",parameters}},sessionOperator_,&error);
    if(method.id.isEmpty()){emit notice("保存失败："+error);return false;}
    emit methodsChanged();emit notice(simulation
        ? "方法参数已保存；选择“设为当前方法”后应用"
        : "方法参数已保存，尚未映射或下发真实仪器");return true;
}
void AppController::activateMethod(const QString &methodId) {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ManageMethods)) {
        emit notice("当前角色无权激活方法版本"); return;
    }
    QString error;
    for(const auto &method:methods()) if(method.id==methodId) {
        if(method.parameters.value("data_scope")=="OFFLINE_DRAFT") {
            emit notice("此方法尚未验证；扫描参数与仪器协议映射未确认，不能用于采集");return;
        }
        const QJsonObject parameters = method.parameters.value("method_parameters").toObject();
        if (instrument_->descriptor().simulation && !parameters.isEmpty()) {
            if (!pendingMethodRequestId_.isEmpty()) { emit notice("请等待上一套方法参数确认完成"); return; }
            const auto validation = instrument_->validateMethodParameters(parameters);
            if (!validation.allowed) { emit notice("方法参数校验未通过：" + validation.reason); return; }
            pendingMethodRequestId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
            pendingMethodId_ = methodId; pendingMethodParameters_ = parameters;
            instrument_->requestMethodParameters(pendingMethodRequestId_, parameters);
            return;
        }
        break;
    }
    if (!workspace_ || !workspace_->activateMethod(methodId, sessionOperator_, &error)) {
        emit notice("方法激活失败：" + error); return;
    }
    emit methodsChanged();
    emit notice("活动方法已更新");
}

void AppController::loadStoredRun(const QString &runId) {
    if (phase_ == Phase::Acquiring || phase_ == Phase::Analyzing) {
        emit notice("检测中不能切换历史记录，请先完成或停止检测"); return;
    }
    if (!workspace_) { emit notice("检测记录不可用"); return; }
    const auto stored = workspace_->loadRun(runId);
    if (!stored.valid) {
        emit notice("该记录来自旧版本或数据不完整，无法恢复谱图");
        return;
    }
    detectionClock_.invalidate();
    detectionDurationMs_ = -1;
    currentRun_ = stored.summary;
    pendingSpectrum_ = stored.rawSpectrum;
    scans_ = stored.scans;
    emit scanSeriesChanged();
    result_ = stored.result;
    liveSpectrum_ = result_.processedSpectrum.points;
    setPhase(Phase::ResultReady, "已打开历史记录 " + runId);
    workspace_->appendAudit(sessionOperator_, "RUN_OPENED", runId, "historical run restored");
    emit spectrumChanged(liveSpectrum_);
    emit analysisCompleted(result_);
    emit runSaved(currentRun_);
    emit notice("历史谱图与候选证据已恢复");
}

void AppController::exportCurrentArchive() {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ExportData)) {
        emit notice("当前角色无权导出归档"); return;
    }
    if (!workspace_ || currentRun_.id.isEmpty()) { emit notice("尚无可归档记录"); return; }
    const QString directory = PlatformPaths::documentsSubdirectory("飞秒质谱归档");
    if (!QDir().mkpath(directory)) { emit notice("无法创建归档目录"); return; }
    const QString path = QDir(directory).filePath("QITest-" + currentRun_.id + ".qit.json");
    exportRunArchive(currentRun_.id, path);
}

void AppController::importRunArchive(const QString &path) {
    importRunArchives({path});
}

void AppController::loadPublicExample() {
    if (importWorker_ || phase_ == Phase::Acquiring || phase_ == Phase::Analyzing) {
        emit notice("请等待当前导入或检测完成"); return;
    }
    // Explicit opt-in only. The bundled resource is a small public scan excerpt,
    // not a generated instrument signal or a customer record to overwrite.
    importRunArchives({":/qitest/resources/examples/openms_bsa.scan.csv"});
    openExampleAfterImport_ = importWorker_ != nullptr;
}

AppController::~AppController() {
    if (importWorker_) {
        importWorker_->requestInterruption();
        importWorker_->wait(); // finish/rollback the current transaction before closing main DB
    }
    if (exportWorker_) exportWorker_->wait();
}

void AppController::cancelArchiveImport() {
    if (importWorker_) importWorker_->requestInterruption();
}

void AppController::loadBundledCustomerSamples() {
    // 厂家 CSV 转换的独立谱图：沿用哈希去重导入，不覆盖已有记录或触发仪器采集。
    const QDir directory(":/qitest/resources/customer_samples");
    QStringList paths;
    for (const auto &name : directory.entryList({"*.qit.json"}, QDir::Files, QDir::Name))
        paths.append(directory.filePath(name));
    const QString preview = directory.filePath(QString::fromUtf8("biscuit-ms-spectrum-13.qit.json"));
    if (paths.removeOne(preview)) paths.append(preview);
    importRunArchives(paths);
}

QVector<SpectrumPoint> AppController::bundledIntensityTrend(double mz, double tolerance) const {
    if (currentRun_.sampleInfo.value("source_sha256").toString()
        != "00de3d7e00009820c9a9f6c57e90ce8c821641c663543f6701841d48a43ef8b4") return {};
    // 只缓存不可变资源；不把序号写入 retentionTime，不进入定量/时间积分计算。
    static const auto spectra = [] {
        QVector<QVector<SpectrumPoint>> result;
        for (int row = 1; row <= 21; ++row) {
            const auto archive = RunArchiveCodec::read(QString(":/qitest/resources/customer_samples/biscuit-ms-spectrum-%1.qit.json")
                .arg(row, 2, 10, QChar('0')));
            if (!archive.valid) return QVector<QVector<SpectrumPoint>>{};
            result.append(archive.rawSpectrum);
        }
        return result;
    }();
    QVector<SpectrumPoint> trend;
    for (int row = 0; row < spectra.size(); ++row) {
        double sum = 0;
        for (const auto &point : spectra[row])
            if (mz <= 0 || std::abs(point.mz - mz) <= tolerance) sum += point.intensity;
        trend.append({double(row + 1), sum});
    }
    return trend;
}

void AppController::importRunArchives(const QStringList &paths) {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::RunAcquisition)) {
        emit notice("当前角色无权导入并分析归档"); return;
    }
    if (importWorker_ || phase_ == Phase::Acquiring || phase_ == Phase::Analyzing) {
        emit notice("请等待当前导入或检测完成"); return;
    }
    if (paths.isEmpty()) return;
    if (paths.size() > 200) { emit notice("每批最多 200 个归档，请分批导入"); return; }
    if (!workspace_) { emit notice("检测记录不可用"); return; }
    importWorker_ = new ArchiveImportWorker(paths, workspace_->databasePath(), sessionOperator_,
        engine_, instrument_->health(), instrument_->telemetry(), this);
    connect(importWorker_, &ArchiveImportWorker::progress, this, &AppController::importProgress);
    const bool bundled = paths.first().startsWith(":/qitest/resources/customer_samples/");
    connect(importWorker_, &QThread::finished, this, [this, showImported = paths.size() == 1 || bundled] {
        auto *finished = importWorker_;
        if (!finished) return;
        const QString summary = finished->summary();
        const bool wasExample = openExampleAfterImport_;
        const QString exampleId = (wasExample || showImported) ? finished->lastRunId() : QString{};
        openExampleAfterImport_ = false;
        importWorker_ = nullptr;
        finished->deleteLater();
        emit recordsChanged();
        emit importFinished(summary);
        emit notice(summary);
        if (!exampleId.isEmpty()) {
            loadStoredRun(exampleId);
            if (wasExample) emit notice({});
            else emit notice("已载入导入谱图，结果待复核");
        }
    });
    emit importProgress(0, paths.size(), "正在导入；可继续浏览页面，停止会保留已完成的记录");
    importWorker_->start(QThread::LowPriority);
}

void AppController::exportRunArchive(const QString &runId, const QString &path) {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ExportData)) {
        emit notice("当前角色无权导出数据"); return;
    }
    if (!workspace_ || path.isEmpty()) return;
    if (exportWorker_) { emit notice("正在导出，请稍候"); return; }
    const QString databasePath = workspace_->databasePath(), actor = sessionOperator_;
    auto outcome = std::make_shared<QPair<bool, QString>>(false, QString{});
    exportWorker_ = QThread::create([databasePath, actor, runId, path, outcome] {
        WorkspaceRepository repository(databasePath, false);
        if (!repository.open(&outcome->second)) return;
        try {
            const auto detail = repository.loadRun(runId);
            outcome->first = RunArchiveCodec::write(path, detail, &outcome->second);
            if (outcome->first) repository.appendAudit(actor, "RUN_ARCHIVED", runId, path);
        } catch (const std::exception &e) { outcome->second = QString::fromUtf8(e.what()); }
    });
    exportWorker_->setParent(this);
    connect(exportWorker_, &QThread::finished, this, [this, path, outcome] {
        auto *finished = exportWorker_;
        exportWorker_ = nullptr;
        finished->deleteLater();
        if (outcome->first) {
            emit archiveGenerated(path);
            emit notice("数据已导出：" + path);
        } else emit notice("导出失败：" + outcome->second);
    });
    emit notice("正在导出数据…");
    exportWorker_->start(QThread::LowPriority);
}

void AppController::startSampleDetection(const QJsonObject &sampleInfo, const QString &savePath) {
    if (phase_ == Phase::Acquiring || phase_ == Phase::Analyzing) return;
    if (exportWorker_) { emit notice("正在保存上一份数据，请完成后再开始新样本"); return; }
    if (sampleInfo.value("sample_id").toString().trimmed().isEmpty() || savePath.isEmpty()
        || QFileInfo::exists(savePath)) { emit notice("请填写样本编号，并选择未使用的保存文件名"); return; }
    if (instrument_->descriptor().simulation && currentRun_.dataScope == "IMPORTED_UNVALIDATED") {
        if (importWorker_ || !workspace_ || !AuthorizationPolicy::allows(sessionRole_, Permission::RunAcquisition)) return;
        // 未连接实机时，对当前导入记录重新分析；不能悄悄换成模拟谱。
        const auto stored = workspace_->loadRun(currentRun_.id);
        if (!stored.valid) { emit notice("原始记录不可用，无法重新分析"); return; }
        pendingSpectrum_ = stored.rawSpectrum;
        scans_ = stored.scans;
        activeSampleInfo_ = stored.summary.sampleInfo;
        for (auto it = sampleInfo.begin(); it != sampleInfo.end(); ++it) activeSampleInfo_.insert(it.key(), it.value());
        activeSampleInfo_.insert("reanalysis_source_run", currentRun_.id);
        activeSamplePath_ = savePath;
        detectionClock_.invalidate(); detectionDurationMs_ = -1;
        finishAcquisition();
        return;
    }
    startDetection();
    if (phase_ == Phase::Acquiring) { activeSampleInfo_ = sampleInfo; activeSamplePath_ = savePath; }
}

void AppController::startDetection() {
    if (importWorker_) { emit notice("正在导入数据，请完成或停止导入后再检测"); return; }
    if (phase_ == Phase::Acquiring || phase_ == Phase::Analyzing) return;
    activeSampleInfo_ = {}; activeSamplePath_.clear();
    detectionClock_.invalidate();
    detectionDurationMs_ = -1;
    if (!instrument_->descriptor().simulation) {
        emit notice("真实采集需完成厂家原始谱图协议与分析验证；当前交付仅开放受控接口，不生成伪装的真实结果。");
        return;
    }
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::RunAcquisition)) {
        emit notice("当前角色无权启动检测"); return;
    }
    if (!instrument_->health().ready) {
        setPhase(Phase::Failed, "设备未就绪");
        emit notice("设备条件不满足，检测已阻断");
        return;
    }
    const auto validation = instrument_->validate({"StartAcquisition", CommandRisk::Routine, {}});
    if (!validation.allowed) {
        setPhase(Phase::Failed, validation.reason);
        emit notice("检测命令被安全门控阻断：" + validation.reason);
        return;
    }
    if (!workspace_) {
        setPhase(Phase::Failed, "检测记录不可用");
        emit notice("无法建立可追溯采集会话，检测已阻断");
        return;
    }
    QString sessionError;
    activeAcquisitionSessionId_ = workspace_->beginAcquisitionSession(
        sessionOperator_, "DEMO_SIMULATION", &sessionError);
    if (activeAcquisitionSessionId_.isEmpty()) {
        setPhase(Phase::Failed, "无法建立采集会话");
        emit notice("采集会话建立失败：" + sessionError);
        return;
    }
    detectionClock_.start();
    pendingSpectrum_ = instrument_->acquireSpectrum();
    if (pendingSpectrum_.isEmpty()) {
        workspace_->finishAcquisitionSession(activeAcquisitionSessionId_, "FAILED",
            "instrument adapter returned no spectrum");
        activeAcquisitionSessionId_.clear();
        setPhase(Phase::Failed, "未获得谱图数据");
        return;
    }
    progress_ = 0;
    liveSpectrum_.clear();
    // Legacy adapters return one spectrum, not a time series. Do not synthesize a TIC.
    scans_.clear();
    emit scanSeriesChanged();
    setPhase(Phase::Acquiring, "正在采集");
    acquisitionTimer_.start();
}

void AppController::cancelDetection() {
    if (phase_ != Phase::Acquiring) {
        emit notice(phase_ == Phase::Analyzing ? "分析已进入本地确定性阶段，不再中断"
                                              : "当前没有可取消的采集");
        return;
    }
    acquisitionTimer_.stop();
    if (instrument_->validate({"CancelAcquisition", CommandRisk::Routine, {}}).allowed)
        instrument_->cancel();
    pendingSpectrum_.clear();
    liveSpectrum_.clear();
    if (workspace_ && !activeAcquisitionSessionId_.isEmpty())
        workspace_->finishAcquisitionSession(activeAcquisitionSessionId_, "CANCELLED", "cancelled by operator");
    activeAcquisitionSessionId_.clear();
    setPhase(Phase::Ready, "检测已取消");
    emit spectrumChanged(liveSpectrum_);
}

void AppController::setPhase(Phase phase, const QString &label) {
    if (detectionClock_.isValid() && phase != Phase::Acquiring && phase != Phase::Analyzing) {
        detectionDurationMs_ = detectionClock_.elapsed();
        detectionClock_.invalidate();
    }
    phase_ = phase;
    phaseLabel_ = label;
    emit phaseChanged(phase, label);
}

void AppController::finishAcquisition() {
    acquisitionTimer_.stop();
    setPhase(Phase::Analyzing, "正在分析");
    QTimer::singleShot(120, this, [this] {
        try {
            result_ = engine_.analyze(pendingSpectrum_, instrument_->health());
            liveSpectrum_ = result_.processedSpectrum.points;
            const auto method = activeMethod();
            const QString methodLabel = method.id.isEmpty() ? "未绑定方法"
                : QString("%1 v%2 [%3]").arg(method.name).arg(method.version).arg(method.checksum.left(8));
            currentRun_ = {QUuid::createUuid().toString(QUuid::WithoutBraces).left(12),
                QDateTime::currentDateTimeUtc(), sessionOperator_, methodLabel,
                activeSampleInfo_.contains("reanalysis_source_run") ? "IMPORTED_UNVALIDATED" : "DEMO_SIMULATION", qualityLevelName(result_.quality.level), result_.quality.score,
                static_cast<int>(result_.candidates.size()), "PENDING_REVIEW", {}};
            currentRun_.sampleInfo = activeSampleInfo_;
            QString storageError;
            const bool stored = workspace_ && workspace_->saveCompletedRun(
                currentRun_, pendingSpectrum_, result_, instrument_->telemetry(), &storageError, scans_);
            if (!stored)
                emit notice("检测完成，但记录保存失败：" + storageError);
            if (workspace_ && !activeAcquisitionSessionId_.isEmpty())
                workspace_->finishAcquisitionSession(activeAcquisitionSessionId_,
                    stored ? "COMPLETED" : "FAILED",
                    stored ? "analysis and traceable run persistence completed"
                           : "analysis completed but traceable run persistence failed: " + storageError);
            activeAcquisitionSessionId_.clear();
            setPhase(Phase::ResultReady,
                result_.quality.level == QualityLevel::Pass ? "质量通过" : "结果需要复核");
            emit spectrumChanged(liveSpectrum_);
            emit analysisCompleted(result_);
            emit runSaved(currentRun_);
            emit recordsChanged();
            if (stored && !activeSamplePath_.isEmpty()) {
                if (QFileInfo::exists(activeSamplePath_)) emit notice("自动保存未完成：目标文件已存在。检测数据已保留在本机，请另行导出。");
                else exportRunArchive(currentRun_.id, activeSamplePath_);
            }
        } catch (const std::exception &error) {
            if (workspace_ && !activeAcquisitionSessionId_.isEmpty())
                workspace_->finishAcquisitionSession(activeAcquisitionSessionId_, "FAILED",
                    "analysis exception: " + QString::fromUtf8(error.what()));
            activeAcquisitionSessionId_.clear();
            setPhase(Phase::Failed, QString::fromUtf8(error.what()));
        }
    });
}

void AppController::exportDiagnosticBundle() {
    if (!AuthorizationPolicy::allows(sessionRole_, Permission::ExportData)) {
        emit notice("当前角色无权导出诊断包"); return;
    }
    const QString directory = PlatformPaths::documentsSubdirectory("飞秒质谱诊断");
    if (!QDir().mkpath(directory)) { emit notice("无法创建诊断目录"); return; }
    const auto descriptor = instrument_->descriptor();
    const auto currentHealth = instrument_->health();
    QJsonObject context{
        {"application", QJsonObject{{"name", "飞秒质谱工作站"}, {"version", QCoreApplication::applicationVersion()},
            {"qt_version", QT_VERSION_STR}, {"os", QSysInfo::prettyProductName()},
            {"architecture", QSysInfo::currentCpuArchitecture()}}},
        {"instrument", QJsonObject{{"model", descriptor.model}, {"serial", descriptor.serialNumber},
            {"protocol", descriptor.protocolVersion}, {"simulator", descriptor.simulation},
            {"connected", currentHealth.connected}, {"ready", currentHealth.ready}}},
        {"components", QJsonObject{{"library", librarySummary_}, {"ai", aiSummary()},
            {"workspace", workspaceSummary()}}},
        {"recovery", QJsonObject{{"interrupted_sessions_recovered", recoveredSessionCount_}}},
        {"last_run", QJsonObject{{"id", currentRun_.id}, {"data_scope", currentRun_.dataScope},
            {"quality_level", currentRun_.qualityLevel}, {"quality_score", currentRun_.qualityScore},
            {"review_status", currentRun_.reviewStatus}}}
    };
    const QString path = QDir(directory).filePath("QITest-diagnostic-"
        + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json");
    QString error;
    if (!DiagnosticBundle::write(path, context, &error)) {
        emit notice("诊断包导出失败：" + error); return;
    }
    if (workspace_) workspace_->appendAudit(sessionOperator_, "DIAGNOSTIC_EXPORTED", "application", path);
    emit diagnosticGenerated(path);
    emit notice("脱敏诊断包已生成");
}

} // namespace qitest
