#include "ui/ChatTranscript.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QResizeEvent>

namespace qitest {

ChatTranscript::ChatTranscript(QWidget *parent) : QScrollArea(parent),
    body_(new QWidget), layout_(new QVBoxLayout(body_)), scrollTimer_(new QTimer(this)) {
    setObjectName("assistantTranscript");
    setAccessibleName("对话记录：智能台在左，你的消息在右");
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(0);
    setFrameShape(QFrame::NoFrame);
    layout_->setContentsMargins(0, 8, 4, 8);
    layout_->setSpacing(12);
    layout_->addStretch();
    setWidget(body_);
    scrollTimer_->setSingleShot(true);
    connect(scrollTimer_, &QTimer::timeout, this, [this] {
        scrollingToEnd_ = true;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        scrollingToEnd_ = false;
    });
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
        if (followEnd_) scrollTimer_->start(0);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!scrollingToEnd_ && !scrollTimer_->isActive())
            followEnd_ = verticalScrollBar()->maximum() - verticalScrollBar()->value() < 24;
    });
}

void ChatTranscript::appendMessage(Role role, const QString &text) {
    if (text.trimmed().isEmpty()) return;
    const bool followEnd = role == Role::User || scrollTimer_->isActive()
        || verticalScrollBar()->maximum() - verticalScrollBar()->value() < 24;
    followEnd_ = followEnd;
    // Model responses are already bounded; cap any external diagnostic as well.
    const QString displayed = text.size() <= 16384 ? text
        : text.left(16384) + "\n（内容过长，仅显示前 16384 个字符）";
    while (!messages_.isEmpty() && (messages_.size() >= MaximumMessages
           || characters_ + displayed.size() > MaximumCharacters)) {
        const auto oldest = messages_.takeFirst();
        characters_ -= oldest.characters;
        delete oldest.row; // synchronous release: repeated inserts cannot queue deletions
    }
    const bool user = role == Role::User;
    auto *row = new QWidget(body_);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(0);
    auto *bubble = new QLabel(displayed, row);
    bubble->setObjectName(user ? "userMessage" : "assistantMessage");
    bubble->setProperty("chatRole", user ? "user" : "assistant");
    bubble->setTextFormat(Qt::PlainText); // never execute/render model-supplied HTML
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    bubble->setFocusPolicy(Qt::ClickFocus);
    bubble->setAccessibleName(user ? "你的消息" : "智能台回复");
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    if (user) line->addStretch(1);
    line->addWidget(bubble, 0, user ? Qt::AlignRight : Qt::AlignLeft);
    if (!user) line->addStretch(1);
    layout_->insertWidget(layout_->count() - 1, row);
    messages_.append({row, bubble, static_cast<int>(displayed.size())});
    characters_ += displayed.size();
    resizeBubbles();
    if (followEnd) scrollTimer_->start(0);
}

void ChatTranscript::clear() {
    scrollTimer_->stop();
    followEnd_ = true;
    for (const auto &message : messages_) delete message.row;
    messages_.clear();
    characters_ = 0;
}

void ChatTranscript::resizeEvent(QResizeEvent *event) {
    QScrollArea::resizeEvent(event);
    resizeBubbles();
}

void ChatTranscript::resizeBubbles() {
    const int bubbleWidth = qMax(48, viewport()->width() - 32);
    for (const auto &message : messages_)
        if (message.bubble->maximumWidth() != bubbleWidth)
            message.bubble->setMaximumWidth(bubbleWidth);
}

} // namespace qitest
