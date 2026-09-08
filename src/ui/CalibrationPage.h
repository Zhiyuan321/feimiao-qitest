#pragma once
#include "core/CalibrationModel.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace qitest {
class SpectrumPlot;
class CalibrationPage final : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationPage(QWidget *parent = nullptr);
    bool loadFile(const QString &path, QString *error);
    bool saveFile(const QString &path, QString *error);
    const CalibrationModel &model() const { return model_; }
private:
    void applyModel();
    void refreshFit();
    void editPoint(int row);
    void calculateSample();
    CalibrationModel model_;
    quint64 modelRevision_ = 0;
    QLineEdit *name_, *unit_, *responseUnit_, *internalName_;
    QComboBox *standard_, *weight_;
    QTableWidget *table_;
    SpectrumPlot *plot_;
    QLabel *status_;
    QPushButton *save_, *calculate_;
};
}
