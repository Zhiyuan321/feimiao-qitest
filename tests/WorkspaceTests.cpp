#include "core/AnalysisEngine.h"
#include "core/ChromatogramEngine.h"
#include "report/ReportGenerator.h"
#include "support/NamedOutputPath.h"
#include "storage/WorkspaceRepository.h"
#include "storage/RunArchiveCodec.h"
#include "storage/ScanSeriesCodec.h"
#include "storage/CalibrationDocument.h"
#include "storage/IntegrationDocument.h"
#include "storage/ArchiveImportWorker.h"
#include "support/DiagnosticBundle.h"

#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <cmath>
#include <limits>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

using namespace qitest;

class WorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void persistsRunAndAudit();
    void migratesTelemetryAndPreservesUnknownReadings();
    void writesTraceablePdf();
    void reportTemplateOnlyListsSuspects();
    void versionsAndActivatesMethods();
    void archivesAndValidatesRawSpectrum();
    void rejectsNonpositiveMassAndNullScanOutput();
    void recoversOnlyInterruptedAcquisitions();
    void sustainsBoundedSixMonthHistoryReads();
    void reopensAfterUncleanShutdownMarker();
    void writesPrivacySafeDiagnosticBundle();
    void importsBatchesWithoutDuplicatesOrMainThreadBlocking();
    void importsAndPreservesScanSeries();
    void publicOpenMsTracesMatchIndependentReference();
    void calibrationDocumentsArePortableAndRejectCorruption();
    void integrationSnapshotsRecalculateAndRejectTampering();
};

void WorkspaceTests::rejectsNonpositiveMassAndNullScanOutput() {
    QString error;
    QVERIFY(!ScanSeriesCodec::decode({}, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QTemporaryDir dir;
    const auto path = dir.filePath("invalid-mass.qit.json");
    // A valid checksum does not make physically invalid values acceptable.
    for (const double firstMass : {-0.5, 0.0}) {
        QJsonArray points;
        points.append(QJsonArray{firstMass, 1.0});
        for (int i = 1; i < 30; ++i) points.append(QJsonArray{50.0 + i, 1.0});
        const QJsonObject payload{{"raw_spectrum", points}};
        const auto hash = QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        const QJsonObject root{{"schema", "qitest-run-archive-1"}, {"payload", payload}, {"payload_sha256", QString::fromLatin1(hash)}};
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson()); file.close();
        const auto result = RunArchiveCodec::read(path);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, QString("invalid or unsorted spectrum"));
    }
}

void WorkspaceTests::integrationSnapshotsRecalculateAndRejectTampering() {
    QTemporaryDir dir; QString error;
    IntegrationSnapshot original;
    original.kind=ChromatogramEngine::Kind::Eic;
    original.trace={{1.012345678901,2},{2.123456789012,8},{4.234567890123,1}};
    original.fromSeconds=1.12345678901; original.toSeconds=4.12345678901;
    original.endpointBaseline=true;
    original.area=ChromatogramEngine::integrate(original.trace,original.fromSeconds,original.toSeconds,true).area;
    const auto path=dir.filePath("test.qint.json");
    QVERIFY2(IntegrationDocument::save(path,original,&error),qPrintable(error));
    IntegrationSnapshot loaded;
    QVERIFY2(IntegrationDocument::load(path,&loaded,&error),qPrintable(error));
    QCOMPARE(loaded.fromSeconds,original.fromSeconds); QCOMPARE(loaded.toSeconds,original.toSeconds);
    QCOMPARE(loaded.trace[1].intensity,original.trace[1].intensity); QCOMPARE(loaded.area,original.area);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
    // Integrity is not authenticity. Even a deliberately recomputed hash must
    // not permit an area inconsistent with the supplied curve and boundaries.
    auto envelope=QJsonDocument::fromJson(bytes).object();
    auto payload=QJsonDocument::fromJson(QByteArray::fromBase64(envelope["payload_base64"].toString().toLatin1())).object();
    payload["area"]=original.area+100;
    const auto changed=QJsonDocument(payload).toJson(QJsonDocument::Compact);
    envelope["payload_base64"]=QString::fromLatin1(changed.toBase64());
    envelope["sha256"]=QString::fromLatin1(QCryptographicHash::hash(changed,QCryptographicHash::Sha256).toHex());
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(envelope).toJson()); file.close();
    QVERIFY(!IntegrationDocument::load(path,&loaded,&error)); QCOMPARE(loaded.area,original.area);
    QVERIFY(error.contains("重算"));
    original.trace[1].mz=original.trace[0].mz;
    QVERIFY(!IntegrationDocument::save(path,original,&error));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray(IntegrationDocument::MaximumBytes+1,'x')); file.close();
    QVERIFY(!IntegrationDocument::load(path,&loaded,&error)); QCOMPARE(loaded.area,original.area);
}

