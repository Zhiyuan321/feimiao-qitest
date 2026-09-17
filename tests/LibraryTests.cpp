#include "library/SpectralLibraryRepository.h"
#include "library/UserStandardRepository.h"
#include "library/LibraryFile.h"
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qitest;

class LibraryTests final : public QObject {
    Q_OBJECT
private slots:
    void legacyLibRoundtripAndCatalog();
    void legacyThresholdLists();
    void externalLegacyLibSamples();
    void importsJcampWithProvenance();
    void opensImportedLibraryReadOnly();
    void searchesImportedCompounds();
    void userStandardsAreVersionedBoundedAndIsolated();
};

void LibraryTests::legacyLibRoundtripAndCatalog() {
    QTemporaryDir dir;QString error;const auto path=dir.filePath("测试谱库.lib");
    const QJsonObject entry{{"id","17"},{"name","测试物质"},{"cas","76-99-3"},
        {"parent_ion","310"},{"qualitify_ion","265,310"},{"quantify_ion","265,310"},
        {"sample_category","精神药物"},{"internal_flag","否"},{"son_area","10.25"},{"msms_son_area","20"},
        {"internal",QJsonObject{{"a",1.23456789},{"b",2.5}}},{"external",QJsonObject{{"a",3},{"b",4}}},
        {"vendor_extra",QJsonObject{{"keep","unchanged"}}}};
    QJsonArray rows{entry};QVERIFY(LibraryFile::write(path,rows,&error));
    QJsonArray read;QByteArray hash;QVERIFY(LibraryFile::read(path,&read,&error,&hash));QCOMPARE(read,rows);
    QCOMPARE(hash,LibraryFile::digest(path));
    auto modified=read[0].toObject();modified["name"]="修改名称";read[0]=modified;
    const auto other=dir.filePath("另存.LIB");QVERIFY(LibraryFile::write(other,read,&error));
    QJsonArray again;QVERIFY(LibraryFile::read(other,&again,&error));QCOMPARE(again,read);
    QCOMPARE(again[0].toObject()["internal"],entry["internal"]);QCOMPARE(again[0].toObject()["vendor_extra"],entry["vendor_extra"]);
    auto bad=modified;bad["parent_ion"]="nan";QVERIFY(!LibraryFile::write(other,QJsonArray{bad},&error));
    QVERIFY(LibraryFile::read(other,&again,&error));QCOMPARE(again,read);
    bad=modified;bad["qualitify_ion"]="1,,2";QVERIFY(!LibraryFile::validateEntry(bad,&error));
    bad=modified;bad["son_area"]="-1";QVERIFY(!LibraryFile::validateEntry(bad,&error));
    QVERIFY(!LibraryFile::write(dir.filePath("wrong.json"),rows,&error));
    const auto empty=dir.filePath("empty.lib");QVERIFY(LibraryFile::write(empty,{},&error));QVERIFY(LibraryFile::read(empty,&again,&error));QVERIFY(again.isEmpty());
    LibraryFileCatalog catalog(dir.filePath("catalog.json"));QVERIFY(catalog.open(&error));
    QVERIFY(catalog.add(path,&error));QVERIFY(catalog.add(path,&error));QCOMPARE(catalog.paths().size(),1);
    LibraryFileCatalog reopened(dir.filePath("catalog.json"));QVERIFY(reopened.open(&error));QCOMPARE(reopened.paths(),catalog.paths());
    QVERIFY(reopened.remove(reopened.paths().first(),&error));QVERIFY(QFile::exists(path));
    const auto broken=dir.filePath("broken.lib");QFile file(broken);QVERIFY(file.open(QIODevice::WriteOnly));file.write("{broken");file.close();
    again=rows;QVERIFY(!LibraryFile::read(broken,&again,&error));QCOMPARE(again,rows);
    QVERIFY(!catalog.add(broken,&error));QCOMPARE(catalog.paths().size(),1);
    QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray(LibraryFile::MaximumBytes+1,' '));file.close();
    QVERIFY(!LibraryFile::read(broken,&again,&error));QCOMPARE(again,rows);
}

