#pragma once

#include "storage/WorkspaceRepository.h"

namespace qitest {

struct ArchiveReadResult {
    bool valid = false;
    QString error;
    QString sourceRunId;
    QString payloadHash;
    QJsonObject sampleInfo;
    QVector<SpectrumPoint> rawSpectrum;
    QVector<SpectrumScan> scans;
};

class RunArchiveCodec final {
public:
    static constexpr qint64 MaximumFileBytes = 64 * 1024 * 1024;
    static bool write(const QString &path, const StoredRunDetail &detail, QString *error = nullptr);
    static ArchiveReadResult read(const QString &path);
};

} // namespace qitest
