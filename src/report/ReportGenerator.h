#pragma once

#include "domain/Models.h"
#include "storage/WorkspaceRepository.h"

namespace qitest {

class ReportGenerator final {
public:
    static bool writePdf(const QString &path, const RunSummary &run,
        const AnalysisResult &result, QString *error = nullptr);
    static bool writePdf(const QString &path, const RunSummary &run,
        const AnalysisResult &result, const QVector<int> &candidateRows,
        QString *error = nullptr);
};

} // namespace qitest
