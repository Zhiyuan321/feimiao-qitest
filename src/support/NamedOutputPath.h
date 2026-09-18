#pragma once
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace qitest {
// Keep the person's original name in the record; sanitize only the Windows filename.
inline QString namedOutputPath(const QString &directory, QString name, const QString &extension) {
    name=name.trimmed();
    name.replace(QRegularExpression("[\\x00-\\x1f\\\\/:*?\"<>|]"),"_");
    name=name.left(60);
    while(name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    if(name.isEmpty()) name="检测结果";
    if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$",QRegularExpression::CaseInsensitiveOption)
        .match(name.section('.',0,0)).hasMatch()) name.prepend('_');
    QString path=QDir(directory).absoluteFilePath(name+extension);
    for(int i=2;QFileInfo::exists(path);++i)
        path=QDir(directory).absoluteFilePath(QString("%1-%2%3").arg(name).arg(i).arg(extension));
    return path;
}
}
