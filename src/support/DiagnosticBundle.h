#pragma once

#include <QJsonObject>
#include <QString>

namespace qitest {

class DiagnosticBundle final {
public:
    static bool write(const QString &path, QJsonObject context, QString *error = nullptr);
};

} // namespace qitest
