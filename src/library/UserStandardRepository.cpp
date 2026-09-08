#include "library/UserStandardRepository.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <cmath>

namespace qitest {
namespace {
constexpr int ApplicationId = 0x51495354;
bool fail(QString *error, const QString &message) { if(error) *error=message; return false; }
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(); }
QByteArray encode(const UserStandard &s) {
    QJsonArray peaks;
    for(const auto &p:s.peaks) peaks.append(QJsonArray{p.mz,p.intensity});
    QJsonObject payload{{"name",s.name},{"formula",s.formula},{"cas",s.cas},
        {"category",s.category},{"ionization",s.ionization},{"provenance",s.provenance},
        {"precursor_mz",s.precursorMz},{"qualifier_mz",s.qualifierMz},{"quantifier_mz",s.quantifierMz},
        {"internal_standard",s.internalStandard},{"peaks",peaks}};
    if (!s.additionalQualifierMzs.isEmpty()) {
        QJsonArray ions; for (double mz : s.additionalQualifierMzs) ions.append(mz);
        payload.insert("additional_qualifier_mzs", ions);
    }
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}
bool decode(const QByteArray &bytes, UserStandard *target, QString *error) {
    QJsonParseError parse;
    const auto doc=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isObject()) return fail(error,"标准内容 JSON 无效");
    const auto p=doc.object();
    for(const QString key:{"name","formula","cas","category","ionization","provenance"})
        if(!p.value(key).isString()) return fail(error,"标准文字字段缺失或类型错误");
    for(const QString key:{"precursor_mz","qualifier_mz","quantifier_mz"})
        if(!p.value(key).isDouble()) return fail(error,"离子字段缺失或类型错误");
    if(!p.value("internal_standard").isBool() || !p.value("peaks").isArray()) return fail(error,"标准标记或谱点格式错误");
    UserStandard s;
    s.name=p.value("name").toString(); s.formula=p.value("formula").toString(); s.cas=p.value("cas").toString();
    s.category=p.value("category").toString(); s.ionization=p.value("ionization").toString(); s.provenance=p.value("provenance").toString();
    s.precursorMz=p.value("precursor_mz").toDouble(); s.qualifierMz=p.value("qualifier_mz").toDouble(); s.quantifierMz=p.value("quantifier_mz").toDouble();
    s.internalStandard=p.value("internal_standard").toBool();
    if (p.contains("additional_qualifier_mzs")) {
        if (!p.value("additional_qualifier_mzs").isArray()) return fail(error,"多定性离子格式错误");
        for (const auto &ion : p.value("additional_qualifier_mzs").toArray()) {
            if (!ion.isDouble()) return fail(error,"定性离子必须为数值");
            s.additionalQualifierMzs.append(ion.toDouble());
        }
    }
    const auto points=p.value("peaks").toArray();
    if(points.size()>UserStandardRepository::MaximumPeaks) return fail(error,"每个标准最多 10000 个谱点");
    for(const auto &v:points) {
        const auto pair=v.toArray();
        if(pair.size()!=2 || !pair[0].isDouble() || !pair[1].isDouble()) return fail(error,"谱点必须是 m/z、强度两个数值");
        s.peaks.append({pair[0].toDouble(),pair[1].toDouble()});
    }
    if(!UserStandardRepository::validate(s,error)) return false;
    *target=std::move(s); return true;
}
}
UserStandardRepository::UserStandardRepository(QString path)
    : path_(std::move(path)), connection_("user-standards-"+QUuid::createUuid().toString()) {}
