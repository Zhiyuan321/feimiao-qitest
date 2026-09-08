#pragma once
#include <QWidget>
namespace qitest {
// Local worksheets with explicit disconnected state. Never dispatch hardware.
QWidget *createInstrumentWorkbench(const QString &kind,QWidget *parent=nullptr);
}
