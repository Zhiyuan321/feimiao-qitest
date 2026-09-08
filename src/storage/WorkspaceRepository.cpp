#include "storage/WorkspaceRepository.h"
#include "storage/ScanSeriesCodec.h"
#include <QJsonArray>
#include "core/ChromatogramEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace qitest {
namespace {

bool exec(QSqlQuery &query, const QString &sql, QString *error) {
    if (query.exec(sql)) return true;
    if (error) *error = query.lastError().text();
    return false;
}

QString levelName(QualityLevel level) {
    switch (level) {
    case QualityLevel::Pass: return "PASS";
    case QualityLevel::Review: return "REVIEW";
    case QualityLevel::Fail: return "FAIL";
    }
    return "FAIL";
}

} // namespace

WorkspaceRepository::WorkspaceRepository(QString databasePath, bool tracksLifecycle)
    : databasePath_(std::move(databasePath)),
      connectionName_("qitest-workspace-" + QUuid::createUuid().toString(QUuid::WithoutBraces)),
      tracksLifecycle_(tracksLifecycle) {}

WorkspaceRepository::~WorkspaceRepository() {
    if (database_.isValid() && database_.isOpen()) {
        if (tracksLifecycle_) {
        QSqlQuery maintenance(database_);
        maintenance.exec("INSERT INTO workspace_meta(key,value) VALUES('clean_shutdown','1') "
                         "ON CONFLICT(key) DO UPDATE SET value='1'");
        maintenance.exec("PRAGMA wal_checkpoint(PASSIVE)");
        maintenance.exec("PRAGMA optimize");
        }
        database_.close();
    }
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

bool WorkspaceRepository::open(QString *error) {
    const QFileInfo info(databasePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = "cannot create workspace directory";
        return false;
    }
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        if (error) *error = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    const QStringList pragmas{
        "PRAGMA foreign_keys=ON",
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=FULL",
        "PRAGMA busy_timeout=5000",
        "PRAGMA wal_autocheckpoint=1000",
        "PRAGMA journal_size_limit=67108864",
        "PRAGMA temp_store=MEMORY",
        "PRAGMA cache_size=-16384"
    };
    for (const auto &pragma : pragmas) {
        if (!query.exec(pragma)) {
            if (error) *error = query.lastError().text();
            database_.close();
            return false;
        }
    }
    if (!initializeSchema(error)) return false;
    // Secondary worker connections must not mark the main session clean, or
    // run crash recovery while that session is still alive.
    if (!tracksLifecycle_) return true;
    QSqlQuery state(database_);
    if (!state.exec("SELECT value FROM workspace_meta WHERE key='clean_shutdown'") || !state.next()) {
        if (error) *error = state.lastError().text();
        database_.close();
        return false;
    }
    const bool previousShutdownWasClean = state.value(0).toString() == "1";
    if (!previousShutdownWasClean) {
        QSqlQuery check(database_);
        if (!check.exec("PRAGMA quick_check(1)") || !check.next() || check.value(0).toString() != "ok") {
            if (error) *error = "workspace database failed recovery integrity check";
            database_.close();
            return false;
        }
    }
    QSqlQuery markOpen(database_);
    if (!markOpen.exec("UPDATE workspace_meta SET value='0' WHERE key='clean_shutdown'")) {
        if (error) *error = markOpen.lastError().text();
        database_.close();
        return false;
    }
    return true;
}

bool WorkspaceRepository::initializeSchema(QString *error) {
    QSqlQuery query(database_);
    const QStringList statements{
        "CREATE TABLE IF NOT EXISTS schema_info(version INTEGER NOT NULL)",
        "INSERT INTO schema_info(version) SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM schema_info)",
        "UPDATE schema_info SET version=3 WHERE version<3",
        "CREATE TABLE IF NOT EXISTS workspace_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS sample_info(run_id TEXT PRIMARY KEY REFERENCES runs(id) ON DELETE CASCADE,payload TEXT NOT NULL)",
        "INSERT OR IGNORE INTO workspace_meta(key,value) VALUES('clean_shutdown','1')",
        "CREATE TABLE IF NOT EXISTS runs("
        "id TEXT PRIMARY KEY, completed_at TEXT NOT NULL, operator_name TEXT NOT NULL, method_name TEXT NOT NULL, "
        "instrument_id TEXT NOT NULL, data_scope TEXT NOT NULL, quality_level TEXT NOT NULL, quality_score INTEGER NOT NULL, "
        "engine_version TEXT NOT NULL, library_version TEXT NOT NULL, candidate_count INTEGER NOT NULL, "
        "review_status TEXT NOT NULL, report_path TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS spectrum_points("
        "run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, "
        "mz REAL NOT NULL, intensity REAL NOT NULL, PRIMARY KEY(run_id,ordinal))",
        "CREATE TABLE IF NOT EXISTS processed_points("
        "run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, "
        "mz REAL NOT NULL, intensity REAL NOT NULL, PRIMARY KEY(run_id,ordinal))",
        "CREATE TABLE IF NOT EXISTS run_metrics("
        "run_id TEXT PRIMARY KEY REFERENCES runs(id) ON DELETE CASCADE, baseline REAL NOT NULL, "
        "noise_mad REAL NOT NULL, total_ion_current REAL NOT NULL)",
        "CREATE TABLE IF NOT EXISTS instrument_telemetry("
        "run_id TEXT PRIMARY KEY REFERENCES runs(id) ON DELETE CASCADE, molecular_pump_rpm REAL NOT NULL, "
        "molecular_pump_current_a REAL NOT NULL, molecular_pump_voltage_v REAL NOT NULL, "
        "molecular_pump_temperature_c REAL NOT NULL, vacuum_mbar REAL NOT NULL, carrier_gas_mode TEXT NOT NULL, "
        "carrier_gas_pressure_torr REAL NOT NULL, carrier_gas_flow_ml_min REAL NOT NULL, "
        "ion_trap_temperature_c REAL NOT NULL, td_temperature_c REAL NOT NULL, ion_source_voltage_v REAL NOT NULL, "
        "multiplier_voltage_v REAL NOT NULL, extraction_flow_percent REAL NOT NULL, syringe_remaining_percent REAL NOT NULL)",
        "CREATE TABLE IF NOT EXISTS detected_peaks("
        "run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, mz REAL NOT NULL, "
        "relative_intensity REAL NOT NULL, signal_to_noise REAL NOT NULL, PRIMARY KEY(run_id,ordinal))",
        "CREATE TABLE IF NOT EXISTS candidates("
        "run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE, rank INTEGER NOT NULL, reference_id TEXT NOT NULL, "
        "name TEXT NOT NULL, category TEXT NOT NULL, measured_mz REAL NOT NULL, mass_error_ppm REAL NOT NULL, "
        "score REAL NOT NULL, evidence TEXT NOT NULL, is_demo INTEGER NOT NULL, PRIMARY KEY(run_id,rank))",
        "CREATE TABLE IF NOT EXISTS quality_checks("
        "run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE, ordinal INTEGER NOT NULL, check_id TEXT NOT NULL, "
        "title TEXT NOT NULL, detail TEXT NOT NULL, passed INTEGER NOT NULL, PRIMARY KEY(run_id,ordinal))",
        "CREATE TABLE IF NOT EXISTS scan_series("
        "run_id TEXT PRIMARY KEY REFERENCES runs(id) ON DELETE CASCADE, payload BLOB NOT NULL)",
        "CREATE TABLE IF NOT EXISTS audit_events("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, occurred_at TEXT NOT NULL, actor TEXT NOT NULL, action TEXT NOT NULL, "
        "target_id TEXT NOT NULL, detail TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS methods("
        "id TEXT PRIMARY KEY, name TEXT NOT NULL, version INTEGER NOT NULL, parameters_json TEXT NOT NULL, "
        "checksum TEXT NOT NULL, created_by TEXT NOT NULL, created_at TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 0, "
        "UNIQUE(name,version))",
        "CREATE TABLE IF NOT EXISTS acquisition_sessions("
        "id TEXT PRIMARY KEY, started_at TEXT NOT NULL, finished_at TEXT NOT NULL DEFAULT '', "
        "actor TEXT NOT NULL, data_scope TEXT NOT NULL, status TEXT NOT NULL, detail TEXT NOT NULL DEFAULT '')",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_methods_one_active ON methods(active) WHERE active=1",
        "CREATE INDEX IF NOT EXISTS idx_runs_completed ON runs(completed_at DESC)",
        "CREATE INDEX IF NOT EXISTS idx_audit_target ON audit_events(target_id,occurred_at)",
        "CREATE INDEX IF NOT EXISTS idx_acquisition_status ON acquisition_sessions(status,started_at DESC)"
    };
    for (const auto &statement : statements) if (!exec(query, statement, error)) return false;
    return true;
}

bool WorkspaceRepository::saveCompletedRun(const RunSummary &summary,
    const QVector<SpectrumPoint> &rawSpectrum, const AnalysisResult &result,
    const InstrumentTelemetry &telemetry, QString *error, const QVector<SpectrumScan> &scans) {
    if (!scans.isEmpty() && !ChromatogramEngine::validate(scans, error)) return false;
    if (!database_.transaction()) {
        if (error) *error = database_.lastError().text();
        return false;
    }
    QSqlQuery run(database_);
    run.prepare("INSERT INTO runs(id,completed_at,operator_name,method_name,instrument_id,data_scope,quality_level,"
                "quality_score,engine_version,library_version,candidate_count,review_status) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
    const QList<QVariant> values{summary.id, summary.completedAt.toUTC().toString(Qt::ISODateWithMs),
        summary.operatorName, summary.methodName, "QITEST-01-SIMULATOR", summary.dataScope,
        levelName(result.quality.level), result.quality.score, result.engineVersion, result.libraryVersion,
        result.candidates.size(), summary.reviewStatus};
    for (const auto &value : values) run.addBindValue(value);
    if (!run.exec()) { if (error) *error = run.lastError().text(); database_.rollback(); return false; }
    QSqlQuery sample(database_);
    sample.prepare("INSERT INTO sample_info(run_id,payload) VALUES(?,?)");
    sample.addBindValue(summary.id);
    sample.addBindValue(QString::fromUtf8(QJsonDocument(summary.sampleInfo).toJson(QJsonDocument::Compact)));
    if (!sample.exec()) { if (error) *error = sample.lastError().text(); database_.rollback(); return false; }

    if (!scans.isEmpty()) {
        QSqlQuery series(database_);
        series.prepare("INSERT INTO scan_series(run_id,payload) VALUES(?,?)");
        series.addBindValue(summary.id);
        series.addBindValue(QJsonDocument(ScanSeriesCodec::encode(scans)).toJson(QJsonDocument::Compact));
        if (!series.exec()) { if (error) *error = series.lastError().text(); database_.rollback(); return false; }
    }

    QSqlQuery point(database_);
    point.prepare("INSERT INTO spectrum_points(run_id,ordinal,mz,intensity) VALUES(?,?,?,?)");
    for (qsizetype i = 0; i < rawSpectrum.size(); ++i) {
        point.bindValue(0, summary.id); point.bindValue(1, i);
        point.bindValue(2, rawSpectrum[i].mz); point.bindValue(3, rawSpectrum[i].intensity);
        if (!point.exec()) { if (error) *error = point.lastError().text(); database_.rollback(); return false; }
    }
    QSqlQuery processed(database_);
    processed.prepare("INSERT INTO processed_points(run_id,ordinal,mz,intensity) VALUES(?,?,?,?)");
    for (qsizetype i = 0; i < result.processedSpectrum.points.size(); ++i) {
        processed.bindValue(0, summary.id); processed.bindValue(1, i);
        processed.bindValue(2, result.processedSpectrum.points[i].mz);
        processed.bindValue(3, result.processedSpectrum.points[i].intensity);
        if (!processed.exec()) { if (error) *error = processed.lastError().text(); database_.rollback(); return false; }
    }
    QSqlQuery metrics(database_);
    metrics.prepare("INSERT INTO run_metrics(run_id,baseline,noise_mad,total_ion_current) VALUES(?,?,?,?)");
    metrics.addBindValue(summary.id); metrics.addBindValue(result.processedSpectrum.baseline);
    metrics.addBindValue(result.processedSpectrum.noiseMad); metrics.addBindValue(result.processedSpectrum.totalIonCurrent);
    if (!metrics.exec()) { if (error) *error = metrics.lastError().text(); database_.rollback(); return false; }
    QSqlQuery telemetryQuery(database_);
    telemetryQuery.prepare("INSERT INTO instrument_telemetry(run_id,molecular_pump_rpm,molecular_pump_current_a,"
        "molecular_pump_voltage_v,molecular_pump_temperature_c,vacuum_mbar,carrier_gas_mode,"
        "carrier_gas_pressure_torr,carrier_gas_flow_ml_min,ion_trap_temperature_c,td_temperature_c,"
        "ion_source_voltage_v,multiplier_voltage_v,extraction_flow_percent,syringe_remaining_percent) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    const QList<QVariant> telemetryValues{summary.id, telemetry.molecularPumpRpm,
        telemetry.molecularPumpCurrentA, telemetry.molecularPumpVoltageV, telemetry.molecularPumpTemperatureC,
        telemetry.vacuumMbar, telemetry.carrierGasMode.isNull() ? QStringLiteral("") : telemetry.carrierGasMode, telemetry.carrierGasPressureTorr,
        telemetry.carrierGasFlowMlMin, telemetry.ionTrapTemperatureC, telemetry.tdTemperatureC,
        telemetry.ionSourceVoltageV, telemetry.multiplierVoltageV, telemetry.extractionFlowPercent,
        telemetry.syringeRemainingPercent};
    for (const auto &value : telemetryValues) telemetryQuery.addBindValue(value);
    if (!telemetryQuery.exec()) {
        if (error) *error = telemetryQuery.lastError().text();
        database_.rollback();
        return false;
    }
    QSqlQuery detected(database_);
    detected.prepare("INSERT INTO detected_peaks(run_id,ordinal,mz,relative_intensity,signal_to_noise) VALUES(?,?,?,?,?)");
    for (qsizetype i = 0; i < result.peaks.size(); ++i) {
        detected.bindValue(0, summary.id); detected.bindValue(1, i);
        detected.bindValue(2, result.peaks[i].mz); detected.bindValue(3, result.peaks[i].relativeIntensity);
        detected.bindValue(4, result.peaks[i].signalToNoise);
        if (!detected.exec()) { if (error) *error = detected.lastError().text(); database_.rollback(); return false; }
    }
    QSqlQuery candidate(database_);
    candidate.prepare("INSERT INTO candidates(run_id,rank,reference_id,name,category,measured_mz,mass_error_ppm,score,evidence,is_demo) "
                      "VALUES(?,?,?,?,?,?,?,?,?,?)");
    for (qsizetype i = 0; i < result.candidates.size(); ++i) {
        const auto &item = result.candidates[i];
        const QList<QVariant> row{summary.id, i + 1, item.referenceId, item.name, item.category,
            item.measuredMz, item.massErrorPpm, item.score, item.evidence, item.demo};
        for (int j = 0; j < row.size(); ++j) candidate.bindValue(j, row[j]);
        if (!candidate.exec()) { if (error) *error = candidate.lastError().text(); database_.rollback(); return false; }
    }
    QSqlQuery check(database_);
    check.prepare("INSERT INTO quality_checks(run_id,ordinal,check_id,title,detail,passed) VALUES(?,?,?,?,?,?)");
    for (qsizetype i = 0; i < result.quality.checks.size(); ++i) {
        const auto &item = result.quality.checks[i];
        check.bindValue(0, summary.id); check.bindValue(1, i + 1); check.bindValue(2, item.id);
        check.bindValue(3, item.title); check.bindValue(4, item.detail); check.bindValue(5, item.passed);
        if (!check.exec()) { if (error) *error = check.lastError().text(); database_.rollback(); return false; }
    }
    if (!database_.commit()) { if (error) *error = database_.lastError().text(); return false; }
    return appendAudit(summary.operatorName, "RUN_SAVED", summary.id,
        QString("scope=%1 quality=%2 score=%3").arg(summary.dataScope, levelName(result.quality.level)).arg(result.quality.score), error);
}

QVector<RunSummary> WorkspaceRepository::recentRuns(int limit) const {
    QVector<RunSummary> result;
    QSqlQuery query(database_);
    query.setForwardOnly(true);
    query.prepare("SELECT id,completed_at,operator_name,method_name,data_scope,quality_level,quality_score,candidate_count,"
                  "review_status,report_path FROM runs ORDER BY completed_at DESC LIMIT ?");
    query.addBindValue(qBound(1, limit, 1000));
    if (!query.exec()) return result;
    while (query.next()) result.push_back({query.value(0).toString(), QDateTime::fromString(query.value(1).toString(), Qt::ISODate),
        query.value(2).toString(), query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
        query.value(6).toInt(), query.value(7).toInt(), query.value(8).toString(), query.value(9).toString()});
    return result;
}

bool WorkspaceRepository::containsRun(const QString &runId) const {
    QSqlQuery query(database_);
    query.prepare("SELECT 1 FROM runs WHERE id=? LIMIT 1");
    query.addBindValue(runId);
    return query.exec() && query.next();
}

StoredRunDetail WorkspaceRepository::loadRun(const QString &runId) const {
    StoredRunDetail detail;
    QSqlQuery run(database_);
    run.prepare("SELECT id,completed_at,operator_name,method_name,data_scope,quality_level,quality_score,candidate_count,"
                "review_status,report_path,engine_version,library_version FROM runs WHERE id=?");
    run.addBindValue(runId);
    if (!run.exec() || !run.next()) return detail;
    detail.summary = {run.value(0).toString(), QDateTime::fromString(run.value(1).toString(), Qt::ISODate),
        run.value(2).toString(), run.value(3).toString(), run.value(4).toString(), run.value(5).toString(),
        run.value(6).toInt(), run.value(7).toInt(), run.value(8).toString(), run.value(9).toString()};
    detail.result.engineVersion = run.value(10).toString();
    detail.result.libraryVersion = run.value(11).toString();
    QSqlQuery sample(database_);
    sample.prepare("SELECT payload FROM sample_info WHERE run_id=?"); sample.addBindValue(runId);
    if (sample.exec() && sample.next())
        detail.summary.sampleInfo = QJsonDocument::fromJson(sample.value(0).toString().toUtf8()).object();
    QSqlQuery series(database_);
    series.prepare("SELECT payload FROM scan_series WHERE run_id=?"); series.addBindValue(runId);
    if (!series.exec()) return {};
    if (series.next()) {
        const QByteArray bytes = series.value(0).toByteArray();
        if (bytes.size() > 64 * 1024 * 1024) return {};
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()
                || !ScanSeriesCodec::decode(document.array(), &detail.scans, nullptr)) return {};
    }
    detail.result.quality.score = detail.summary.qualityScore;
    detail.result.quality.level = detail.summary.qualityLevel == "PASS" ? QualityLevel::Pass
        : detail.summary.qualityLevel == "REVIEW" ? QualityLevel::Review : QualityLevel::Fail;

    QSqlQuery raw(database_);
    raw.prepare("SELECT mz,intensity FROM spectrum_points WHERE run_id=? ORDER BY ordinal"); raw.addBindValue(runId);
    if (raw.exec()) while (raw.next()) detail.rawSpectrum.push_back({raw.value(0).toDouble(), raw.value(1).toDouble()});
    QSqlQuery processed(database_);
    processed.prepare("SELECT mz,intensity FROM processed_points WHERE run_id=? ORDER BY ordinal"); processed.addBindValue(runId);
    if (processed.exec()) while (processed.next()) detail.result.processedSpectrum.points.push_back(
        {processed.value(0).toDouble(), processed.value(1).toDouble()});
    QSqlQuery metrics(database_);
    metrics.prepare("SELECT baseline,noise_mad,total_ion_current FROM run_metrics WHERE run_id=?"); metrics.addBindValue(runId);
    if (metrics.exec() && metrics.next()) {
        detail.result.processedSpectrum.baseline = metrics.value(0).toDouble();
        detail.result.processedSpectrum.noiseMad = metrics.value(1).toDouble();
        detail.result.processedSpectrum.totalIonCurrent = metrics.value(2).toDouble();
    }
    QSqlQuery telemetryQuery(database_);
    telemetryQuery.prepare("SELECT molecular_pump_rpm,molecular_pump_current_a,molecular_pump_voltage_v,"
        "molecular_pump_temperature_c,vacuum_mbar,carrier_gas_mode,carrier_gas_pressure_torr,"
        "carrier_gas_flow_ml_min,ion_trap_temperature_c,td_temperature_c,ion_source_voltage_v,"
        "multiplier_voltage_v,extraction_flow_percent,syringe_remaining_percent "
        "FROM instrument_telemetry WHERE run_id=?");
    telemetryQuery.addBindValue(runId);
    if (telemetryQuery.exec() && telemetryQuery.next()) {
        detail.telemetry = {telemetryQuery.value(0).toDouble(), telemetryQuery.value(1).toDouble(),
            telemetryQuery.value(2).toDouble(), telemetryQuery.value(3).toDouble(),
            telemetryQuery.value(4).toDouble(), telemetryQuery.value(5).toString(),
            telemetryQuery.value(6).toDouble(), telemetryQuery.value(7).toDouble(),
            telemetryQuery.value(8).toDouble(), telemetryQuery.value(9).toDouble(),
            telemetryQuery.value(10).toDouble(), telemetryQuery.value(11).toDouble(),
            telemetryQuery.value(12).toDouble(), telemetryQuery.value(13).toDouble()};
    }
    QSqlQuery peaks(database_);
    peaks.prepare("SELECT mz,relative_intensity,signal_to_noise FROM detected_peaks WHERE run_id=? ORDER BY ordinal"); peaks.addBindValue(runId);
    if (peaks.exec()) while (peaks.next()) detail.result.peaks.push_back(
        {peaks.value(0).toDouble(), peaks.value(1).toDouble(), peaks.value(2).toDouble()});
    QSqlQuery candidates(database_);
    candidates.prepare("SELECT reference_id,name,category,measured_mz,mass_error_ppm,score,evidence,is_demo "
                       "FROM candidates WHERE run_id=? ORDER BY rank"); candidates.addBindValue(runId);
    if (candidates.exec()) while (candidates.next()) detail.result.candidates.push_back({
        candidates.value(0).toString(), candidates.value(1).toString(), candidates.value(2).toString(),
        candidates.value(3).toDouble(), candidates.value(4).toDouble(), candidates.value(5).toDouble(),
        0, 0, candidates.value(6).toString(), candidates.value(7).toBool()});
    QSqlQuery checks(database_);
    checks.prepare("SELECT check_id,title,detail,passed FROM quality_checks WHERE run_id=? ORDER BY ordinal"); checks.addBindValue(runId);
    if (checks.exec()) while (checks.next()) detail.result.quality.checks.push_back({
        checks.value(0).toString(), checks.value(1).toString(), checks.value(2).toString(), checks.value(3).toBool()});
    detail.valid = !detail.rawSpectrum.isEmpty() && !detail.result.processedSpectrum.points.isEmpty();
    return detail;
}

bool WorkspaceRepository::appendAudit(const QString &actor, const QString &action,
    const QString &targetId, const QString &detail, QString *error) {
    QSqlQuery query(database_);
    query.prepare("INSERT INTO audit_events(occurred_at,actor,action,target_id,detail) VALUES(?,?,?,?,?)");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(actor); query.addBindValue(action); query.addBindValue(targetId); query.addBindValue(detail);
    if (query.exec()) return true;
    if (error) *error = query.lastError().text();
    return false;
}

bool WorkspaceRepository::setReviewStatus(const QString &runId, const QString &status,
    const QString &actor, QString *error) {
    QSqlQuery query(database_);
    query.prepare("UPDATE runs SET review_status=? WHERE id=?");
    query.addBindValue(status); query.addBindValue(runId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error) *error = query.lastError().text().isEmpty() ? "run not found" : query.lastError().text();
        return false;
    }
    return appendAudit(actor, "REVIEW_STATUS_CHANGED", runId, status, error);
}

