#pragma once

#include <QComboBox>
#include <QListView>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>

namespace qitest {

// 统一选择框的列表弹层。使用简单遮罩实现圆角，不引入阴影或逐帧动画，
// 以保证 Windows 7 低配设备上的点击反馈稳定。
class RoundedComboBox final : public QComboBox {
public:
    explicit RoundedComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        auto *list = new QListView(this);
        list->setUniformItemSizes(true);
        list->setSpacing(2);
        setView(list);
    }

protected:
    void showPopup() override {
        QComboBox::showPopup();
        QTimer::singleShot(0, this, [this] {
            QWidget *popup = view() ? view()->window() : nullptr;
            if (!popup || popup->rect().isEmpty()) return;
            QPainterPath outline;
            outline.addRoundedRect(QRectF(popup->rect()).adjusted(0, 0, -1, -1), 10, 10);
            popup->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        });
    }
};

} // namespace qitest
