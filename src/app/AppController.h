#pragma once

#include "ai/AiEvidenceBuilder.h"
#include "ai/LocalKnowledgeStore.h"
#include "core/AnalysisEngine.h"
#include "device/IInstrumentAdapter.h"
#include "storage/WorkspaceRepository.h"
#include "library/SpectralLibraryRepository.h"
#include "security/AuthorizationPolicy.h"

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QVariantMap>
#include <QElapsedTimer>
#include <memory>

namespace qitest {

class LocalAiBridge;
class ArchiveImportWorker;

struct StartupCheck {
    QString name;
    QString detail;
    bool passed = false;
    bool blocking = false;
};

// 业务协调层：接收界面意图，负责状态、权限、确认、审计及设备回执。
// 新增硬件控制必须走 updateInstrumentSetting，不能在按钮槽中直接写 SDK。
// 主界面只订阅这里的状态信号，不把“点击过”当作“设备已执行”。
class AppController final : public QObject {
    Q_OBJECT
public:
    enum class Phase { Ready, Acquiring, Analyzing, ResultReady, Failed };
    Q_ENUM(Phase)
    enum class AiMode { Automatic, Manual, AlwaysOn, Off };
    Q_ENUM(AiMode)

    explicit AppController(std::unique_ptr<IInstrumentAdapter> instrument, QObject *parent = nullptr);
    ~AppController() override;
    bool importInProgress() const { return importWorker_ != nullptr; }

    InstrumentHealth health() const;
    InstrumentTelemetry telemetry() const;
    InstrumentDescriptor instrumentDescriptor() const;
    QVariantMap instrumentSettings() const { return instrumentSettings_; }
    QString sessionSummary() const;
    QString librarySummary() const { return librarySummary_; }
    QString aiSummary() const;
    QString aiContextSummary() const;
    QString workspaceSummary() const;
    QVector<RunSummary> recentRuns(int limit = 100) const;
    QVector<LibraryCompound> searchLibrary(const QString &query, int limit = 100) const;
    QVector<StartupCheck> startupChecks() const;
    QVector<MethodDefinition> methods() const;
    MethodDefinition activeMethod() const;
    const RunSummary &currentRun() const { return currentRun_; }
    Phase phase() const { return phase_; }
    qint64 softwareElapsedMs() const { return softwareClock_.elapsed(); }
    qint64 detectionElapsedMs() const {
        return detectionClock_.isValid() ? detectionClock_.elapsed() : detectionDurationMs_;
    }
    const QVector<SpectrumPoint> &liveSpectrum() const { return liveSpectrum_; }
    const QVector<SpectrumScan> &scans() const { return scans_; }
    const AnalysisResult &result() const { return result_; }

public slots:
    void startDetection();
    void startSampleDetection(const QJsonObject &sampleInfo, const QString &savePath);
    void cancelDetection();
    void explainCurrentState();
    void prepareAiAssistant();
    void askAiAssistant(const QString &question);
    void setSessionOperator(const QString &operatorName);
    void setOfflineDemoSession();
    void exportCurrentReport();
    void exportSelectedReport(const QVector<int> &candidateRows);
    void markCurrentRunReviewed();
    void createDemoMethodVersion(const QString &name, const QString &revisionNote);
    bool createMethodDraft(const QString &name, const QJsonObject &parameters);
    void activateMethod(const QString &methodId);
    void loadStoredRun(const QString &runId);
    void exportCurrentArchive();
    void importRunArchive(const QString &path);
    void importRunArchives(const QStringList &paths);
    void loadBundledCustomerSamples();
    // 时间单位未确认时只返回按原始行序的强度趋势，不能用于时间积分。
    QVector<SpectrumPoint> bundledIntensityTrend(double mz = 0, double tolerance = 0.5) const;
    void loadPublicExample();
    void cancelArchiveImport();
    void exportRunArchive(const QString &runId, const QString &path);
    void exportDiagnosticBundle();
    // 返回 true 只表示已提交请求，不代表仪器成功；结果由状态/提示信号异步通知。
    // confirmed 表示用户完成确认，不是设备确认，也不能替代权限和互锁检查。
    bool updateInstrumentSetting(const QString &key, const QVariant &value, bool confirmed = false);
    void setDeepAiEnabled(bool enabled);
    void setAiMode(qitest::AppController::AiMode mode);
    void cancelAiQuestion();
    void explainFeature(const QString &feature);
    bool saveInstrumentPreset(const QVariantMap &preset);
    bool deepAiEnabled() const { return aiMode_ != AiMode::Off; }
    AiMode aiMode() const { return aiMode_; }

signals:
    void phaseChanged(qitest::AppController::Phase phase, const QString &label);
    void progressChanged(int percent);
    void spectrumChanged(const QVector<qitest::SpectrumPoint> &points);
    void scanSeriesChanged();
    void analysisCompleted(const qitest::AnalysisResult &result);
    void notice(const QString &text);
    void aiStateChanged(const QString &state);
    void aiBusyChanged(bool busy);
    void aiExplanationReady(const QString &text);
    void aiAssistantAnswerReady(const QString &question, const QString &text);
    void aiAssistantFailed(const QString &message);
    void aiOperationProposed(const QString &question, const QString &toolEnvelope);
    void runSaved(const qitest::RunSummary &run);
    void recordsChanged();
    void reportGenerated(const QString &path);
    void methodsChanged();
    void archiveGenerated(const QString &path);
    void diagnosticGenerated(const QString &path);
    void sessionChanged(const QString &operatorName, const QString &roleName);
    void instrumentSettingsChanged(const QVariantMap &settings);
    void instrumentCommandPending(const QString &key, bool pending);
    void instrumentConfirmationRequired(const QString &key, const QVariant &value);
    void deepAiEnabledChanged(bool enabled);
    void aiModeChanged(qitest::AppController::AiMode mode);
    void importProgress(int completed, int total, const QString &detail);
    void importFinished(const QString &summary);

private:
    void setPhase(Phase phase, const QString &label);
    void finishAcquisition();
    AiContextSnapshot buildAiContext() const;
    QString currentAiEvidence() const;