bool WorkspaceRepository::setReportPath(const QString &runId, const QString &path,
    const QString &actor, QString *error) {
    QSqlQuery query(database_);
    query.prepare("UPDATE runs SET report_path=? WHERE id=?");
    query.addBindValue(path); query.addBindValue(runId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error) *error = query.lastError().text().isEmpty() ? "run not found" : query.lastError().text();
        return false;
    }
    return appendAudit(actor, "REPORT_GENERATED", runId, path, error);
}

MethodDefinition WorkspaceRepository::createMethodVersion(const QString &name,
    const QJsonObject &parameters, const QString &actor, QString *error) {
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) { if (error) *error = "method name is empty"; return {}; }
    const QByteArray json = QJsonDocument(parameters).toJson(QJsonDocument::Compact);
    const QString checksum = QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
    QSqlQuery versionQuery(database_);
    versionQuery.prepare("SELECT COALESCE(MAX(version),0)+1 FROM methods WHERE name=?");
    versionQuery.addBindValue(trimmedName);
    if (!versionQuery.exec() || !versionQuery.next()) {
        if (error) *error = versionQuery.lastError().text(); return {};
    }
    const int version = versionQuery.value(0).toInt();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QDateTime createdAt = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(database_);
    insert.prepare("INSERT INTO methods(id,name,version,parameters_json,checksum,created_by,created_at,active) "
                   "VALUES(?,?,?,?,?,?,?,0)");
    insert.addBindValue(id); insert.addBindValue(trimmedName); insert.addBindValue(version);
    insert.addBindValue(QString::fromUtf8(json)); insert.addBindValue(checksum);
    insert.addBindValue(actor); insert.addBindValue(createdAt.toString(Qt::ISODateWithMs));
    if (!insert.exec()) { if (error) *error = insert.lastError().text(); return {}; }
    appendAudit(actor, "METHOD_VERSION_CREATED", id,
        QString("%1 v%2 sha256=%3").arg(trimmedName).arg(version).arg(checksum), error);
    return {id, trimmedName, version, parameters, checksum, actor, createdAt, false};
}

