#pragma once

#include "domain/Models.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace qitest {

struct RunSummary {
    QString id;
    QDateTime completedAt;
    QString operatorName;
    QString methodName;
    QString dataScope;
    QString qualityLevel;
    int qualityScore = 0;
    int candidateCount = 0;
    QString reviewStatus;
    QString reportPath;
    QJsonObject sampleInfo; // 样本资料独立于操作者，禁止加入 AI 上下文。
};

struct MethodDefinition {
    QString id;
    QString name;
    int version = 0;
    QJsonObject parameters;
    QString checksum;
    QString createdBy;
    QDateTime createdAt;
    bool active = false;
};

struct StoredRunDetail {
    RunSummary summary;
    QVector<SpectrumPoint> rawSpectrum;
    InstrumentTelemetry telemetry;
    AnalysisResult result;
    bool valid = false;
    QVector<SpectrumScan> scans;
};

struct AcquisitionSession {
    QString id;
    QDateTime startedAt;
    QDateTime finishedAt;
    QString actor;
    QString dataScope;
    QString status;
    QString detail;
};

class WorkspaceRepository final {
public:
    explicit WorkspaceRepository(QString databasePath, bool tracksLifecycle = true);
    ~WorkspaceRepository();

    bool open(QString *error = nullptr);
    bool saveCompletedRun(const RunSummary &summary,
        const QVector<SpectrumPoint> &rawSpectrum,
        const AnalysisResult &result,
        const InstrumentTelemetry &telemetry,
        QString *error = nullptr, const QVector<SpectrumScan> &scans = {});
    QVector<RunSummary> recentRuns(int limit = 100) const;
    StoredRunDetail loadRun(const QString &runId) const;
    bool containsRun(const QString &runId) const;
    bool setReviewStatus(const QString &runId, const QString &status,
        const QString &actor, QString *error = nullptr);
    bool setReportPath(const QString &runId, const QString &path,
        const QString &actor, QString *error = nullptr);
    bool appendAudit(const QString &actor, const QString &action,
        const QString &targetId, const QString &detail, QString *error = nullptr);
    MethodDefinition createMethodVersion(const QString &name, const QJsonObject &parameters,
        const QString &actor, QString *error = nullptr);
    QVector<MethodDefinition> methods() const;
    bool activateMethod(const QString &methodId, const QString &actor, QString *error = nullptr);
    MethodDefinition activeMethod() const;
    QString beginAcquisitionSession(const QString &actor, const QString &dataScope,
        QString *error = nullptr);
    bool finishAcquisitionSession(const QString &sessionId, const QString &status,
        const QString &detail, QString *error = nullptr);
    int recoverInterruptedSessions(QString *error = nullptr);
    QVector<AcquisitionSession> recentAcquisitionSessions(int limit = 50) const;
    QString databasePath() const { return databasePath_; }

private:
    bool initializeSchema(QString *error);
    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    bool tracksLifecycle_;
};

} // namespace qitest
