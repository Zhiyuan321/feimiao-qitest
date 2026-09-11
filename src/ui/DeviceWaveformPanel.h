#pragma once
#include <QWidget>
namespace qitest {
class AppController;
QWidget *createDeviceWaveformPanel(AppController *controller, bool tuning, QWidget *parent=nullptr);
}
