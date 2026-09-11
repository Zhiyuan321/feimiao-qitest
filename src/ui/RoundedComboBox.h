#pragma once

#include <QComboBox>
#include <QListView>
#include <QRegion>
#include <QTimer>
#include <QWheelEvent>

namespace qitest {

// 统一选择框的列表弹层。弹层和选项使用直角，避免 Qt 5 在
// macOS 与 Windows 7 上出现“方形外框+圆角选中块”的混合样式。
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
        // The popup is a separate native window.  Styling the QListView alone
        // does not remove macOS' window corner mask, so force the container to
        // use the same rectangular outline as its rows on every platform.
        QTimer::singleShot(0, this, [this] {
            QWidget *popup = view() ? view()->window() : nullptr;
            if (!popup || popup->rect().isEmpty()) return;
            popup->setMask(QRegion(popup->rect()));
        });
    }

    void wheelEvent(QWheelEvent *event) override {
        // A closed selector must not change merely because the pointer crossed
        // it while the user was scrolling the settings page.
        event->ignore();
    }
};

} // namespace qitest
