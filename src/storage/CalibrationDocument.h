#pragma once
#include "core/CalibrationModel.h"

namespace qitest {
// Portable, bounded and atomic files. No evaluation of user-provided code.
class CalibrationDocument final {
public:
    static constexpr qint64 MaximumBytes = 2 * 1024 * 1024;
    static bool readCsv(const QString &path, CalibrationModel *model, QString *error);
    static bool save(const QString &path, const CalibrationModel &model, QString *error);
    static bool load(const QString &path, CalibrationModel *model, QString *error);
    static bool exportCsv(const QString &path, const CalibrationModel &model, QString *error);
};
}
