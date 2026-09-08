#include "report/ReportGenerator.h"
#include "domain/DisplayLabels.h"

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextOption>
#include <QSet>
#include <QStringList>

namespace qitest {
namespace {
QString qualityText(QualityLevel level) {
    switch (level) {
    case QualityLevel::Pass: return "质量门控通过";
    case QualityLevel::Review: return "需要人工复核";
    case QualityLevel::Fail: return "质量门控失败";
    }
    return "未知";
}

// Imported names and evidence are plain text, never report markup.
QString escaped(const QString &text) {
    return text.toHtmlEscaped().replace("\n", "<br/>");
}

QString numbers(const QVector<double> &values) {
    QStringList text;
    for (double value : values)
        text << QString::number(value, 'f', 2).remove(QRegularExpression("\\.?0+$"));
    return text.join(", ");
}
}

bool ReportGenerator::writePdf(const QString &path, const RunSummary &run,
    const AnalysisResult &result, QString *error) {
    QVector<int> rows;
    for (int row = 0; row < result.candidates.size(); ++row) rows.append(row);
    return writePdf(path, run, result, rows, error);
}

bool ReportGenerator::writePdf(const QString &path, const RunSummary &run,
    const AnalysisResult &result, const QVector<int> &candidateRows, QString *error) {
    // Validate before touching the destination: never silently omit bad selections.
    QSet<int> seen;
    for (int row : candidateRows) {
        if (row < 0 || row >= result.candidates.size() || seen.contains(row)) {
            if (error) *error = "候选选择无效或重复，请重新选择";
            return false;
        }
        seen.insert(row);
    }
    const QString personName = run.sampleInfo.value("person_name").toString().trimmed();
    const QString sampleId = run.sampleInfo.value("sample_id").toString().trimmed();
    const QString identityNumber = run.sampleInfo.value("identity_number").toString().trimmed();
    QString html = "<html><body><h1>检测报告</h1>";
    const auto field = [&](const QString &name, const QString &value) {
        html += "<p><b>" + escaped(name) + "：</b>" + escaped(value) + "</p>";
    };
    field("记录编号", run.id);
    field("完成时间", run.completedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"));
    if (!sampleId.isEmpty()) field("样本编号", sampleId);
    if (!personName.isEmpty()) field("被筛查人", personName);
    if (!identityNumber.isEmpty()) field("证件号码", identityNumber);
    field("操作人", operatorLabel(run.operatorName));
    field("方法", run.methodName);
    field("数据范围", dataScopeLabel(run.dataScope));
    field("科学引擎", result.engineVersion);
    field("参考库", result.libraryVersion);
    html += "<h2>质量结论</h2>";
    field("质量门控", QString("%1（%2/100）").arg(qualityText(result.quality.level)).arg(result.quality.score));
    for (const auto &check : result.quality.checks)
        field((check.passed ? "通过 · " : "未通过 · ") + check.title, check.detail);
    html += "<h2>筛查结果</h2>";
    if (candidateRows.isEmpty()) html += "<p>未选择候选物；不据此判定样品阴性。</p>";
    else html += "<table><thead><tr><th>序号</th><th>名称</th><th>母离子</th><th>碎片离子</th>"
                 "<th>实测强度比</th><th>筛查结果</th></tr></thead><tbody>";
    int number = 0;
    for (int row : candidateRows) {
        const auto &candidate = result.candidates[row];
        const ScreeningItem *screening = nullptr;
        for (const auto &item : result.screeningItems) {
            if (item.referenceId == candidate.referenceId) { screening = &item; break; }
        }
        const QString precursor = QString::number(screening ? screening->precursorMz : candidate.measuredMz, 'f', 2)
            .remove(QRegularExpression("\\.?0+$"));
        html += "<tr><td>" + QString::number(++number) + "</td><td>" + escaped(candidate.name)
            + "</td><td>" + escaped(precursor) + "</td><td>"
            + escaped(screening ? numbers(screening->fragmentMz) : QStringLiteral("—")) + "</td><td>"
            + escaped(screening ? numbers(screening->measuredRelativeIntensity) : QStringLiteral("—"))
            + "</td><td class='suspect'>可疑</td></tr>";
    }
    if (!candidateRows.isEmpty()) html += "</tbody></table>";
    html += "<h2>候选证据</h2>";
    number = 0;
    for (int row : candidateRows) {
        const auto &candidate = result.candidates[row];
        html += "<h3>" + escaped(QString("%1. %2").arg(++number).arg(candidate.name)) + "</h3>";
        field("匹配得分", QString::number(candidate.score, 'f', 1));
        field("候选类型", "筛查候选");
        field("证据", candidate.evidence);
    }
    html += "<h2>复核与适用范围</h2>";
    field("复核状态", run.reviewStatus == "REVIEWED" ? "已复核" : "待复核");
    field("重要边界", run.dataScope == "PUBLIC_EXAMPLE"
        ? "本报告来自 OpenMS BSA 公开示例，用于查看曲线和软件流程，不是客户检测结果，不用于浓度验证。"
        : run.dataScope.contains("DEMO")
        ? "本报告须经人工复核，并按适用检测规范确认后方可形成正式结论。"
        : "候选结果须结合实验室质控、标准物和授权复核流程后才能形成正式结论。");
    html += "<p>本报告由确定性 C++ 引擎生成数值；本地大语言模型不参与数值计算。</p></body></html>";

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    {
        QPdfWriter pdf(&output);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(16, 16, 16, 16));
        pdf.setResolution(144);
        pdf.setTitle((personName.isEmpty() ? sampleId : personName) + " 检测报告");
        pdf.setCreator("飞秒质谱工作站 Qt/C++");
        QTextDocument document;
        document.documentLayout()->setPaintDevice(&pdf);
#ifdef Q_OS_WIN
        document.setDefaultFont(QFont("Microsoft YaHei", 10));
#else
        document.setDefaultFont(QFont("PingFang SC", 10));
#endif
        QTextOption options = document.defaultTextOption();
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document.setDefaultTextOption(options);
        document.setDefaultStyleSheet("body { color:#303634; background-color:white; }"
            "h1 { color:#007f80; font-size:19pt; } h2 { color:#007f80; font-size:13pt; margin-top:16px; }"
            "h3 { font-size:11pt; margin-top:12px; } p { margin-top:4px; margin-bottom:6px; }"
            "table { width:100%; border-collapse:collapse; margin-top:8px; }"
            "th, td { border:1px solid #b8c8c4; padding:5px; } th { background:#d9f1ec; }"
            ".suspect { color:#c62828; font-weight:bold; }");
        document.setHtml(html);
        // Page-aware layout keeps wrapped paragraphs visible; the former fixed
        // y-coordinate painter silently dropped long names and later candidates.
        const int footer = 50;
        const QSizeF body(pdf.width(), pdf.height() - footer);
        document.setPageSize(body);
        const int pages = document.pageCount();
        QPainter painter(&pdf);
        if (!painter.isActive()) {
            if (error) *error = "无法创建 PDF 绘图设备";
            return false;
        }
        for (int page = 0; page < pages; ++page) {
            if (page && !pdf.newPage()) {
                if (error) *error = "PDF 分页写入失败";
                return false;
            }
            painter.fillRect(QRectF(0, 0, pdf.width(), pdf.height()), Qt::white);
            painter.save();
            painter.setClipRect(QRectF(QPointF(0, 0), body));
            painter.translate(0, -page * body.height());
            document.drawContents(&painter, QRectF(0, page * body.height(), body.width(), body.height()));
            painter.restore();
            painter.setPen(QColor("#65736f"));
            painter.setFont(document.defaultFont());
            painter.drawText(QRectF(0, body.height() + 8, body.width(), footer - 8), Qt::AlignRight | Qt::AlignVCenter,
                QString("第 %1 / %2 页").arg(page + 1).arg(pages));
        }
        painter.end();
    } // Finish the PDF trailer before committing the atomic file.
    if (output.size() < 1000 || !output.commit()) {
        if (error) *error = "PDF 输出失败：" + output.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}
} // namespace qitest
