#pragma once
#include "domain/Models.h"
#include <QDialog>
#include <functional>
namespace qitest {
// Offline analysis of measured/imported scans. No device commands or model calls.
class ChromatogramDialog final : public QDialog {
public:
    explicit ChromatogramDialog(QVector<SpectrumScan> scans, QWidget *parent = nullptr);
    bool saveFile(const QString &path, QString *error);
    bool loadFile(const QString &path, QString *error);
private:
    // UI-bound operations use the same validated path as native file dialogs.
    std::function<bool(const QString &, QString *)> saveRecord_, loadRecord_;
};
}
