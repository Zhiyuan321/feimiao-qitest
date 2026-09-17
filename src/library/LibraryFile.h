#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace qitest {
// Legacy .lib is a JSON array. Keep whole objects so unknown/calibration fields survive edits.
class LibraryFile final {
public:
    static constexpr int MaximumEntries=10000;
    static constexpr int MaximumBytes=16*1024*1024;
    static QStringList keys();
    static QStringList labels();
    static QString text(const QJsonObject &entry,const QString &key);
    static bool validateEntry(const QJsonObject &entry,QString *error);
    static bool read(const QString &path,QJsonArray *entries,QString *error,QByteArray *digest=nullptr);
    static bool write(const QString &path,const QJsonArray &entries,QString *error);
    static QByteArray digest(const QString &path);
};
class LibraryFileCatalog final {
public:
    explicit LibraryFileCatalog(QString path):path_(std::move(path)){}
    bool open(QString *error);
    QStringList paths() const {return paths_;}
    bool add(const QString &path,QString *error);
    bool remove(const QString &path,QString *error);
private:
    bool persist(const QStringList &paths,QString *error);
    QString path_;
    QStringList paths_;
};
}
