#pragma once

#include "core/AnalysisEngine.h"
#include <QThread>
#include <QStringList>

namespace qitest {

// 每批有限额、每个工作线程单独持有 SQLite 连接，每次只装入一个归档。
// 仅向界面发送小型进度消息，不在信号队列堆积整份谱图。
// summary()/lastRunId() 必须在 finished 或 wait 完成后读取，避免跨线程读写冲突。
class ArchiveImportWorker final : public QThread {
    Q_OBJECT
public:
    ArchiveImportWorker(QStringList paths, QString databasePath, QString actor,
        AnalysisEngine engine, InstrumentHealth health, InstrumentTelemetry telemetry,
        QObject *parent = nullptr);
    QString summary() const { return summary_; } // read only after finished/wait
    QString lastRunId() const { return lastRunId_; } // read only after finished/wait
signals:
    void progress(int completed, int total, const QString &detail);
protected:
    void run() override;
private:
    QStringList paths_;
    QString databasePath_, actor_, summary_;
    QString lastRunId_;
    AnalysisEngine engine_;
    InstrumentHealth health_;
    InstrumentTelemetry telemetry_;
};

} // namespace qitest