void WorkspaceTests::calibrationDocumentsArePortableAndRejectCorruption() {
    QTemporaryDir dir; QString error;
    CalibrationModel model; model.name="标准 A"; model.internalStandard="IS-A";
    model.standard=CalibrationModel::Standard::Internal; model.weight=QuantitationEngine::Weight::InverseXSquared;
    model.observations={{2,21,2,10,true},{8,82,4,20,true},{30,202,6,20,true},{0,0,2,10,false}};
    const auto path=dir.filePath("standard.qcal.json");
    QVERIFY2(CalibrationDocument::save(path,model,&error),qPrintable(error));
    CalibrationModel loaded;
    QVERIFY2(CalibrationDocument::load(path,&loaded,&error),qPrintable(error));
    QCOMPARE(loaded.name,model.name); QCOMPARE(loaded.concentrationUnit,model.concentrationUnit);
    QCOMPARE(loaded.standard,model.standard); QCOMPARE(loaded.weight,model.weight);
    QCOMPARE(loaded.observations.size(),size_t(4)); QVERIFY(!loaded.observations.back().included);
    double value=0; QVERIFY(CalibrationCalculator::calculate(loaded,61,10,2,&value,&error));
    QVERIFY(std::abs(value-6)<1e-10);
    const auto csvPath=dir.filePath("points.csv");
    QVERIFY(CalibrationDocument::exportCsv(csvPath,model,&error));
    QVERIFY(CalibrationDocument::readCsv(csvPath,&loaded,&error)); QCOMPARE(loaded.observations.size(),3);
    QCOMPARE(loaded.observations[1].internalConcentration,4.0);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    auto envelope=QJsonDocument::fromJson(file.readAll()).object(); file.close();
    envelope["sha256"]="wrong";
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(envelope).toJson()); file.close();
    const auto name=loaded.name;
    QVERIFY(!CalibrationDocument::load(path,&loaded,&error)); QCOMPARE(loaded.name,name);
    QFile bad(csvPath); QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("concentration,response\n1,,2\n2,4\n3,6\n"); bad.close();
    QVERIFY(!CalibrationDocument::readCsv(csvPath,&loaded,&error)); QCOMPARE(loaded.name,name);
    QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write(QByteArray(CalibrationDocument::MaximumBytes+1,'x')); bad.close();
    QVERIFY(!CalibrationDocument::readCsv(csvPath,&loaded,&error));
    model.observations[0].internalResponse=0;
    QVERIFY(!CalibrationDocument::save(dir.filePath("invalid.qcal.json"),model,&error));
    QVERIFY(!QFile::exists(dir.filePath("invalid.qcal.json")));
}