    std::unique_ptr<IInstrumentAdapter> instrument_;
    AnalysisEngine engine_;
    QTimer acquisitionTimer_;
    QElapsedTimer softwareClock_, detectionClock_;
    qint64 detectionDurationMs_ = -1; // Unknown for imported/historical records.
    QVector<SpectrumScan> scans_;
    int progress_ = 0;
    Phase phase_ = Phase::Ready;
    QVector<SpectrumPoint> pendingSpectrum_;
    QVector<SpectrumPoint> liveSpectrum_;
    AnalysisResult result_;
    QString librarySummary_ = "正式参考库未加载";
    QString libraryDatabasePath_;
    LocalAiBridge *aiBridge_ = nullptr;
    LocalKnowledgeStore knowledgeStore_;
    std::unique_ptr<WorkspaceRepository> workspace_;
    RunSummary currentRun_;
    QJsonObject activeSampleInfo_;
    QString activeSamplePath_;
    QString sessionOperator_ = "offline-demo";
    SessionRole sessionRole_ = SessionRole::OfflineDemo;
    QString activeAcquisitionSessionId_;
    int recoveredSessionCount_ = 0;
    QString phaseLabel_ = "就绪";
    QVariantMap instrumentSettings_;
    QString pendingSettingId_;
    QString pendingSettingKey_;
    QVariant pendingSettingValue_;
    QTimer settingTimeout_;
    // 默认自动：简单问题本地处理，复杂问题按需加载模型。
    AiMode aiMode_ = AiMode::Automatic;
    ArchiveImportWorker *importWorker_ = nullptr;
    bool openExampleAfterImport_ = false;
    QThread *exportWorker_ = nullptr;
};

} // namespace qitest

Q_DECLARE_METATYPE(qitest::AppController::Phase)
Q_DECLARE_METATYPE(qitest::AppController::AiMode)
Q_DECLARE_METATYPE(qitest::RunSummary)
Q_DECLARE_METATYPE(qitest::StartupCheck)
