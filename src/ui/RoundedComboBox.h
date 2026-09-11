#pragma once

#include <QComboBox>
#include <QListView>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <QWheelEvent>

namespace qitest {

// 统一选择框的列表弹层，并给独立原生弹层施加同一圆角轮廓。
class RoundedComboBox final : public QComboBox {
public:
    explicit RoundedComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        auto *list = new QListView(this);
        list->setUniformItemSizes(true);
        list->setSpacing(0);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setView(list);
        setMaxVisibleItems(12);
    }

protected:
    void showPopup() override {
        const int visibleRows = qMin(count(), maxVisibleItems());
        if (view() && visibleRows > 0) view()->setMinimumHeight(visibleRows * 40 + 2);
        QComboBox::showPopup();
        // The popup is a separate native window; mask its container as well as
        // styling the list, otherwise Windows/Wine shows a square outer block.
        QTimer::singleShot(0, this, [this] {
            QWidget *popup = view() ? view()->window() : nullptr;
            if (!popup || popup->rect().isEmpty()) return;
            QPainterPath outline;
            outline.addRoundedRect(QRectF(popup->rect()), 10, 10);
            popup->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        });
    }

    void wheelEvent(QWheelEvent *event) override {
        // A closed selector must not change merely because the pointer crossed
        // it while the user was scrolling the settings page.
        event->ignore();
    }
};

} // namespace qitest