QVector<MethodDefinition> WorkspaceRepository::methods() const {
    QVector<MethodDefinition> result;
    QSqlQuery query(database_);
    if (!query.exec("SELECT id,name,version,parameters_json,checksum,created_by,created_at,active "
                    "FROM methods ORDER BY name,version DESC")) return result;
    while (query.next()) result.push_back({query.value(0).toString(), query.value(1).toString(),
        query.value(2).toInt(), QJsonDocument::fromJson(query.value(3).toString().toUtf8()).object(),
        query.value(4).toString(), query.value(5).toString(),
        QDateTime::fromString(query.value(6).toString(), Qt::ISODate), query.value(7).toBool()});
    return result;
}

bool WorkspaceRepository::activateMethod(const QString &methodId, const QString &actor, QString *error) {
    if (!database_.transaction()) { if (error) *error = database_.lastError().text(); return false; }
    QSqlQuery clear(database_);
    if (!clear.exec("UPDATE methods SET active=0")) { if (error) *error = clear.lastError().text(); database_.rollback(); return false; }
    QSqlQuery set(database_);
    set.prepare("UPDATE methods SET active=1 WHERE id=?");
    set.addBindValue(methodId);
    if (!set.exec() || set.numRowsAffected() != 1) {
        if (error) *error = set.lastError().text().isEmpty() ? "method not found" : set.lastError().text();
        database_.rollback(); return false;
    }
    if (!database_.commit()) { if (error) *error = database_.lastError().text(); return false; }
    return appendAudit(actor, "METHOD_ACTIVATED", methodId, "active method changed", error);
}

