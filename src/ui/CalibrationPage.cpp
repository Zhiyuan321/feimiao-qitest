#include "ui/CalibrationPage.h"
#include "ui/RoundedComboBox.h"
#include "ui/SpectrumPlot.h"
#include "storage/CalibrationDocument.h"
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace qitest {
namespace {
QLineEdit *field(const QString &placeholder, const QString &name) {
    auto *edit = new QLineEdit; edit->setMaxLength(80); edit->setMinimumWidth(0);
    edit->setPlaceholderText(placeholder); edit->setAccessibleName(placeholder); edit->setObjectName(name);
    return edit;
}
QDoubleSpinBox *number(const QString &name) {
    auto *value = new QDoubleSpinBox; value->setRange(0, 1e15); value->setDecimals(8);
    value->setAccessibleName(name); value->setObjectName(name); return value;
}
}
CalibrationPage::CalibrationPage(QWidget *parent) : QWidget(parent) {
    setObjectName("calibrationPage");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12,12,12,12); layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("定量曲线"); title->setProperty("sciTone", "pageTitle");
    header->addWidget(title); header->addStretch();
    auto *import = new QPushButton("导入校准"); import->setObjectName("importCalibration"); import->setProperty("sciRole", "primary");
    save_ = new QPushButton("保存曲线"); save_->setObjectName("saveCalibration");
    auto *more = new QPushButton("更多");
    auto *menu = new QMenu(more); more->setMenu(menu);
    auto *open = menu->addAction("打开已保存曲线");
    auto *exportPoints = menu->addAction("导出参与拟合的点 CSV");
    auto *add = menu->addAction("添加校准点");
    calculate_ = new QPushButton("计算浓度"); calculate_->setObjectName("calculateCalibrationSample");
    calculate_->setToolTip("使用当前校准曲线计算样品浓度");
    calculate_->setProperty("sciRole", "primary"); calculate_->setFixedHeight(34);
    import->setProperty("sciRole", "");
    header->addWidget(import); header->addWidget(save_); header->addWidget(calculate_); header->addWidget(more); layout->addLayout(header);
    // 基本信息、校准点和拟合图属于同一条工作流，只保留最外层圆角。
    // 内部分区用浅色直线，避免多个白色圆角卡片上下拼接形成凹凸边。
    auto *contentPanel = new QFrame;
    contentPanel->setObjectName("calibrationContentPanel");
    contentPanel->setProperty("sciRole", "workspaceSection");
    auto *contentLayout = new QVBoxLayout(contentPanel);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    auto *metadataPanel = new QFrame;
    auto *metadata = new QGridLayout(metadataPanel);
    metadata->setContentsMargins(12,10,12,10); metadata->setHorizontalSpacing(10); metadata->setVerticalSpacing(6);
    name_ = field("校准名称", "calibrationName"); unit_ = field("浓度单位", "calibrationUnit");
    responseUnit_ = field("响应单位（面积或峰高）", "calibrationResponseUnit");
    internalName_ = field("内标名称", "internalStandardName");
    standard_ = new RoundedComboBox; standard_->setObjectName("calibrationStandard"); standard_->addItems({"外标线性", "内标线性"});
    weight_ = new RoundedComboBox; weight_->setObjectName("calibrationWeight"); weight_->addItems({"无权重", "1/x²"});
    for (auto *edit : {name_, unit_, responseUnit_, internalName_}) {
        edit->setProperty("sciRole", "analysisInput"); edit->setFixedHeight(34);
    }
    for (auto *combo : {standard_, weight_}) {
        combo->setProperty("sciRole", "analysisInput"); combo->setFixedHeight(34); combo->setMinimumWidth(0);
    }
    const QStringList labels{"校准名称", "浓度单位", "响应单位"};
    for (int i=0;i<3;++i) {
        auto *label=new QLabel(labels[i]); label->setProperty("sciTone", "metadata");
        metadata->addWidget(label,0,i == 0 ? 0 : i+1,1,i == 0 ? 2 : 1);
    }
    metadata->addWidget(name_,1,0,1,2); metadata->addWidget(unit_,1,2); metadata->addWidget(responseUnit_,1,3);
    metadata->addWidget(standard_,2,0,1,2); metadata->addWidget(weight_,2,2);
    metadata->addWidget(internalName_,2,3);
    for(int i=0;i<4;++i) metadata->setColumnStretch(i,1);
    contentLayout->addWidget(metadataPanel);
    auto *metadataDivider = new QFrame;
    metadataDivider->setObjectName("separator");
    metadataDivider->setFrameShape(QFrame::HLine);
    contentLayout->addWidget(metadataDivider);
    for (auto *button : {import, save_, more}) { button->setFixedHeight(34); }
    unit_->setToolTip("整份校准及样品内标采用同一浓度单位；更改标签不会换算数值");
    responseUnit_->setToolTip("响应必须统一采用峰面积或峰高，不混用；更改标签不会换算数值");
    auto *body = new QSplitter; body->setObjectName("calibrationSplit"); body->setChildrenCollapsible(false);
    body->setHandleWidth(1);
    table_ = new QTableWidget(0,4); table_->setObjectName("calibrationPoints"); table_->setMinimumWidth(0);
    table_->setStyleSheet("QTableWidget#calibrationPoints { border-radius: 0px; }");
    table_->setHorizontalHeaderLabels({"使用", "x 浓度", "y 响应", "残差"});
    table_->setAlternatingRowColors(true); table_->setShowGrid(false); table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setMinimumSectionSize(34);
    table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    table_->setToolTip("勾选参与拟合；双击数值编辑原始点。取消勾选不会删除数据。");
    body->addWidget(table_);
    auto *curve = new QFrame;
    auto *curveLayout = new QVBoxLayout(curve); curveLayout->setContentsMargins(12,10,12,10);
    auto *curveTitle = new QLabel("校准拟合"); curveTitle->setProperty("sciTone", "panelTitle");
    curveLayout->addWidget(curveTitle);
    plot_ = new SpectrumPlot(SpectrumPlot::Mode::Line); plot_->setMinimumWidth(0);
    plot_->setEmptyMessage("等待校准点", "导入数据或从“更多”添加"); curveLayout->addWidget(plot_,1);
    status_ = new QLabel; status_->setWordWrap(true); status_->setObjectName("calibrationStatus"); curveLayout->addWidget(status_);
    body->addWidget(curve); body->setStretchFactor(0,2); body->setStretchFactor(1,3);
    body->setSizes({360,500}); contentLayout->addWidget(body,1); layout->addWidget(contentPanel,1);
    for (auto *edit : {name_, unit_, responseUnit_, internalName_}) connect(edit, &QLineEdit::editingFinished, this, [this] {
        model_.name=name_->text().trimmed(); model_.concentrationUnit=unit_->text().trimmed();
        model_.responseUnit=responseUnit_->text().trimmed(); model_.internalStandard=internalName_->text().trimmed(); refreshFit();
    });
    connect(standard_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        model_.standard=index ? CalibrationModel::Standard::Internal : CalibrationModel::Standard::External; refreshFit();
    });
    connect(weight_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        model_.weight=index ? QuantitationEngine::Weight::InverseXSquared : QuantitationEngine::Weight::None; refreshFit();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column()!=0 || item->row()<0 || item->row()>=int(model_.observations.size())) return;
        model_.observations[item->row()].included=item->checkState()==Qt::Checked; refreshFit();
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row,int column) { if(column) editPoint(row); });
    connect(add, &QAction::triggered, this, [this] { editPoint(-1); });
    connect(calculate_, &QPushButton::clicked, this, &CalibrationPage::calculateSample);
    const auto choose = [this](bool saved) {
        const QString path = QFileDialog::getOpenFileName(this, saved ? "打开校准曲线" : "导入校准点", {},
            saved ? "QITest 校准 (*.qcal.json)" : "校准 CSV (*.csv *.tsv *.txt)");
        if (path.isEmpty()) return;
        QString error; if (!loadFile(path, &error)) status_->setText(error + "；保留原数据");
    };
    connect(import, &QPushButton::clicked, this, [choose] { choose(false); });
    connect(open, &QAction::triggered, this, [choose] { choose(true); });
    connect(save_, &QPushButton::clicked, this, [this] {
        const auto path=QFileDialog::getSaveFileName(this,"保存完整校准","calibration.qcal.json","QITest 校准 (*.qcal.json)");
        if(path.isEmpty()) return; QString error;
        status_->setText(saveFile(path,&error) ? "校准已保存："+path : error);
    });
    connect(exportPoints, &QAction::triggered, this, [this] {
        const auto path=QFileDialog::getSaveFileName(this,"导出参与拟合的点（单位与元数据请另存完整曲线）","calibration.csv","CSV (*.csv)");
        if(path.isEmpty()) return; QString error;
        status_->setText(CalibrationDocument::exportCsv(path,model_,&error) ? "拟合点已导出；完整元数据请使用保存曲线" : error);
    });
    applyModel();
}

