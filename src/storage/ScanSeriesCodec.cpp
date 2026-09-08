#include "storage/ScanSeriesCodec.h"
#include "core/ChromatogramEngine.h"
#include <QJsonObject>
#include <cmath>
namespace qitest {
QJsonArray ScanSeriesCodec::encode(const QVector<SpectrumScan> &scans) {
    QJsonArray result;
    for (const auto &scan : scans) {
        QJsonArray points;
        for (const auto &p : scan.points) points.append(QJsonArray{p.mz, p.intensity});
        result.append(QJsonObject{{"time_s", scan.timeSeconds}, {"ms_level", scan.msLevel}, {"points", points}});
    }
    return result;
}
bool ScanSeriesCodec::decode(const QJsonArray &array, QVector<SpectrumScan> *scans, QString *error) {
    if (!scans) {
        if (error) *error = "缺少扫描序列输出位置";
        return false;
    }
    QVector<SpectrumScan> parsed;
    if (array.isEmpty() || array.size() > ChromatogramEngine::MaximumScans) {
        if (error) *error = "扫描序列为空或超出支持数量"; return false;
    }
    qint64 count = 0;
    for (const auto &value : array) {
        const auto object = value.toObject();
        const auto points = object.value("points").toArray();
        if ((count += points.size()) > ChromatogramEngine::MaximumPoints) {
            if (error) *error = "扫描谱点超过 100 万"; return false;
        }
        const double level = object.value("ms_level").toDouble(std::nan(""));
        if (level != 1.0 && level != 2.0) { if (error) *error = "扫描级别无效"; return false; }
        SpectrumScan scan{object.value("time_s").toDouble(std::nan("")), int(level), {}};
        for (const auto &point : points) {
            const auto pair = point.toArray();
            if (pair.size() != 2) { if (error) *error = "扫描谱点格式无效"; return false; }
            scan.points.append({pair[0].toDouble(std::nan("")), pair[1].toDouble(std::nan(""))});
        }
        parsed.append(scan);
    }
    if (!ChromatogramEngine::validate(parsed, error)) return false;
    *scans = std::move(parsed);
    return true;
}
}
