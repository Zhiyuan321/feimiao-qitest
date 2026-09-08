#include "storage/RunArchiveCodec.h"
#include "storage/ScanSeriesCodec.h"
#include "core/ChromatogramEngine.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace qitest {
namespace {
QByteArray canonical(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QString digest(const QJsonObject &object) {
    return QString::fromLatin1(QCryptographicHash::hash(canonical(object), QCryptographicHash::Sha256).toHex());
}

ArchiveReadResult readScanCsv(QFile &file) {
    ArchiveReadResult result;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray header = file.readLine(256);
    hash.addData(header);
    if (header.startsWith("\xef\xbb\xbf")) header.remove(0, 3);
    if (header.trimmed() != "time_s,ms_level,mz,intensity") {
        result.error = "扫描 CSV 表头必须为 time_s,ms_level,mz,intensity（秒、扫描级别、m/z、强度）"; return result;
    }
    int rows = 0;
    qint64 bytesRead = header.size();
    while (!file.atEnd()) {
        const QByteArray line = file.readLine(256);
        bytesRead += line.size();
        if (bytesRead > RunArchiveCodec::MaximumFileBytes) { result.error = "扫描 CSV 超过 64 MiB"; return result; }
        hash.addData(line);
        if (line.size() >= 255 || ++rows > ChromatogramEngine::MaximumPoints) {
            result.error = "扫描 CSV 行过长或谱点超出 100 万，请分段导入"; return result;
        }
        if (line.trimmed().isEmpty()) continue;
        const auto cells = line.trimmed().split(',');
        if (cells.size() != 4) { result.error = "扫描 CSV 必须是四列数值"; return result; }
        bool tOk, lOk, mOk, iOk;
        const double time = cells[0].toDouble(&tOk), mz = cells[2].toDouble(&mOk), intensity = cells[3].toDouble(&iOk);
        const int level = cells[1].toInt(&lOk);
        if (!tOk || !lOk || !mOk || !iOk || !std::isfinite(time) || !std::isfinite(mz) || !std::isfinite(intensity)) {
            result.error = QString("扫描 CSV 第 %1 行不是有效数值").arg(rows + 1); return result;
        }
        if (result.scans.isEmpty() || result.scans.last().timeSeconds != time) {
            if (result.scans.size() >= ChromatogramEngine::MaximumScans) {
                result.error = "扫描数超过 5000，请分段导入"; return result;
            }
            result.scans.append({time, level, {}});
        }
        if (result.scans.last().msLevel != level) { result.error = "同一扫描时间不能混合扫描级别"; return result; }
        result.scans.last().points.append({mz, intensity});
    }
    if (file.error() != QFileDevice::NoError || !ChromatogramEngine::validate(result.scans, &result.error)) return result;
    // Screening remains a single-scan task. Never label it as whole-run identification.
    for (const auto &scan : result.scans) if (scan.msLevel == 1 && scan.points.size() >= 20) {
        result.rawSpectrum = scan.points; break;
    }
    if (result.rawSpectrum.isEmpty()) {
        result.error = "需要至少一次含 20 个谱点的 MS1 扫描，才能建立检测记录"; return result;
    }
    result.payloadHash = QString::fromLatin1(hash.result().toHex());
    result.sourceRunId = "scan-csv";
    result.valid = true;
    return result;
}
}

bool RunArchiveCodec::write(const QString &path, const StoredRunDetail &detail, QString *error) {
    if (!detail.valid || detail.rawSpectrum.isEmpty()) {
        if (error) *error = "run detail is incomplete";
        return false;
    }
    QJsonArray points;
    for (const auto &point : detail.rawSpectrum) points.append(QJsonArray{point.mz, point.intensity});
    QJsonObject payload{{"source_run_id", detail.summary.id},
        {"completed_at", detail.summary.completedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"operator", detail.summary.operatorName}, {"method", detail.summary.methodName},
        {"sample_info", detail.summary.sampleInfo},
        {"data_scope", detail.summary.dataScope}, {"engine_version", detail.result.engineVersion},
        {"library_version", detail.result.libraryVersion}, {"raw_spectrum", points}};
    if (!detail.scans.isEmpty()) {
        if (!ChromatogramEngine::validate(detail.scans, error)) return false;
        payload.insert("scans", ScanSeriesCodec::encode(detail.scans));
    }
    const QJsonObject archive{{"schema", "qitest-run-archive-1"}, {"payload", payload},
        {"payload_sha256", digest(payload)}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString(); return false;
    }
    const QByteArray bytes = QJsonDocument(archive).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumFileBytes) {
        if (error) *error = "归档超过 64 MiB，请分段导出";
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

ArchiveReadResult RunArchiveCodec::read(const QString &path) {
    ArchiveReadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    if (file.size() > MaximumFileBytes) {
        result.error = "文件超过 64 MiB，请分批或分段导入"; return result;
    }
    if (path.endsWith(".csv", Qt::CaseInsensitive)) return readScanCsv(file);
    const QByteArray bytes = file.read(MaximumFileBytes + 1);
    if (bytes.size() > MaximumFileBytes || file.error() != QFileDevice::NoError) {
        result.error = "归档过大或读取失败"; return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = "invalid JSON archive"; return result;
    }
    const auto root = document.object();
    if (root.value("schema").toString() != "qitest-run-archive-1") {
        result.error = "unsupported archive schema"; return result;
    }
    const auto payload = root.value("payload").toObject();
    result.sampleInfo = payload.value("sample_info").toObject();
    if (root.value("payload_sha256").toString() != digest(payload)) {
        result.error = "archive checksum mismatch"; return result;
    }
    const auto points = payload.value("raw_spectrum").toArray();
    if (points.size() < 20 || points.size() > 5000000) {
        result.error = "spectrum point count outside supported range"; return result;
    }
    double previousMz = -1.0;
    for (const auto &value : points) {
        const auto pair = value.toArray();
        if (pair.size() != 2) { result.error = "invalid spectrum point"; return result; }
        const double mz = pair[0].toDouble(std::nan(""));
        const double intensity = pair[1].toDouble(std::nan(""));
        if (!std::isfinite(mz) || !std::isfinite(intensity) || mz <= 0.0 || mz <= previousMz || intensity < 0.0) {
            result.error = "invalid or unsorted spectrum"; return result;
        }
        result.rawSpectrum.push_back({mz, intensity});
        previousMz = mz;
    }
    result.sourceRunId = payload.value("source_run_id").toString();
    result.payloadHash = root.value("payload_sha256").toString();
    if (payload.contains("scans") && !ScanSeriesCodec::decode(payload.value("scans").toArray(), &result.scans, &result.error))
        return result;
    result.valid = true;
    return result;
}

} // namespace qitest