void WorkspaceTests::publicOpenMsTracesMatchIndependentReference() {
    QFile expectedFile(":/public-ms/expected.json"); QVERIFY(expectedFile.open(QIODevice::ReadOnly));
    const auto expected = QJsonDocument::fromJson(expectedFile.readAll()).object();
    QFile source(":/public-ms/openms_bsa.scan.csv"); QVERIFY(source.open(QIODevice::ReadOnly));
    const auto csv = source.readAll();
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(csv, QCryptographicHash::Sha256).toHex()),
             expected.value("csv_sha256").toString());
    QTemporaryDir dir;
    const QString path = dir.filePath("public-bsa.scan.csv");
    QFile output(path); QVERIFY(output.open(QIODevice::WriteOnly));
    QCOMPARE(output.write(csv), qint64(csv.size())); output.close();
    const auto loaded = RunArchiveCodec::read(path);
    QVERIFY2(loaded.valid, qPrintable(loaded.error));
    QCOMPARE(loaded.scans.size(), expected.value("scan_count").toInt());
    int count = 0; for (const auto &scan : loaded.scans) count += scan.points.size();
    QCOMPARE(count, expected.value("point_count").toInt());
    const auto closeEnough = [](double a, double b) {
        return std::isfinite(a) && std::abs(a-b) <= 1e-10 * std::max(1.0, std::abs(b));
    };
    const QVector<QPair<QString, ChromatogramEngine::Kind>> kinds{
        {"tic", ChromatogramEngine::Kind::Tic}, {"bpc", ChromatogramEngine::Kind::Bpc},
        {"eic", ChromatogramEngine::Kind::Eic}};
    for (const auto &kind : kinds) {
        const auto trace = ChromatogramEngine::trace(loaded.scans, kind.second, 1,
            expected.value("target_mz").toDouble(), expected.value("tolerance_da").toDouble());
        const auto rows = expected.value("trace").toArray();
        QCOMPARE(trace.size(), rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows[i].toObject();
            QVERIFY(closeEnough(trace[i].mz, row.value("time_s").toDouble()));
            QVERIFY2(closeEnough(trace[i].intensity, row.value(kind.first).toDouble()), qPrintable(kind.first));
        }
        const auto area = ChromatogramEngine::integrate(trace, trace.first().mz, trace.last().mz, false);
        const auto baseline = ChromatogramEngine::integrate(trace, trace.first().mz, trace.last().mz, true);
        const auto reference = expected.value("integrals").toObject().value(kind.first).toObject();
        QVERIFY(area.valid); QVERIFY(baseline.valid);
        QVERIFY(closeEnough(area.area, reference.value("raw").toDouble()));
        QVERIFY(closeEnough(baseline.area, reference.value("endpoint_baseline").toDouble()));
    }
    // The mzML header is retained only as provenance, not mistaken for a sum of
    // its processed centroid array; this fixture intentionally detects the difference.
    const auto tic = ChromatogramEngine::trace(loaded.scans, ChromatogramEngine::Kind::Tic);
    QVERIFY(!closeEnough(tic.first().intensity, expected.value("source_header_tic_first").toDouble()));
    WorkspaceRepository db(dir.filePath("public.sqlite")); QString error;
    QVERIFY(db.open(&error));
    AnalysisEngine engine(demoReferences(), "test-only");
    RunSummary summary{"public-openms", QDateTime::currentDateTimeUtc(), "tester", "Public BSA", "IMPORTED_UNVALIDATED", "REVIEW", 0, 0, "PENDING_REVIEW", {}};
    QVERIFY2(db.saveCompletedRun(summary, loaded.rawSpectrum, engine.analyze(loaded.rawSpectrum, {}), {},
        &error, loaded.scans), qPrintable(error));
    const auto stored = db.loadRun(summary.id); QVERIFY(stored.valid);
    QVERIFY(RunArchiveCodec::write(dir.filePath("roundtrip.qit.json"), stored, &error));
    const auto reloaded = RunArchiveCodec::read(dir.filePath("roundtrip.qit.json")); QVERIFY(reloaded.valid);
    const auto roundtrip = ChromatogramEngine::trace(reloaded.scans, ChromatogramEngine::Kind::Tic);
    QCOMPARE(roundtrip.size(), tic.size());
    for (int i = 0; i < tic.size(); ++i) {
        QCOMPARE(roundtrip[i].mz, tic[i].mz);
        QCOMPARE(roundtrip[i].intensity, tic[i].intensity);
    }
}

void WorkspaceTests::importsAndPreservesScanSeries() {
    QTemporaryDir dir;
    const QString path = dir.filePath("example.scan.csv");
    QFile csv(path); QVERIFY(csv.open(QIODevice::WriteOnly));
    QByteArray text = "time_s,ms_level,mz,intensity\n";
    for (int scan = 0; scan < 3; ++scan)
        for (int point = 0; point < 20; ++point)
            text += QString("%1,1,%2,%3\n").arg(scan*2).arg(100+point).arg(10+scan).toUtf8();
    QCOMPARE(csv.write(text), qint64(text.size())); csv.close();
    const auto decoded = RunArchiveCodec::read(path); QVERIFY2(decoded.valid, qPrintable(decoded.error));
    QCOMPARE(decoded.scans.size(), 3); QCOMPARE(decoded.scans[2].timeSeconds, 4.0);
    WorkspaceRepository db(dir.filePath("workspace.sqlite")); QString error; QVERIFY(db.open(&error));
    AnalysisEngine engine(demoReferences(), "test");
    const auto analysis = engine.analyze(decoded.rawSpectrum, {});
    RunSummary summary{"scan-test", QDateTime::currentDateTimeUtc(), "tester", "MS1", "IMPORTED_UNVALIDATED", "REVIEW", 0, 0, "PENDING_REVIEW", {}};
    QVERIFY2(db.saveCompletedRun(summary, decoded.rawSpectrum, analysis, {}, &error, decoded.scans), qPrintable(error));
    const auto stored = db.loadRun(summary.id); QVERIFY(stored.valid); QCOMPARE(stored.scans.size(), 3);
    QVERIFY(RunArchiveCodec::write(dir.filePath("export.qit.json"), stored, &error));
    const auto again = RunArchiveCodec::read(dir.filePath("export.qit.json")); QVERIFY(again.valid);
    QCOMPARE(again.scans[1].points[4].intensity, 11.0);
    QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Truncate));
    csv.write("time_s,ms_level,mz,intensity\n0,1,100,1\n0,2,101,2\n"); csv.close();
    QVERIFY(!RunArchiveCodec::read(path).valid);
}

