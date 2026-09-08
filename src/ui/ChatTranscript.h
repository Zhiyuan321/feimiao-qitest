#pragma once

#include <QScrollArea>
#include <QList>

class QLabel;
class QVBoxLayout;
class QTimer;

namespace qitest {

// A bounded, selectable native-widget transcript. No web engine, remote assets,
// disk history, animation loop or per-token rebuilding of the whole conversation.
class ChatTranscript final : public QScrollArea {
    Q_OBJECT
public:
    enum class Role { User, Assistant };
    explicit ChatTranscript(QWidget *parent = nullptr);
    void appendMessage(Role role, const QString &text);
    void clear();
    int messageCount() const { return messages_.size(); }
    static constexpr int MaximumMessages = 80;
    static constexpr int MaximumCharacters = 65536;
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    struct Message { QWidget *row; QLabel *bubble; int characters; };
    QWidget *body_;
    QVBoxLayout *layout_;
    QTimer *scrollTimer_;
    QList<Message> messages_;
    int characters_ = 0;
    bool followEnd_ = true;
    bool scrollingToEnd_ = false;
    void resizeBubbles();
};

} // namespace qitest
