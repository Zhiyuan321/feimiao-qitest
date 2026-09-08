#pragma once
#include "library/UserStandardRepository.h"
#include <QDialog>

namespace qitest {
struct StandardComparisonInput {
    QVector<SpectrumPoint> peaks;
    QString recordId, description;
};
class StandardComparisonDialog final : public QDialog {
    Q_OBJECT
public:
    StandardComparisonDialog(UserStandard reference, StandardComparisonInput query, QWidget *parent);
    bool exportEvidence(const QString &path, QString *error) const;
private:
    QByteArray evidence_;
};
}
