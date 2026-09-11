#include "ui/Rs485ConnectionPanel.h"
#include "app/AppController.h"
#include "domain/DisplayLabels.h"
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qitest {
Rs485ConnectionPanel::Rs485ConnectionPanel(AppController *controller, QWidget *parent) : QWidget(parent) {
    setObjectName("rs485ConnectionPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    auto *ports = new QComboBox;
    ports->setObjectName("rs485Port");
    // Windows 7 is the delivery target.  A serial port must come from the
    // system enumeration; an editable empty box looks like an unexplained
    // text field and lets users submit a path that cannot be opened.
    ports->setEditable(false);
    ports->setMinimumHeight(36);
    ports->setMinimumWidth(154);
    ports->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *refresh = new QPushButton("刷新");
    refresh->setObjectName("rs485Refresh");
    auto *connectButton = new QPushButton("连接");
    connectButton->setObjectName("rs485Connect");
    auto *disconnectButton = new QPushButton("断开");
    disconnectButton->setObjectName("rs485Disconnect");
    for (auto *button : {refresh, connectButton, disconnectButton}) button->setFixedHeight(36);
    refresh->setMinimumWidth(62);
    connectButton->setMinimumWidth(62);
    disconnectButton->setMinimumWidth(54);
    refresh->setToolTip("重新扫描可用串口");
    connectButton->setToolTip("连接串口并读取设备状态");
    disconnectButton->setToolTip("连接串口后可断开");
    auto *save = new QPushButton("导出报文"); save->setObjectName("pumpExport");
    save->setFixedHeight(36);
    save->setMinimumWidth(72);
    save->setToolTip("收到有效485或分子泵报文后可导出");
    row->addWidget(ports); row->addWidget(refresh); row->addWidget(connectButton);
    row->addWidget(disconnectButton); row->addWidget(save); row->addStretch();
    layout->addLayout(row);
    auto *status = new QLabel(this);
    status->setObjectName("rs485ConnectionStatus");
    status->hide();
    auto *pumpStatus = new QLabel(this); pumpStatus->setObjectName("pumpStatus"); pumpStatus->hide();
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("rs485Readings");
    table->setHorizontalHeaderLabels({"485回读项目", "当前值", "说明"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    // Wine/Windows 7 may ignore a transparent gridline color while the native
    // grid is still enabled, producing the black blocks seen on the target UI.
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(30);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setMinimumHeight(260);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(table, 1);
    const auto refreshPorts = [controller, ports] {
        const QString selected = ports->currentText();
        const QStringList available = controller->rs485Ports();
        ports->clear();
        ports->addItems(available);
        ports->setProperty("hasAvailablePort", !available.isEmpty());
        if (available.isEmpty()) {
            ports->addItem("未发现串口");
            ports->setCurrentIndex(0);
            ports->setToolTip("未检测到串口，请连接设备后刷新");
            return;
        }
        const QString saved = QSettings(QSettings::defaultFormat(), QSettings::UserScope,
            "SCIENTZ", "QITest01").value("rs485/port").toString();
        const QString preferred = available.contains(selected) ? selected : saved;
        const int index = available.indexOf(preferred);
        ports->setCurrentIndex(index >= 0 ? index : 0);
    };
    refreshPorts();
    connect(refresh, &QPushButton::clicked, this, refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, [controller, ports] {
        controller->connectRs485(ports->currentText(), true);
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
        const bool open = data.value("open").toBool();
        if (open) {
            const QString activePort = data.value("port").toString();
            if (!activePort.isEmpty() && ports->findText(activePort) < 0) ports->addItem(activePort);
            if (!activePort.isEmpty()) ports->setCurrentText(activePort);
            ports->setProperty("hasAvailablePort", true);
        }
        const bool hasPort = ports->property("hasAvailablePort").toBool();
        connectButton->setEnabled(hasPort && !open && !busy);
        refresh->setEnabled(!open && !busy);
        ports->setEnabled(hasPort && !open && !busy);
        const auto pump = controller->pumpStatus();
        save->setEnabled(pump.value("retainedRecords").toInt() > 0);
        pumpStatus->setText(pump.value("message", "勾选后，连接一次即可依次读取主控板和分子泵。").toString());
        disconnectButton->setEnabled(open);
        status->setText(active ? data.value("message").toString()
            + (connected ? " · 更新于" + data.value("lastReadback").toString() : QString())
            : "选择连接仪器的串口。仅查询状态，不发送加热、电源或泵控制命令。");
        ports->setToolTip(status->text());
        table->setToolTip(status->text());
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