void LibraryTests::legacyThresholdLists() {
    QTemporaryDir dir;QString error;
    for(const auto &threshold:QStringList{"35000","0,25000","0,0,1500","0,0,0,2000"}) {
        const QJsonObject entry{{"name","合成兼容测试"},{"parent_ion","238.1"},
            {"qualitify_ion","220,238.10"},{"quantify_ion","238.10"},
            {"son_area",threshold},{"msms_son_area",threshold},{"scan_flag",true}};
        const auto path=dir.filePath("thresholds.lib");
        QVERIFY2(LibraryFile::write(path,QJsonArray{entry},&error),qPrintable(error));
        QJsonArray rows;QVERIFY2(LibraryFile::read(path,&rows,&error),qPrintable(error));
        QCOMPARE(rows,QJsonArray{entry});
        LibraryFileCatalog catalog(dir.filePath("catalog.json"));
        QVERIFY(catalog.open(&error));QVERIFY2(catalog.add(path,&error),qPrintable(error));
        for(const auto &key:QStringList{"son_area","msms_son_area"}) {
            for(const auto &invalid:QStringList{"0,,10",",10","10,","0,-1","0,nan","0,inf"}) {
                auto bad=entry;bad[key]=invalid;
                QVERIFY(!LibraryFile::write(path,QJsonArray{bad},&error));
                QVERIFY(LibraryFile::read(path,&rows,&error));QCOMPARE(rows,QJsonArray{entry});
            }
        }
        auto bad=entry;bad["parent_ion"]="1,2";QVERIFY(!LibraryFile::validateEntry(bad,&error));
    }
}

void LibraryTests::externalLegacyLibSamples() {
    const auto manifest=qgetenv("QITEST_LIB_SAMPLE_MANIFEST");
    if(manifest.isEmpty())QSKIP("Optional customer samples remain outside the repository");
    QFile file(QString::fromUtf8(manifest));QVERIFY(file.open(QIODevice::ReadOnly));
    const auto samples=QJsonDocument::fromJson(file.readAll()).array();QVERIFY(!samples.isEmpty());
    QTemporaryDir dir;QString error;LibraryFileCatalog catalog(dir.filePath("catalog.json"));
    QVERIFY(catalog.open(&error));
    int index=0;
    for(const auto &sample:samples) {
        const auto path=sample.toString();const auto before=LibraryFile::digest(path);
        QJsonArray rows;QVERIFY2(LibraryFile::read(path,&rows,&error),qPrintable(path+": "+error));
        QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(rows,QJsonDocument::fromJson(original.readAll()).array());original.close();
        QVERIFY2(catalog.add(path,&error),qPrintable(error));
        const auto copy=dir.filePath(QString("copy-%1.lib").arg(++index));
        QVERIFY2(LibraryFile::write(copy,rows,&error),qPrintable(error));
        QJsonArray reloaded;QVERIFY(LibraryFile::read(copy,&reloaded,&error));QCOMPARE(reloaded,rows);
        QCOMPARE(LibraryFile::digest(path),before);
        qInfo()<<QFileInfo(path).fileName()<<rows.size()<<"entries: import and lossless roundtrip passed";
    }
    QCOMPARE(catalog.paths().size(),samples.size());
}

