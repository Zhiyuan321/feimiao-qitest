#pragma once
#include <QDialog>
#include <QJsonObject>
#include <functional>
namespace qitest {
class MethodEditorDialog final : public QDialog {
public:
    MethodEditorDialog(const QString &name, const QJsonObject &values,
        std::function<bool(const QString &, const QJsonObject &)> save, QWidget *parent=nullptr);
    void reject() override;
private:
    bool dirty_=false;
};
}
