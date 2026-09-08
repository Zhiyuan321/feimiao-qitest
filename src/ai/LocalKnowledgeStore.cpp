#include "ai/LocalKnowledgeStore.h"
#include "core/QtCompat.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>

namespace qitest {
namespace {

QStringList searchTerms(QString text) {
    text = text.toLower().simplified();
    text.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")));
    QStringList terms;
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        const QString term = text.mid(i, 2);
        if (!terms.contains(term)) terms.append(term);
    }
    const auto words = text.split(QRegularExpression(QStringLiteral("\\s+")), skipEmptyParts);
    for (const auto &word : words)
        if (word.size() >= 2 && !terms.contains(word)) terms.append(word);
    return terms;
}

} // namespace

bool LocalKnowledgeStore::loadMarkdown(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    // The shipped manual is UTF-8; Qt 5's platform locale on Windows is not.
    const QString source = QString::fromUtf8(file.readAll());
    const QStringList lines = source.split('\n');
    QString title;
    QString body;
    QVector<KnowledgeHit> loadedChunks;
    const auto flush = [&loadedChunks, &title, &body] {
        const QString clean = body.trimmed();
        if (!title.isEmpty() && !clean.isEmpty()) loadedChunks.append({title, clean, 0});
        body.clear();
    };
    for (const auto &line : lines) {
        if (line.startsWith("## ")) {
            flush();
            title = line.mid(3).trimmed();
        } else if (!title.isEmpty()) {
            body += line + '\n';
        }
    }
    flush();
    if (loadedChunks.isEmpty()) {
        if (error) *error = "知识文件没有二级标题内容";
        return false;
    }
    chunks_ = std::move(loadedChunks);
    return true;
}

QVector<KnowledgeHit> LocalKnowledgeStore::search(const QString &query, int limit) const {
    QVector<KnowledgeHit> ranked;
    if (limit <= 0) return ranked;
    limit = std::min(limit, 8);
    const auto terms = searchTerms(query.left(2048));
    if (terms.isEmpty()) return ranked;
    for (const auto &chunk : chunks_) {
        const QString title = chunk.title.toLower();
        const QString body = chunk.text.toLower();
        int score = 0;
        for (const auto &term : terms) {
            if (title.contains(term)) score += 7;
            if (body.contains(term)) score += 2;
        }
        if (score > 0) ranked.append({chunk.title, chunk.text, score});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.score > b.score;
    });
    if (ranked.size() > limit) ranked.resize(limit);
    return ranked;
}

QString LocalKnowledgeStore::contextFor(const QString &query, int limit, int characterBudget) const {
    QString context;
    characterBudget = std::clamp(characterBudget, 0, 6000);
    for (const auto &hit : search(query, limit)) {
        const QString block = QString("[manual:%1]\n%2\n").arg(hit.title, hit.text);
        if (context.size() + block.size() > characterBudget) break;
        context += block;
    }
    return context.trimmed();
}

} // namespace qitest
