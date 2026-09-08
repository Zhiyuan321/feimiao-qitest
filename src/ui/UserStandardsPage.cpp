#include "ui/UserStandardsPage.h"
#include "ui/SpectrumPlot.h"
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace qitest {
namespace {
class StandardEditorDialog final : public QDialog {
public:
    using QDialog::QDialog;
    bool dirty = false;
    void reject() override {
        if (dirty && QMessageBox::question(this, "未保存的修改",
                "修改尚未保存，确定放弃并关闭吗？",
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
                != QMessageBox::Discard) return;
        QDialog::reject();
    }
};
}
UserStandardsPage::UserStandardsPage(QString path, QWidget *parent) : QWidget(parent),
    repository_(new UserStandardRepository(std::move(path))) {
    setObjectName("userStandardsPage");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(12,12,12,12);
    auto *toolbar=new QHBoxLayout;
    search_=new QLineEdit; search_->setPlaceholderText("名称或 CAS"); search_->setMaxLength(120); search_->setObjectName("userStandardSearch");
    auto *searchButton=new QPushButton("检索");
    auto *create=new QPushButton("新建标准"); create->setObjectName("newUserStandard"); create->setProperty("sciRole","primary");
    auto *import=new QPushButton("导入");
    auto *more=new QPushButton("管理"); auto *menu=new QMenu(more); more->setMenu(menu);
    auto *categories=menu->addAction("类别管理");
    auto *archive=menu->addAction("归档所选标准");
    toolbar->addWidget(search_,1); toolbar->addWidget(searchButton); toolbar->addWidget(create); toolbar->addWidget(import); layout->addLayout(toolbar);
    toolbar->addWidget(more);
    connect(archive,&QAction::triggered,this,[this] {
        if(selected_.id.isEmpty()) { status_->setText("请先选择标准"); return; }
        if(QMessageBox::question(this,"归档标准","从当前列表移除，但保留原始标准、修订历史和已生成证据。确定归档？")!=QMessageBox::Yes) return;
        QString error;
        if(repository_->archive(selected_.id,selected_.revision,&error)) { refresh(); status_->setText("已归档；历史证据未删除"); }
        else status_->setText(error);
    });
    connect(categories,&QAction::triggered,this,[this] {
        auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("类别管理"); dialog->resize(450,440);
        auto *layout=new QVBoxLayout(dialog); auto *list=new QListWidget; layout->addWidget(list);
        auto *feedback=new QLabel; feedback->setWordWrap(true); layout->addWidget(feedback);
        auto *row=new QHBoxLayout; auto *add=new QPushButton("新增"); auto *remove=new QPushButton("删除空类别"); auto *close=new QPushButton("关闭");
        row->addWidget(add); row->addWidget(remove); row->addStretch(); row->addWidget(close); layout->addLayout(row);
        const auto reload=[=] { QString error; list->clear(); list->addItems(repository_->categories(&error)); feedback->setText(error); }; reload();
        connect(add,&QPushButton::clicked,dialog,[=] {
            bool ok=false; const auto name=QInputDialog::getText(dialog,"新增类别","类别名称",QLineEdit::Normal,{},&ok);
            if(!ok) return; QString error; if(repository_->addCategory(name,&error)) reload(); else feedback->setText(error);
        });
        connect(remove,&QPushButton::clicked,dialog,[=] {
            if(!list->currentItem()) return; QString error;
            if(repository_->removeCategory(list->currentItem()->text(),&error)) reload(); else feedback->setText(error);
        });
        connect(close,&QPushButton::clicked,dialog,&QDialog::accept); dialog->open();
    });
    table_=new QTableWidget(0,3); table_->setObjectName("userStandardsTable");
    table_->setHorizontalHeaderLabels({"名称","CAS","修订"}); table_->setShowGrid(false); table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    layout->addWidget(table_,1);
    auto *actions=new QHBoxLayout;
    edit_=new QPushButton("编辑所选"); edit_->setObjectName("editUserStandard");
    export_=new QPushButton("导出所选"); export_->setObjectName("exportUserStandard");
    compare_=new QPushButton("比对检测谱"); compare_->setObjectName("compareUserStandard");
    compare_->setProperty("sciRole","quietAction");
    compare_->setToolTip("选择标准，比对当前检测记录的已检出峰；不加载大模型，不覆盖原始结果");
    previous_=new QPushButton("上一页"); next_=new QPushButton("下一页");
    actions->addWidget(edit_); actions->addWidget(export_); actions->addWidget(compare_); actions->addStretch(); actions->addWidget(previous_); actions->addWidget(next_); layout->addLayout(actions);
    plot_=new SpectrumPlot(SpectrumPlot::Mode::Sticks); plot_->setMinimumHeight(145);
    plot_->setEmptyMessage("选择标准查看谱图", ""); layout->addWidget(plot_,1);
    status_=new QLabel; status_->setWordWrap(true); status_->setObjectName("userStandardStatus"); layout->addWidget(status_);
    connect(searchButton,&QPushButton::clicked,this,[this] { offset_=0; refresh(); });
    connect(search_,&QLineEdit::returnPressed,searchButton,&QPushButton::click);
    connect(previous_,&QPushButton::clicked,this,[this] { offset_=qMax(0,offset_-50); refresh(); });
    connect(next_,&QPushButton::clicked,this,[this] { offset_+=50; refresh(); });
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this] { selectCurrent(); });
    connect(create,&QPushButton::clicked,this,[this] { edit(true); });
    connect(edit_,&QPushButton::clicked,this,[this] { edit(false); });
    connect(compare_,&QPushButton::clicked,this,[this] {
        if(!comparisonProvider_ || selected_.id.isEmpty()) return;
        const auto input=comparisonProvider_();
        if(input.recordId.isEmpty() || input.peaks.isEmpty()) {
            status_->setText("请先在检测记录中打开一条已有分析峰的记录，再选择标准比对。"); return;
        }
        auto *dialog=new StandardComparisonDialog(selected_,input,this); dialog->open();
    });
    connect(import,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getOpenFileName(this,"导入用户标准",{},"QITest 标准 (*.qstd.json)");
        if(path.isEmpty()) return; QString error; if(!importFile(path,&error)) status_->setText(error);
    });
    connect(export_,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getSaveFileName(this,"导出当前标准","standard.qstd.json","QITest 标准 (*.qstd.json)");
        if(path.isEmpty()) return; QString error;
        status_->setText(exportSelected(path,&error) ? "已导出标准："+path:error);
    });
    QString error;
    if(!repository_->open(&error)) { setEnabled(false); status_->setText(error); return; }
    refresh();
}
void UserStandardsPage::refresh() {
    QString error; const auto rows=repository_->search(search_->text().trimmed(),offset_,&error);
    const QSignalBlocker blocker(table_); table_->setRowCount(rows.size());
    for(int i=0;i<rows.size();++i) {
        auto *name=new QTableWidgetItem(rows[i].name); name->setData(Qt::UserRole,rows[i].id); name->setToolTip(rows[i].name); table_->setItem(i,0,name);
        table_->setItem(i,1,new QTableWidgetItem(rows[i].cas)); table_->setItem(i,2,new QTableWidgetItem(QString::number(rows[i].revision)));
    }
    previous_->setEnabled(offset_>0); next_->setEnabled(rows.size()==50);
    table_->clearSelection(); table_->setCurrentCell(-1,-1);
    selected_={}; plot_->setPoints({}); edit_->setEnabled(false); export_->setEnabled(false); compare_->setEnabled(false);
    status_->setText(error.isEmpty() ? QString("本页 %1 条 · 用户标准需经方法验证后使用").arg(rows.size()):error);
}
void UserStandardsPage::selectCurrent() {
    selected_={}; QString error;
    const auto *item=table_->item(table_->currentRow(),0);
    const bool ok=item && repository_->load(item->data(Qt::UserRole).toString(),&selected_,&error);
    edit_->setEnabled(ok); export_->setEnabled(ok); plot_->setPoints(ok ? selected_.peaks:QVector<SpectrumPoint>{});
    compare_->setEnabled(ok && bool(comparisonProvider_));
    if(ok) {
        plot_->setAxisLabels("m/z",selected_.ionization+" · 用户标准");
        status_->setText(QString("%1 · %2 个谱点 · 修订 %3 · 待方法验证").arg(selected_.name).arg(selected_.peaks.size()).arg(selected_.revision));
        status_->setToolTip(selected_.provenance);
    } else if(!error.isEmpty()) status_->setText(error);
}
void UserStandardsPage::setComparisonProvider(std::function<StandardComparisonInput()> provider) {
    comparisonProvider_=std::move(provider);
    compare_->setEnabled(!selected_.id.isEmpty() && bool(comparisonProvider_));
}
bool UserStandardsPage::importFile(const QString &path, QString *error) {
    UserStandard candidate;
    if(!UserStandardRepository::readFile(path,&candidate,error) || !repository_->save(&candidate,error)) return false;
    search_->clear(); offset_=0; refresh();
    status_->setText("标准已导入，相同内容不会重复新增；需经方法验证后使用"); return true;
}
bool UserStandardsPage::exportSelected(const QString &path, QString *error) {
    if(selected_.id.isEmpty()) { if(error) *error="请先选择标准"; return false; }
    return UserStandardRepository::writeFile(path,selected_,error);
}
void UserStandardsPage::edit(bool create) {
    if(!create && selected_.id.isEmpty()) return;
    const UserStandard original=create ? UserStandard{}:selected_;
    auto *dialog=new StandardEditorDialog(this); dialog->setObjectName("userStandardEditor"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal); dialog->setWindowTitle(create ? "新建用户标准":"编辑用户标准"); dialog->resize(650,640);
    auto *layout=new QVBoxLayout(dialog); auto *form=new QFormLayout;
    const auto field=[&](const QString &label,const QString &value,const QString &id) {
        auto *edit=new QLineEdit(value); edit->setMaxLength(120); edit->setObjectName(id); form->addRow(label,edit); return edit;
    };
    auto *name=field("名称",original.name,"standardName");
    auto *cas=field("CAS",original.cas,"standardCas");
    auto *formula=field("分子式",original.formula,"standardFormula");
    auto *category=field("类别",original.category,"standardCategory");
    auto *ionization=field("离子化方式",original.ionization,"standardIonization"); ionization->setPlaceholderText("例如 EI、ESI+；请按原始数据填写");
    auto *ions=field("母 / 定性 / 定量离子",QString("%1,%2,%3").arg(original.precursorMz,0,'g',17).arg(original.qualifierMz,0,'g',17).arg(original.quantifierMz,0,'g',17),"standardIons");
    ions->setToolTip("三个 m/z 以逗号分隔；0 表示未指定，不自动猜测离子");
    QStringList extraIons; for(double mz:original.additionalQualifierMzs) extraIons.append(QString::number(mz,'g',17));
    auto *qualifiers=field("其他定性离子 m/z",extraIons.join(','),"standardAdditionalQualifiers");
    qualifiers->setMaxLength(1024); qualifiers->setPlaceholderText("可选；逗号分隔，不重复，最多再加 31 个");
    auto *internal=new QCheckBox("用作内标"); internal->setChecked(original.internalStandard); form->addRow(internal);
    auto *source=field("来源与适用条件",original.provenance,"standardProvenance"); source->setMaxLength(2048);
    auto *formBody=new QWidget;formBody->setLayout(form);
    auto *formScroll=new QScrollArea;formScroll->setWidgetResizable(true);formScroll->setWidget(formBody);
    formScroll->setMinimumHeight(160);formScroll->setMaximumHeight(340);layout->addWidget(formScroll);
    auto *peakHeader=new QHBoxLayout; peakHeader->addWidget(new QLabel("谱点 · 每行 mz,intensity")); peakHeader->addStretch();
    auto *readCsv=new QPushButton("读取谱点 CSV"); peakHeader->addWidget(readCsv); layout->addLayout(peakHeader);
    auto *peaks=new QPlainTextEdit; peaks->setObjectName("standardPeaks"); peaks->setPlaceholderText("mz,intensity\n31,5000\n46,10000");
    QStringList lines; for(const auto &p:original.peaks) lines.append(QString::number(p.mz,'g',17)+','+QString::number(p.intensity,'g',17));
    peaks->setPlainText(lines.join('\n')); layout->addWidget(peaks,1);
    auto *feedback=new QLabel; feedback->setWordWrap(true); feedback->setObjectName("standardEditFeedback"); layout->addWidget(feedback);
    auto *buttons=new QHBoxLayout; auto *save=new QPushButton("保存标准"); save->setObjectName("saveUserStandard"); save->setProperty("sciRole","primary");
    auto *cancel=new QPushButton("取消"); buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(save); layout->addLayout(buttons);
    auto *saveAs = new QPushButton("另存为新标准");
    saveAs->setObjectName("saveAsUserStandard");
    saveAs->setToolTip("请填写不同的名称；保留原标准及其修订记录");
    saveAs->setVisible(!create);
    buttons->insertWidget(1, saveAs);
    for (auto *input : {name, cas, formula, category, ionization, ions, source, qualifiers})
        connect(input, &QLineEdit::textChanged, dialog, [dialog] { dialog->dirty = true; });
    connect(internal, &QCheckBox::toggled, dialog, [dialog] { dialog->dirty = true; });
    connect(peaks, &QPlainTextEdit::textChanged, dialog, [dialog] { dialog->dirty = true; });
    connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(readCsv,&QPushButton::clicked,dialog,[=] {
        const auto path=QFileDialog::getOpenFileName(dialog,"读取谱点 CSV",{},"CSV (*.csv)"); if(path.isEmpty()) return;
        QFile file(path); if(!file.open(QIODevice::ReadOnly)) { feedback->setText(file.errorString()); return; }
        const auto bytes=file.read(UserStandardRepository::MaximumFileBytes+1);
        const auto text=QString::fromUtf8(bytes); QVector<SpectrumPoint> parsed; QString error;
        if(file.error()!=QFile::NoError || bytes.size()>UserStandardRepository::MaximumFileBytes || text.toUtf8()!=bytes || !UserStandardRepository::parsePeaks(text,&parsed,&error)) {
            feedback->setText("未导入："+error+"；要求 UTF-8、最多 1 MiB 的两列谱点"); return;
        }
        peaks->setPlainText(text); feedback->setText("谱点已读入，填写来源后保存。");
    });
    const auto persist = [=](bool asNew) {
        UserStandard candidate=original; QString error;
        candidate.name=name->text().trimmed(); candidate.cas=cas->text().trimmed(); candidate.formula=formula->text().trimmed();
        if (asNew) {
            if (candidate.name == original.name) {
                feedback->setText("另存前请填写不同的名称，原标准不会被覆盖。");
                name->setFocus(); return;
            }
            candidate.id.clear(); candidate.revision = 0;
        }
        candidate.category=category->text().trimmed(); candidate.ionization=ionization->text().trimmed(); candidate.provenance=source->text().trimmed();
        candidate.internalStandard=internal->isChecked();
        const auto parts=ions->text().split(',');
        if(parts.size()!=3) { feedback->setText("请填写三个离子 m/z，以逗号分隔；0 表示未指定"); return; }
        double values[3];
        for(int i=0;i<3;++i) { bool ok=false; values[i]=parts[i].trimmed().toDouble(&ok); if(!ok) { feedback->setText("离子 m/z 不是数值"); return; } }
        candidate.precursorMz=values[0]; candidate.qualifierMz=values[1]; candidate.quantifierMz=values[2];
        candidate.additionalQualifierMzs.clear();
        if(!qualifiers->text().trimmed().isEmpty()) for(const auto &part:qualifiers->text().split(',')) {
            bool ok=false; const double mz=part.trimmed().toDouble(&ok);
            if(!ok) { feedback->setText("其他定性离子不是数值"); return; }
            candidate.additionalQualifierMzs.append(mz);
        }
        if(!UserStandardRepository::parsePeaks(peaks->toPlainText(),&candidate.peaks,&error) || !repository_->save(&candidate,&error)) { feedback->setText(error); return; }
        search_->clear(); offset_=0; refresh(); dialog->dirty = false; dialog->accept();
    };
    connect(save,&QPushButton::clicked,dialog,[=] { persist(false); });
    connect(saveAs,&QPushButton::clicked,dialog,[=] { persist(true); });
    dialog->open();
}
}
