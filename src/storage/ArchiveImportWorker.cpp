#include "storage/ArchiveImportWorker.h"
#include "storage/RunArchiveCodec.h"
#include "core/IonThresholdScreening.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <limits>

namespace qitest {

ArchiveImportWorker::ArchiveImportWorker(QStringList paths, QString databasePath, QString actor,
    AnalysisEngine engine, InstrumentHealth health, InstrumentTelemetry telemetry, QObject *parent)
    : QThread(parent), paths_(std::move(paths)), databasePath_(std::move(databasePath)),
      actor_(std::move(actor)), engine_(std::move(engine)), health_(health), telemetry_(telemetry) {}

void ArchiveImportWorker::run() {
    WorkspaceRepository repository(databasePath_, false);
    QString error;
    if (!repository.open(&error)) { summary_ = "无法打开数据目录：" + error; return; }
    int imported = 0, skipped = 0, failed = 0, done = 0;
    for (const auto &path : paths_) {
        if (isInterruptionRequested()) break;
        QString outcome;
        try {
            const auto archive = RunArchiveCodec::read(path);
            if (!archive.valid) { ++failed; outcome = archive.error; }
            else {
                const bool example = path == ":/qitest/resources/examples/openms_bsa.scan.csv";
                const QString id = (example ? "example-" : "import-") + QString::fromLatin1(QCryptographicHash::hash(
                    (archive.payloadHash + ":" + AnalysisEngine::Version).toUtf8(),
                    QCryptographicHash::Sha256).toHex());
                if (repository.containsRun(id)) { ++skipped; outcome = "已存在，跳过"; lastRunId_ = id; }
                else {
                    auto sampleInfo=archive.sampleInfo;
                    const bool thresholdScreening=sampleInfo.contains("ion_screening_snapshot");
                    const bool rawOnly=thresholdScreening || sampleInfo.value("screening_status").toString()=="NOT_CONFIGURED";
                    AnalysisResult result;
                    if(rawOnly) {
                        result.processedSpectrum.points=archive.rawSpectrum;
                        for(const auto &point:archive.rawSpectrum) result.processedSpectrum.totalIonCurrent+=point.intensity;
                        result.engineVersion="tcp-fullscan-raw-1";result.libraryVersion="未配置实机筛查库";
                        result.quality.level=QualityLevel::Review;
                        if(thresholdScreening) {
                            QString screeningError;
                            const auto status=IonThresholdScreening::apply(archive.scans,sampleInfo.value("ion_screening_snapshot").toObject(),&result,&screeningError);
                            sampleInfo.insert("screening_status",status);sampleInfo.insert("screening_error",screeningError);
                        }
                    } else result = engine_.analyze(archive.rawSpectrum, health_);
                    if (isInterruptionRequested()) break;
                    const QString quality = result.quality.level == QualityLevel::Pass ? "PASS"
                        : result.quality.level == QualityLevel::Review ? "REVIEW" : "FAIL";
                    const RunSummary summary{id, QDateTime::currentDateTimeUtc(), actor_,
                        example ? "公开示例 · OpenMS BSA" : (archive.scans.isEmpty() ? "导入谱图分析" : "扫描序列·首个有效 MS1 分析"),
                        example ? "PUBLIC_EXAMPLE" : "IMPORTED_UNVALIDATED", quality, result.quality.score,
                        static_cast<int>(result.candidates.size()), "PENDING_REVIEW", {}, sampleInfo};
                    const double unknown=std::numeric_limits<double>::quiet_NaN();
                    const InstrumentTelemetry noTelemetry{unknown,unknown,unknown,unknown,unknown,"未提供",
                        unknown,unknown,unknown,unknown,unknown,unknown,unknown,unknown};
                    if (repository.saveCompletedRun(summary, archive.rawSpectrum, result, rawOnly?noTelemetry:telemetry_, &error, archive.scans)) {
                        ++imported; outcome = thresholdScreening?"已导入，按记录内谱库快照恢复筛查":rawOnly?"已导入原始谱，筛查未配置":"已导入"; lastRunId_ = id;
                    } else { ++failed; outcome = "保存失败：" + error; }
                }
            }
        } catch (const std::exception &e) {
            ++failed; outcome = "分析失败：" + QString::fromUtf8(e.what());
        }
        emit progress(++done, paths_.size(), QFileInfo(path).fileName() + "：" + outcome.left(300));
    }
    summary_ = QString("%1 · 导入 %2，重复跳过 %3，失败 %4，未处理 %5")
        .arg(isInterruptionRequested() ? "已停止" : "处理完成")
        .arg(imported).arg(skipped).arg(failed).arg(paths_.size() - done);
}

} // namespace qitest