bool CalibrationPage::loadFile(const QString &path, QString *error) {
    CalibrationModel candidate=model_;
    const bool ok=path.endsWith(".json",Qt::CaseInsensitive) ? CalibrationDocument::load(path,&candidate,error)
        : CalibrationDocument::readCsv(path,&candidate,error);
    if(!ok) return false;
    model_=candidate; applyModel(); return true;
}
bool CalibrationPage::saveFile(const QString &path, QString *error) { return CalibrationDocument::save(path,model_,error); }
void CalibrationPage::applyModel() {
    name_->setText(model_.name); unit_->setText(model_.concentrationUnit); responseUnit_->setText(model_.responseUnit); internalName_->setText(model_.internalStandard);
    const QSignalBlocker a(standard_), b(weight_);
    standard_->setCurrentIndex(model_.standard==CalibrationModel::Standard::Internal ? 1:0);
    weight_->setCurrentIndex(model_.weight==QuantitationEngine::Weight::InverseXSquared ? 1:0); refreshFit();
}
void CalibrationPage::refreshFit() {
    ++modelRevision_;
    const bool internal=model_.standard==CalibrationModel::Standard::Internal;
    internalName_->setVisible(internal);
    const auto evaluated=CalibrationCalculator::evaluate(model_);
    const QSignalBlocker blocker(table_); table_->setRowCount(model_.observations.size());
    table_->setHorizontalHeaderLabels({"使用",internal ? "x 浓度比" : "x 浓度",internal ? "y 响应比" : "y 响应","残差"});
    for(int i=0;i<int(model_.observations.size());++i) {
        const auto &r=model_.observations[i];
        auto *used=table_->item(i,0); if(!used) { used=new QTableWidgetItem; table_->setItem(i,0,used); }
        used->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable); used->setCheckState(r.included ? Qt::Checked:Qt::Unchecked);
        const bool usable=!internal || (r.internalConcentration>0 && r.internalResponse>0);
        const double x=internal && usable ? r.concentration/r.internalConcentration:r.concentration;
        const double y=internal && usable ? r.response/r.internalResponse:r.response;
        const QStringList values{usable ? QString::number(x,'g',7):"—", usable ? QString::number(y,'g',7):"—",
            evaluated.fit.valid && r.included && usable ? QString::number(y-evaluated.fit.slope*x-evaluated.fit.intercept,'g',4):"—"};
        for(int c=1;c<4;++c) {
            auto *item=table_->item(i,c); if(!item) { item=new QTableWidgetItem; table_->setItem(i,c,item); }
            item->setText(values[c-1]); item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setToolTip(QString("浓度 %1；响应 %2；内标浓度 %3；内标响应 %4\n双击编辑原始点")
                .arg(r.concentration,0,'g',17).arg(r.response,0,'g',17).arg(r.internalConcentration,0,'g',17).arg(r.internalResponse,0,'g',17));
        }
    }
    save_->setEnabled(evaluated.fit.valid); calculate_->setEnabled(evaluated.fit.valid);
    plot_->setAxisLabels(internal ? "浓度比" : model_.concentrationUnit,internal ? "响应比" : model_.responseUnit);
    if(!evaluated.fit.valid) { plot_->setPoints({}); status_->setText(evaluated.fit.error); return; }
    const auto &fit=evaluated.fit;
    plot_->setPoints({{evaluated.minimum,fit.slope*evaluated.minimum+fit.intercept},{evaluated.maximum,fit.slope*evaluated.maximum+fit.intercept}});
    status_->setText(QString("y = %1x %2 %3 · %4R² %5\n%6 个拟合点 · x 范围 %7–%8；不外推，需复核")
        .arg(fit.slope,0,'g',6).arg(fit.intercept>=0 ? "+":"−").arg(std::abs(fit.intercept),0,'g',5)
        .arg(weight_->currentIndex() ? "加权 ":"").arg(fit.rSquared,0,'f',5).arg(evaluated.points.size()).arg(evaluated.minimum).arg(evaluated.maximum));
}

