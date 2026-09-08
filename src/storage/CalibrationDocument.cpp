#include "storage/CalibrationDocument.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

namespace qitest {
namespace {
bool failure(QString *error, const QString &message) { if (error) *error = message; return false; }
bool readBounded(const QString &path, QByteArray *data, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(error, file.errorString());
    *data = file.read(CalibrationDocument::MaximumBytes + 1);
    if (data->size() > CalibrationDocument::MaximumBytes || file.error() != QFile::NoError)
        return failure(error, "文件过大或读取失败");
    return true;
}
bool writeAtomic(const QString &path, const QByteArray &data, QString *error) {
    if (data.size() > CalibrationDocument::MaximumBytes) return failure(error, "校准文件超过 2 MiB");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return failure(error, file.errorString());
    return true;
}
bool metadataValid(const CalibrationModel &model, QString *error) {
    for (const auto &field : {model.name, model.concentrationUnit, model.responseUnit, model.internalStandard})
        if (field.size() > 80 || field.contains(QRegularExpression("[\\x00-\\x1f]"))) return failure(error, "名称和单位须不超过 80 字，且不含控制字符");
    if (model.name.trimmed().isEmpty() || model.concentrationUnit.trimmed().isEmpty() || model.responseUnit.trimmed().isEmpty())
        return failure(error, "请填写校准名称、浓度单位和响应单位");
    const auto evaluation = CalibrationCalculator::evaluate(model);
    return evaluation.fit.valid || failure(error, evaluation.fit.error);
}
}

bool CalibrationDocument::readCsv(const QString &path, CalibrationModel *model, QString *error) {
    if (!model) return failure(error, "缺少校准目标");
    QByteArray data; if (!readBounded(path, &data, error)) return false;
    if (data.startsWith("\xef\xbb\xbf")) data.remove(0, 3);
    if (QString::fromUtf8(data).toUtf8() != data) return failure(error, "CSV 必须使用 UTF-8 编码");
    CalibrationModel candidate = *model;
    candidate.observations.clear();
    candidate.name = QFileInfo(path).completeBaseName().left(80);
    int width = 0, lineNumber = 0;
    const QStringList two{"concentration", "response"};
    const QStringList four{"concentration", "response", "internal_concentration", "internal_response"};
    for (const auto &line : QString::fromUtf8(data).split('\n')) {
        ++lineNumber;
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        QStringList columns = trimmed.split(QRegularExpression("[,;\\t]"));
        for (auto &column : columns) column = column.trimmed();
        if (!width) {
            width = columns.size();
            if (width != 2 && width != 4) return failure(error, "校准 CSV 应为两列外标或四列内标数据");
            candidate.standard = width == 4 ? CalibrationModel::Standard::Internal : CalibrationModel::Standard::External;
            QStringList lower; for (const auto &column : columns) lower.append(column.toLower());
            if (lower == two || lower == four || columns == QStringList{"浓度", "响应"}) continue;
        }
        if (columns.size() != width) return failure(error, QString("第 %1 行列数不一致").arg(lineNumber));
        double values[4]{};
        for (int i = 0; i < width; ++i) {
            bool ok = false; values[i] = columns[i].toDouble(&ok);
            if (!ok || !std::isfinite(values[i]) || values[i] < 0) return failure(error, QString("第 %1 行数值无效").arg(lineNumber));
        }
        candidate.observations.push_back({values[0], values[1], values[2], values[3], true});
        if (candidate.observations.size() > 10000) return failure(error, "最多 10000 个校准点");
    }
    if (candidate.observations.size() < 3) return failure(error, "至少需要三个校准点");
    // Import preserves zero/blank values for an explicit exclusion decision in
    // the UI. No usable result exists until evaluate succeeds.
    *model = candidate;
    return true;
}

bool CalibrationDocument::save(const QString &path, const CalibrationModel &model, QString *error) {
    if (!metadataValid(model, error)) return false;
    QJsonArray rows;
    for (const auto &r : model.observations) rows.append(QJsonArray{r.concentration, r.response, r.internalConcentration, r.internalResponse, r.included});
    const QJsonObject payload{{"name", model.name}, {"concentration_unit", model.concentrationUnit}, {"response_unit", model.responseUnit},
        {"internal_standard", model.internalStandard}, {"standard", model.standard == CalibrationModel::Standard::Internal ? "internal" : "external"},
        {"weight", model.weight == QuantitationEngine::Weight::None ? "none" : "inverse_x_squared"}, {"observations", rows},
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"saved_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    // Hash exact stored bytes, not a Qt-version-dependent reserialization.
    const QJsonObject envelope{{"schema", "qitest-calibration-1"}, {"payload_base64", QString::fromLatin1(bytes.toBase64())},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    return writeAtomic(path, QJsonDocument(envelope).toJson(), error);
}

bool CalibrationDocument::load(const QString &path, CalibrationModel *model, QString *error) {
    if (!model) return failure(error, "缺少校准目标");
    QByteArray raw; if (!readBounded(path, &raw, error)) return false;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(raw, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return failure(error, "校准文件不是有效 JSON");
    const auto envelope = document.object();
    const QByteArray encoded = envelope.value("payload_base64").toString().toLatin1();
    const auto bytes = QByteArray::fromBase64(encoded);
    if (envelope.value("schema") != "qitest-calibration-1" || bytes.isEmpty() || bytes.toBase64() != encoded
        || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != envelope.value("sha256").toString())
        return failure(error, "校准文件格式或完整性校验不通过");
    const auto payloadDocument = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !payloadDocument.isObject()) return failure(error, "校准内容无效");
    const auto p = payloadDocument.object();
    CalibrationModel candidate;
    candidate.name = p.value("name").toString(); candidate.concentrationUnit = p.value("concentration_unit").toString();
    candidate.responseUnit = p.value("response_unit").toString(); candidate.internalStandard = p.value("internal_standard").toString();
    const auto type = p.value("standard").toString(), weight = p.value("weight").toString();
    if ((type != "internal" && type != "external") || (weight != "none" && weight != "inverse_x_squared")) return failure(error, "不支持的校准类型或权重");
    candidate.standard = type == "internal" ? CalibrationModel::Standard::Internal : CalibrationModel::Standard::External;
    candidate.weight = weight == "none" ? QuantitationEngine::Weight::None : QuantitationEngine::Weight::InverseXSquared;
    const auto rows = p.value("observations").toArray();
    if (rows.size() > 10000) return failure(error, "校准点过多");
    for (const auto &entry : rows) {
        const auto r = entry.toArray();
        if (r.size() != 5 || !r[4].isBool()) return failure(error, "校准点结构无效");
        for (int i = 0; i < 4; ++i) if (!r[i].isDouble()) return failure(error, "校准数值字段类型无效");
        candidate.observations.push_back({r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble(), r[4].toBool()});
    }
    if (!metadataValid(candidate, error)) return false;
    *model = candidate; // Invalid files never replace the current model.
    return true;
}

bool CalibrationDocument::exportCsv(const QString &path, const CalibrationModel &model, QString *error) {
    if (!metadataValid(model, error)) return false;
    const bool internal = model.standard == CalibrationModel::Standard::Internal;
    QByteArray data = internal ? "concentration,response,internal_concentration,internal_response\n" : "concentration,response\n";
    for (const auto &r : model.observations) {
        if (!r.included) continue; // Explicitly labeled export of included fit points only.
        data += QString("%1,%2").arg(r.concentration,0,'g',17).arg(r.response,0,'g',17).toUtf8();
        if (internal) data += QString(",%1,%2").arg(r.internalConcentration,0,'g',17).arg(r.internalResponse,0,'g',17).toUtf8();
        data += '\n';
    }
    return writeAtomic(path, data, error);
}
}
