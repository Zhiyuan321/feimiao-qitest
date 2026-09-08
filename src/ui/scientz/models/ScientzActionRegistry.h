#pragma once

#include <QObject>
#include <QHash>
#include <QIcon>
#include <QKeySequence>

class QAction;

namespace Scientz::Ui {

class ActionRegistry final : public QObject {
    Q_OBJECT
public:
    explicit ActionRegistry(QObject *parent = nullptr);

    QAction *registerAction(
        const QString &id,
        const QString &text,
        const QIcon &icon = {},
        const QKeySequence &shortcut = {});
    QAction *action(const QString &id) const;
    QList<QAction *> actions() const;

private:
    QHash<QString, QAction *> actions_;
};

} // namespace Scientz::Ui
