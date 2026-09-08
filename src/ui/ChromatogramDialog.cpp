#include "ui/ChromatogramDialog.h"
#include "ui/RoundedComboBox.h"
#include "ui/SpectrumPlot.h"
#include "core/ChromatogramEngine.h"
#include "storage/IntegrationDocument.h"
#include <QAction>
#include <QMenu>
#include <QSignalBlocker>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QSaveFile>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <memory>
#include <algorithm>

namespace qitest {
bool ChromatogramDialog::saveFile(const QString &path, QString *error) { return saveRecord_(path,error); }
bool ChromatogramDialog::loadFile(const QString &path, QString *error) { return loadRecord_(path,error); }
ChromatogramDialog::ChromatogramDialog(QVector<SpectrumScan> scans, QWidget *parent) : QDialog(parent) {
    setWindowTitle("提取离子与积分");
    setObjectName("chromatogramAnalysis");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 620);
    setMinimumSize(740, 440);
    const bool compact = parent && parent->height() < 620;
    if (compact) resize(760, 440);
    if (auto *screen = QGuiApplication::primaryScreen())
        resize(size().boundedTo(screen->availableGeometry().size() - QSize(12, 36)));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(compact ? 6 : 12);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("提取与积分");
    title->setProperty("sciTone", "pageTitle");
    header->addWidget(title); header->addStretch();
    auto *kind = new RoundedComboBox;
    kind->setObjectName("traceKind");
    kind->addItems({"TIC 总离子", "BPC 基峰", "EIC 提取离子"});
    auto *level = new RoundedComboBox;
    level->addItems({"MS1", "MS2"});
    kind->setAccessibleName("曲线类型"); level->setAccessibleName("扫描级别");
    header->addWidget(kind); header->addWidget(level);
    layout->addLayout(header);
    auto *extraction = new QWidget;
    extraction->setObjectName("extractionParameters");
    auto *controls = new QHBoxLayout(extraction);
    controls->setContentsMargins(0, 0, 0, 0); controls->setSpacing(10);
    auto *target = new QDoubleSpinBox;
    target->setRange(0.001, 100000); target->setDecimals(4); target->setValue(100); target->setSuffix(" m/z");
    auto *tolerance = new QDoubleSpinBox;
    tolerance->setRange(0.0001, 100); tolerance->setDecimals(4); tolerance->setValue(0.5); tolerance->setSuffix(" Da");
    target->setAccessibleName("提取离子 m/z"); tolerance->setAccessibleName("质量半窗口 Da");
    controls->addWidget(new QLabel("目标离子")); controls->addWidget(target, 1);
    controls->addWidget(new QLabel("± 质量窗口")); controls->addWidget(tolerance, 1);
    layout->addWidget(extraction);
    auto *plotPanel = new QFrame;
    plotPanel->setProperty("sciRole", "plotPanel");
    auto *plotLayout = new QVBoxLayout(plotPanel);
    plotLayout->setContentsMargins(10, 10, 10, 4);
    auto *plot = new SpectrumPlot(SpectrumPlot::Mode::Line);
    plot->setMinimumHeight(compact ? 95 : 145);
    plot->setAxisLabels("时间 / s", "信号强度");
    plot->setEmptyMessage("没有对应扫描", "导入含时间戳的扫描数据后使用");
    plotLayout->addWidget(plot);
    layout->addWidget(plotPanel, 1);
    auto *integrationPanel = new QFrame;
    integrationPanel->setProperty("sciRole", "workspaceSection");
    auto *integrationLayout = new QVBoxLayout(integrationPanel);
    integrationLayout->setContentsMargins(14, 12, 14, 12);
    integrationLayout->setSpacing(8);
    auto *bounds = new QHBoxLayout;
    bounds->setSpacing(8);
    auto *from = new QDoubleSpinBox;
    auto *to = new QDoubleSpinBox;
    for (auto *input : {from, to}) { input->setDecimals(6); input->setRange(0, 1e12); input->setSuffix(" s"); }
    from->setObjectName("integrationFrom"); to->setObjectName("integrationTo");
    auto *baseline = new RoundedComboBox;
    baseline->setObjectName("integrationBaseline");
    baseline->setAccessibleName("积分基线方式");
    baseline->addItems({"不扣基线", "扣除端点直线基线"});
    auto *boundsHeader = new QHBoxLayout;
    auto *boundsTitle = new QLabel("积分区间"); boundsTitle->setProperty("sciTone", "sectionTitle");
    boundsHeader->addWidget(boundsTitle); boundsHeader->addStretch(); boundsHeader->addWidget(baseline);
    integrationLayout->addLayout(boundsHeader);
    from->setAccessibleName("积分起点，秒"); to->setAccessibleName("积分终点，秒");
    bounds->addWidget(new QLabel("起点")); bounds->addWidget(from, 1);
    bounds->addWidget(new QLabel("终点")); bounds->addWidget(to, 1);
    integrationLayout->addLayout(bounds);
    // Compact native inputs retain typing, focus and keyboard stepping; no
    // oversized instrument-control steppers inside the analysis workspace.
    for (auto *input : {target, tolerance, from, to}) {
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setProperty("sciRole", "analysisInput");
        input->setMinimumWidth(150); input->setFixedHeight(34);
    }
    for (auto *combo : {kind, level, baseline}) {
        combo->setProperty("sciRole", "analysisInput");
        combo->setFixedHeight(34);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }
    auto *result = new QLabel("选择边界后计算面积；面积不能直接当作浓度。");
    result->setObjectName("integrationResult"); result->setWordWrap(true);
    result->setProperty("sciTone", "secondary"); integrationLayout->addWidget(result);
    layout->addWidget(integrationPanel);
    auto *actions = new QHBoxLayout;
    auto *integrate = new QPushButton("计算积分"); integrate->setObjectName("calculateIntegral");
    integrate->setProperty("sciRole", "primary");
    auto *exportButton = new QPushButton("导出曲线 CSV");
    exportButton->setIcon(QIcon(":/qitest/resources/icons/export.svg"));
    auto *records = new QPushButton("积分记录");
    records->setToolTip("保存可复算的曲线、边界与面积；不保存浓度，也不替代原始扫描归档");
    auto *recordMenu = new QMenu(records); records->setMenu(recordMenu);
    auto *saveRecord = recordMenu->addAction("保存积分结果"); saveRecord->setObjectName("saveIntegration"); saveRecord->setEnabled(false);
    auto *loadRecord = recordMenu->addAction("恢复积分结果");
    auto *close = new QPushButton("关闭");
    for (auto *button : {integrate, exportButton, records, close}) button->setFixedHeight(34);
    exportButton->setProperty("sciRole", "quietAction"); close->setProperty("sciRole", "quietAction");
    actions->addWidget(integrate); actions->addWidget(exportButton); actions->addWidget(records); actions->addStretch(); actions->addWidget(close);
    layout->addLayout(actions);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto trace = std::make_shared<QVector<SpectrumPoint>>();
    auto snapshot = std::make_shared<IntegrationSnapshot>();
    auto current = std::make_shared<bool>(false);
    const auto invalidate = [=] {
        *current=false; saveRecord->setEnabled(false);
        result->setText("参数已变化，请重新计算积分。");
    };
    for (auto *input : {from,to})
        connect(input,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[=] {
            input->setProperty("exactLoadedBoundary",QVariant{}); invalidate();
        });
    connect(baseline,QOverload<int>::of(&QComboBox::currentIndexChanged),this,invalidate);
    // Import size is bounded; only this dialog's selected run is held in memory.
    auto source = std::make_shared<QVector<SpectrumScan>>(std::move(scans));
    const auto refresh = [=] {
        invalidate();
        from->setProperty("exactLoadedBoundary",QVariant{});
        to->setProperty("exactLoadedBoundary",QVariant{});
        target->setEnabled(kind->currentIndex() == 2); tolerance->setEnabled(kind->currentIndex() == 2);
        extraction->setVisible(kind->currentIndex() == 2);
        *trace = ChromatogramEngine::trace(*source, static_cast<ChromatogramEngine::Kind>(kind->currentIndex()),
            level->currentIndex() + 1, target->value(), tolerance->value());
        plot->setPoints(*trace);
        const bool usable = trace->size() >= 2;
        integrate->setEnabled(usable); exportButton->setEnabled(usable);
        if (usable) {
            for (auto *input : {from, to}) input->setRange(trace->first().mz, trace->last().mz);
            from->setValue(trace->first().mz); to->setValue(trace->last().mz);
            from->setProperty("exactLoadedBoundary",trace->first().mz);
            to->setProperty("exactLoadedBoundary",trace->last().mz);
        }
        result->setText(usable ? "点击曲线查看信号；设置时间边界后计算积分。" : "没有至少两次对应级别的扫描，不能计算面积。");
    };
    auto *refreshTimer = new QTimer(this); refreshTimer->setSingleShot(true); refreshTimer->setInterval(200);
    connect(refreshTimer, &QTimer::timeout, this, refresh);
    const auto schedule = [=] { invalidate(); integrate->setEnabled(false); exportButton->setEnabled(false); refreshTimer->start(); };
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedule);
    connect(level, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedule);
    connect(target, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedule);
    connect(tolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedule);
    connect(integrate, &QPushButton::clicked, this, [=] {
        if (trace->size() < 2) return;
        // QDoubleSpinBox rounds its range to displayed decimals. Preserve exact
        // scan endpoints so a rounded first/last timestamp cannot reject a valid
        // full-range integral. Inputs are already constrained to this range.
        const auto exactBoundary = [&](const QDoubleSpinBox *input) {
            if (input->property("exactLoadedBoundary").isValid()) return input->property("exactLoadedBoundary").toDouble();
            if (input->value() == input->minimum()) return trace->first().mz;
            if (input->value() == input->maximum()) return trace->last().mz;
            return std::clamp(input->value(), trace->first().mz, trace->last().mz);
        };
        const double first = exactBoundary(from);
        const double last = exactBoundary(to);
        const auto area = ChromatogramEngine::integrate(*trace, first, last, baseline->currentIndex()==1);
        *current=area.valid; saveRecord->setEnabled(area.valid);
        if (area.valid) {
            snapshot->kind=static_cast<ChromatogramEngine::Kind>(kind->currentIndex());
            snapshot->msLevel=level->currentIndex()+1;
            snapshot->targetMz=target->value(); snapshot->toleranceDa=tolerance->value();
            snapshot->fromSeconds=first; snapshot->toSeconds=last;
            snapshot->endpointBaseline=baseline->currentIndex()==1; snapshot->trace=*trace; snapshot->area=area.area;
        }
        result->setText(area.valid ? QString("面积 %1（原始强度 × s） · %2–%3 s · %4。未经浓度校准。")
            .arg(area.area, 0, 'g', 10).arg(first, 0, 'g', 8).arg(last, 0, 'g', 8)
            .arg(baseline->currentIndex()==1 ? "端点直线基线，保留有符号面积" : "未扣基线") : area.error);
    });
    saveRecord_ = [=](const QString &path, QString *error) {
        if (!*current || refreshTimer->isActive()) {
            if(error) *error="请先计算当前参数的积分"; return false;
        }
        return IntegrationDocument::save(path,*snapshot,error);
    };
    loadRecord_ = [=](const QString &path, QString *error) {
        IntegrationSnapshot candidate;
        if (!IntegrationDocument::load(path,&candidate,error)) return false;
        const auto regenerated=ChromatogramEngine::trace(*source,candidate.kind,candidate.msLevel,candidate.targetMz,candidate.toleranceDa);
        bool same = regenerated.size()==candidate.trace.size();
        for(int i=0;same && i<regenerated.size();++i)
            same=regenerated[i].mz==candidate.trace[i].mz && regenerated[i].intensity==candidate.trace[i].intensity;
        if (!same) { if(error) *error="积分记录与当前扫描不匹配，请先打开对应的检测记录"; return false; }
        if(candidate.targetMz<target->minimum() || candidate.targetMz>target->maximum()
            || candidate.toleranceDa<tolerance->minimum() || candidate.toleranceDa>tolerance->maximum()
            || QString::number(candidate.targetMz,'f',4).toDouble()!=candidate.targetMz
            || QString::number(candidate.toleranceDa,'f',4).toDouble()!=candidate.toleranceDa) {
            if(error) *error="提取参数超出当前界面支持的范围或精度；原结果未改变"; return false;
        }
        refreshTimer->stop();
        const QSignalBlocker a(kind), b(level), c(target), d(tolerance), e(from), f(to), g(baseline);
        kind->setCurrentIndex(static_cast<int>(candidate.kind)); level->setCurrentIndex(candidate.msLevel-1);
        target->setValue(candidate.targetMz); tolerance->setValue(candidate.toleranceDa);
        refresh();
        from->setValue(candidate.fromSeconds); to->setValue(candidate.toSeconds);
        from->setProperty("exactLoadedBoundary",candidate.fromSeconds); to->setProperty("exactLoadedBoundary",candidate.toSeconds);
        baseline->setCurrentIndex(candidate.endpointBaseline ? 1 : 0);
        *snapshot=candidate; *current=true; saveRecord->setEnabled(true);
        result->setText(QString("已恢复并重算：面积 %1（原始强度 × s），%2–%3 s；未经浓度校准。")
            .arg(candidate.area,0,'g',10).arg(candidate.fromSeconds,0,'g',10).arg(candidate.toSeconds,0,'g',10));
        return true;
    };
    connect(saveRecord,&QAction::triggered,this,[=] {
        const auto path=QFileDialog::getSaveFileName(this,"保存积分结果","integration.qint.json","积分记录 (*.qint.json)");
        if(path.isEmpty()) return;
        QString error; result->setText(saveFile(path,&error) ? "积分结果已保存："+path:error);
    });
    connect(loadRecord,&QAction::triggered,this,[=] {
        const auto path=QFileDialog::getOpenFileName(this,"恢复当前扫描的积分",{},"积分记录 (*.qint.json)");
        if(path.isEmpty()) return;
        QString error; if(!loadFile(path,&error)) result->setText(error+"；原结果未改变");
    });
    connect(exportButton, &QPushButton::clicked, this, [=] {
        const QString path = QFileDialog::getSaveFileName(this, "导出当前曲线", "trace.csv", "CSV (*.csv)");
        if (path.isEmpty()) return;
        QByteArray data = "time_s,intensity,ms_level,trace_kind,target_mz,tolerance_da\n";
        for (const auto &p : *trace) data += QString("%1,%2,%3,%4,%5,%6\n")
            .arg(p.mz, 0, 'g', 17).arg(p.intensity, 0, 'g', 17).arg(level->currentIndex()+1)
            .arg(kind->currentIndex()==0 ? "TIC" : kind->currentIndex()==1 ? "BPC" : "EIC")
            .arg(kind->currentIndex()==2 ? QString::number(target->value(),'g',17) : "")
            .arg(kind->currentIndex()==2 ? QString::number(tolerance->value(),'g',17) : "").toUtf8();
        QSaveFile file(path);
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
        result->setText(saved ? "曲线已导出：" + path : "导出失败：" + file.errorString());
    });
    refresh();
}
}
