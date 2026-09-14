#pragma once
#include <QDialog>
#include <QJsonObject>
#include <functional>
namespace qitest {
class MethodEditorDialog final : public QDialog {
public:
    MethodEditorDialog(const QString &name, const QJsonObject &values,
        std::function<bool(const QString &, const QJsonObject &)> saveAs, QWidget *parent=nullptr,
        bool fullAccess=true,
        std::function<bool(const QString &, const QJsonObject &)> saveCurrent = {});
    void reject() override;
private:
    bool dirty_=false;
};
}