MethodDefinition WorkspaceRepository::activeMethod() const {
    QSqlQuery query(database_);
    if (!query.exec("SELECT id,name,version,parameters_json,checksum,created_by,created_at,active "
                    "FROM methods WHERE active=1 LIMIT 1") || !query.next()) return {};
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toInt(),
        QJsonDocument::fromJson(query.value(3).toString().toUtf8()).object(), query.value(4).toString(),
        query.value(5).toString(), QDateTime::fromString(query.value(6).toString(), Qt::ISODate), true};
}

QString WorkspaceRepository::beginAcquisitionSession(const QString &actor,
    const QString &dataScope, QString *error) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    QSqlQuery query(database_);
    query.prepare("INSERT INTO acquisition_sessions(id,started_at,actor,data_scope,status,detail) "
                  "VALUES(?,?,?,?,?,?)");
    query.addBindValue(id);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(actor);
    query.addBindValue(dataScope);
    query.addBindValue("RUNNING");
    query.addBindValue("acquisition accepted by instrument safety gate");
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return {};
    }
    if (!appendAudit(actor, "ACQUISITION_STARTED", id, "scope=" + dataScope, error)) {
        QSqlQuery rollback(database_);
        rollback.prepare("DELETE FROM acquisition_sessions WHERE id=?");
        rollback.addBindValue(id);
        rollback.exec();
        return {};
    }
    return id;
}