AnalysisResult fixtureResult() {
    AnalysisResult result;
    result.processedSpectrum.points = {{100, 10}, {200, 100}};
    result.peaks = {{200, 100, 20}};
    result.candidates = {{"demo", "演示候选", "合成演示", 200, 0, 90, 1, 1, "演示证据", true}};
    result.quality = {QualityLevel::Review, 75, {{"q1", "信号", "需要复核", false}}};
    result.engineVersion = "test-engine";
    result.libraryVersion = "test-library";
    return result;
}

RunSummary fixtureRun() {
    return {"run-001", QDateTime::currentDateTimeUtc(), "tester", "test-method",
        "DEMO_SIMULATION", "REVIEW", 75, 1, "PENDING_REVIEW", {}};
}

InstrumentTelemetry fixtureTelemetry() {
    return {60000.0, 1.2, 24.0, 42.0, 1.8e-5, "内载气", 801.0, 28.4,
        85.0, 245.0, 3200.0, 1450.0, 20.0, 76.0};
}

void WorkspaceTests::persistsRunAndAudit() {
    QTemporaryDir dir;
    WorkspaceRepository repository(dir.filePath("workspace.sqlite"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const QVector<SpectrumPoint> raw{{100, 5}, {200, 50}};
    QVERIFY2(repository.saveCompletedRun(fixtureRun(), raw, fixtureResult(), fixtureTelemetry(), &error), qPrintable(error));
    const auto runs = repository.recentRuns();
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.first().id, QString("run-001"));
    const auto detail = repository.loadRun("run-001");
    QVERIFY(detail.valid);
    QCOMPARE(detail.rawSpectrum.size(), 2);
    QCOMPARE(detail.result.candidates.size(), 1);
    QCOMPARE(detail.result.quality.checks.size(), 1);
    QCOMPARE(detail.telemetry.molecularPumpRpm, 60000.0);
    QCOMPARE(detail.telemetry.syringeRemainingPercent, 76.0);
    QVERIFY(repository.setReviewStatus("run-001", "REVIEWED", "reviewer", &error));
    QCOMPARE(repository.recentRuns().first().reviewStatus, QString("REVIEWED"));
}

void WorkspaceTests::migratesTelemetryAndPreservesUnknownReadings() {
    QTemporaryDir dir; QString error;
    const auto path=dir.filePath("old-workspace.sqlite");
    const QVector<SpectrumPoint> raw{{100,5},{200,50}};
    {
        WorkspaceRepository repository(path); QVERIFY(repository.open(&error));
        QVERIFY(repository.saveCompletedRun(fixtureRun(),raw,fixtureResult(),fixtureTelemetry(),&error));
    }
    // Recreate the previous NOT NULL telemetry schema, including an existing row.
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","legacy-telemetry-fixture");db.setDatabaseName(path);
        QVERIFY(db.open());QSqlQuery query(db);
        QVERIFY(query.exec("SELECT sql FROM sqlite_master WHERE name='instrument_telemetry'"));
        QVERIFY(query.next());auto schema=query.value(0).toString();query.finish();
        schema.replace("instrument_telemetry(","legacy_telemetry(");schema.replace(" REAL"," REAL NOT NULL");
        QVERIFY(query.exec(schema));
        QVERIFY(query.exec("INSERT INTO legacy_telemetry SELECT * FROM instrument_telemetry"));
        QVERIFY(query.exec("DROP TABLE instrument_telemetry"));
        QVERIFY(query.exec("ALTER TABLE legacy_telemetry RENAME TO instrument_telemetry"));
        db.close();
    }
    QSqlDatabase::removeDatabase("legacy-telemetry-fixture");
    {
        WorkspaceRepository repository(path);QVERIFY2(repository.open(&error),qPrintable(error));
        const auto old=repository.loadRun("run-001");QVERIFY(old.valid);
        QCOMPARE(old.telemetry.molecularPumpRpm,60000.0);QCOMPARE(old.rawSpectrum[1].intensity,50.0);
        auto run=fixtureRun();run.id="real-unknown";run.dataScope="DEVICE_UNVALIDATED";
        auto telemetry=fixtureTelemetry();telemetry.molecularPumpRpm=std::numeric_limits<double>::quiet_NaN();
        telemetry.syringeRemainingPercent=std::numeric_limits<double>::quiet_NaN();
        QVERIFY2(repository.saveCompletedRun(run,raw,fixtureResult(),telemetry,&error),qPrintable(error));
        const auto loaded=repository.loadRun(run.id);QVERIFY(loaded.valid);
        QVERIFY(std::isnan(loaded.telemetry.molecularPumpRpm));
        QVERIFY(std::isnan(loaded.telemetry.syringeRemainingPercent));
        QCOMPARE(loaded.telemetry.molecularPumpCurrentA,1.2);
    }
    WorkspaceRepository reopened(path);QVERIFY(reopened.open(&error));
    QCOMPARE(reopened.recentRuns().size(),2);
    QVERIFY(std::isnan(reopened.loadRun("real-unknown").telemetry.molecularPumpRpm));
}

