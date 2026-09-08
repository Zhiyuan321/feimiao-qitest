#pragma once

#include <QSqlDatabase>
#include <QVector>
#include <QString>

namespace qitest {

struct LibrarySourceMetadata {
    QString sourceName;
    QString version;
    QString sourceUrl;
    QString sha256;
    QString ionization;
    QString licenseStatus;
    QString spectrumMode = "EI";
    QString instrumentFamily = "unspecified";
    QString validationStatus = "reference-only";
    bool matchEligible = false;
};

struct LibraryImportResult {
    int compounds = 0;
    int spectra = 0;
    int peaks = 0;
};

struct LibraryCompound {
    QString entryId;
    QString name;
    QString formula;
    QString cas;
    double nominalMass = 0.0;
    int peakCount = 0;
    QString source;
    QString ionization;
};

class SpectralLibraryRepository final {
public:
    explicit SpectralLibraryRepository(QString databasePath);
    ~SpectralLibraryRepository();

    bool open(QString *error = nullptr);
    bool openReadOnly(QString *error = nullptr);
    LibraryImportResult importJcamp(
        const QString &jcampPath,
        const LibrarySourceMetadata &metadata,
        QString *error = nullptr);
    int spectrumCount(const QString &sourceName = {}) const;
    QString sourceSummary() const;
    QVector<LibraryCompound> searchCompounds(const QString &query, int limit = 100) const;
    QString databasePath() const { return databasePath_; }

private:
    bool initializeSchema(QString *error);
    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace qitest