void LibraryTests::userStandardsAreVersionedBoundedAndIsolated() {
    QTemporaryDir dir; QString error;
    const auto path=dir.filePath("standards.sqlite");
    UserStandard first; first.name="用户标准 A"; first.cas="64-17-5"; first.ionization="EI";
    first.provenance="synthetic test only"; first.peaks={{31,5000},{46,9999}};
    {
        UserStandardRepository repo(path); QVERIFY2(repo.open(&error),qPrintable(error));
        QVERIFY2(repo.save(&first,&error),qPrintable(error)); QCOMPARE(first.revision,1);
        QVERIFY(repo.save(&first,&error)); QCOMPARE(first.revision,1); // No new history for unchanged saves.
        UserStandard duplicate=first; duplicate.id.clear(); duplicate.revision=0;
        QVERIFY(repo.save(&duplicate,&error)); QCOMPARE(duplicate.id,first.id); QCOMPARE(repo.count(&error),1);
        UserStandard stale=first; first.category="新类别";
        QVERIFY(repo.save(&first,&error)); QCOMPARE(first.revision,2);
        stale.name="过期编辑"; QVERIFY(!repo.save(&stale,&error)); QVERIFY(error.contains("更新"));
        UserStandard loaded; QVERIFY(repo.load(first.id,&loaded,&error)); QCOMPARE(loaded.name,first.name); QCOMPARE(loaded.category,first.category);
        loaded.peaks.append({31,1}); QVERIFY(!repo.save(&loaded,&error));
        QVERIFY(repo.load(first.id,&loaded,&error)); QCOMPARE(loaded.peaks.size(),2);
        const auto archive=dir.filePath("standard.qstd.json");
        QVERIFY(UserStandardRepository::writeFile(archive,loaded,&error));
        UserStandard imported; QVERIFY(UserStandardRepository::readFile(archive,&imported,&error));
        QVERIFY(imported.id.isEmpty()); QCOMPARE(imported.peaks[1].intensity,9999.0);
        QVERIFY(repo.save(&imported,&error)); QCOMPARE(repo.count(&error),1);
        QFile corrupt(archive); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("{}"); corrupt.close();
        const auto previous=imported.name; QVERIFY(!UserStandardRepository::readFile(archive,&imported,&error)); QCOMPARE(imported.name,previous);
        QVector<SpectrumPoint> peaks=first.peaks;
        QVERIFY(!UserStandardRepository::parsePeaks("mz,intensity\n1,,2",&peaks,&error)); QCOMPARE(peaks.size(),2);
        QVERIFY(!UserStandardRepository::parsePeaks("1,nan\n2,3",&peaks,&error));
        QVERIFY(UserStandardRepository::parsePeaks("mz,intensity\n1.25,0\n2.5,8",&peaks,&error));
        QCOMPARE(peaks[0].mz,1.25);
        for(int i=0;i<52;++i) {
            UserStandard row=first; row.id.clear(); row.revision=0; row.name=QString("page %1").arg(i);
            QVERIFY(repo.save(&row,&error));
        }
        QCOMPARE(repo.search("",0,&error).size(),50); QCOMPARE(repo.search("",50,&error).size(),3);
        QCOMPARE(repo.search("64-17",0,&error).size(),50);
    }
    { UserStandardRepository reopened(path); QVERIFY(reopened.open(&error)); UserStandard saved;
      QVERIFY(reopened.load(first.id,&saved,&error)); QCOMPARE(saved.revision,2); QCOMPARE(saved.category,first.category); }
    const auto publicPath=dir.filePath("public.sqlite");
    { SpectralLibraryRepository publicRepo(publicPath); QVERIFY(publicRepo.open(&error)); }
    { UserStandardRepository wrong(publicPath); QVERIFY(!wrong.open(&error)); QVERIFY(error.contains("拒绝")); QVERIFY(!wrong.save(&first,&error)); }
    { SpectralLibraryRepository publicRepo(publicPath); QVERIFY(publicRepo.openReadOnly(&error)); QCOMPARE(publicRepo.spectrumCount(),0); }
}

void LibraryTests::importsJcampWithProvenance() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fixture = directory.filePath("fixture.hpj");
    QFile file(fixture);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("##TITLE=Library Entry 1\n##CAS NAME=Test compound\n##MOLFORM=C2H6O\n"
               "##CAS REGISTRY NO=64-17-5\n##MW=46\n##XYDATA=(XY..XY)\n46 9999\n31 5000\n##END=\n");
    file.close();
    SpectralLibraryRepository repository(directory.filePath("library.sqlite"));
    QString error;
    QVERIFY2(repository.open(&error), qPrintable(error));
    const auto result = repository.importJcamp(fixture, {"fixture", "1", "local", "abc", "EI", "test"}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.compounds, 1);
    QCOMPARE(result.spectra, 1);
    QCOMPARE(result.peaks, 2);
    QCOMPARE(repository.spectrumCount(), 1);
    QVERIFY(repository.sourceSummary().contains("fixture 1"));
}

void LibraryTests::opensImportedLibraryReadOnly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath("library.sqlite");
    {
        SpectralLibraryRepository writable(databasePath);
        QString error;
        QVERIFY2(writable.open(&error), qPrintable(error));
    }
    SpectralLibraryRepository readOnly(databasePath);
    QString error;
    QVERIFY2(readOnly.openReadOnly(&error), qPrintable(error));
    QCOMPARE(readOnly.spectrumCount(), 0);
}

void LibraryTests::searchesImportedCompounds() {
    QTemporaryDir directory;
    const QString fixture = directory.filePath("fixture.hpj");
    QFile file(fixture);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("##TITLE=Ethanol entry\n##CAS NAME=Ethanol\n##MOLFORM=C2H6O\n"
               "##CAS REGISTRY NO=64-17-5\n##MW=46\n##XYDATA=(XY..XY)\n46 9999\n31 5000\n##END=\n");
    file.close();
    const QString databasePath = directory.filePath("library.sqlite");
    {
        SpectralLibraryRepository writable(databasePath);
        QString error;
        QVERIFY(writable.open(&error));
        QCOMPARE(writable.importJcamp(fixture, {"fixture", "1", "local", "abc", "EI", "test"}, &error).spectra, 1);
    }
    SpectralLibraryRepository readOnly(databasePath);
    QString error;
    QVERIFY(readOnly.openReadOnly(&error));
    const auto results = readOnly.searchCompounds("64-17");
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().name, QString("Ethanol"));
    QCOMPARE(results.first().peakCount, 2);
}

QTEST_GUILESS_MAIN(LibraryTests)
#include "LibraryTests.moc"
