#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

namespace qitest::PlatformPaths {

// 不在源码或设置中固化 macOS/Windows 用户目录。程序每次都由
// Qt 取得当前系统的可写位置，再用 QDir 组合路径。
inline QString writableLocation(QStandardPaths::StandardLocation location) {
    QString root = QStandardPaths::writableLocation(location);
    if (root.trimmed().isEmpty()) root = QDir::homePath();
    return QDir::cleanPath(root);
}

inline QString documentsSubdirectory(const QString &name) {
    return QDir(writableLocation(QStandardPaths::DocumentsLocation)).filePath(name);
}

inline QString appDataFile(const QString &name) {
    return QDir(writableLocation(QStandardPaths::AppDataLocation)).filePath(name);
}

// 旧设置可能来自另一台电脑或另一个系统。该目录在本机不存在时，
// 回退到本机默认目录，避免 Windows 继续显示 /Users/... 路径。
inline QString existingDirectoryOrDefault(const QString &stored, const QString &fallback) {
    const QString candidate = QDir::cleanPath(stored.trimmed());
    const QFileInfo info(candidate);
    return !candidate.isEmpty() && info.exists() && info.isDir()
        ? info.absoluteFilePath() : QDir::cleanPath(fallback);
}

inline QString nativeDisplay(const QString &path) {
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

} // namespace qitest::PlatformPaths
