#include "storage/IntegrationDocument.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace qitest {
namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool validate(const IntegrationSnapshot &s, QString *error) {
    const int kind = static_cast<int>(s.kind);
    if (kind < 0 || kind > 2 || (s.msLevel != 1 && s.msLevel != 2)
        || !std::isfinite(s.targetMz) || s.targetMz <= 0
        || !std::isfinite(s.toleranceDa) || s.toleranceDa <= 0
        || s.trace.size() > ChromatogramEngine::MaximumScans)
        return fail(error, "积分参数或曲线数量无效");
    const auto checked = ChromatogramEngine::integrate(s.trace, s.fromSeconds, s.toSeconds, s.endpointBaseline);
    if (!checked.valid) return fail(error, checked.error);
    if (!std::isfinite(s.area) || std::abs(s.area-checked.area) > 1e-10 * std::max(1.0,std::abs(checked.area)))
        return fail(error, "保存面积与曲线重算结果不一致");
    return true;
}
}
bool IntegrationDocument::save(const QString &path, const IntegrationSnapshot &s, QString *error) {
    if (!validate(s,error)) return false;
    QJsonArray points;
    for (const auto &p : s.trace) points.append(QJsonArray{p.mz,p.intensity});
    const QJsonObject payload{{"algorithm","trapezoid-endpoint-v1"},
        {"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"kind",static_cast<int>(s.kind)}, {"ms_level",s.msLevel},
        {"target_mz",s.targetMz}, {"tolerance_da",s.toleranceDa},
        {"from_s",s.fromSeconds}, {"to_s",s.toSeconds},
        {"endpoint_baseline",s.endpointBaseline}, {"area",s.area}, {"trace",points}};
    const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QJsonObject envelope{{"schema","qitest-integration-1"},
        {"payload_base64",QString::fromLatin1(bytes.toBase64())},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
    const auto data = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (data.size()>MaximumBytes) return fail(error,"积分记录超过 2 MiB");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())
        return fail(error,file.errorString());
    return true;
}
bool IntegrationDocument::load(const QString &path, IntegrationSnapshot *snapshot, QString *error) {
    if (!snapshot) return fail(error,"缺少积分记录目标");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error,file.errorString());
    const auto data = file.read(MaximumBytes+1);
    if (data.size()>MaximumBytes || file.error()!=QFile::NoError) return fail(error,"文件过大或读取失败");
    QJsonParseError parse;
    const auto envelopeDoc = QJsonDocument::fromJson(data,&parse);
    if (parse.error!=QJsonParseError::NoError || !envelopeDoc.isObject()) return fail(error,"积分文件 JSON 无效");
    const auto envelope=envelopeDoc.object();
    const auto encoded=envelope.value("payload_base64").toString().toLatin1();
    const auto bytes=QByteArray::fromBase64(encoded);
    if (envelope.value("schema")!="qitest-integration-1" || bytes.isEmpty() || bytes.toBase64()!=encoded
        || QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=envelope.value("sha256").toString())
        return fail(error,"积分文件格式或完整性校验不通过");
    const auto payloadDoc=QJsonDocument::fromJson(bytes,&parse);
    if (parse.error!=QJsonParseError::NoError || !payloadDoc.isObject()) return fail(error,"积分内容无效");
    const auto p=payloadDoc.object();
    for (const QString key : {"kind","ms_level","target_mz","tolerance_da","from_s","to_s","area"})
        if (!p.value(key).isDouble()) return fail(error,"积分数值字段缺失或类型不正确");
    if (p.value("algorithm")!="trapezoid-endpoint-v1" || !p.value("endpoint_baseline").isBool()
        || !p.value("trace").isArray()) return fail(error,"积分算法或曲线字段不支持");
    const double kind=p.value("kind").toDouble(), level=p.value("ms_level").toDouble();
    if (kind!=0 && kind!=1 && kind!=2) return fail(error,"曲线类型无效");
    if (level!=1 && level!=2) return fail(error,"扫描级别无效");
    IntegrationSnapshot s;
    s.kind=static_cast<ChromatogramEngine::Kind>(int(kind)); s.msLevel=int(level);
    s.targetMz=p.value("target_mz").toDouble(); s.toleranceDa=p.value("tolerance_da").toDouble();
    s.fromSeconds=p.value("from_s").toDouble(); s.toSeconds=p.value("to_s").toDouble();
    s.endpointBaseline=p.value("endpoint_baseline").toBool(); s.area=p.value("area").toDouble();
    const auto points=p.value("trace").toArray();
    if (points.size()>ChromatogramEngine::MaximumScans) return fail(error,"积分曲线超过 5000 点");
    for (const auto &v : points) {
        const auto pair=v.toArray();
        if (pair.size()!=2 || !pair[0].isDouble() || !pair[1].isDouble()) return fail(error,"曲线点格式错误");
        s.trace.append({pair[0].toDouble(),pair[1].toDouble()});
    }
    if (!validate(s,error)) return false;
    *snapshot=std::move(s);
    return true;
}
}
