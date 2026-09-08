#include "library/SpectralLibraryRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace qitest {
namespace {

struct JcampEntry {
    QString entryId;
    QString name;
    QString formula;
    QString cas;
    double nominalMass = 0.0;
    QVector<QPair<double, double>> peaks;
};

QString valueAfterEquals(const QString &line) {
    const auto index = line.indexOf('=');
    return index < 0 ? QString{} : line.mid(index + 1).trimmed();
}

bool exec(QSqlQuery &query, const QString &sql, QString *error) {
    if (query.exec(sql)) return true;
    if (error) *error = query.lastError().text();
    return false;
}

bool ensureColumn(QSqlDatabase &database, const QString &table, const QString &column,
                  const QString &declaration, QString *error) {
    QSqlQuery inspect(database);
    if (!inspect.exec("PRAGMA table_info(" + table + ")")) {
        if (error) *error = inspect.lastError().text();
        return false;
    }
    while (inspect.next())
        if (inspect.value(1).toString() == column) return true;
    QSqlQuery alter(database);
    return exec(alter, "ALTER TABLE " + table + " ADD COLUMN " + column + " " + declaration, error);
}

} // namespace

SpectralLibraryRepository::SpectralLibraryRepository(QString databasePath)
    : databasePath_(std::move(databasePath)),
      connectionName_("qitest-library-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

SpectralLibraryRepository::~SpectralLibraryRepository() {
    if (database_.isValid()) database_.close();
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

bool SpectralLibraryRepository::open(QString *error) {
    const QFileInfo info(databasePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = "cannot create database directory";
        return false;
    }
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        if (error) *error = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    query.exec("PRAGMA foreign_keys=ON");
    query.exec("PRAGMA journal_mode=WAL");
    return initializeSchema(error);
}

bool SpectralLibraryRepository::openReadOnly(QString *error) {
    if (!QFileInfo::exists(databasePath_)) {
        if (error) *error = "database does not exist";
        return false;
    }
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setConnectOptions("QSQLITE_OPEN_READONLY");
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        if (error) *error = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    if (!query.exec("SELECT 1 FROM sources LIMIT 1")) {
        if (error) *error = query.lastError().text();
        database_.close();
        return false;
    }
    return true;
}

bool SpectralLibraryRepository::initializeSchema(QString *error) {
    QSqlQuery query(database_);
    const QStringList statements{
        "CREATE TABLE IF NOT EXISTS sources ("
        "id INTEGER PRIMARY KEY, name TEXT NOT NULL, version TEXT NOT NULL, source_url TEXT NOT NULL, "
        "sha256 TEXT NOT NULL, ionization TEXT NOT NULL, license_status TEXT NOT NULL, imported_at TEXT NOT NULL, "
        "spectrum_mode TEXT NOT NULL DEFAULT 'EI', instrument_family TEXT NOT NULL DEFAULT 'unspecified', "
        "validation_status TEXT NOT NULL DEFAULT 'reference-only', match_eligible INTEGER NOT NULL DEFAULT 0, "
        "UNIQUE(name, version))",
        "CREATE TABLE IF NOT EXISTS compounds ("
        "id INTEGER PRIMARY KEY, source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE, "
        "entry_id TEXT NOT NULL, name TEXT NOT NULL, formula TEXT, cas TEXT, nominal_mass REAL, "
        "UNIQUE(source_id, entry_id))",
        "CREATE TABLE IF NOT EXISTS spectra ("
        "id INTEGER PRIMARY KEY, compound_id INTEGER NOT NULL REFERENCES compounds(id) ON DELETE CASCADE, "
        "ionization TEXT NOT NULL, spectrum_type TEXT NOT NULL, peak_count INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS peaks ("
        "spectrum_id INTEGER NOT NULL REFERENCES spectra(id) ON DELETE CASCADE, mz REAL NOT NULL, "
        "intensity REAL NOT NULL, PRIMARY KEY(spectrum_id, mz))",
        "CREATE INDEX IF NOT EXISTS idx_compounds_name ON compounds(name)",
        "CREATE INDEX IF NOT EXISTS idx_compounds_cas ON compounds(cas)",
        "CREATE INDEX IF NOT EXISTS idx_peaks_mz ON peaks(mz)"
    };
    for (const auto &statement : statements) if (!exec(query, statement, error)) return false;
    if (!ensureColumn(database_, "sources", "spectrum_mode", "TEXT NOT NULL DEFAULT 'EI'", error)
        || !ensureColumn(database_, "sources", "instrument_family", "TEXT NOT NULL DEFAULT 'unspecified'", error)
        || !ensureColumn(database_, "sources", "validation_status", "TEXT NOT NULL DEFAULT 'reference-only'", error)
        || !ensureColumn(database_, "sources", "match_eligible", "INTEGER NOT NULL DEFAULT 0", error))
        return false;
    return true;
}

LibraryImportResult SpectralLibraryRepository::importJcamp(
    const QString &jcampPath,
    const LibrarySourceMetadata &metadata,
    QString *error) {
    LibraryImportResult result;
    QFile file(jcampPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return result;
    }
    if (!database_.transaction()) {
        if (error) *error = database_.lastError().text();
        return result;
    }

    QSqlQuery source(database_);
    source.prepare("INSERT INTO sources(name,version,source_url,sha256,ionization,license_status,imported_at,"
                   "spectrum_mode,instrument_family,validation_status,match_eligible) "
                   "VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(name,version) DO UPDATE SET source_url=excluded.source_url, "
                   "sha256=excluded.sha256, ionization=excluded.ionization, license_status=excluded.license_status, "
                   "imported_at=excluded.imported_at,spectrum_mode=excluded.spectrum_mode,"
                   "instrument_family=excluded.instrument_family,validation_status=excluded.validation_status,"
                   "match_eligible=excluded.match_eligible");
    source.addBindValue(metadata.sourceName);
    source.addBindValue(metadata.version);
    source.addBindValue(metadata.sourceUrl);
    source.addBindValue(metadata.sha256);
    source.addBindValue(metadata.ionization);
    source.addBindValue(metadata.licenseStatus);
    source.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    source.addBindValue(metadata.spectrumMode);
    source.addBindValue(metadata.instrumentFamily);
    source.addBindValue(metadata.validationStatus);
    source.addBindValue(metadata.matchEligible ? 1 : 0);
    if (!source.exec()) { if (error) *error = source.lastError().text(); database_.rollback(); return {}; }

    QSqlQuery sourceIdQuery(database_);
    sourceIdQuery.prepare("SELECT id FROM sources WHERE name=? AND version=?");
    sourceIdQuery.addBindValue(metadata.sourceName);
    sourceIdQuery.addBindValue(metadata.version);
    if (!sourceIdQuery.exec() || !sourceIdQuery.next()) {
        if (error) *error = sourceIdQuery.lastError().text(); database_.rollback(); return {};
    }
    const qlonglong sourceId = sourceIdQuery.value(0).toLongLong();
    QSqlQuery cleanup(database_);
    cleanup.prepare("DELETE FROM compounds WHERE source_id=?");
    cleanup.addBindValue(sourceId);
    if (!cleanup.exec()) { if (error) *error = cleanup.lastError().text(); database_.rollback(); return {}; }

    QSqlQuery compound(database_);
    compound.prepare("INSERT INTO compounds(source_id,entry_id,name,formula,cas,nominal_mass) VALUES(?,?,?,?,?,?)");
    QSqlQuery spectrum(database_);
    spectrum.prepare("INSERT INTO spectra(compound_id,ionization,spectrum_type,peak_count) VALUES(?,?,?,?)");
    QSqlQuery peak(database_);
    peak.prepare("INSERT INTO peaks(spectrum_id,mz,intensity) VALUES(?,?,?)");

    JcampEntry current;
    bool inPeaks = false;
    auto flush = [&]() -> bool {
        if (current.entryId.isEmpty()) return true;
        compound.bindValue(0, sourceId);
        compound.bindValue(1, current.entryId);
        compound.bindValue(2, current.name.isEmpty() ? current.entryId : current.name);
        compound.bindValue(3, current.formula);
        compound.bindValue(4, current.cas);
        compound.bindValue(5, current.nominalMass);
        if (!compound.exec()) { if (error) *error = compound.lastError().text(); return false; }
        const qlonglong compoundId = compound.lastInsertId().toLongLong();
        spectrum.bindValue(0, compoundId);
        spectrum.bindValue(1, metadata.ionization);
        spectrum.bindValue(2, "EI mass spectrum");
        spectrum.bindValue(3, current.peaks.size());
        if (!spectrum.exec()) { if (error) *error = spectrum.lastError().text(); return false; }
        const qlonglong spectrumId = spectrum.lastInsertId().toLongLong();
        for (const auto &[mz, intensity] : current.peaks) {
            peak.bindValue(0, spectrumId);
            peak.bindValue(1, mz);
            peak.bindValue(2, intensity);
            if (!peak.exec()) { if (error) *error = peak.lastError().text(); return false; }
            ++result.peaks;
        }
        ++result.compounds;
        ++result.spectra;
        current = {};
        return true;
    };

    while (!file.atEnd()) {
        const QString line = QString::fromLatin1(file.readLine()).trimmed();
        if (line.startsWith("##TITLE=")) {
            if (!flush()) { database_.rollback(); return {}; }
            current.entryId = valueAfterEquals(line);
            inPeaks = false;
        } else if (line.startsWith("##CAS NAME=")) current.name = valueAfterEquals(line);
        else if (line.startsWith("##MOLFORM=")) current.formula = valueAfterEquals(line);
        else if (line.startsWith("##CAS REGISTRY NO=")) current.cas = valueAfterEquals(line);
        else if (line.startsWith("##MW=")) current.nominalMass = valueAfterEquals(line).toDouble();
        else if (line.startsWith("##XYDATA=")) inPeaks = true;
        else if (line.startsWith("##END=")) { inPeaks = false; if (!flush()) { database_.rollback(); return {}; } }
        else if (inPeaks && !line.isEmpty() && !line.startsWith("##")) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() >= 2) current.peaks.push_back({parts[0].toDouble(), parts[1].toDouble()});
        }
    }
    if (!flush() || !database_.commit()) {
        if (error && error->isEmpty()) *error = database_.lastError().text();
        database_.rollback();
        return {};
    }
    return result;
}

