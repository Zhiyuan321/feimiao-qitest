#include "library/SpectralLibraryRepository.h"
#include "library/UserStandardRepository.h"
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
    void importsJcampWithProvenance();
    void opensImportedLibraryReadOnly();
    void searchesImportedCompounds();
    void userStandardsAreVersionedBoundedAndIsolated();
};

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
