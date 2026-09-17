#include "library/LibraryFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace qitest {
namespace {
bool fail(QString *error,const QString &message) {if(error)*error=message;return false;}
bool libPath(const QString &path) {return QFileInfo(path).suffix().compare("lib",Qt::CaseInsensitive)==0;}
QString normalized(const QString &path) {
    const QFileInfo info(path);return info.exists()?info.canonicalFilePath():info.absoluteFilePath();
}
bool samePath(const QString &a,const QString &b) {
#ifdef Q_OS_WIN
    return a.compare(b,Qt::CaseInsensitive)==0;
#else
    return a==b;
#endif
}
bool save(const QString &path,const QByteArray &bytes,QString *error) {
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        return fail(error,"无法保存："+file.errorString());
    if(error)error->clear();return true;
}
}
QStringList LibraryFile::keys() {
    return {"name","cas","parent_ion","qualitify_ion","quantify_ion","sample_category","internal_flag","son_area","msms_son_area"};
}
QStringList LibraryFile::labels() {
    return {"名称","CAS","母离子","定性离子","定量离子","样品类别","是否为内标","一级阈值","二级阈值"};
}
QString LibraryFile::text(const QJsonObject &entry,const QString &key) {
    const auto v=entry.value(key);
    if(v.isDouble())return QString::number(v.toDouble(),'g',17);
    if(v.isBool())return v.toBool()?"是":"否";
    return v.toString();
}
bool LibraryFile::validateEntry(const QJsonObject &entry,QString *error) {
    if(text(entry,"name").trimmed().isEmpty())return fail(error,"请填写名称");
    for(const auto &key:keys()) {
        const auto v=entry.value(key);
        if(!v.isUndefined() && !v.isNull() && !v.isString() && !v.isDouble() && !(key=="internal_flag" && v.isBool()))
            return fail(error,"字段格式不正确："+key);
        if(text(entry,key).size()>4096)return fail(error,"字段内容过长："+key);
    }
    for(const auto &key:QStringList{"parent_ion","qualitify_ion","quantify_ion","son_area","msms_son_area"}) {
        const auto value=text(entry,key).trimmed();if(value.isEmpty())continue;
        // Legacy libraries store threshold lists too; preserve their order and zeros.
        const bool list=(key!="parent_ion");
        const auto parts=list?QString(value).replace(QChar(0xff0c),',').split(','):QStringList{value};
        for(const auto &part:parts) {
            bool ok=false;const double number=part.trimmed().toDouble(&ok);
            if(!ok || !std::isfinite(number) || number<0)
                return fail(error,labels().at(keys().indexOf(key))+"须为非负数"+
                    (list?QString("，多个数值用逗号分隔且不能留空"):QString("，只能填写一个数值")));
        }
    }
    if(error)error->clear();return true;
}
QByteArray LibraryFile::digest(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>MaximumBytes)return {};
    return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
}
bool LibraryFile::read(const QString &path,QJsonArray *entries,QString *error,QByteArray *digestOut) {
    if(!libPath(path))return fail(error,"请选择后缀为 .lib 的谱库文件");
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(error,"无法读取谱库："+file.errorString());
    if(file.size()>MaximumBytes)return fail(error,"谱库文件超过16 MiB");
    const auto bytes=file.read(MaximumBytes+1);
    if(file.error()!=QFile::NoError || bytes.size()>MaximumBytes)return fail(error,"谱库读取失败或文件过大");
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isArray())return fail(error,"无法读取此 .lib：需要旧软件的 JSON 数组格式");
    const auto rows=doc.array();if(rows.size()>MaximumEntries)return fail(error,"谱库物质超过10000条");
    for(int i=0;i<rows.size();++i) {
        QString detail;
        if(!rows[i].isObject() || !validateEntry(rows[i].toObject(),&detail))
            return fail(error,QString("第%1条物质无效：%2").arg(i+1).arg(detail));
    }
    *entries=rows;if(digestOut)*digestOut=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    if(error)error->clear();return true;
}
bool LibraryFile::write(const QString &path,const QJsonArray &entries,QString *error) {
    if(!libPath(path))return fail(error,"文件名必须以 .lib 结尾");
    if(entries.size()>MaximumEntries)return fail(error,"谱库物质超过10000条");
    for(const auto &v:entries)if(!v.isObject() || !validateEntry(v.toObject(),error))return false;
    const auto bytes=QJsonDocument(entries).toJson(QJsonDocument::Indented);
    if(bytes.size()>MaximumBytes)return fail(error,"谱库文件超过16 MiB");
    return save(path,bytes,error);
}
bool LibraryFileCatalog::open(QString *error) {
    paths_.clear();if(!QFileInfo::exists(path_))return true;
    QFile f(path_);if(!f.open(QIODevice::ReadOnly) || f.size()>LibraryFile::MaximumBytes)return fail(error,"无法读取谱库列表");
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isArray())return fail(error,"谱库列表损坏，未覆盖原文件");
    for(const auto &v:doc.array()) {if(!v.isString())return fail(error,"谱库列表格式不正确");paths_.append(v.toString());}
    return true;
}
bool LibraryFileCatalog::persist(const QStringList &paths,QString *error) {
    if(!QDir().mkpath(QFileInfo(path_).absolutePath()))return fail(error,"无法创建谱库列表目录");
    QJsonArray array;for(const auto &p:paths)array.append(p);
    if(!save(path_,QJsonDocument(array).toJson(),error))return false;
    paths_=paths;return true;
}
bool LibraryFileCatalog::add(const QString &path,QString *error) {
    QJsonArray rows;if(!LibraryFile::read(path,&rows,error))return false;
    const auto p=normalized(path);for(const auto &known:paths_)if(samePath(known,p))return true;
    auto next=paths_;next.append(p);return persist(next,error);
}
bool LibraryFileCatalog::remove(const QString &path,QString *error) {
    auto next=paths_;next.removeAll(path);return persist(next,error);
}
}
