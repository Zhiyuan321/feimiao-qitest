#pragma once

#include <QString>
#include <QVector>

namespace qitest {

struct KnowledgeHit {
    QString title;
    QString text;
    int score = 0;
};

class LocalKnowledgeStore final {
public:
    bool loadMarkdown(const QString &path, QString *error = nullptr);
    QVector<KnowledgeHit> search(const QString &query, int limit = 4) const;
    QString contextFor(const QString &query, int limit = 4, int characterBudget = 6000) const;
    int chunkCount() const { return chunks_.size(); }

private:
    QVector<KnowledgeHit> chunks_;
};

} // namespace qitest
