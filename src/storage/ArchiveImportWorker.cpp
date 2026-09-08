#include "storage/ArchiveImportWorker.h"
#include "storage/RunArchiveCodec.h"
#include <QCryptographicHash>
#include <QFileInfo>

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
                    const auto result = engine_.analyze(archive.rawSpectrum, health_);
                    if (isInterruptionRequested()) break;
                    const QString quality = result.quality.level == QualityLevel::Pass ? "PASS"
                        : result.quality.level == QualityLevel::Review ? "REVIEW" : "FAIL";
                    const RunSummary summary{id, QDateTime::currentDateTimeUtc(), actor_,
                        example ? "公开示例 · OpenMS BSA" : (archive.scans.isEmpty() ? "导入谱图分析" : "扫描序列·首个有效 MS1 分析"),
                        example ? "PUBLIC_EXAMPLE" : "IMPORTED_UNVALIDATED", quality, result.quality.score,
                        static_cast<int>(result.candidates.size()), "PENDING_REVIEW", {}, archive.sampleInfo};
                    if (repository.saveCompletedRun(summary, archive.rawSpectrum, result, telemetry_, &error, archive.scans)) {
                        ++imported; outcome = "已导入，待复核"; lastRunId_ = id;
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