void CalibrationPage::editPoint(int row) {
    if (row >= int(model_.observations.size())) return;
    if(row<0 && model_.observations.size()>=10000) { status_->setText("最多 10000 个点"); return; }
    auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("编辑校准点");
    dialog->setObjectName("calibrationPointDialog");
    dialog->setWindowModality(Qt::WindowModal);
    auto *layout=new QFormLayout(dialog);
    // Fixed-decimal spin boxes can silently round a loaded scientific value
    // even when the user presses Apply without editing. Round-trip doubles.
    auto *concentration=field("浓度", "pointConcentration"), *response=field("响应", "pointResponse"),
         *ic=field("内标浓度", "pointInternalConcentration"), *ir=field("内标响应", "pointInternalResponse");
    layout->addRow("浓度 · "+model_.concentrationUnit,concentration); layout->addRow("响应 · "+model_.responseUnit,response);
    if(model_.standard==CalibrationModel::Standard::Internal) { layout->addRow("内标浓度",ic); layout->addRow("内标响应",ir); }
    else { ic->setParent(dialog); ir->setParent(dialog); ic->hide(); ir->hide(); }
    const CalibrationObservation original = row >= 0 ? model_.observations.at(row) : CalibrationObservation{};
    const QList<QLineEdit *> inputs{concentration,response,ic,ir};
    const double values[]{original.concentration,original.response,original.internalConcentration,original.internalResponse};
    for (int i=0;i<inputs.size();++i) {
        inputs[i]->setText(QString::number(values[i],'g',17));
        inputs[i]->setToolTip("支持科学计数法，例如 1.25e-9；保留原始双精度值");
    }
    auto *feedback=new QLabel; feedback->setObjectName("pointEditFeedback"); feedback->setWordWrap(true); layout->addRow(feedback);
    auto *apply=new QPushButton("应用"); apply->setObjectName("applyCalibrationPoint"); layout->addRow(apply);
    const quint64 revision = modelRevision_;
    connect(apply,&QPushButton::clicked,dialog,[=] {
        if (revision != modelRevision_) { feedback->setText("校准已变化，请关闭窗口后重新编辑；未覆盖新数据。"); return; }
        double parsed[4];
        for (int i=0;i<inputs.size();++i) {
            bool ok=false; parsed[i]=inputs[i]->text().trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(parsed[i]) || parsed[i]<0) {
                feedback->setText("请输入有限的非负数；原校准点未改变。"); return;
            }
        }
        CalibrationObservation r{parsed[0],parsed[1],parsed[2],parsed[3],original.included};
        if(row>=0) model_.observations.at(row)=r; else model_.observations.push_back(r);
        refreshFit(); dialog->accept();
    }); dialog->open();
}

