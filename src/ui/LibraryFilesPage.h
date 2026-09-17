#pragma once
#include "library/LibraryFile.h"
#include <QDialog>
#include <QWidget>
class QTableWidget;
class QLabel;
class QLineEdit;
namespace qitest {
class LibraryFileEditor final : public QDialog {
    Q_OBJECT
public:
    explicit LibraryFileEditor(QString path,QWidget *parent=nullptr);
    bool loaded() const {return loaded_;}
    bool saveTo(const QString &path,bool switchFile,QString *error);
    void reject() override;
signals:
    void fileSaved(const QString &path);
private:
    void refresh();
    void editEntry(bool create);
    void chooseSave(bool switchFile);
    QString path_;
    QByteArray digest_;
    QJsonArray entries_;
    bool loaded_=false,dirty_=false;
    QTableWidget *table_;
    QLabel *status_,*pathLabel_;
};
class LibraryFilesPage final : public QWidget {
    Q_OBJECT
public:
    explicit LibraryFilesPage(QString catalogPath,QWidget *parent=nullptr);
    bool importFile(const QString &path,QString *error);
    bool createFile(const QString &path,QString *error);
    bool removeFile(const QString &path,QString *error);
    void openEditor(const QString &path);
private:
    void refresh();
    LibraryFileCatalog catalog_;
    QTableWidget *table_;
    QLineEdit *search_;
    QLabel *status_;
};
}
