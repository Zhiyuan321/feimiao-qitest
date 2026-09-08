#pragma once
#include "domain/Models.h"
#include <QSqlDatabase>

namespace qitest {
struct UserStandard {
    QString id;
    int revision = 0;
    QString name, formula, cas, category, ionization, provenance;
    double precursorMz = 0, qualifierMz = 0, quantifierMz = 0;
    QVector<double> additionalQualifierMzs;
    bool internalStandard = false;
    QVector<SpectrumPoint> peaks;
};

// Deliberately separate from the bundled read-only library. Data only, never
// executable formulas or model weights. All standards need method validation.
class UserStandardRepository final {
public:
    static constexpr int MaximumPeaks = 10000;
    static constexpr int MaximumEntries = 10000;
    static constexpr qint64 MaximumFileBytes = 1024 * 1024;
    explicit UserStandardRepository(QString path);
    ~UserStandardRepository();
    bool open(QString *error);
    bool save(UserStandard *standard, QString *error);
    bool load(const QString &id, UserStandard *standard, QString *error) const;
    QVector<UserStandard> search(const QString &text, int offset, QString *error) const;
    int count(QString *error) const;
    QStringList categories(QString *error) const;
    bool addCategory(const QString &name, QString *error);
    bool removeCategory(const QString &name, QString *error);
    bool archive(const QString &id, int revision, QString *error);
    static bool validate(const UserStandard &standard, QString *error);
    static bool readFile(const QString &path, UserStandard *standard, QString *error);
    static bool writeFile(const QString &path, const UserStandard &standard, QString *error);
    static bool parsePeaks(const QString &csv, QVector<SpectrumPoint> *peaks, QString *error);
private:
    QString path_, connection_;
    QSqlDatabase database_;
};
}