void CalibrationPage::calculateSample() {
    // Snapshot protects a result from subsequent edits in the underlying page.
    const auto snapshot=model_;
    auto *dialog=new QDialog(this); dialog->setObjectName("calibrationSampleDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("样品定量");
    auto *layout=new QFormLayout(dialog);
    auto *response=number("sampleResponse"), *internalResponse=number("sampleInternalResponse"), *internalConcentration=number("sampleInternalConcentration");
    layout->addRow("样品响应 · "+snapshot.responseUnit,response);
    if(snapshot.standard==CalibrationModel::Standard::Internal) { layout->addRow(snapshot.internalStandard+" 响应",internalResponse); layout->addRow("内标浓度 · "+snapshot.concentrationUnit,internalConcentration); }
    else { internalResponse->setParent(dialog); internalResponse->hide(); internalConcentration->setParent(dialog); internalConcentration->hide(); }
    auto *answer=new QLabel("按当前校准计算；不自动写入鉴定报告。"); answer->setWordWrap(true); answer->setObjectName("sampleConcentrationResult"); layout->addRow(answer);
    auto *calculate=new QPushButton("计算"); calculate->setObjectName("calculateSample"); calculate->setProperty("sciRole","primary"); layout->addRow(calculate);
    connect(calculate,&QPushButton::clicked,dialog,[=] {
        double result=0; QString error;
        const bool ok=CalibrationCalculator::calculate(snapshot,response->value(),internalResponse->value(),internalConcentration->value(),&result,&error);
        answer->setText(ok ? QString("浓度 %1 %2\n校准：%3；请复核配对、单位和适用范围").arg(result,0,'g',10).arg(snapshot.concentrationUnit,snapshot.name):error);
    });
    for(auto *input : {response,internalResponse,internalConcentration}) connect(input,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dialog,[=] { answer->setText("输入已变化，请重新计算"); });
    dialog->open();
}
}
