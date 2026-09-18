#pragma once
#include <QWidget>
namespace qitest {
class AppController;
// Calibration synchronization is persisted through the controller; method transmission remains explicit.
QWidget *createInstrumentWorkbench(const QString &kind,QWidget *parent=nullptr,AppController *controller=nullptr);
}
