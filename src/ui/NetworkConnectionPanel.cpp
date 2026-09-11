#include "ui/NetworkConnectionPanel.h"
#include "app/AppController.h"
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qitest {
NetworkConnectionPanel::NetworkConnectionPanel(AppController *controller, QWidget *parent) : QWidget(parent) {
    setObjectName("networkConnectionPanel");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel("电脑监听TCP，仪器主动连接。请将仪器目标IP设为电脑对应网卡IP，端口默认11000。");
    hint->setWordWrap(true); layout->addWidget(hint);
    QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    auto *row = new QHBoxLayout;
    auto *addresses = new QComboBox; addresses->setObjectName("networkAddress"); addresses->setEditable(true);
    addresses->addItem("0.0.0.0");
    for (const auto &address : QNetworkInterface::allAddresses())
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            if (addresses->findText(address.toString()) < 0) addresses->addItem(address.toString());
    addresses->setCurrentText(preferences.value("network/address", "0.0.0.0").toString());
    addresses->setToolTip("0.0.0.0监听所有IPv4网卡；仪器目标IP请填写下拉列表中的实际网卡IP。");
    auto *port = new QSpinBox; port->setObjectName("networkPort"); port->setRange(1, 65535);
    port->setValue(preferences.value("network/port", 11000).toInt());
    auto *start = new QPushButton("开始监听"); start->setObjectName("networkListen");
    auto *stop = new QPushButton("停止"); stop->setObjectName("networkStop");
    row->addWidget(new QLabel("本机IP")); row->addWidget(addresses, 1);
    row->addWidget(new QLabel("端口")); row->addWidget(port); row->addWidget(start); row->addWidget(stop);
    layout->addLayout(row);
    auto *options = new QHBoxLayout;
    auto *stale = new QSpinBox; stale->setObjectName("networkStaleSeconds"); stale->setRange(1, 3600);
    stale->setValue(preferences.value("network/staleMs", 5000).toInt() / 1000); stale->setSuffix(" 秒");
    stale->setToolTip("上位机读数失效时间，可按实际状态上传周期调整；不是固件协议参数。");
    auto *save = new QPushButton("导出最近报文"); save->setObjectName("networkExport");
    options->addWidget(new QLabel("状态超时")); options->addWidget(stale); options->addStretch(); options->addWidget(save);
    layout->addLayout(options);
    auto *status = new QLabel; status->setObjectName("networkConnectionStatus"); status->setWordWrap(true);
    layout->addWidget(status);
    auto *table = new QTableWidget(4, 3); table->setObjectName("networkReadings");
    table->setHorizontalHeaderLabels({"网口回读项目", "当前值", "说明"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setShowGrid(false); table->setAlternatingRowColors(true); table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide(); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setMinimumHeight(165); table->setMaximumHeight(185); layout->addWidget(table);
    auto *counts = new QLabel; counts->setObjectName("networkFrameCounts"); counts->setWordWrap(true); layout->addWidget(counts);
    connect(start, &QPushButton::clicked, this, [=] {
        controller->startNetworkListening(addresses->currentText(), quint16(port->value()), stale->value() * 1000);
    });
    connect(stop, &QPushButton::clicked, controller, &AppController::stopNetworkListening);
    connect(save, &QPushButton::clicked, this, [this, controller] {
        const auto path = QFileDialog::getSaveFileName(this, "导出最近256条CRC有效网口报文", "网口报文.json", "JSON (*.json)");
        if (!path.isEmpty()) controller->exportNetworkFrames(path);
    });
    const auto update = [=] {
        const auto data = controller->networkStatus();
        const bool listening = data.value("listening").toBool();
        const bool busy = controller->phase() == AppController::Phase::Acquiring || controller->phase() == AppController::Phase::Analyzing;
        start->setEnabled(!listening && !busy); stop->setEnabled(listening);
        addresses->setEnabled(!listening); port->setEnabled(!listening); stale->setEnabled(!listening);
        save->setEnabled(data.value("retainedFrames").toInt() > 0);
        status->setText(data.isEmpty() ? "尚未监听。可与485同时回读，调谐启停在射频页操作。" : data.value("message").toString()
            + (data.value("connected").toBool() ? " · 更新于" + data.value("lastReadback").toString() : QString()));
        const auto raw = [&data](const QString &key) { return data.contains(key) ? data.value(key).toString() : QString("—"); };
        const QList<QStringList> rows{
            {"倍增管高压", data.contains("multiplierVoltageV") ? raw("multiplierVoltageV") + " V" : "—", "网口实际回读"},
            {"真空规原始值", raw("vacuumRaw"), "网口原始读数"},
            {"实验状态", !data.contains("experimentRunning") ? "—" : data.value("experimentRunning").toBool() ? "开启" : "关闭", "设备上报状态"},
            {"真空度", data.contains("vacuumMbar") ? QString::number(data.value("vacuumMbar").toDouble(), 'E', 2) + " mbar" : "—", "按已确认公式换算"}
        };
        for (int r = 0; r < rows.size(); ++r) for (int c = 0; c < 3; ++c) {
            auto *item = table->item(r, c);
            if (!item) { item = new QTableWidgetItem; table->setItem(r, c, item); }
            item->setText(rows[r][c]); item->setToolTip(rows[r][c]);
        }
        counts->setText(QString("接收 %1 字节 · CRC有效 %2 帧 · 未解析 %3 帧 · 丢弃 %4 字节\n保留最近256条收发帧供导出；气压曲线与调谐启停已接入，完整谱图采集未开放。")
            .arg(data.value("receivedBytes", 0).toString()).arg(data.value("validFrames", 0).toString())
            .arg(data.value("unparsedFrames", 0).toString()).arg(data.value("rejectedBytes", 0).toString()));
    };
    connect(controller, &AppController::instrumentSettingsChanged, this, update);
    connect(controller, &AppController::phaseChanged, this, update); update();
}
}
