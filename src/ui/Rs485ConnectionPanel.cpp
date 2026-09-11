#include "ui/Rs485ConnectionPanel.h"
#include "app/AppController.h"
#include "domain/DisplayLabels.h"
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qitest {
Rs485ConnectionPanel::Rs485ConnectionPanel(AppController *controller, QWidget *parent) : QWidget(parent) {
    setObjectName("rs485ConnectionPanel");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("rs485PageScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(content); outer->addWidget(scroll);
    auto *title = new QLabel("主控板与分子泵共用485 · 9600 / 8N1 / 无流控");
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *row = new QHBoxLayout;
    auto *ports = new QComboBox;
    ports->setObjectName("rs485Port");
    ports->setEditable(true); // Supports custom Linux/macOS serial paths too.
    auto *refresh = new QPushButton("刷新串口");
    auto *connectButton = new QPushButton("连接并读取");
    connectButton->setObjectName("rs485Connect");
    auto *disconnectButton = new QPushButton("断开");
    disconnectButton->setObjectName("rs485Disconnect");
    row->addWidget(ports, 1); row->addWidget(refresh); row->addWidget(connectButton); row->addWidget(disconnectButton);
    layout->addLayout(row);
    auto *options = new QHBoxLayout;
    auto *includePump = new QCheckBox("同时读取分子泵"); includePump->setObjectName("rs485IncludePump");
    includePump->setChecked(QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").value("rs485/includePump", true).toBool());
    auto *save = new QPushButton("导出485报文"); save->setObjectName("pumpExport");
    options->addWidget(includePump); options->addStretch(); options->addWidget(save); layout->addLayout(options);
    auto *status = new QLabel;
    status->setObjectName("rs485ConnectionStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *pumpStatus = new QLabel; pumpStatus->setObjectName("pumpStatus"); pumpStatus->setWordWrap(true);
    layout->addWidget(pumpStatus);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("rs485Readings");
    table->setHorizontalHeaderLabels({"485回读项目", "当前值", "说明"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    // Wine/Windows 7 may ignore a transparent gridline color while the native
    // grid is still enabled, producing the black blocks seen on the target UI.
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setMinimumHeight(300); table->setMaximumHeight(360);
    layout->addWidget(table);
    auto *note = new QLabel("泵体温度即控制器温度。原始值保留在说明中；回复校验待确认。更多参数向下滚动查看。");
    note->setObjectName("rs485ReadbackNote");
    note->setWordWrap(true); layout->addWidget(note);
    const auto refreshPorts = [controller, ports] {
        const QString selected = ports->currentText();
        ports->clear(); ports->addItems(controller->rs485Ports());
        if (!selected.isEmpty()) ports->setCurrentText(selected);
        else ports->setCurrentText(QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").value("rs485/port").toString());
    };
    refreshPorts();
    connect(refresh, &QPushButton::clicked, this, refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, [controller, ports, includePump] {
        if (controller->connectRs485(ports->currentText(), includePump->isChecked()))
            QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").setValue("rs485/includePump", includePump->isChecked());
    });
    connect(disconnectButton, &QPushButton::clicked, controller, &AppController::disconnectRs485);
    connect(save, &QPushButton::clicked, this, [=] {
        const auto path = QFileDialog::getSaveFileName(this, "导出主控板与分子泵收发报文", "485报文.json", "JSON (*.json)");
        if (!path.isEmpty()) controller->exportPumpFrames(path);
    });
    const auto update = [=] {
        const auto data = controller->rs485Status();
        const bool active = !data.isEmpty(), connected = data.value("connected").toBool();
        const bool busy = controller->phase() == AppController::Phase::Acquiring
            || controller->phase() == AppController::Phase::Analyzing;
        connectButton->setEnabled(!busy);
        const auto pump = controller->pumpStatus();
        includePump->setEnabled(!data.value("open").toBool() && !busy);
        save->setEnabled(pump.value("retainedRecords").toInt() > 0);
        pumpStatus->setText(pump.value("message", "勾选后，连接一次即可依次读取主控板和分子泵。").toString());
        disconnectButton->setEnabled(data.value("open").toBool());
        if (active && data.value("open").toBool()) ports->setCurrentText(data.value("port").toString());
        status->setText(active ? data.value("message").toString()
            + (connected ? " · 更新于" + data.value("lastReadback").toString() : QString())
            : "选择连接仪器的串口。仅查询状态，不发送加热、电源或泵控制命令。");
        table->setVisible(true);
        const auto telemetry = controller->telemetry();
        const auto numeric = [connected](double v, const QString &unit) {
            if (!connected) return QString("—");
            const QString value = measurementText(v, 'f', 1);
            return value == "未提供" ? value : value + unit;
        };
        const auto raw = [&data](const QString &key, const QString &unit) {
            return data.contains(key) ? data.value(key).toString() + unit : QString("—");
        };
        const auto flag = [&data](const QString &key) {
            return !data.contains(key) ? QString("—") : data.value(key).toBool() ? QString("开启") : QString("关闭");
        };
        const auto pumpValue = [&pump](const QString &key, int decimals, const QString &unit) {
            if (!pump.contains(key)) return QString("—");
            const QString value = measurementText(pump.value(key).toDouble(), 'f', decimals);
            return value == "未提供" ? value : value + unit;
        };
        const auto pumpSource = [&pump](const QString &parameter) {
            return "原始 " + pump.value(parameter, "—").toString() + " · " + pump.value(parameter + "Time", "未更新").toString();
        };
        const QList<QStringList> rows{
            {"TD温度", numeric(telemetry.tdTemperatureC, " ℃"), "回读值÷10"},
            {"离子阱温度", numeric(telemetry.ionTrapTemperatureC, " ℃"), "回读值÷10"},
            {"EFC流量", numeric(telemetry.carrierGasFlowMlMin, " mL/min"), "回读值÷200"},
            {"气压", numeric(telemetry.carrierGasPressureTorr, " Torr"), "485气压读数"},
            {"分子泵转速（398）", pumpValue("molecularPumpRpm", 0, " RPM"), pumpSource("398")},
            {"分子泵电流（310）", pumpValue("molecularPumpCurrentA", 2, " A"), pumpSource("310")},
            {"分子泵电压（313）", pumpValue("molecularPumpVoltageV", 2, " V"), pumpSource("313")},
            {"泵体温度（326）", pumpValue("molecularPumpTemperatureC", 1, " ℃"), pumpSource("326")},
            {"气泵PWM", raw("gasPumpPwmPercent", " %"), "气泵占空比"},
            {"高压模块原始值", raw("highVoltageV", ""), "原始值÷10＝离子源电压(V)"},
            {"高压模块电流", raw("highVoltageCurrentUa", " μA"), "高压模块回读"},
            {"真空规原始值", raw("vacuumGaugeMv", " mV"), "压力换算待确认"},
            {"载气选择", connected ? telemetry.carrierGasMode : "—", "内/外载气，不是供气开关"},
            {"喷雾观察灯", flag("observationLightOn"), "设备状态"},
            {"TD/离子阱加热", flag("heatingOn"), "协议合并状态位"},
            {"废液泵", flag("wastePumpOn"), "设备状态"},
            {"HV 24V", flag("hv24VOn"), "485板状态，控制通道待确认"},
            {"RF 24V", flag("rf24VOn"), "485板状态，控制通道待确认"},
            {"隔膜泵", flag("diaphragmPumpOn"), "485板状态，控制通道待确认"}
        };
        table->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r)
            for (int col = 0; col < 3; ++col) {
                auto *item = table->item(r, col);
                if (!item) { item = new QTableWidgetItem; table->setItem(r, col, item); }
                if (item->text() != rows[r][col]) item->setText(rows[r][col]);
                item->setToolTip(rows[r][col]);
            }
    };
    connect(controller, &AppController::instrumentSettingsChanged, this, update);
    connect(controller, &AppController::phaseChanged, this, update);
    update();
}
}