int SpectralLibraryRepository::spectrumCount(const QString &sourceName) const {
    QSqlQuery query(database_);
    if (sourceName.isEmpty()) query.prepare("SELECT COUNT(*) FROM spectra");
    else {
        query.prepare("SELECT COUNT(*) FROM spectra s JOIN compounds c ON c.id=s.compound_id "
                      "JOIN sources src ON src.id=c.source_id WHERE src.name=?");
        query.addBindValue(sourceName);
    }
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

QString SpectralLibraryRepository::sourceSummary() const {
    QSqlQuery query(database_);
    if (!query.exec("SELECT name,version,ionization,(SELECT COUNT(*) FROM spectra s JOIN compounds c ON c.id=s.compound_id WHERE c.source_id=src.id) FROM sources src ORDER BY imported_at DESC LIMIT 1") || !query.next())
        return "未导入正式参考库";
    return QString("%1 %2 · %3 · %4 条谱图")
        .arg(query.value(0).toString(), query.value(1).toString(), query.value(2).toString())
        .arg(query.value(3).toInt());
}

QVector<LibraryCompound> SpectralLibraryRepository::searchCompounds(const QString &text, int limit) const {
    QVector<LibraryCompound> result;
    QSqlQuery query(database_);
    const QString sql = "SELECT c.entry_id,c.name,c.formula,c.cas,c.nominal_mass,s.peak_count,"
                        "src.name||' '||src.version,s.ionization FROM compounds c "
                        "JOIN spectra s ON s.compound_id=c.id JOIN sources src ON src.id=c.source_id "
                        "WHERE (?='' OR c.name LIKE ? ESCAPE '\\' OR c.cas LIKE ? ESCAPE '\\' OR c.formula LIKE ? ESCAPE '\\') "
                        "ORDER BY c.name LIMIT ?";
    query.prepare(sql);
    QString escaped = text.trimmed();
    escaped.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
    const QString pattern = "%" + escaped + "%";
    query.addBindValue(text.trimmed());
    query.addBindValue(pattern); query.addBindValue(pattern); query.addBindValue(pattern);
    query.addBindValue(qBound(1, limit, 500));
    if (!query.exec()) return result;
    while (query.next()) result.push_back({query.value(0).toString(), query.value(1).toString(),
        query.value(2).toString(), query.value(3).toString(), query.value(4).toDouble(),
        query.value(5).toInt(), query.value(6).toString(), query.value(7).toString()});
    return result;
}

} // namespace qitest
