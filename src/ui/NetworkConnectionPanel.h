#pragma once
#include <QWidget>
namespace qitest {
class AppController;
class NetworkConnectionPanel final : public QWidget {
public:
    explicit NetworkConnectionPanel(AppController *controller, QWidget *parent = nullptr);
};
}
