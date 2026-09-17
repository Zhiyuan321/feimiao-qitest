#pragma once
#include "domain/Models.h"
#include <QDialog>

namespace qitest {
// Frozen current-record view; never starts acquisition or substitutes example data.
class ResultSpectrumDialog final : public QDialog {
public:
    ResultSpectrumDialog(QVector<SpectrumScan> scans, QVector<SpectrumPoint> spectrum,
                         const QString &recordLabel, QWidget *parent = nullptr, double detectionSeconds = 0);
};
}