void WorkspaceTests::reportTemplateOnlyListsSuspects() {
    auto run=fixtureRun();run.sampleInfo={{"person_name","测试姓名<&>"},{"sample_id","S-001"},
        {"instrument_model","测试仪器"},{"sample_type","普通"},{"ionization","APCI"},
        {"screening_status","COMPLETE"},
        {"ion_screening_snapshot",QJsonObject{{"entries",QJsonArray{
            QJsonObject{{"name","可疑测试物"},{"qualitify_ion","85,127"},{"quantify_ion","127"}}}}}}};
    auto result=fixtureResult();result.candidates[0].referenceId="lib-row-1";
    result.candidates[0].name="可疑测试物";
    ScreeningItem suspect;suspect.referenceId="lib-row-1";suspect.conclusion="可疑";
    result.screeningItems={suspect};
    for(const QString &state:{QString("未检出"),QString("未筛查")}) {
        auto candidate=result.candidates[0];candidate.referenceId=state;candidate.name=state+"测试物";
        result.candidates.append(candidate);auto item=suspect;item.referenceId=state;item.conclusion=state;
        result.screeningItems.append(item);
    }
    const auto html=ReportGenerator::reportHtml(run,result,{0,1,2});
    QVERIFY(html.contains("化合物名称"));QVERIFY(html.contains("被检人"));
    QVERIFY(html.contains("测试姓名&lt;&amp;&gt;"));QVERIFY(!html.contains("浓度"));
    QVERIFY(html.contains("可疑测试物"));QVERIFY(html.contains(">127<"));QVERIFY(!html.contains("85,127"));
    QVERIFY(!html.contains("未检出测试物"));QVERIFY(!html.contains("未筛查测试物"));
    QVERIFY(!ReportGenerator::reportHtml(run,result,{1,2}).contains("可疑测试物"));
    const auto empty=ReportGenerator::reportHtml(run,result,{});
    QVERIFY(empty.contains("本次报告无可疑化合物"));QVERIFY(!empty.contains("可疑测试物"));
    run.sampleInfo["screening_status"]="PARTIAL";run.sampleInfo["screening_error"]="测试条目未完成";
    QVERIFY(ReportGenerator::reportHtml(run,result,{}).contains("测试条目未完成"));
    QTemporaryDir dir;QString error;
    QCOMPARE(QFileInfo(namedOutputPath(dir.path(),"张三",".pdf")).fileName(),QString("张三.pdf"));
    QFile existing(dir.filePath("张三.pdf"));QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
    QCOMPARE(QFileInfo(namedOutputPath(dir.path(),"张三",".pdf")).fileName(),QString("张三-2.pdf"));
    QCOMPARE(QFileInfo(namedOutputPath(dir.path(),"CON",".qit.json")).fileName(),QString("_CON.qit.json"));
    QCOMPARE(QFileInfo(namedOutputPath(dir.path(),"甲/乙:*",".pdf")).fileName(),QString("甲_乙__.pdf"));
    const auto capture=qEnvironmentVariable("QITEST_PDF_CAPTURE_DIR");
    if(!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));run.sampleInfo["screening_status"]="COMPLETE";run.sampleInfo.remove("screening_error");
        QVERIFY2(ReportGenerator::writePdf(capture+"/template-suspect.pdf",run,result,&error),qPrintable(error));
        result.candidates.clear();
        QVERIFY2(ReportGenerator::writePdf(capture+"/template-empty.pdf",run,result,&error),qPrintable(error));
    }
}

