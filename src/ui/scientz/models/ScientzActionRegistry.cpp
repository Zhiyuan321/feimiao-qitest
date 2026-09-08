#include "ui/scientz/models/ScientzActionRegistry.h"

#include <QAction>

namespace Scientz::Ui {

ActionRegistry::ActionRegistry(QObject *parent) : QObject(parent) {}

QAction *ActionRegistry::registerAction(
    const QString &id,
    const QString &text,
    const QIcon &icon,
    const QKeySequence &shortcut) {
    if (actions_.contains(id)) return actions_.value(id);
    auto *action = new QAction(icon, text, this);
    action->setObjectName(id);
    action->setShortcut(shortcut);
    actions_.insert(id, action);
    return action;
}

QAction *ActionRegistry::action(const QString &id) const { return actions_.value(id); }

QList<QAction *> ActionRegistry::actions() const { return actions_.values(); }

} // namespace Scientz::Ui
