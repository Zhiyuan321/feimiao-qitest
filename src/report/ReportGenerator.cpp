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
#include <QJsonArray>
#include <QStringList>

namespace qitest {
namespace {
// Imported names and evidence are plain text, never report markup.
QString escaped(const QString &text) {
    return text.toHtmlEscaped().replace("\n", "<br/>");
}

}

QString ReportGenerator::reportHtml(const RunSummary &run, const AnalysisResult &result,
                                  const QVector<int> &candidateRows) {
    const auto value=[&](const char *key) {
        const auto text=run.sampleInfo.value(key).toString().trimmed();
        return escaped(text.isEmpty()?QString("未提供"):text);
    };
    const auto now=QDateTime::currentDateTime();
    const int offset=now.offsetFromUtc()/60;
    const QString zone=QString("UTC%1%2:%3").arg(offset<0?"-":"+")
        .arg(qAbs(offset)/60,2,10,QChar('0')).arg(qAbs(offset)%60,2,10,QChar('0'));
    QString html="<html><body><h1 align='center'>检测报告</h1><hr/>";
    html+="<table width='100%' border='1' cellspacing='0' cellpadding='5'>"
        "<tr><td align='center'>检测报告生成时间（当地时间）</td></tr><tr><td align='center'>"
        +escaped(now.toString("yyyy-MM-dd HH:mm:ss")+" ("+zone+")")+"</td></tr></table>";
    html+="<table width='100%' cellspacing='0' cellpadding='6'>"
        "<tr><td width='18%'><b>样品类型</b></td><td width='42%'>"+value("sample_type")
        +"</td><td width='15%'><b>被检人</b></td><td width='25%'>"+value("person_name")+"</td></tr>"
        "<tr><td><b>仪器名称</b></td><td>"+value("instrument_model")
        +"</td><td><b>样本编号</b></td><td>"+value("sample_id")+"</td></tr>"
        "<tr><td><b>时间</b></td><td colspan='3'>"
        +escaped(run.completedAt.toLocalTime().toString("yyyy年MM月dd日 HH时mm分ss秒"))+"</td></tr></table>";
    html+="<p>离子模式："+value("ionization")+"</p>";
    html+="<p class='meta'>数据来源："+escaped(dataScopeLabel(run.dataScope))+"</p>";
    if(run.sampleInfo.contains("screening_status"))
        html+="<p class='meta'>筛查状态："+escaped(screeningStatusLabel(run.sampleInfo["screening_status"].toString()))+"</p>";
    if(!run.sampleInfo["screening_error"].toString().isEmpty())
        html+="<p>"+escaped(run.sampleInfo["screening_error"].toString())+"</p>";
    html+="<hr/><table width='100%' cellspacing='0' cellpadding='7'><thead><tr>"
        "<th width='9%'>序号</th><th width='41%'>化合物名称</th>"
        "<th width='32%'>定量离子</th><th width='18%'>是否检出</th></tr></thead>";
    const auto entries=run.sampleInfo["ion_screening_snapshot"].toObject()["entries"].toArray();
    int number=0;
    for(int row:candidateRows) {
        if(row<0 || row>=result.candidates.size())continue;
        const auto &candidate=result.candidates[row];
        const ScreeningItem *item=nullptr;
        for(const auto &screening:result.screeningItems)
            if(screening.referenceId==candidate.referenceId){item=&screening;break;}
        if(!result.screeningItems.isEmpty() && (!item || item->conclusion!="可疑"))continue;
        // Quantitative ions must come from the acquisition's frozen library, not
        // a later edited file, and are distinct from qualitative screening ions.
        QString ions="未提供";
        const auto match=QRegularExpression("^lib-row-([1-9][0-9]*)$").match(candidate.referenceId);
        if(match.hasMatch()) {
            const int index=match.captured(1).toInt()-1;
            const auto entry=index>=0 && index<entries.size()?entries.at(index).toObject():QJsonObject{};
            if(entry["name"].toString()==candidate.name) {
                const auto ion=entry["quantify_ion"];
                const QString text=ion.isDouble()?QString::number(ion.toDouble(),'g',12):ion.toString().trimmed();
                if(!text.isEmpty())ions=text;
            }
        }
        html+="<tr><td align='center'>"+QString::number(++number)+"</td><td align='center'>"
            +escaped(candidate.name)+"</td><td align='center'>"+escaped(ions)
            +"</td><td align='center' class='suspect'>可疑</td></tr>";
    }
    html+="</table>";
    if(number==0)html+="<p>本次报告无可疑化合物。</p>";
    html+="<p class='meta'>本报告仅列出可疑筛查结果，未列出不等于证明样品阴性。</p>"
        "<p>---报告结束---</p></body></html>";
    return html;
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
    const QString html=reportHtml(run,result,candidateRows);

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    {
        QPdfWriter pdf(&output);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(22, 24, 22, 18),QPageLayout::Millimeter);
        pdf.setResolution(144);
        pdf.setTitle((personName.isEmpty() ? sampleId : personName) + " 检测报告");
        pdf.setCreator("飞秒质谱工作站 Qt/C++");
        QTextDocument document;
        document.documentLayout()->setPaintDevice(&pdf);
        document.setDefaultFont(QFont("SimSun", 11));
        QTextOption options = document.defaultTextOption();
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document.setDefaultTextOption(options);
        document.setDefaultStyleSheet("body { color:#111111; background-color:white; }"
            "h1 { font-size:20pt; margin-bottom:20px; }"
            "p { margin-top:8px; margin-bottom:8px; }"
            "th { font-weight:bold; } .meta { font-size:9pt; color:#555555; }"
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
