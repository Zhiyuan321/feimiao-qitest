#include "ui/ResultSpectrumDialog.h"
#include "ui/SpectrumPlot.h"
#include "ui/ChromatogramDialog.h"
#include "core/ChromatogramEngine.h"
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace qitest {
ResultSpectrumDialog::ResultSpectrumDialog(QVector<SpectrumScan> scans, QVector<SpectrumPoint> spectrum,
                                           const QString &recordLabel, QWidget *parent) : QDialog(parent) {
    setObjectName("resultSpectrumDialog");
    setWindowTitle("谱图查看");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(960, 640);
    setMinimumSize(720, 540);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    auto *record = new QLabel(recordLabel);
    record->setWordWrap(true);
    layout->addWidget(record);
    auto makePlot = [layout](const QString &title, SpectrumPlot::Mode mode, const QString &name,
                             QHBoxLayout **header) {
        auto *panel = new QFrame;
        panel->setObjectName("panel");
        auto *box = new QVBoxLayout(panel);
        box->setContentsMargins(10, 5, 10, 5);
        box->setSpacing(2);
        *header = new QHBoxLayout;
        auto *label = new QLabel(title);
        label->setProperty("sciTone", "panelTitle");
        (*header)->addWidget(label);
        (*header)->addStretch();
        box->addLayout(*header);
        auto *plot = new SpectrumPlot(mode);
        plot->setObjectName(name);
        plot->setMinimumHeight(95);
        box->addWidget(plot, 1);
        layout->addWidget(panel, 1);
        return plot;
    };
    QHBoxLayout *ticHeader, *msHeader, *eicHeader;
    auto *tic = makePlot("TIC · MS1", SpectrumPlot::Mode::Line, "resultTicPlot", &ticHeader);
    auto *ms = makePlot("质谱图 · MS1", SpectrumPlot::Mode::Sticks, "resultMsPlot", &msHeader);
    auto *eic = makePlot("EIC · MS1", SpectrumPlot::Mode::Line, "resultEicPlot", &eicHeader);
    ms->setAccentColor(QColor("#3485cf"));
    eic->setAccentColor(QColor("#B97824"));
    tic->setAxisLabels("时间 / s", "总离子信号");
    ms->setAxisLabels("m/z", "当前记录分析谱");
    eic->setAxisLabels("时间 / s", "提取离子信号");
    tic->setEmptyMessage("当前记录没有 MS1 时间序列", "");
    ms->setEmptyMessage("当前记录没有质谱数据", "");
    eic->setEmptyMessage("当前记录没有 MS1 时间序列", "");
    const auto trace = ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Tic);
    tic->setPoints(trace);
    ms->setPoints(spectrum);
    auto *extract = new QPushButton("提取 / 积分");
    extract->setObjectName("resultTraceAnalysis");
    extract->setEnabled(trace.size() >= 2);
    ticHeader->addWidget(extract);
    connect(extract, &QPushButton::clicked, this, [this, scans] {
        (new ChromatogramDialog(scans, this))->open();
    });
    auto *reset = new QPushButton("复位");
    ticHeader->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [tic, ms, eic] {
        for (auto *plot : {tic, ms, eic}) plot->resetView();
    });
    auto *mz = new QDoubleSpinBox;
    mz->setObjectName("resultEicMz"); mz->setRange(0, 1000000); mz->setDecimals(6);
    auto *tolerance = new QDoubleSpinBox;
    tolerance->setObjectName("resultEicTolerance"); tolerance->setRange(0.000001, 1000);
    tolerance->setDecimals(6); tolerance->setValue(0.5);
    for (auto *input : {mz, tolerance}) {
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setProperty("sciRole", "plotInput");
        input->setFixedSize(140, 30);
        input->setAlignment(Qt::AlignCenter);
    }
    eicHeader->addWidget(new QLabel("m/z")); eicHeader->addWidget(mz);
    eicHeader->addWidget(new QLabel("±")); eicHeader->addWidget(tolerance);
    eicHeader->addWidget(new QLabel("Da"));
    const auto selectScan = [scans, ms](double seconds) {
        const SpectrumScan *nearest = nullptr;
        for (const auto &scan : scans)
            if (scan.msLevel == 1 && (!nearest || std::abs(scan.timeSeconds-seconds) < std::abs(nearest->timeSeconds-seconds)))
                nearest = &scan;
        if (!nearest) return;
        ms->setPoints(nearest->points);
        ms->setAxisLabels(QString("m/z · %1 s").arg(nearest->timeSeconds), "MS1 原始谱");
    };
    connect(tic, &SpectrumPlot::pointActivated, this, selectScan);
    connect(eic, &SpectrumPlot::pointActivated, this, selectScan);
    if (!trace.isEmpty()) selectScan(trace.first().mz);
    const auto updateEic = [scans, eic, mz, tolerance] {
        eic->setPoints(ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Eic, 1, mz->value(), tolerance->value()));
        eic->setAxisLabels("时间 / s", QString("m/z %1 ± %2 Da").arg(mz->value(),0,'g',8).arg(tolerance->value()));
    };
    connect(mz, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateEic);
    connect(tolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateEic);
    connect(ms, &SpectrumPlot::pointActivated, this, [mz](double value) { mz->setValue(value); });
    if (!ms->points().isEmpty()) {
        const auto peak = std::max_element(ms->points().begin(), ms->points().end(),
            [](const SpectrumPoint &a, const SpectrumPoint &b) { return a.intensity < b.intensity; });
        mz->setValue(peak->mz);
    }
    updateEic();
    auto *close = new QPushButton("关闭");
    close->setObjectName("closeResultSpectrum");
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}
}
