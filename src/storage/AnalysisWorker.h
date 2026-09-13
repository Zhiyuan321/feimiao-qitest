#pragma once
#include "core/AnalysisEngine.h"
#include "storage/WorkspaceRepository.h"
#include <QThread>

namespace qitest {
// Input snapshots are immutable once started. Read outputs only after finished/wait.
// The worker owns its database connection; GUI-thread repositories never cross threads.
class AnalysisWorker final : public QThread {
public:
    AnalysisWorker(AnalysisEngine engine, QVector<SpectrumPoint> spectrum,
                   QVector<SpectrumScan> scans, InstrumentHealth health,
                   InstrumentTelemetry telemetry, RunSummary summary, QString databasePath,
                   QObject *parent = nullptr)
        : QThread(parent), engine_(std::move(engine)), spectrum_(std::move(spectrum)),
          scans_(std::move(scans)), health_(health), telemetry_(telemetry),
          summary(std::move(summary)), databasePath_(std::move(databasePath)) {}
    AnalysisResult result;
    RunSummary summary;
    QString error;
    bool stored = false;
protected:
    void run() override {
        try {
            result = engine_.analyze(spectrum_, health_);
            summary.completedAt = QDateTime::currentDateTimeUtc();
            summary.qualityLevel = result.quality.level == QualityLevel::Pass ? "PASS"
                : result.quality.level == QualityLevel::Review ? "REVIEW" : "FAIL";
            summary.qualityScore = result.quality.score;
            summary.candidateCount = result.candidates.size();
            WorkspaceRepository repository(databasePath_, false);
            stored = repository.open(&error) && repository.saveCompletedRun(
                summary, spectrum_, result, telemetry_, &error, scans_);
        } catch (const std::exception &exception) {
            error = QString::fromUtf8(exception.what());
        } catch (...) {
            error = "分析未完成";
        }
    }
private:
    AnalysisEngine engine_;
    QVector<SpectrumPoint> spectrum_;
    QVector<SpectrumScan> scans_;
    InstrumentHealth health_;
    InstrumentTelemetry telemetry_;
    QString databasePath_;
};
} // namespace qitest
