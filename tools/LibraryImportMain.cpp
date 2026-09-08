#include "library/SpectralLibraryRepository.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (argc != 4) {
        err << "usage: qitest_library_import <database> <jcamp> <sha256>\n";
        return 2;
    }
    qitest::SpectralLibraryRepository repository(QString::fromLocal8Bit(argv[1]));
    QString error;
    if (!repository.open(&error)) { err << error << '\n'; return 1; }
    const qitest::LibrarySourceMetadata metadata{
        "SWGDRUG", "3.14", "https://swgdrug.org/ms.htm", QString::fromLatin1(argv[3]),
        "EI", "downloadable-for-laboratory-use; redistribution-review-required"
    };
    const auto result = repository.importJcamp(QString::fromLocal8Bit(argv[2]), metadata, &error);
    if (!error.isEmpty()) { err << error << '\n'; return 1; }
    out << "compounds=" << result.compounds << " spectra=" << result.spectra << " peaks=" << result.peaks << '\n';
    return result.spectra > 0 ? 0 : 1;
}
