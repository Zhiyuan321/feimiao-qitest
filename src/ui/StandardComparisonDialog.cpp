#include "ui/StandardComparisonDialog.h"
#include "core/SpectralComparison.h"
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace qitest {
namespace {
QJsonArray pointsJson(const QVector<SpectrumPoint> &points) {
    QJsonArray array;
    for (const auto &p : points) array.append(QJsonArray{p.mz,p.intensity});
    return array;
}
}
StandardComparisonDialog::StandardComparisonDialog(UserStandard reference,
    StandardComparisonInput query, QWidget *parent) : QDialog(parent) {
    setObjectName("standardComparisonDialog"); setWindowTitle("标准谱图比对");
    setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal); resize(760,580);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(16,16,16,16);
    auto *title = new QLabel("标准谱图比对"); title->setProperty("sciTone","sectionTitle"); layout->addWidget(title);
    auto *source = new QLabel(QString("%1 · 修订 %2 · %3\n%4")
        .arg(reference.name).arg(reference.revision).arg(reference.ionization,query.description));
    source->setWordWrap(true); source->setToolTip(reference.provenance+"\n记录："+query.recordId); layout->addWidget(source);
    auto *conditions = new QCheckBox("已核对离子化方式和峰表含义可比（不代表鉴定确认）");
    conditions->setObjectName("comparisonConditions"); layout->addWidget(conditions);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel("质量容差"));
    auto *tolerance = new QDoubleSpinBox; tolerance->setObjectName("comparisonTolerance");
    tolerance->setRange(0,100); tolerance->setDecimals(4); tolerance->setValue(0.5); tolerance->setSuffix(" Da");
    tolerance->setButtonSymbols(QAbstractSpinBox::NoButtons); tolerance->setProperty("sciRole","analysisInput");
    tolerance->setFixedWidth(150); controls->addWidget(tolerance); controls->addStretch();
    auto *calculate = new QPushButton("开始比对"); calculate->setObjectName("calculateStandardComparison");
    calculate->setProperty("sciRole","primary"); calculate->setEnabled(false); controls->addWidget(calculate); layout->addLayout(controls);
    auto *summary = new QLabel("比对检测记录的已检出峰，不改变原始数据、候选结果或复核状态。");
    summary->setObjectName("standardComparisonSummary"); summary->setWordWrap(true);
    summary->setMinimumHeight(summary->fontMetrics().lineSpacing()*3+8); layout->addWidget(summary);
    auto *table = new QTableWidget(0,4); table->setObjectName("standardComparisonPairs");
    table->setHorizontalHeaderLabels({"标准 m/z","检测 m/z","偏差 Da","相对强度 %\n标准 / 检测"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setShowGrid(false);
    table->setAlternatingRowColors(true); table->verticalHeader()->hide(); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table,1);
    auto *footer = new QHBoxLayout;
    auto *exportButton = new QPushButton("导出比对证据"); exportButton->setObjectName("exportStandardComparison");
    exportButton->setEnabled(false); footer->addWidget(exportButton); footer->addStretch();
    auto *close = new QPushButton("关闭"); footer->addWidget(close); layout->addLayout(footer);
    connect(close,&QPushButton::clicked,this,&QDialog::close);
    const auto invalidate = [this,summary,table,exportButton] {
        evidence_.clear(); table->setRowCount(0); exportButton->setEnabled(false);
        summary->setText("条件已改变，请重新比对；旧结果不会用于导出。");
    };
    connect(conditions,&QCheckBox::toggled,this,[calculate,invalidate](bool checked) { invalidate(); calculate->setEnabled(checked); });
    connect(tolerance,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[invalidate](double) { invalidate(); });
    connect(calculate,&QPushButton::clicked,this,[this,query,reference,tolerance,conditions,summary,table,exportButton] {
        evidence_.clear(); table->setRowCount(0); exportButton->setEnabled(false);
        if (!conditions->isChecked()) return;
        const auto result = SpectralComparison::compare(query.peaks,reference.peaks,tolerance->value());
        if (!result.valid) { summary->setText(result.error); return; }
        table->setRowCount(qMin(500,result.pairs.size()));
        double qmax=0,rmax=0;
        for(const auto &p:query.peaks) qmax=std::max(qmax,p.intensity);
        for(const auto &p:reference.peaks) rmax=std::max(rmax,p.intensity);
        QJsonArray pairs;
        for (int row=0;row<result.pairs.size();++row) {
            const auto &pair=result.pairs[row]; const auto &q=query.peaks[pair.query]; const auto &r=reference.peaks[pair.reference];
            pairs.append(QJsonArray{pair.query,pair.reference,pair.deltaDa});
            if (row>=table->rowCount()) continue;
            const QStringList texts{QString::number(r.mz,'g',12),QString::number(q.mz,'g',12),
                QString::number(pair.deltaDa,'g',8),QString("%1 / %2").arg(100*(r.intensity/rmax),0,'f',1).arg(100*(q.intensity/qmax),0,'f',1)};
            for (int col=0;col<texts.size();++col) {
                auto *item=new QTableWidgetItem(texts[col]); item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
                item->setToolTip(texts[col]); table->setItem(row,col,item);
            }
        }
        summary->setText(QString("谱形相似度 %1 / 100 · 匹配 %2 对峰\n检测 %3 峰、标准 %4 峰；表格最多显示 500 对，导出保留全部。相似度不是鉴定概率或浓度。")
            .arg(result.cosine*100,0,'f',2).arg(result.pairs.size()).arg(query.peaks.size()).arg(reference.peaks.size()));
        const QJsonObject evidence{{"algorithm",SpectralComparison::Version},{"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"record_id",query.recordId},{"query_description",query.description},{"query_peaks",pointsJson(query.peaks)},
            {"standard_id",reference.id},{"standard_revision",reference.revision},{"standard_name",reference.name},
            {"standard_ionization",reference.ionization},{"standard_provenance",reference.provenance},
            {"reference_peaks",pointsJson(reference.peaks)},{"conditions_confirmed",true},
            {"tolerance_da",tolerance->value()},{"cosine",result.cosine},{"pairs",pairs},
            {"scope","spectral_comparison_not_identification"}};
        evidence_=QJsonDocument(evidence).toJson(QJsonDocument::Compact); exportButton->setEnabled(true);
    });
    connect(exportButton,&QPushButton::clicked,this,[this,summary] {
        const QString path=QFileDialog::getSaveFileName(this,"保存比对证据","comparison.qcompare.json","比对证据 (*.qcompare.json)");
        if(path.isEmpty()) return; QString error;
        if(!exportEvidence(path,&error)) summary->setText(error);
    });
}
bool StandardComparisonDialog::exportEvidence(const QString &path,QString *error) const {
    if(evidence_.isEmpty()) { if(error)*error="请先完成有效比对"; return false; }
    const auto bytes=QJsonDocument(QJsonObject{{"schema","qitest-spectral-comparison-1"},
        {"payload_base64",QString::fromLatin1(evidence_.toBase64())},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(evidence_,QCryptographicHash::Sha256).toHex())}}).toJson();
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) { if(error)*error=file.errorString(); return false; }
    return true;
}
}
