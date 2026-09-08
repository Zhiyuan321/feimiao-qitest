#pragma once
#include "domain/Models.h"
#include <QJsonArray>
namespace qitest {
class ScanSeriesCodec final {
public:
    static QJsonArray encode(const QVector<SpectrumScan> &scans);
    static bool decode(const QJsonArray &array, QVector<SpectrumScan> *scans, QString *error);
};
}
