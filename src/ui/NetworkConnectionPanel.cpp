#include "ui/NetworkConnectionPanel.h"
#include "ui/RoundedComboBox.h"
#include "app/AppController.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace qitest {
NetworkConnectionPanel::NetworkConnectionPanel(AppController *controller, QWidget *parent) : QWidget(parent) {
    setObjectName("networkConnectionPanel");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    auto *endpointRow = new QHBoxLayout;
    endpointRow->setSpacing(6);
    auto *addresses = new RoundedComboBox; addresses->setObjectName("networkAddress"); addresses->setEditable(true);
    addresses->addItem("0.0.0.0");
    for (const auto &address : QNetworkInterface::allAddresses())
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            if (addresses->findText(address.toString()) < 0) addresses->addItem(address.toString());
    addresses->setCurrentText(preferences.value("network/address", "0.0.0.0").toString());
    addresses->setToolTip("0.0.0.0监听所有IPv4网卡；仪器目标IP请填写下拉列表中的实际网卡IP。");
    addresses->setMinimumWidth(220); addresses->setMinimumHeight(36);
    addresses->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addresses->lineEdit()->setCursorPosition(0); addresses->lineEdit()->deselect();
    connect(addresses, QOverload<int>::of(&QComboBox::activated), addresses, [addresses] {
        QTimer::singleShot(0, addresses, [addresses] {
            addresses->lineEdit()->setCursorPosition(0);
            addresses->lineEdit()->deselect();
        });
    });
    auto *port = new QSpinBox; port->setObjectName("networkPort"); port->setRange(1, 65535);
    port->setValue(preferences.value("network/port", 11000).toInt());
    port->setButtonSymbols(QAbstractSpinBox::NoButtons); port->setAlignment(Qt::AlignCenter); port->setFixedWidth(76); port->setFixedHeight(36);
    auto *start = new QPushButton("监听"); start->setObjectName("networkListen");
    auto *stop = new QPushButton("停止"); stop->setObjectName("networkStop");
    auto *save = new QPushButton("导出报文"); save->setObjectName("networkExport");
    for (auto *button : {start, stop, save}) {
        button->setFixedHeight(36);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    stop->setToolTip("开始监听后可停止");
    save->setToolTip("保存TXT十六进制收发记录或JSON诊断；没有有效报文也可导出");
    auto *addressLabel = new QLabel("IP");
    addressLabel->setToolTip("本机监听地址");
    endpointRow->addWidget(addressLabel); endpointRow->addWidget(addresses, 1);
    endpointRow->addWidget(new QLabel("端口")); endpointRow->addWidget(port);
    layout->addLayout(endpointRow);
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(6);
    for (auto *button : {start, stop, save}) actionRow->addWidget(button, 1);
    layout->addLayout(actionRow);
    auto *stale = new QSpinBox; stale->setObjectName("networkStaleSeconds"); stale->setRange(1, 3600);
    stale->setValue(preferences.value("network/staleMs", 5000).toInt() / 1000); stale->setSuffix(" 秒");
    stale->setToolTip("上位机读数失效时间，可按实际状态上传周期调整；不是固件协议参数。");
    stale->hide();
    auto *status = new QLabel(this); status->setObjectName("networkConnectionStatus");
    status->setWordWrap(true); layout->addWidget(status);
    auto *table = new QTableWidget(4, 3); table->setObjectName("networkReadings");
    table->setHorizontalHeaderLabels({"网口回读项目", "当前值", "说明"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setShowGrid(false); table->setAlternatingRowColors(true); table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setMinimumHeight(165); table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); layout->addWidget(table, 1);
    auto *counts = new QLabel(this); counts->setObjectName("networkFrameCounts");
    counts->setWordWrap(true); layout->addWidget(counts);
    auto *exportResult = new QLabel(this); exportResult->setObjectName("networkExportResult");
    exportResult->setWordWrap(true); exportResult->hide(); layout->addWidget(exportResult);
    connect(start, &QPushButton::clicked, this, [=] {
        controller->startNetworkListening(addresses->currentText(), quint16(port->value()), stale->value() * 1000);
    });
    connect(stop, &QPushButton::clicked, controller, &AppController::stopNetworkListening);
    connect(save, &QPushButton::clicked, this, [this, controller, exportResult] {
        QString selectedFilter;
        auto path = QFileDialog::getSaveFileName(this, "导出十六进制收发报文", "网口报文.txt",
            "十六进制文本 (*.txt);;JSON诊断 (*.json)", &selectedFilter);
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += selectedFilter.startsWith("JSON") ? ".json" : ".txt";
        const bool saved = controller->exportNetworkFrames(path);
        exportResult->setText(saved ? "已导出：" + path : "导出失败，请检查保存位置是否可写：" + path);
        exportResult->show();
    });
    const auto update = [=] {
        const auto data = controller->networkStatus();
        const bool listening = data.value("listening").toBool();
        const bool busy = controller->phase() == AppController::Phase::Acquiring || controller->phase() == AppController::Phase::Analyzing;
        start->setEnabled(!listening && !busy); stop->setEnabled(listening);
        addresses->setEnabled(!listening); port->setEnabled(!listening); stale->setEnabled(!listening);
        save->setEnabled(true);
        start->setToolTip(listening ? "网口正在监听" : "开始监听仪器的TCP连接");
        stop->setToolTip(listening ? "停止当前网口监听" : "开始监听后可停止");

        status->setText(data.isEmpty() ? "尚未监听。可与485同时回读，调谐启停在射频页操作。" : data.value("message").toString()
            + (data.value("connected").toBool() ? " · 更新于" + data.value("lastReadback").toString() : QString()));
        addresses->setToolTip(status->text());
        table->setToolTip(status->text());
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
        counts->setText(QString("接收 %1 字节 · 有效 %2 帧 · 未解析 %3 帧 · 丢弃 %4 字节")
            .arg(data.value("receivedBytes", 0).toString()).arg(data.value("validFrames", 0).toString())
            .arg(data.value("unparsedFrames", 0).toString()).arg(data.value("rejectedBytes", 0).toString()));
    };
    connect(controller, &AppController::instrumentSettingsChanged, this, update);
    connect(controller, &AppController::phaseChanged, this, update); update();
}
}