UserStandardRepository::~UserStandardRepository() {
    database_.close(); database_={}; QSqlDatabase::removeDatabase(connection_);
}
bool UserStandardRepository::open(QString *error) {
    if(database_.isOpen()) return true;
    if(!QDir().mkpath(QFileInfo(path_).absolutePath())) return fail(error,"无法创建用户标准目录");
    database_=QSqlDatabase::addDatabase("QSQLITE",connection_);
    database_.setDatabaseName(path_); database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=1500");
    if(!database_.open()) return fail(error,database_.lastError().text());
    QSqlQuery q(database_);
    if(!q.exec("PRAGMA application_id") || !q.next()) { const auto message=q.lastError().text(); database_.close(); return fail(error,message); }
    const int appId=q.value(0).toInt(); q.finish();
    if(!q.exec("SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'") || !q.next()) {
        const auto message=q.lastError().text(); database_.close(); return fail(error,message);
    }
    const int tables=q.value(0).toInt(); q.finish();
    if(appId!=ApplicationId && (appId!=0 || tables!=0)) {
        database_.close(); return fail(error,"拒绝写入非用户标准数据库，包括公共谱库");
    }
    const QStringList commands{
        "PRAGMA application_id="+QString::number(ApplicationId),
        "PRAGMA journal_mode=WAL", "PRAGMA synchronous=FULL", "PRAGMA cache_size=-4096", "PRAGMA journal_size_limit=16777216",
        "CREATE TABLE IF NOT EXISTS standards(id TEXT PRIMARY KEY,revision INTEGER NOT NULL,name TEXT NOT NULL,cas TEXT NOT NULL,payload BLOB NOT NULL,hash TEXT UNIQUE NOT NULL)",
        "CREATE TABLE IF NOT EXISTS revisions(id TEXT NOT NULL,revision INTEGER NOT NULL,payload BLOB NOT NULL,hash TEXT NOT NULL,created_utc TEXT NOT NULL,PRIMARY KEY(id,revision))",
        "CREATE TABLE IF NOT EXISTS categories(name TEXT PRIMARY KEY)",
        "CREATE TABLE IF NOT EXISTS archived_standards(id TEXT PRIMARY KEY,revision INTEGER NOT NULL,created_utc TEXT NOT NULL)",
        "CREATE INDEX IF NOT EXISTS standards_name ON standards(name)", "CREATE INDEX IF NOT EXISTS standards_cas ON standards(cas)"};
    for(const auto &sql:commands) if(!q.exec(sql)) { const auto message=q.lastError().text(); database_.close(); return fail(error,message); }
    return true;
}
bool UserStandardRepository::validate(const UserStandard &s, QString *error) {
    for(const auto &text:{s.name,s.formula,s.cas,s.category,s.ionization})
        if(text.size()>120 || text.contains(QRegularExpression("[\\x00-\\x1f]"))) return fail(error,"名称等字段最多 120 字，不含控制字符");
    if(s.name.trimmed().isEmpty() || s.ionization.trimmed().isEmpty() || s.provenance.trimmed().isEmpty())
        return fail(error,"请填写名称、离子化方式和来源说明");
    if(s.provenance.size()>2048 || s.provenance.contains(QChar(0))) return fail(error,"来源说明过长或含无效字符");
    for(double mz:{s.precursorMz,s.qualifierMz,s.quantifierMz})
        if(!std::isfinite(mz) || mz<0 || mz>1e6) return fail(error,"离子 m/z 应为 0–1000000；0 表示未指定");
    if (s.additionalQualifierMzs.size() > 31) return fail(error,"定性离子最多 32 个");
    QVector<double> seen{s.qualifierMz};
    for (double mz : s.additionalQualifierMzs) {
        if (!std::isfinite(mz) || mz <= 0 || mz > 1e6 || seen.contains(mz))
            return fail(error,"附加定性离子必须为有限正值且不重复");
        seen.append(mz);
    }
    if(s.peaks.isEmpty() || s.peaks.size()>MaximumPeaks) return fail(error,"标准应有 1–10000 个谱点");
    double last=0; bool signal=false;
    for(const auto &p:s.peaks) {
        if(!std::isfinite(p.mz) || !std::isfinite(p.intensity) || p.mz<=last || p.mz>1e6 || p.intensity<0)
            return fail(error,"谱点 m/z 必须正值递增且不重复，强度须为有限非负数");
        last=p.mz; signal=signal || p.intensity>0;
    }
    return signal || fail(error,"标准不能全部为零强度");
}
bool UserStandardRepository::parsePeaks(const QString &csv, QVector<SpectrumPoint> *peaks, QString *error) {
    if(!peaks || csv.size()>MaximumFileBytes) return fail(error,"谱点文本过大或目标无效");
    QVector<SpectrumPoint> parsed;
    for(const auto &line:csv.split('\n')) {
        const auto text=line.trimmed();
        if(text.isEmpty()) continue;
        if(parsed.isEmpty() && text=="mz,intensity") continue;
        const auto fields=text.split(',');
        if(fields.size()!=2) return fail(error,"谱点每行只能是 mz,intensity 两列");
        bool a=false,b=false; const double mz=fields[0].trimmed().toDouble(&a), intensity=fields[1].trimmed().toDouble(&b);
        if(!a || !b) return fail(error,"谱点中有非数值内容");
        parsed.append({mz,intensity});
        if(parsed.size()>MaximumPeaks) return fail(error,"最多 10000 个谱点");
    }
    UserStandard check; check.name="validation"; check.ionization="unspecified"; check.provenance="validation"; check.peaks=parsed;
    if(!validate(check,error)) return false;
    *peaks=std::move(parsed); return true;
}
bool UserStandardRepository::save(UserStandard *s, QString *error) {
    if(!s || !database_.isOpen()) return fail(error,"用户标准数据库不可用");
    if(!validate(*s,error)) return false;
    const auto bytes=encode(*s); const auto hash=QString::fromLatin1(digest(bytes));
    if(bytes.size()>MaximumFileBytes) return fail(error,"标准过大");
    if(!database_.transaction()) return fail(error,database_.lastError().text());
    const auto rollback=[&](const QString &message) { database_.rollback(); return fail(error,message); };
    QSqlQuery q(database_);
    QString id=s->id; int revision=s->revision;
    if(id.isEmpty()) {
        q.prepare("SELECT id,revision FROM standards WHERE hash=? AND id NOT IN (SELECT id FROM archived_standards)"); q.addBindValue(hash);
        if(!q.exec()) return rollback(q.lastError().text());
        if(q.next()) {
            const auto existing=q.value(0).toString(); const int rev=q.value(1).toInt(); q.finish();
            database_.rollback(); s->id=existing; s->revision=rev; return true;
        }
        q.finish();
        if(!q.exec("SELECT count(*) FROM standards") || !q.next()) return rollback("无法查询标准数量，未保存");
        const int total=q.value(0).toInt();q.finish();
        if(total>=MaximumEntries) return rollback("用户标准最多 10000 条，请先备份并规划资料库");
        id=QUuid::createUuid().toString(QUuid::WithoutBraces); revision=0;
    } else {
        q.prepare("SELECT revision,hash FROM standards WHERE id=? AND id NOT IN (SELECT id FROM archived_standards)"); q.addBindValue(id);
        if(!q.exec()) return rollback(q.lastError().text());
        if(!q.next() || q.value(0).toInt()!=revision) return rollback("标准已被更新，请重新打开，未覆盖新版本");
        if(q.value(1).toString()==hash) { q.finish(); database_.rollback(); return true; }
        q.finish();
    }
    if(revision==0) q.prepare("INSERT INTO standards(id,revision,name,cas,payload,hash) VALUES(?,?,?,?,?,?)");
    else q.prepare("UPDATE standards SET id=?,revision=?,name=?,cas=?,payload=?,hash=? WHERE id=? AND revision=?");
    q.addBindValue(id); q.addBindValue(revision+1); q.addBindValue(s->name); q.addBindValue(s->cas.isNull()?QString(""):s->cas); q.addBindValue(bytes); q.addBindValue(hash);
    if(revision) { q.addBindValue(id); q.addBindValue(revision); }
    if(!q.exec() || q.numRowsAffected()!=1) return rollback("保存失败或内容重复："+q.lastError().text());
    q.prepare("INSERT INTO revisions(id,revision,payload,hash,created_utc) VALUES(?,?,?,?,?)");
    q.addBindValue(id); q.addBindValue(revision+1); q.addBindValue(bytes); q.addBindValue(hash); q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if(!q.exec()) return rollback(q.lastError().text());
    if (!s->category.isEmpty()) {
        q.prepare("INSERT OR IGNORE INTO categories(name) VALUES(?)"); q.addBindValue(s->category);
        if (!q.exec()) return rollback(q.lastError().text());
    }
    if(!database_.commit()) return rollback(database_.lastError().text());
    s->id=id; s->revision=revision+1; return true;
}
bool UserStandardRepository::load(const QString &id, UserStandard *s, QString *error) const {
    if(!s || !database_.isOpen()) return fail(error,"用户标准数据库不可用");
    QSqlQuery q(database_); q.prepare("SELECT revision,payload,hash FROM standards WHERE id=?"); q.addBindValue(id);
    if(!q.exec() || !q.next()) return fail(error,"未找到标准："+q.lastError().text());
    const auto bytes=q.value(1).toByteArray(); UserStandard candidate;
    if(bytes.size()>MaximumFileBytes || digest(bytes)!=q.value(2).toByteArray()) return fail(error,"标准完整性检查失败");
    if(!decode(bytes,&candidate,error)) return false;
    candidate.id=id; candidate.revision=q.value(0).toInt(); *s=std::move(candidate); return true;
}
QVector<UserStandard> UserStandardRepository::search(const QString &text, int offset, QString *error) const {
    QVector<UserStandard> result;
    if(!database_.isOpen()) { fail(error,"用户标准数据库不可用"); return result; }
    QSqlQuery q(database_);
    q.prepare("SELECT id,revision,name,cas FROM standards WHERE id NOT IN (SELECT id FROM archived_standards) AND (instr(lower(name),lower(?))>0 OR instr(cas,?)>0) ORDER BY name,id LIMIT 50 OFFSET ?");
    q.addBindValue(text.left(120)); q.addBindValue(text.left(120)); q.addBindValue(qMax(0,offset));
    if(!q.exec()) { fail(error,q.lastError().text()); return result; }
    while(q.next()) { UserStandard s; s.id=q.value(0).toString(); s.revision=q.value(1).toInt(); s.name=q.value(2).toString(); s.cas=q.value(3).toString(); result.append(s); }
    return result;
}
int UserStandardRepository::count(QString *error) const {
    QSqlQuery q(database_);
    if(!database_.isOpen() || !q.exec("SELECT count(*) FROM standards WHERE id NOT IN (SELECT id FROM archived_standards)") || !q.next()) { fail(error,q.lastError().text()); return -1; }
    return q.value(0).toInt();
}
bool UserStandardRepository::writeFile(const QString &path, const UserStandard &s, QString *error) {
    if(!validate(s,error)) return false;
    const auto bytes=encode(s);
    const auto output=QJsonDocument(QJsonObject{{"schema",s.additionalQualifierMzs.isEmpty() ? "qitest-user-standard-1" : "qitest-user-standard-2"},
        {"payload_base64",QString::fromLatin1(bytes.toBase64())},{"sha256",QString::fromLatin1(digest(bytes))}}).toJson();
    if(output.size()>MaximumFileBytes) return fail(error,"标准文件超过 1 MiB");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(output)!=output.size() || !file.commit()) return fail(error,file.errorString());
    return true;
}
bool UserStandardRepository::readFile(const QString &path, UserStandard *s, QString *error) {
    if(!s) return fail(error,"缺少标准目标");
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) return fail(error,file.errorString());
    const auto bytes=file.read(MaximumFileBytes+1);
    if(bytes.size()>MaximumFileBytes || file.error()!=QFile::NoError) return fail(error,"文件过大或读取失败");
    const auto envelope=QJsonDocument::fromJson(bytes).object();
    const auto encoded=envelope.value("payload_base64").toString().toLatin1(); const auto raw=QByteArray::fromBase64(encoded);
    if((envelope.value("schema")!="qitest-user-standard-1" && envelope.value("schema")!="qitest-user-standard-2") || raw.isEmpty() || raw.toBase64()!=encoded || digest(raw)!=envelope.value("sha256").toString().toLatin1())
        return fail(error,"标准文件格式或完整性校验失败");
    return decode(raw,s,error);
}
QStringList UserStandardRepository::categories(QString *error) const {
    QStringList result; QSqlQuery q(database_);
    if (!database_.isOpen() || !q.exec("SELECT name FROM categories ORDER BY name")) {
        fail(error,q.lastError().text()); return result;
    }
    while(q.next()) result.append(q.value(0).toString());
    // Include categories from pre-category-table archives without rewriting payloads.
    if (!q.exec("SELECT payload FROM standards")) { fail(error,q.lastError().text()); return result; }
    while(q.next()) {
        const auto name=QJsonDocument::fromJson(q.value(0).toByteArray()).object().value("category").toString();
        if (!name.isEmpty() && !result.contains(name)) result.append(name);
    }
    result.sort(); return result;
}
bool UserStandardRepository::addCategory(const QString &raw, QString *error) {
    const auto name=raw.trimmed();
    if(name.isEmpty() || name.size()>120 || name.contains(QRegularExpression("[\\x00-\\x1f]"))) return fail(error,"类别名称应为 1–120 字，不含控制字符");
    QSqlQuery q(database_); q.prepare("INSERT INTO categories(name) VALUES(?)"); q.addBindValue(name);
    return q.exec() || fail(error,"类别已存在或保存失败："+q.lastError().text());
}
bool UserStandardRepository::removeCategory(const QString &name, QString *error) {
    QSqlQuery q(database_);
    if (!q.exec("SELECT payload FROM standards")) return fail(error,q.lastError().text());
    while(q.next()) if(QJsonDocument::fromJson(q.value(0).toByteArray()).object().value("category").toString()==name)
        return fail(error,"该类别仍被标准或归档引用，不能删除；请先编辑标准类别");
    q.finish(); q.prepare("DELETE FROM categories WHERE name=?"); q.addBindValue(name);
    return (q.exec() && q.numRowsAffected()==1) || fail(error,"未找到类别或删除失败");
}
bool UserStandardRepository::archive(const QString &id, int revision, QString *error) {
    QSqlQuery q(database_);
    q.prepare("INSERT INTO archived_standards(id,revision,created_utc) SELECT id,revision,? FROM standards WHERE id=? AND revision=?");
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)); q.addBindValue(id); q.addBindValue(revision);
    return (q.exec() && q.numRowsAffected()==1) || fail(error,"归档失败：标准已变更或已归档，请刷新");
}
}
