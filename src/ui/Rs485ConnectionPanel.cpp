#include "ui/Rs485ConnectionPanel.h"
#include "app/AppController.h"
#include "domain/DisplayLabels.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qitest {
Rs485ConnectionPanel::Rs485ConnectionPanel(AppController *controller, QWidget *parent) : QWidget(parent) {
    setObjectName("rs485ConnectionPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel("485状态读取 · 9600 / 8N1 / 无流控");
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
    auto *status = new QLabel;
    status->setObjectName("rs485ConnectionStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("rs485Readings");
    table->setHorizontalHeaderLabels({"485回读项目", "当前值", "说明"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setMinimumHeight(160); table->setMaximumHeight(250);
    layout->addWidget(table);
    const auto refreshPorts = [controller, ports] {
        const QString selected = ports->currentText();
        ports->clear(); ports->addItems(controller->rs485Ports());
        if (!selected.isEmpty()) ports->setCurrentText(selected);
        else ports->setCurrentText(QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").value("rs485/port").toString());
    };
    refreshPorts();
    connect(refresh, &QPushButton::clicked, this, refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, [controller, ports] {
        controller->connectRs485(ports->currentText());
    });
    connect(disconnectButton, &QPushButton::clicked, controller, &AppController::disconnectRs485);
    const auto update = [controller, ports, connectButton, disconnectButton, status, table] {
        const auto data = controller->rs485Status();
        const bool active = !data.isEmpty(), connected = data.value("connected").toBool();
        const bool busy = controller->phase() == AppController::Phase::Acquiring
            || controller->phase() == AppController::Phase::Analyzing;
        connectButton->setEnabled(!busy);
        disconnectButton->setEnabled(data.value("open").toBool());
        if (active && data.value("open").toBool()) ports->setCurrentText(data.value("port").toString());
        status->setText(active ? data.value("message").toString()
            + (connected ? " · 更新于" + data.value("lastReadback").toString() : QString())
            : "选择连接仪器的串口。仅查询状态，不发送加热、电源或泵控制命令。");
        table->setVisible(active);
        if (!active) return;
        const auto telemetry = controller->telemetry();
        const auto numeric = [connected](double v, const QString &unit) {
            return connected ? measurementText(v, 'f', 1) + unit : QString("—");
        };
        const auto raw = [&data](const QString &key, const QString &unit) {
            return data.contains(key) ? data.value(key).toString() + unit : QString("—");
        };
        const auto flag = [&data](const QString &key) {
            return !data.contains(key) ? QString("—") : data.value(key).toBool() ? QString("开启") : QString("关闭");
        };
        const QList<QStringList> rows{
            {"TD温度", numeric(telemetry.tdTemperatureC, " ℃"), "回读值÷10"},
            {"离子阱温度", numeric(telemetry.ionTrapTemperatureC, " ℃"), "回读值÷10"},
            {"EFC流量", numeric(telemetry.carrierGasFlowMlMin, " mL/min"), "回读值÷200"},
            {"气压", numeric(telemetry.carrierGasPressureTorr, " Torr"), "485气压读数"},
            {"气泵PWM", raw("gasPumpPwmPercent", " %"), "气泵占空比"},
            {"高压模块电压", raw("highVoltageV", " V"), "未映射为离子源或倍增器电压"},
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
