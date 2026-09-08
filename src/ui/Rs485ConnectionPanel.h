#pragma once
#include <QWidget>

namespace qitest {
class AppController;
class Rs485ConnectionPanel final : public QWidget {
public:
    explicit Rs485ConnectionPanel(AppController *controller, QWidget *parent = nullptr);
};
}