void WorkspaceTests::writesTraceablePdf() {
    QTemporaryDir dir;
    const QString path = dir.filePath("report.pdf");
    QString error;
    auto identifiedRun = fixtureRun();
    identifiedRun.sampleInfo = {{"sample_id", "SAMPLE-001"}, {"person_name", "张三"},
        {"identity_number", "330100199001010000"}};
    QVERIFY2(ReportGenerator::writePdf(path, identifiedRun, fixtureResult(), &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(4) == "%PDF");
    QVERIFY(file.size() > 1000);
    file.seek(0);
    const auto original = file.readAll();
    QVERIFY(!ReportGenerator::writePdf(path, fixtureRun(), fixtureResult(), QVector<int>{99}, &error));
    file.close();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    file.close();
    auto longResult = fixtureResult();
    longResult.candidates.clear();
    for (int i=0; i<45; ++i) {
        auto candidate = fixtureResult().candidates.first();
        candidate.name = QString("长名称候选 %1 <literal> & %2").arg(i).arg(QString(90, 'A'));
        candidate.evidence = QString("证据 %1：").arg(i) + QString("需要结合标准物和实验条件进行复核。 ").repeated(6)
            + QString(" END-EVIDENCE-%1").arg(i);
        longResult.candidates.append(candidate);
    }
    auto longRun = fixtureRun();
    longRun.id = "import-" + QString(128, 'f');
    longRun.candidateCount = longResult.candidates.size();
    const auto capture = qEnvironmentVariable("QITEST_PDF_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(QDir().mkpath(capture));
    const QString longPath = capture.isEmpty() ? dir.filePath("long-report.pdf") : capture + "/long-report.pdf";
    QVERIFY2(ReportGenerator::writePdf(longPath, longRun, longResult, &error), qPrintable(error));
    QFile multi(longPath); QVERIFY(multi.open(QIODevice::ReadOnly));
    const auto bytes = multi.readAll();
    QVERIFY(bytes.count("/Type /Page\n") > 1);
}

void WorkspaceTests::versionsAndActivatesMethods() {
    QTemporaryDir dir;
    WorkspaceRepository repository(dir.filePath("workspace.sqlite"));
    QString error;
    QVERIFY(repository.open(&error));
    const auto first = repository.createMethodVersion("筛查方法", {{"scope", "DEMO"}}, "engineer", &error);
    const auto second = repository.createMethodVersion("筛查方法", {{"scope", "DEMO"}, {"note", "revision"}}, "engineer", &error);
    QCOMPARE(first.version, 1);
    QCOMPARE(second.version, 2);
    QVERIFY(first.checksum != second.checksum);
    QVERIFY(repository.activateMethod(second.id, "engineer", &error));
    QCOMPARE(repository.activeMethod().id, second.id);
    QCOMPARE(repository.methods().size(), 2);
    QVERIFY(repository.updateMethodVersion(first.id,"快速筛查",{{"scope","DEMO"},{"note","saved"}},"engineer",&error));
    QCOMPARE(repository.methods().size(),2);
    MethodDefinition updated;for(const auto &method:repository.methods())if(method.id==first.id)updated=method;
    QCOMPARE(updated.name,QString("快速筛查"));QCOMPARE(updated.version,1);QCOMPARE(updated.parameters.value("note").toString(),QString("saved"));
    QVERIFY(!repository.deleteMethodVersion(second.id,"engineer",&error));
    QVERIFY(repository.updateMethodVersion(second.id,"筛查方法",{{"scope","DEMO"}},"engineer",&error));
    QVERIFY(repository.activeMethod().id.isEmpty());
    QVERIFY(repository.activateMethod(second.id,"engineer",&error));
    QVERIFY(repository.deleteMethodVersion(first.id,"engineer",&error));
    QCOMPARE(repository.methods().size(),1);
}

void WorkspaceTests::archivesAndValidatesRawSpectrum() {
    QTemporaryDir dir;
    WorkspaceRepository repository(dir.filePath("workspace.sqlite"));
    QString error;
    QVERIFY(repository.open(&error));
    QVector<SpectrumPoint> raw;
    for (int i = 0; i < 30; ++i) raw.push_back({50.0 + i, 5.0 + i});
    auto result = fixtureResult();
    result.processedSpectrum.points = raw;
    QVERIFY(repository.saveCompletedRun(fixtureRun(), raw, result, fixtureTelemetry(), &error));
    const auto detail = repository.loadRun("run-001");
    const QString path = dir.filePath("run.qit.json");
    QVERIFY(RunArchiveCodec::write(path, detail, &error));
    const auto imported = RunArchiveCodec::read(path);
    QVERIFY2(imported.valid, qPrintable(imported.error));
    QCOMPARE(imported.rawSpectrum.size(), raw.size());
    const QByteArray validBytes = [&] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }();
    QVERIFY(!RunArchiveCodec::write(path, StoredRunDetail{}, &error));
    { QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), validBytes); }
    { QFile huge(dir.filePath("too-large.json")); QVERIFY(huge.open(QIODevice::WriteOnly));
      QVERIFY(huge.resize(RunArchiveCodec::MaximumFileBytes + 1)); }
    QVERIFY(!RunArchiveCodec::read(dir.filePath("too-large.json")).valid);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    auto bytes = file.readAll();
    bytes.replace("\"payload_sha256\": \"", "\"payload_sha256\": \"0");
    file.resize(0); file.write(bytes); file.close();
    QVERIFY(!RunArchiveCodec::read(path).valid);
}