bool WorkspaceRepository::finishAcquisitionSession(const QString &sessionId,
    const QString &status, const QString &detail, QString *error) {
    static const QStringList terminalStatuses{"COMPLETED", "CANCELLED", "FAILED", "INTERRUPTED"};
    if (!terminalStatuses.contains(status)) {
        if (error) *error = "invalid terminal acquisition status";
        return false;
    }
    QSqlQuery query(database_);
    query.prepare("UPDATE acquisition_sessions SET finished_at=?,status=?,detail=? "
                  "WHERE id=? AND status='RUNNING'");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(status);
    query.addBindValue(detail);
    query.addBindValue(sessionId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error) *error = query.lastError().text().isEmpty()
            ? "running acquisition session not found" : query.lastError().text();
        return false;
    }
    return appendAudit("system", "ACQUISITION_" + status, sessionId, detail, error);
}

int WorkspaceRepository::recoverInterruptedSessions(QString *error) {
    QSqlQuery ids(database_);
    if (!ids.exec("SELECT id FROM acquisition_sessions WHERE status='RUNNING'")) {
        if (error) *error = ids.lastError().text();
        return -1;
    }
    QStringList interruptedIds;
    while (ids.next()) interruptedIds.push_back(ids.value(0).toString());
    for (const auto &id : interruptedIds) {
        QString localError;
        if (!finishAcquisitionSession(id, "INTERRUPTED",
                "recovered after previous process ended before acquisition completion", &localError)) {
            if (error) *error = localError;
            return -1;
        }
    }
    if (!interruptedIds.isEmpty())
        appendAudit("system-recovery", "INTERRUPTED_ACQUISITIONS_RECOVERED", "application",
            QString("count=%1").arg(interruptedIds.size()), error);
    return interruptedIds.size();
}

QVector<AcquisitionSession> WorkspaceRepository::recentAcquisitionSessions(int limit) const {
    QVector<AcquisitionSession> sessions;
    QSqlQuery query(database_);
    query.setForwardOnly(true);
    query.prepare("SELECT id,started_at,finished_at,actor,data_scope,status,detail "
                  "FROM acquisition_sessions ORDER BY started_at DESC LIMIT ?");
    query.addBindValue(qBound(1, limit, 1000));
    if (!query.exec()) return sessions;
    while (query.next()) sessions.push_back({query.value(0).toString(),
        QDateTime::fromString(query.value(1).toString(), Qt::ISODate),
        QDateTime::fromString(query.value(2).toString(), Qt::ISODate),
        query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
        query.value(6).toString()});
    return sessions;
}

} // namespace qitest
