#include "support/DiagnosticBundle.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSaveFile>

namespace qitest {

bool DiagnosticBundle::write(const QString &path, QJsonObject context, QString *error) {
    context["schema"] = "qitest-diagnostic-1";
    context["generated_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    context["privacy"] = QJsonObject{
        {"contains_passwords", false},
        {"contains_raw_spectrum", false},
        {"contains_ai_prompts", false},
        {"contains_candidate_names", false}
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(context).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace qitest