void WorkspaceTests::recoversOnlyInterruptedAcquisitions() {
    QTemporaryDir dir;
    const QString databasePath = dir.filePath("workspace.sqlite");
    QString error;
    {
        WorkspaceRepository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        const QString interrupted = repository.beginAcquisitionSession("tester", "DEMO_SIMULATION", &error);
        QVERIFY2(!interrupted.isEmpty(), qPrintable(error));
        const QString completed = repository.beginAcquisitionSession("tester", "DEMO_SIMULATION", &error);
        QVERIFY2(!completed.isEmpty(), qPrintable(error));
        QVERIFY2(repository.finishAcquisitionSession(completed, "COMPLETED", "done", &error), qPrintable(error));
    }
    WorkspaceRepository reopened(databasePath);
    QVERIFY2(reopened.open(&error), qPrintable(error));
    QCOMPARE(reopened.recoverInterruptedSessions(&error), 1);
    QCOMPARE(reopened.recoverInterruptedSessions(&error), 0);
    const auto sessions = reopened.recentAcquisitionSessions();
    QCOMPARE(sessions.size(), 2);
    int interruptedCount = 0;
    int completedCount = 0;
    for (const auto &session : sessions) {
        if (session.status == "INTERRUPTED") ++interruptedCount;
        if (session.status == "COMPLETED") ++completedCount;
    }
    QCOMPARE(interruptedCount, 1);
    QCOMPARE(completedCount, 1);
}

void WorkspaceTests::sustainsBoundedSixMonthHistoryReads() {
    QTemporaryDir dir;
    const QString databasePath = dir.filePath("workspace.sqlite");
    QString error;
    {
        WorkspaceRepository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
        const QVector<SpectrumPoint> raw{{100, 5}, {200, 50}};
        for (int day = 0; day < 186; ++day) {
            auto run = fixtureRun();
            run.id = QString("six-month-%1").arg(day, 3, 10, QLatin1Char('0'));
            run.completedAt = QDateTime::currentDateTimeUtc().addDays(-day);
            QVERIFY2(repository.saveCompletedRun(run, raw, fixtureResult(), fixtureTelemetry(), &error), qPrintable(error));
        }
        const auto visibleWindow = repository.recentRuns(30);
        QCOMPARE(visibleWindow.size(), 30);
        QCOMPARE(visibleWindow.first().id, QString("six-month-000"));
    }
    WorkspaceRepository reopened(databasePath);
    QVERIFY2(reopened.open(&error), qPrintable(error));
    QCOMPARE(reopened.recentRuns(30).size(), 30);
    QVERIFY(reopened.loadRun("six-month-185").valid);
}

void WorkspaceTests::reopensAfterUncleanShutdownMarker() {
    QTemporaryDir dir;
    const QString databasePath = dir.filePath("workspace.sqlite");
    QString error;
    {
        WorkspaceRepository repository(databasePath);
        QVERIFY2(repository.open(&error), qPrintable(error));
    }
    const QString connection = "force-unclean-marker";
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec("UPDATE workspace_meta SET value='0' WHERE key='clean_shutdown'"));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    WorkspaceRepository recovered(databasePath);
    QVERIFY2(recovered.open(&error), qPrintable(error));
    QVERIFY(recovered.recentRuns().isEmpty());
}

