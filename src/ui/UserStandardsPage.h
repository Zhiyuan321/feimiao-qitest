#pragma once
#include "library/UserStandardRepository.h"
#include "ui/StandardComparisonDialog.h"
#include <QWidget>
#include <memory>
#include <functional>
class QTableWidget;
class QLineEdit;
class QLabel;
class QPushButton;
namespace qitest {
class SpectrumPlot;
class UserStandardsPage final : public QWidget {
public:
    explicit UserStandardsPage(QString databasePath, QWidget *parent=nullptr);
    bool importFile(const QString &path, QString *error);
    bool exportSelected(const QString &path, QString *error);
    void setComparisonProvider(std::function<StandardComparisonInput()> provider);
private:
    void refresh();
    void selectCurrent();
    void edit(bool create);
    std::unique_ptr<UserStandardRepository> repository_;
    QTableWidget *table_;
    QLineEdit *search_;
    QLabel *status_;
    QPushButton *edit_, *export_, *previous_, *next_;
    QPushButton *compare_;
    std::function<StandardComparisonInput()> comparisonProvider_;
    SpectrumPlot *plot_;
    UserStandard selected_;
    int offset_=0;
};
}