void WorkspaceTests::writesPrivacySafeDiagnosticBundle() {
    QTemporaryDir dir;
    const QString path = dir.filePath("diagnostic.json");
    QString error;
    QVERIFY2(DiagnosticBundle::write(path, {{"application", QJsonObject{{"version", "test"}}}}, &error),
        qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(object.value("schema").toString(), QString("qitest-diagnostic-1"));
    const auto privacy = object.value("privacy").toObject();
    QCOMPARE(privacy.value("contains_passwords").toBool(), false);
    QCOMPARE(privacy.value("contains_raw_spectrum").toBool(), false);
    QCOMPARE(privacy.value("contains_ai_prompts").toBool(), false);
    QCOMPARE(privacy.value("contains_candidate_names").toBool(), false);
}

void WorkspaceTests::importsBatchesWithoutDuplicatesOrMainThreadBlocking() {
    QTemporaryDir dir;
    const QString db = dir.filePath("batch.sqlite");
    QString error;
    WorkspaceRepository mainConnection(db);
    QVERIFY(mainConnection.open(&error));
    StoredRunDetail detail;
    detail.valid = true;
    detail.summary = fixtureRun();
    detail.result = fixtureResult();
    for (int i = 0; i < 256; ++i) detail.rawSpectrum.append({50.0 + i, 5.0 + (i % 30)});
    QStringList paths;
    for (int i = 0; i < 200; ++i) {
        detail.summary.id = QString("batch-source-%1").arg(i);
        const QString path = dir.filePath(detail.summary.id + ".qit.json");
        QVERIFY2(RunArchiveCodec::write(path, detail, &error), qPrintable(error));
        paths.append(path);
    }
    const auto connectionCount = QSqlDatabase::connectionNames().size();
    int heartbeat = 0;
    QTimer heartbeatTimer;
    connect(&heartbeatTimer, &QTimer::timeout, this, [&] { ++heartbeat; });
    heartbeatTimer.start(1);
    const AnalysisEngine engine(demoReferences(), "test");
    ArchiveImportWorker worker(paths, db, "tester", engine, {}, fixtureTelemetry());
    worker.start();
    QTRY_VERIFY_WITH_TIMEOUT(worker.isFinished(), 30000);
    worker.wait();
    QVERIFY(heartbeat > 1);
    QVERIFY2(worker.summary().contains("导入 200"), qPrintable(worker.summary()));
    QCOMPARE(mainConnection.recentRuns(1000).size(), 200);
    QCOMPARE(mainConnection.recentRuns(30).size(), 30);
    ArchiveImportWorker duplicate(paths, db, "tester", engine, {}, fixtureTelemetry());
    duplicate.start();
    QTRY_VERIFY_WITH_TIMEOUT(duplicate.isFinished(), 10000);
    duplicate.wait();
    QVERIFY(duplicate.summary().contains("重复跳过 200"));
    QCOMPARE(mainConnection.recentRuns(1000).size(), 200);
    QCOMPARE(QSqlDatabase::connectionNames().size(), connectionCount);
    ArchiveImportWorker invalid({dir.filePath("missing.json")}, db, "tester", engine, {}, {});
    invalid.start();
    QTRY_VERIFY_WITH_TIMEOUT(invalid.isFinished(), 5000);
    invalid.wait();
    QVERIFY(invalid.summary().contains("失败 1"));
    const QString cancelledDb = dir.filePath("cancelled.sqlite");
    ArchiveImportWorker cancelled(paths, cancelledDb, "tester", engine, {}, fixtureTelemetry());
    connect(&cancelled, &ArchiveImportWorker::progress, &cancelled,
        [&cancelled](int done, int, const QString &) { if (done == 3) cancelled.requestInterruption(); },
        Qt::DirectConnection);
    cancelled.start();
    QTRY_VERIFY_WITH_TIMEOUT(cancelled.isFinished(), 10000);
    cancelled.wait();
    QVERIFY(cancelled.summary().contains("导入 3"));
    WorkspaceRepository reopened(cancelledDb);
    QVERIFY(reopened.open(&error));
    QCOMPARE(reopened.recentRuns().size(), 3);
    const auto saved = reopened.recentRuns().first();
    QVERIFY(reopened.loadRun(saved.id).valid);
    // A worker closing must not mark the still-running main session as clean.
    const QString inspectName = "batch-inspect";
    {
        auto inspect = QSqlDatabase::addDatabase("QSQLITE", inspectName);
        inspect.setDatabaseName(db);
        QVERIFY(inspect.open());
        QSqlQuery query(inspect);
        QVERIFY(query.exec("SELECT value FROM workspace_meta WHERE key='clean_shutdown'"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString("0"));
        QVERIFY(query.exec("PRAGMA integrity_check"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString("ok"));
    }
    QSqlDatabase::removeDatabase(inspectName);
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    WorkspaceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "WorkspaceTests.moc"
