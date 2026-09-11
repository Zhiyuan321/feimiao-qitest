#include "ui/MainWindow.h"
#include "ui/Rs485ConnectionPanel.h"
#include "ui/NetworkConnectionPanel.h"
#include <QTabWidget>
#include "ui/MethodEditorDialog.h"
#include "ui/InstrumentWorkbench.h"
#include "ui/DeviceWaveformPanel.h"
#include "device/VendorControlCatalog.h"
#include "domain/DisplayLabels.h"
#include "ui/CalibrationPage.h"
#include "ui/RoundedComboBox.h"
#include "ai/AiCommandRouter.h"
#include "ai/AiToolProtocol.h"
#include "core/QuantitationEngine.h"
#include "core/ChromatogramEngine.h"
#include "ui/ChromatogramDialog.h"
#include "core/QtCompat.h"
#include "core/PlatformPaths.h"
#include "ui/scientz/models/ScientzActionRegistry.h"
#include "ui/scientz/theme/ScientzTheme.h"

#include <QApplication>
#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QListView>
#include <QFormLayout>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QTemporaryFile>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainterPath>
#include <QRegion>
#include <QMenu>
#include <QPlainTextEdit>
#include <QButtonGroup>
#include "ui/UserStandardsPage.h"
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSpinBox>
#include "ui/ChatTranscript.h"
#include <QStandardPaths>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDesktopServices>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QTextStream>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace qitest {
namespace {
// A hidden editor must not force the active small-screen page to its own
// minimum height. The enclosing scroll area handles only the current page.
class CurrentPageStack final : public QStackedWidget {
public:
    CurrentPageStack() {
        connect(this,&QStackedWidget::currentChanged,this,[this]{updateGeometry();});
    }
    QSize sizeHint() const override {return currentWidget()?currentWidget()->sizeHint():QSize(0,0);}
    QSize minimumSizeHint() const override {return currentWidget()?currentWidget()->minimumSizeHint():QSize(0,0);}
};
// A local accidental-touch guard, explicitly not an authentication boundary.
// Escape/window-close must not silently release it; use the labelled button.
class OperationGuardDialog final : public QDialog {
public:
    using QDialog::QDialog;
    void reject() override {}
};
}
namespace {

QLabel *makeLabel(const QString &text, const char *role = nullptr) {
    auto *label = new QLabel(text);
    if (role) label->setProperty("sciTone", role);
    return label;
}

QFrame *separator(bool vertical = false) {
    auto *line = new QFrame;
    line->setFrameShape(vertical ? QFrame::VLine : QFrame::HLine);
    line->setObjectName("separator");
    line->setProperty("orientation", vertical ? "vertical" : "horizontal");
    if (vertical) line->setFixedWidth(1);
    else line->setFixedHeight(1);
    return line;
}

QIcon commandIcon(const QString &name) {
    return QIcon(QString(":/qitest/resources/icons/%1.svg").arg(name));
}

void prepareTableRows(QTableWidget *table, int visibleRows) {
    if (table->rowCount() < visibleRows) table->setRowCount(visibleRows);
    for (int row = 0; row < table->rowCount(); ++row)
        table->setRowHidden(row, row >= visibleRows);
}

void polishDataTable(QTableWidget *table) {
    if (!table) return;
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setCornerButtonEnabled(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMouseTracking(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(38);
    table->horizontalHeader()->setHighlightSections(false);
}

QTableWidgetItem *setTableText(QTableWidget *table, int row, int column, const QString &text) {
    auto *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        table->setItem(row, column, item);
    }
    item->setText(text);
    item->setToolTip(text);
    const auto *header = table->horizontalHeaderItem(column);
    const QString title = header ? header->text() : QString{};
    if (title.contains("浓度") || title.contains("证据") || title.contains("版本")
        || title.contains("校验值") || title.contains("m/z") || title.contains("强度"))
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    else
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return item;
}

QWidget *readoutRow(const QString &name, const QString &value, const QString &unit, bool healthy = true) {
    auto *row = new QWidget;
    row->setProperty("sciRole", "readout");
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *nameLabel = makeLabel(name, "metadata");
    nameLabel->setObjectName("readoutName");
    auto *valueLabel = makeLabel(value, "readoutValue");
    valueLabel->setObjectName("readoutValue");
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (!healthy) valueLabel->setProperty("sciState", "critical");
    auto *unitLabel = makeLabel(unit, "metadata");
    unitLabel->setObjectName("readoutUnit");
    auto *measurement = new QHBoxLayout;
    measurement->setSpacing(5);
    measurement->addWidget(valueLabel);
    measurement->addWidget(unitLabel);
    measurement->addStretch();
    layout->addWidget(nameLabel);
    layout->addLayout(measurement);
    row->setFixedHeight(46);
    return row;
}

QWidget *placeholderPage(const QString &technical, const QString &title, const QString &detail) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addStretch();
    auto *technicalLabel = makeLabel(technical, "technical");
    technicalLabel->setAlignment(Qt::AlignCenter);
    auto *titleLabel = makeLabel(title, "pageTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    auto *detailLabel = makeLabel(detail, "secondary");
    detailLabel->setAlignment(Qt::AlignCenter);
    detailLabel->setWordWrap(true);
    layout->addWidget(technicalLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(detailLabel);
    layout->addStretch();
    return page;
}

} // namespace

MainWindow::MainWindow(AppController *controller, QWidget *parent)
    : QMainWindow(parent), controller_(controller) {
    setWindowTitle("飞秒质谱工作站");
    resize(1024, 768);
    setMinimumSize(1024, 700);
    if (auto *screen = QGuiApplication::primaryScreen())
        resize(size().boundedTo(screen->availableGeometry().size()));

    actions_ = new Scientz::Ui::ActionRegistry(this);
    // macOS Accessibility can still be reading the selected table row while a
    // button action is delivered. Coalesce method changes and update the table
    // after that accessibility transaction has finished instead of rewriting
    // QTableWidgetItems synchronously from the click handler.
    methodRefreshTimer_ = new QTimer(this);
    methodRefreshTimer_->setSingleShot(true);
    methodRefreshTimer_->setInterval(160);
    connect(methodRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshMethods);
    settingsPages_ = {
        {"仪器配置", {"运行状态", "常用部件", "辅助部件", "参数预设", "射频调谐", "质量轴校准", "注射泵", "降温与关机", "硬件接入说明"}},
        {"视图", {"显示布局"}},
        {"数据处理", {"处理管线"}},
        {"参考谱库", {"参考谱库"}},
        {"定量曲线", {"定量曲线"}},
        {"用户及参数设置", {"账户与权限"}},
        {"帮助", {"操作与状态"}},
        {"清洗模式", {"清洗流程"}},
        {"载气节省", {"载气节省"}},
        {"离子源", {"离子源状态与设定"}},
        {"锁屏", {"会话保护"}}
    };

    rootStack_ = new QStackedWidget;
    rootStack_->setObjectName("rootStack");
    rootStack_->addWidget(createLoginPage());
    rootStack_->addWidget(createStartupPage());
    rootStack_->addWidget(createWorkspacePage());
    setCentralWidget(rootStack_);
    applyDesignSystem();

    connect(controller_, &AppController::phaseChanged, this, &MainWindow::updatePhase);
    connect(controller_, &AppController::spectrumChanged, this, [this](const auto &points) {
        spectrumPlot_->setPoints(points);
        spectrumPlot_->setAxisLabels("m/z", "记录分析谱");
        if (emptyDataBanner_)
            emptyDataBanner_->setVisible(height() >= 620 && points.isEmpty() && controller_->phase() == AppController::Phase::Ready);
    });
    connect(controller_, &AppController::scanSeriesChanged, this, [this] {
        ticPlot_->setAxisLabels("时间 / s", "总离子信号");
        ticPlot_->setPoints(ChromatogramEngine::trace(controller_->scans(), ChromatogramEngine::Kind::Tic));
        // Keep the three scientific views in place, but never invent a time
        // series from one spectrum. An absent series stays explicitly empty.
        if (ticPlot_->points().size() < 2) ticPlot_->setPoints({});
        eicRefreshTimer_->stop();
        refreshRunEic();
    });
    connect(controller_, &AppController::analysisCompleted, this, &MainWindow::showResult);
    connect(controller_, &AppController::analysisCompleted, this, [this] {
        const auto trend = controller_->bundledIntensityTrend();
        if (trend.isEmpty()) return;
        ticPlot_->setPoints(trend);
        ticPlot_->setAxisLabels("原始谱序号（非时间）", "总强度");
        // 仅预览原始行序趋势，不将未知时间单位换算为秒。
        if (eicMz_->value() <= 0 && !controller_->result().processedSpectrum.points.isEmpty()) {
            const auto &points = controller_->result().processedSpectrum.points;
            const auto peak = std::max_element(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.intensity < b.intensity; });
            eicMz_->setValue(peak->mz);
        }
        refreshRunEic();
    });
    connect(controller_, &AppController::notice, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });
    connect(controller_, &AppController::instrumentConfirmationRequired, this,
        [this](const QString &key, const QVariant &value) {
            if (QMessageBox::warning(this, "确认仪器操作",
                    "此操作会改变真实仪器状态。请核对设备、安全互锁和目标值。\n"
                    + key + " → " + value.toString() + "\n是否继续？",
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                controller_->updateInstrumentSetting(key, value, true);
        });
    connect(controller_, &AppController::instrumentCommandPending, this,
        [this](const QString &, bool pending) {
            for (auto *widget : findChildren<QWidget *>())
                if (widget->property("instrumentControl").toBool()) widget->setEnabled(!pending && !controller_->instrumentReadOnly());
        });
    connect(controller_, &AppController::instrumentSettingsChanged, this, [this] {
        if (controller_->instrumentReadOnly())
            for (auto *widget : findChildren<QWidget *>())
                if (widget->property("instrumentControl").toBool()) widget->setEnabled(false);
        if (settingsDetailAction_ && settingsDetailStack_->currentIndex() == 0)
            populateSettingsDetail(settingsDetailAction_->property("module").toString(),
                                   settingsDetailAction_->property("subpage").toString());
    });
    QTimer::singleShot(0, this, [this] {
        if (controller_->instrumentReadOnly())
            for (auto *widget : findChildren<QWidget *>())
                if (widget->property("instrumentControl").toBool()) widget->setEnabled(false);
    });
    connect(controller_, &AppController::aiStateChanged, this, [this](const QString &state) {
        if (aiStatus_) { aiStatus_->setText(state); aiStatus_->show(); }
    });
    connect(controller_, &AppController::aiBusyChanged, this, [this](bool busy) {
        aiRequestBusy_ = busy;
        aiSendButton_->setEnabled(true);
        aiSendButton_->setText(busy ? "停止" : "发送");
        aiSendButton_->setToolTip(busy ? "停止本次深度回答，保留提问与已有对话" : "按 Return 发送");
        if (!busy && aiStatus_) aiStatus_->setText("本地助手就绪");
    });
    connect(controller_, &AppController::aiAssistantFailed, this, [this](const QString &message) {
        if (aiConversation_) aiConversation_->appendMessage(ChatTranscript::Role::Assistant, message);
        pendingChatQuestions_.clear();
        if (aiSendButton_ && !aiRequestBusy_) {
            aiSendButton_->setEnabled(true);
            aiSendButton_->setText("发送");
        }
    });
    connect(controller_, &AppController::aiExplanationReady, this, [this](const QString &text) {
        if (aiOutput_) {
            const QString preview = text.size() > 120 ? text.left(120) + "..." : text;
            aiOutput_->setText(preview);
            aiOutput_->setVisible(true);
        }
    });
    connect(controller_, &AppController::aiAssistantAnswerReady, this,
        [this](const QString &question, const QString &text) {
            if (!aiConversation_) return;
            if (!pendingChatQuestions_.removeOne(question))
                aiConversation_->appendMessage(ChatTranscript::Role::User, question);
            aiConversation_->appendMessage(ChatTranscript::Role::Assistant, text);
            if (aiSendButton_ && !aiRequestBusy_) {
                aiSendButton_->setEnabled(true);
                aiSendButton_->setText("发送");
            }
            refreshAiContext();
        });
    connect(controller_, &AppController::aiOperationProposed, this,
        [this](const QString &question, const QString &toolEnvelope) {
            const auto call = AiToolProtocol::parseEnvelope(
                "<tool_call>" + toolEnvelope + "</tool_call>");
            if (!call || !executeAssistantCommand(call->command, question)) {
                if (aiConversation_)
                    aiConversation_->appendMessage(ChatTranscript::Role::Assistant,
                        "未执行：操作未通过本地工具白名单或风险门控。");
            }
        });
    updateWorkspaceLayout();
}

QWidget *MainWindow::createLoginPage() {
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *body = new QWidget;
    body->setObjectName("loginBody");
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(80, 48, 80, 48);
    bodyLayout->setSpacing(72);
    bodyLayout->addStretch();
    auto *intro = new QWidget;
    intro->setObjectName("loginIntro");
    auto *introLayout = new QVBoxLayout(intro);
    introLayout->addStretch();
    introLayout->addWidget(makeLabel("飞秒质谱工作站", "loginTitle"));
    introLayout->addWidget(makeLabel("准备、检测、复核与报告", "sectionTitle"));
    introLayout->addStretch();
    intro->setFixedWidth(300);
    bodyLayout->addWidget(intro);

    auto *formCard = new QFrame;
    formCard->setObjectName("panel");
    formCard->setFixedWidth(390);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(30, 28, 30, 28);
    formLayout->setSpacing(10);
    formLayout->addWidget(makeLabel("进入工作站", "pageTitle"));
    auto *skip = new QPushButton("进入工作站");
    skip->setProperty("sciRole", "primary");
    skip->setMinimumHeight(38);
    formLayout->addWidget(skip);
    formLayout->addWidget(separator());
    auto *accountToggle = new QToolButton;
    accountToggle->setText("使用正式账户登录  ›");
    accountToggle->setCheckable(true);
    accountToggle->setProperty("sciRole", "disclosure");
    formLayout->addWidget(accountToggle);
    auto *accountFields = new QWidget;
    auto *accountLayout = new QVBoxLayout(accountFields);
    accountLayout->setContentsMargins(0, 4, 0, 0);
    accountLayout->setSpacing(8);
    accountLayout->addWidget(makeLabel("用户名", "fieldLabel"));
    username_ = new QLineEdit("operator");
    accountLayout->addWidget(username_);
    accountLayout->addWidget(makeLabel("密码", "fieldLabel"));
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    accountLayout->addWidget(password_);
    loginError_ = makeLabel("", "error");
    accountLayout->addWidget(loginError_);
    auto *actions = new QHBoxLayout;
    auto *login = new QPushButton("登录正式账户");
    actions->addWidget(login);
    actions->addStretch();
    accountLayout->addLayout(actions);
    const bool accountConfigured = !qEnvironmentVariable("QITEST_OPERATOR_PASSWORD").isEmpty();
    login->setEnabled(accountConfigured);
    if (!accountConfigured)
        accountLayout->addWidget(makeLabel("尚未配置账户，可从上方进入工作站。", "metadata"));
    accountFields->setVisible(false);
    formLayout->addWidget(accountFields);
    formLayout->addStretch();
    bodyLayout->addWidget(formCard, 0, Qt::AlignVCenter);
    bodyLayout->addStretch();
    pageLayout->addWidget(body, 1);

    auto begin = [this] {
        const QString configuredPassword = qEnvironmentVariable("QITEST_OPERATOR_PASSWORD");
        if (configuredPassword.isEmpty() || password_->text() != configuredPassword) {
            loginError_->setText(configuredPassword.isEmpty()
                ? "尚未配置本地账户，请从上方进入工作站"
                : "用户名或密码不正确");
            return;
        }
        loginError_->clear();
        controller_->setSessionOperator(username_->text());
        startStartupSequence();
    };
    connect(login, &QPushButton::clicked, this, begin);
    connect(password_, &QLineEdit::returnPressed, this, begin);
    connect(skip, &QPushButton::clicked, this, [this] {
        controller_->setOfflineDemoSession();
        startStartupSequence();
    });
    connect(accountToggle, &QToolButton::toggled, this, [accountToggle, accountFields](bool open) {
        accountFields->setVisible(open);
        accountToggle->setText(open ? "收起正式账户登录  ⌄" : "使用正式账户登录  ›");
    });
    return page;
}

QWidget *MainWindow::createStartupPage() {
    auto *page = new QWidget;
    page->setObjectName("startupPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(60, 60, 60, 60);
    layout->addStretch();
    auto *title = makeLabel("飞秒质谱工作站", "startupTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    startupLabel_ = makeLabel("加载本地程序", "startupDetail");
    startupLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(startupLabel_);
    startupProgress_ = new QProgressBar;
    startupProgress_->setRange(0, 100);
    startupProgress_->setTextVisible(false);
    startupProgress_->setFixedWidth(420);
    layout->addWidget(startupProgress_, 0, Qt::AlignHCenter);
    layout->addWidget(makeLabel("程序  ·  方法与谱库  ·  仪器状态  ·  真空与温控", "startupSteps"), 0, Qt::AlignHCenter);
    layout->addStretch();
    return page;
}

void MainWindow::startStartupSequence() {
    rootStack_->setCurrentIndex(1);
    startupProgress_->setValue(0);
    const auto checks = controller_->startupChecks();
    auto *timer = new QTimer(this);
    timer->setInterval(260);
    connect(timer, &QTimer::timeout, this, [this, timer, checks] {
        const int value = std::min(100, startupProgress_->value() + 20);
        startupProgress_->setValue(value);
        const int index = std::clamp(value / 20 - 1, 0, static_cast<int>(checks.size()) - 1);
        const auto &check = checks[index];
        startupLabel_->setText(QString("%1  %2 · %3")
            .arg(check.passed ? "✓" : (check.blocking ? "✕" : "△"), check.name, check.detail));
        if (value >= 100) {
            timer->stop();
            timer->deleteLater();
            const bool blockingFailure = std::any_of(checks.begin(), checks.end(), [](const auto &check) {
                return check.blocking && !check.passed;
            });
            if (blockingFailure) {
                startupLabel_->setText("启动检查失败：请修复检测记录或仪器适配器后重试");
            } else {
                QTimer::singleShot(220, this, &MainWindow::showWorkspace);
            }
        }
    });
    timer->start();
}

QWidget *MainWindow::createWorkspacePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *commands = new QWidget;
    commands->setObjectName("commandBar");
    commands->setFixedHeight(64);
    auto *commandLayout = new QHBoxLayout(commands);
    commandLayout->setContentsMargins(12, 0, 12, 0);
    commandLayout->setSpacing(3);
    assistantButton_ = createCommandButton("OpenAssistant", "智能台", commandIcon("assistant"));
    assistantButton_->setToolTip("打开智能台，与当前页面并行操作");
    commandLayout->addWidget(assistantButton_);
    commandLayout->addWidget(separator(true));
    commandLayout->addStretch(1);
    auto *runMethod = createCommandButton("OpenHome", "样品分析", commandIcon("run"));
    runMethod->setProperty("sciState", "current");
    actions_->registerAction("StartRun", "开始检测", commandIcon("run"));
    auto *report = createCommandButton("OpenReport", "报告查看", commandIcon("report"));
    auto *method = createCommandButton("OpenMethod", "方法选择", commandIcon("method"));
    runMethod->setToolTip("打开运行工作区；不会开始或停止检测");
    method->setToolTip("编辑、保存和启用检测方法版本");
    report->setToolTip("复核结果并生成报告");
    runMethod->setFixedWidth(78);
    method->setFixedWidth(78);
    report->setFixedWidth(78);
    // 客户主流程：先选方法，再分析，最后查看报告。历史存储不随导航精简而删除。
    actions_->registerAction("OpenLibrary", "参考谱库", commandIcon("library"));
    actions_->registerAction("OpenQuantitation", "定量曲线", commandIcon("curve"));
    for (auto *button : {method, runMethod, report}) {
        button->setFixedWidth(96);
        commandLayout->addWidget(button);
    }
    commandLayout->addStretch(1);
    auto *instrumentStatusAction = actions_->registerAction(
        "OpenInstrumentStatus", "仪器", commandIcon("view"));
    instrumentToolsButton_ = new QToolButton;
    instrumentToolsButton_->setDefaultAction(instrumentStatusAction);
    instrumentToolsButton_->setToolTip("显示或收起仪器状态");
    instrumentToolsButton_->setIconSize({20, 20});
    instrumentToolsButton_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    instrumentToolsButton_->setProperty("sciRole", "command");
    instrumentToolsButton_->setProperty("actionId", "OpenInstrumentStatus");
    instrumentToolsButton_->setProperty("sciState", "current");
    instrumentToolsButton_->setFixedSize(62, 50);
    auto *settingsButton = createCommandButton("OpenSettings", "设置", commandIcon("settings"));
    settingsButton->setToolTip("打开或收起设置与维护");
    commandLayout->addWidget(settingsButton);
    commandLayout->addWidget(instrumentToolsButton_);
    auto *batteryStatus = new QLabel;
    batteryStatus->setObjectName("batteryStatus");
    // No battery telemetry is defined by the supplied protocols. A filled
    // battery would falsely imply a measured charge level.
    batteryStatus->setText("电量\n—");
    batteryStatus->setAlignment(Qt::AlignCenter);
    batteryStatus->setToolTip("仪器电池状态尚未接入，不能显示实际电量或充电状态");
    batteryStatus->setAccessibleName("仪器电池电量未知");
    batteryStatus->setFixedSize(54, 48);
    commandLayout->addWidget(batteryStatus);
    layout->addWidget(commands);

    workspaceStack_ = new CurrentPageStack;
    workspaceStack_->addWidget(createHomePage());
    // Keep page frames fixed; long records use their own data views.
    workspaceStack_->addWidget(createSettingsPage());
    workspaceStack_->addWidget(createReportPage());
    workspaceStack_->addWidget(createMethodPage());
    auto *workspaceBody = new QWidget;
    workspaceBody->setObjectName("workspaceBody");
    workspaceBodyLayout_ = new QGridLayout(workspaceBody);
    workspaceBodyLayout_->setContentsMargins(0, 0, 0, 0);
    workspaceBodyLayout_->setSpacing(0);
    aiAssistantPanel_ = createAiAssistantPage();
    aiAssistantPanel_->setVisible(false);
    workspaceStack_->setObjectName("centralWorkspace");
    workspaceStack_->setMinimumWidth(640);
    instrumentTools_ = createMonitorPanel();
    instrumentTools_->setMinimumWidth(260);
    instrumentTools_->setMaximumWidth(290);
    workspaceBodyLayout_->addWidget(aiAssistantPanel_, 0, 0, 2, 1);
    workspaceBodyLayout_->addWidget(workspaceStack_, 0, 1, 2, 1);
    workspaceBodyLayout_->addWidget(instrumentTools_, 0, 2, 2, 1);
    workspaceBodyLayout_->setColumnStretch(1, 1);
    workspaceBodyLayout_->setRowStretch(0, 1);
    workspaceBodyLayout_->setRowStretch(1, 1);
    layout->addWidget(workspaceBody, 1);

    connect(actions_->action("OpenHome"), &QAction::triggered, this, [this] { setWorkspaceSection(0); });
    connect(actions_->action("StartRun"), &QAction::triggered, this, [this] {
        const auto phase = controller_->phase();
        if (phase == AppController::Phase::Acquiring || phase == AppController::Phase::Analyzing
            || showReportAfterRunSaved_ || detectionAwaitingConfirmation_) return;
        setWorkspaceSection(0);
        {
            if (findChild<QDialog *>("sampleSaveDialog")) return;
            auto *dialog = new QDialog(this);
            dialog->setObjectName("sampleSaveDialog");
            dialog->setWindowTitle("样本信息与保存");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowModality(Qt::WindowModal);
            dialog->setMinimumWidth(560);
            auto *layout = new QVBoxLayout(dialog);
            auto *form = new QFormLayout;
            form->setVerticalSpacing(12); layout->addLayout(form);
            auto field = [form](const QString &label, const char *name) {
                auto *input = new QLineEdit; input->setObjectName(name); input->setMaxLength(80);
                form->addRow(label, input); return input;
            };
            auto *sample = field("样本编号 *", "sampleNumber");
            sample->setText("S-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"));
            auto *person = field("姓名（选填）", "samplePersonName");
            auto *identity = field("身份证号（选填）", "sampleIdentityNumber");
            identity->setMaxLength(18);
            auto *name = field("文件名 *", "sampleFileName");
            name->setText(sample->text());
            connect(sample, &QLineEdit::textChanged, name, &QLineEdit::setText);
            auto *folder = field("保存位置 *", "sampleSaveFolder");
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
            const QString defaultFolder = PlatformPaths::documentsSubdirectory("飞秒检测数据");
            QDir().mkpath(defaultFolder);
            folder->setText(PlatformPaths::nativeDisplay(PlatformPaths::existingDirectoryOrDefault(
                settings.value("sampleSaveFolder").toString(), defaultFolder)));
            auto *browse = new QPushButton("选择文件夹"); form->addRow("", browse);
            auto *options = new QPushButton("更改保存位置");
            options->setCheckable(true); form->addRow("", options);
            for (auto *input : {name, folder}) { input->hide(); form->labelForField(input)->hide(); }
            browse->hide();
            connect(options, &QPushButton::toggled, dialog, [=](bool visible) {
                for (auto *input : {name, folder}) { input->setVisible(visible); form->labelForField(input)->setVisible(visible); }
                browse->setVisible(visible);
            });
            connect(browse, &QPushButton::clicked, dialog, [dialog,folder] {
                const auto path = QFileDialog::getExistingDirectory(dialog,"保存位置",folder->text());
                if (!path.isEmpty()) folder->setText(PlatformPaths::nativeDisplay(path));
            });
            auto *error = new QLabel; error->setWordWrap(true); error->setProperty("sciTone", "error"); layout->addWidget(error);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            buttons->button(QDialogButtonBox::Ok)->setText("保存并开始");
            buttons->button(QDialogButtonBox::Ok)->setObjectName("confirmSampleStart");
            buttons->button(QDialogButtonBox::Cancel)->setText("取消"); layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
            connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
                QString filename = name->text().trimmed();
                bool invalid = filename.isEmpty() || filename.endsWith('.') || filename.endsWith(' ');
                for (const QChar ch : filename) if (ch.unicode()<32 || QString("\\/:*?\"<>|").contains(ch)) invalid=true;
                const auto stem = filename.section('.',0,0).toUpper();
                if (QStringList{"CON","PRN","AUX","NUL","COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9","LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"}.contains(stem)) invalid=true;
                if (sample->text().trimmed().isEmpty() || invalid) { error->setText("请填写样本编号，文件名不能包含特殊字符。"); return; }
                const QDir directory(folder->text().trimmed());
                if (!directory.exists() || !QFileInfo(directory.absolutePath()).isDir()) { error->setText("请选择存在的文件夹。"); return; }
                if (!filename.endsWith(".qit.json",Qt::CaseInsensitive)) filename += ".qit.json";
                const QString path = directory.absoluteFilePath(filename);
                if (QFileInfo::exists(path)) { error->setText("已有同名文件，请更换名称，避免覆盖。"); return; }
                QTemporaryFile probe(directory.absoluteFilePath(".qitest-write-XXXXXX"));
                if (!probe.open()) { error->setText("该位置不能写入，请选择其他文件夹。"); return; }
                QSettings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01").setValue("sampleSaveFolder",directory.absolutePath());
                const QJsonObject info{{"sample_id",sample->text().trimmed()}, {"person_name",person->text().trimmed()},
                    {"identity_number",identity->text().trimmed()}};
                dialog->accept();
                showReportAfterRunSaved_ = true;
                controller_->startSampleDetection(info,path);
                // Rejected starts do not emit a phase change or a completion.
                if(controller_->phase()!=AppController::Phase::Acquiring
                    && controller_->phase()!=AppController::Phase::Analyzing
                    && !detectionAwaitingConfirmation_) showReportAfterRunSaved_=false;
                updatePhase(controller_->phase(),phaseLabel_->text());
            });
            dialog->open(); sample->setFocus();
        }
    });
    connect(actions_->action("OpenMethod"), &QAction::triggered, this, [this] {
        refreshMethods(); setWorkspaceSection(5);
    });
    connect(actions_->action("OpenLibrary"), &QAction::triggered, this, [this] { setWorkspaceSection(4); });
    connect(actions_->action("OpenQuantitation"), &QAction::triggered, this, [this] {
        setWorkspaceSection(6);
    });
    connect(actions_->action("OpenReport"), &QAction::triggered, this, [this] {
        refreshReport(controller_->currentRun()); setWorkspaceSection(2);
    });
    connect(actions_->action("OpenSettings"), &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1 && workspaceStack_->isVisible()) closeSettings();
        else setWorkspaceSection(1);
    });
    connect(actions_->action("OpenAssistant"), &QAction::triggered, this, [this] {
        setAssistantVisible(!assistantTargetVisible_);
    });
    connect(instrumentStatusAction, &QAction::triggered, this, [this] {
        if (!instrumentTools_) return;
        setInstrumentToolsVisible(!instrumentTargetVisible_);
    });
    connect(controller_, &AppController::runSaved, this, [this](const RunSummary &run) {
        refreshReport(run);
        if (showReportAfterRunSaved_) {
            showReportAfterRunSaved_ = false;
            detectionAwaitingConfirmation_ = true;
            updatePhase(controller_->phase(),phaseLabel_->text());
            setWorkspaceSection(2);
            // Explicit confirmation is required; Escape and the window close action
            // must not silently re-enable acquisition.
            class CompletionDialog final : public QDialog {
            public:
                explicit CompletionDialog(QWidget *parent):QDialog(parent){}
                void reject() override {}
            };
            auto *complete=new CompletionDialog(this);
            complete->setObjectName("detectionCompletedDialog");
            complete->setWindowTitle("检测完成");
            complete->setWindowFlags(Qt::Dialog|Qt::CustomizeWindowHint|Qt::WindowTitleHint);
            complete->setWindowModality(Qt::WindowModal);
            complete->setAttribute(Qt::WA_DeleteOnClose);
            complete->setMinimumWidth(320);
            auto *layout=new QVBoxLayout(complete);layout->setContentsMargins(28,24,28,24);layout->setSpacing(20);
            auto *message=new QLabel("检测已完成");message->setAlignment(Qt::AlignCenter);layout->addWidget(message);
            auto *confirm=new QPushButton("确认");confirm->setObjectName("confirmDetectionCompleted");
            confirm->setMinimumHeight(44);confirm->setProperty("sciRole","primary");layout->addWidget(confirm);
            connect(confirm,&QPushButton::clicked,complete,&QDialog::accept);
            connect(complete,&QDialog::accepted,this,[this] {
                detectionAwaitingConfirmation_=false;
                updatePhase(controller_->phase(),phaseLabel_->text());
            });
            complete->open();confirm->setFocus();
        }
    });
    connect(controller_, &AppController::phaseChanged, this, [this](AppController::Phase phase) {
        if (phase == AppController::Phase::Failed) showReportAfterRunSaved_ = false;
    });
    connect(controller_, &AppController::reportGenerated, this, [this](const QString &path) {
        reportStatus_->setText("报告已生成：" + PlatformPaths::nativeDisplay(path));
        statusBar()->showMessage("报告已生成", 3000);
        if (reportExportButton_) {
            reportExportButton_->setText("已生成");
            QTimer::singleShot(1600, this, [this] {
                if (reportExportButton_) reportExportButton_->setText("生成 PDF");
            });
        }
        if (!QStandardPaths::isTestModeEnabled())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(controller_, &AppController::methodsChanged, this, [this] {
        if (methodRefreshTimer_) methodRefreshTimer_->start();
    });
    return page;
}

QWidget *MainWindow::createHomePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *context = new QWidget;
    context->setObjectName("contextHeader");
    context->setFixedHeight(40);
    auto *contextLayout = new QHBoxLayout(context);
    contextLayout->setContentsMargins(16, 0, 16, 0);
    phaseLabel_ = makeLabel("就绪", "contextTitle");
    phaseLabel_->setObjectName("runPhaseLabel");
    contextLayout->addWidget(makeLabel("●", "healthy"));
    contextLayout->addWidget(phaseLabel_);
    contextLayout->addStretch();
    workflowLabel_ = makeLabel("● 准备  ›  采集  ›  分析  ›  复核/报告", "metadata");
    contextLayout->addWidget(workflowLabel_);
    layout->addWidget(context);

    // Compact live context, separate from recorded scientific evidence. The
    // clock measures this software session, never the physical device uptime.
    auto *runStatus = new QWidget;
    runStatus->setObjectName("runStatusStrip");
    runStatus->setFixedHeight(36);
    runStatus->setMinimumWidth(138);
    auto *runStatusLayout = new QHBoxLayout(runStatus);
    runStatusLayout->setContentsMargins(4, 0, 4, 0);
    runStatusLayout->setSpacing(6);
    auto *deviceState = makeLabel({}, "metadata"); deviceState->setObjectName("runDeviceState");
    auto *detectionTime = makeLabel({}, "metadata"); detectionTime->setObjectName("runDetectionTime");
    auto *softwareTime = makeLabel({}, "metadata"); softwareTime->setObjectName("runSoftwareTime");
    for (auto *label : {deviceState, detectionTime}) {
        label->setWordWrap(false); label->setMinimumWidth(0);
        label->setFixedHeight(36);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        runStatusLayout->addWidget(label);
    }
    softwareTime->setWordWrap(false);
    softwareTime->setProperty("sciRole","compactRuntime");
    softwareTime->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    contextLayout->addWidget(softwareTime);
    contextLayout->addWidget(runStatus);
    // Windows 7 上原生 tooltip 窗口可能在这一条每秒刷新时残留成深色块。
    // 状态文字本身已经完整，补充说明放入无浮层的辅助功能描述。
    detectionTime->setAccessibleDescription("从开始采集到分析结束的软件用时");
    softwareTime->setAccessibleDescription("本次软件启动后的运行时间");
    const auto refreshRunStatus = [this, deviceState, detectionTime, softwareTime] {
        const auto health = controller_->health();
        const auto phase = controller_->phase();
        const bool simulation = controller_->instrumentDescriptor().simulation;
        const QString state = !health.connected ? "未连接" : !health.ready ? "未就绪"
            : phase == AppController::Phase::Acquiring ? "采集中"
            : phase == AppController::Phase::Analyzing ? "分析中" : "就绪";
        deviceState->setText((simulation ? QString("系统") : QString("仪器")) + state);
        deviceState->setAccessibleDescription("当前连接与运行状态");
        const qint64 elapsed = controller_->detectionElapsedMs();
        detectionTime->setText(elapsed < 0 ? QString("检测待命")
            : QString("检测 %1 s").arg(elapsed / 1000.0, 0, 'f', 1));
        const qint64 seconds = controller_->softwareElapsedMs() / 1000;
        softwareTime->setText(QString("运行 %1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0')));
    };
    auto *runStatusTimer = new QTimer(runStatus);
    runStatusTimer->setInterval(1000);
    connect(runStatusTimer, &QTimer::timeout, runStatus, [runStatus, refreshRunStatus] {
        if (runStatus->isVisible()) refreshRunStatus();
    });
    connect(controller_, &AppController::phaseChanged, runStatus, refreshRunStatus);
    connect(controller_, &AppController::instrumentSettingsChanged, runStatus, refreshRunStatus);
    refreshRunStatus(); runStatusTimer->start();

    // Three bounded scientific views. Screening review has one home in the
    // report workspace, not a duplicate table below or beside these charts.
    auto *canvas = new QWidget;
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvas->setObjectName("analysisCanvas");
    canvasLayout->setContentsMargins(14, 12, 14, 12);
    canvasLayout->setSpacing(10);

    emptyDataBanner_ = new QFrame;
    emptyDataBanner_->setProperty("sciRole", "emptyState");
    auto *emptyLayout = new QVBoxLayout(emptyDataBanner_);
    emptyLayout->setContentsMargins(14, 10, 14, 10);
    emptyLayout->setSpacing(10);
    auto *emptyTop = new QHBoxLayout;
    emptyTop->setSpacing(10);
    auto *emptyCopy = new QWidget;
    auto *emptyCopyLayout = new QVBoxLayout(emptyCopy);
    emptyCopyLayout->setContentsMargins(0, 0, 0, 0);
    emptyCopyLayout->setSpacing(2);
    emptyCopyLayout->addWidget(makeLabel("检测准备", "sectionTitle"));
    emptyTop->addWidget(emptyCopy, 1);
    auto *importData = new QPushButton("导入数据");
    importData->setIcon(commandIcon("import"));
    emptyTop->addWidget(importData);
    emptyLayout->addLayout(emptyTop);

    canvasLayout->addWidget(emptyDataBanner_);

    auto *plotsRow = new QWidget;
    plotsRow->setObjectName("runPlotsRow");
    auto *plotsLayout = new QVBoxLayout(plotsRow);
    plotsLayout->setContentsMargins(0, 0, 0, 0);
    plotsLayout->setSpacing(6);
    canvasLayout->addWidget(plotsRow, 3);
    ticPlot_ = new SpectrumPlot(SpectrumPlot::Mode::Line);
    ticPlot_->setObjectName("runPrimaryPlot");
    ticPlot_->setMinimumHeight(100);
    ticPlot_->setAxisLabels("时间 / s", "总离子信号");
    ticPlot_->setEmptyMessage("等待时间序列", "单次质谱不生成 TIC");
    ticPlot_->setToolTip("TIC：点击时间点查看该次 MS1 扫描的原始质谱");
    auto *ticContainer = new QWidget;
    ticContainer->setObjectName("runTicPanel");
    auto *ticLayout = new QVBoxLayout(ticContainer);
    ticLayout->setContentsMargins(0, 0, 0, 0);
    ticLayout->setSpacing(0);
    auto *ticHeader = new QWidget;
    ticHeader->setObjectName("panelHeader");
    ticHeader->setFixedHeight(32);
    auto *ticHeaderLayout = new QHBoxLayout(ticHeader);
    ticHeaderLayout->setContentsMargins(10, 0, 10, 0);
    ticHeaderLayout->addWidget(makeLabel("TIC · MS1", "panelTitle"));
    ticHeaderLayout->addStretch();
    auto *example = new QPushButton("载入示例曲线");
    example->setObjectName("loadPublicExample");
    example->setProperty("sciRole", "plotAction");
    example->setFixedSize(120, 30);
    example->setToolTip("载入内置 OpenMS BSA 公开扫描片段；保留原检测记录，不连接仪器，不启动 AI");
    ticHeaderLayout->addWidget(example);
    connect(example, &QPushButton::clicked, controller_, &AppController::loadPublicExample);
    connect(controller_, &AppController::scanSeriesChanged, example, [this, example] {
        example->setVisible(controller_->scans().size() < 2);
    });
    connect(controller_, &AppController::phaseChanged, example, [example](AppController::Phase phase, const QString &) {
        example->setEnabled(phase != AppController::Phase::Acquiring && phase != AppController::Phase::Analyzing);
    });
    auto *traceAnalysis = new QPushButton("提取 / 积分");
    traceAnalysis->setObjectName("openTraceAnalysis");
    traceAnalysis->setProperty("sciRole", "plotAction");
    traceAnalysis->setFixedSize(120, 30);
    traceAnalysis->setToolTip("对导入扫描计算 TIC、BPC、EIC 和指定时间区间的面积");
    ticHeaderLayout->addWidget(traceAnalysis);
    auto *resetPlots = new QToolButton;
    resetPlots->setObjectName("resetRunPlots");
    resetPlots->setIcon(commandIcon("reset-view"));
    resetPlots->setAccessibleName("复位全部曲线视图");
    resetPlots->setToolTip("复位三张图的缩放范围，不清除数据或积分参数");
    resetPlots->setProperty("sciRole", "utility");
    resetPlots->setFixedSize(30, 30);
    ticHeaderLayout->addWidget(resetPlots);
    connect(resetPlots, &QToolButton::clicked, this, [this] {
        for (auto *plot : {ticPlot_, spectrumPlot_, eicPlot_}) plot->resetView();
    });
    connect(traceAnalysis, &QPushButton::clicked, this, [this] {
        auto *dialog = new ChromatogramDialog(controller_->scans(), this);
        dialog->open();
    });
    startButton_ = new QPushButton("开始检测");
    startButton_->setObjectName("runAcquisitionButton");
    startButton_->setProperty("sciRole", "quietAction");
    startButton_->setFixedSize(100, 30);
    startButton_->setToolTip("按当前方法开始检测；检测完成并确认后可再次开始");
    // Keep acquisition at the workspace level, not inside one scientific plot.
    contextLayout->addWidget(startButton_);
    ticLayout->addWidget(ticHeader);
    ticLayout->addWidget(ticPlot_, 1);
    ticContainer->setProperty("sciRole", "workspaceSection");
    plotsLayout->addWidget(ticContainer, 1);

    spectrumPlot_ = new SpectrumPlot(SpectrumPlot::Mode::Sticks);
    spectrumPlot_->setObjectName("runMsPlot");
    spectrumPlot_->setMinimumHeight(100);
    spectrumPlot_->setAccentColor(Scientz::Ui::Colors::Blue500);
    spectrumPlot_->setEmptyMessage("等待进样", "");
    spectrumContainer_ = createPanel("质谱图", "质谱图 · MS1", spectrumPlot_);
    spectrumContainer_->layout()->setContentsMargins(10, 2, 10, 4);
    spectrumContainer_->layout()->setSpacing(0);
    spectrumContainer_->setVisible(true);
    const auto selectScan = [this](double seconds) {
        const SpectrumScan *nearest = nullptr;
        for (const auto &scan : controller_->scans())
            if (scan.msLevel == 1 && (!nearest || std::abs(scan.timeSeconds - seconds) < std::abs(nearest->timeSeconds - seconds)))
                nearest = &scan;
        if (!nearest) return;
        spectrumPlot_->setPoints(nearest->points);
        spectrumPlot_->setAxisLabels(QString("m/z · %1 s").arg(nearest->timeSeconds), "MS1 原始谱");
        spectrumPlot_->setToolTip(QString("原始 MS1 · %1 s；筛查结果仍属于记录中的分析谱图").arg(nearest->timeSeconds));
        statusBar()->showMessage(spectrumPlot_->toolTip(), 5000);
    };
    connect(ticPlot_, &SpectrumPlot::pointActivated, this, selectScan);
    plotsLayout->addWidget(spectrumContainer_, 1);
    eicPlot_ = new SpectrumPlot(SpectrumPlot::Mode::Line);
    eicPlot_->setObjectName("runEicPlot");
    eicPlot_->setMinimumHeight(100);
    eicPlot_->setAccentColor(QColor("#B97824"));
    eicPlot_->setAxisLabels("时间 / s", "提取离子信号");
    eicPlot_->setEmptyMessage("等待时间序列", "");
    auto *eicPanel = new QWidget;
    eicPanel->setObjectName("runEicPanel");
    eicPanel->setProperty("sciRole", "workspaceSection");
    auto *eicLayout = new QVBoxLayout(eicPanel);
    eicLayout->setContentsMargins(10, 2, 10, 4); eicLayout->setSpacing(0);
    auto *eicHeader = new QHBoxLayout;
    eicHeader->addWidget(makeLabel("EIC · MS1", "panelTitle"));
    eicHeader->addStretch();
    // 行内参数采用相同字号、行高和对齐方式，避免标签与编辑器上下错位。
    auto addEicLabel = [eicHeader](const QString &text) {
        auto *label = new QLabel(text);
        label->setProperty("sciRole", "plotInputLabel");
        label->setFixedHeight(30);
        label->setAlignment(Qt::AlignCenter);
        eicHeader->addWidget(label, 0, Qt::AlignVCenter);
    };
    addEicLabel("m/z");
    eicMz_ = new QDoubleSpinBox;
    eicMz_->setObjectName("runEicMz"); eicMz_->setDecimals(6);
    eicMz_->setRange(0, 1e6); eicMz_->setSpecialValueText("选择离子");
    eicMz_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    eicMz_->setProperty("sciRole", "plotInput");
    eicMz_->setFixedSize(112, 30);
    eicMz_->setToolTip("点击质谱峰选择，或填写待提取的 m/z");
    eicMz_->setAlignment(Qt::AlignCenter);
    eicHeader->addWidget(eicMz_, 0, Qt::AlignVCenter);
    addEicLabel("±");
    eicTolerance_ = new QDoubleSpinBox;
    eicTolerance_->setObjectName("runEicTolerance"); eicTolerance_->setDecimals(4);
    eicTolerance_->setRange(0.0001, 100); eicTolerance_->setValue(0.5);
    eicTolerance_->setProperty("sciRole", "plotInput");
    eicTolerance_->setSuffix(" Da"); eicTolerance_->setFixedSize(90, 30);
    eicTolerance_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    eicTolerance_->setAlignment(Qt::AlignCenter);
    eicHeader->addWidget(eicTolerance_, 0, Qt::AlignVCenter);
    eicLayout->addLayout(eicHeader); eicLayout->addWidget(eicPlot_, 1);
    plotsLayout->addWidget(eicPanel, 1);
    // On short embedded displays keep TIC plus one detailed view readable.
    // This only switches presentation; no scans or extraction values are reset.
    auto *detailChoice = new QWidget;
    detailChoice->setObjectName("smallScreenPlotChoice");
    auto *choiceLayout = new QHBoxLayout(detailChoice);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    auto *choiceGroup = new QButtonGroup(detailChoice);
    for (const auto &name : {QString("质谱图"), QString("EIC 提取离子")}) {
        auto *choice = new QPushButton(name);
        choice->setObjectName(name == "质谱图" ? "smallScreenMs" : "smallScreenEic");
        choice->setCheckable(true);
        choice->setProperty("sciRole", "choice");
        choice->setIcon(commandIcon(name == "质谱图" ? "process" : "curve"));
        choice->setFixedHeight(30);
        choiceGroup->addButton(choice);
        choiceLayout->addWidget(choice);
        connect(choice, &QPushButton::toggled, this, [this](bool checked) {
            if (checked) updateWorkspaceLayout();
        });
    }
    choiceGroup->buttons().first()->setChecked(true);
    choiceLayout->addStretch();
    plotsLayout->insertWidget(1, detailChoice);
    detailChoice->hide();
    eicRefreshTimer_ = new QTimer(this); eicRefreshTimer_->setSingleShot(true); eicRefreshTimer_->setInterval(150);
    connect(eicRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshRunEic);
    for (auto *input : {eicMz_, eicTolerance_})
        connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
            eicPlot_->setPoints({}); eicPlot_->setEmptyMessage("正在提取", "");
            eicRefreshTimer_->start();
        });
    connect(spectrumPlot_, &SpectrumPlot::pointActivated, this, [this](double mz) { eicMz_->setValue(mz); });
    connect(eicPlot_, &SpectrumPlot::pointActivated, this, selectScan);
    for (auto *plot : {ticPlot_, spectrumPlot_, eicPlot_})
        connect(plot, &SpectrumPlot::exportFinished, this, [this](bool, const QString &message) {
            statusBar()->showMessage(message, 8000);
        });

    // 样品分析只保留三张质谱分析图。0x82 气压单包是仪器诊断数据，
    // 在“仪器配置 / 运行状态”查看，不与正式检测图谱混在一起。
    layout->addWidget(canvas,1);
    connect(startButton_, &QPushButton::clicked, actions_->action("StartRun"), &QAction::trigger);
    connect(importData, &QPushButton::clicked, this, &MainWindow::importRunArchiveFromDialog);
    return page;
}

QWidget *MainWindow::createMonitorPanel() {
    auto *panel = new QWidget;
    panel->setObjectName("monitorPanel");
    panel->setMinimumWidth(0);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    header->addWidget(makeLabel("仪器监控", "panelTitle"));
    header->addStretch();
    auto *close = new QToolButton;
    close->setText("×");
    close->setToolTip("收起仪器监控");
    close->setProperty("sciRole", "utility");
    close->setFixedSize(32, 32);
    header->addWidget(close);
    layout->addLayout(header);

    auto *alarmDetail = makeLabel("—", "readoutValue");
    alarmDetail->setObjectName("carrierPressureValue");

    auto group = [](const QString &title, const QList<QWidget *> &rows) {
        auto *frame = new QFrame;
        frame->setProperty("sciRole", "monitorGroup");
        auto *groupLayout = new QVBoxLayout(frame);
        groupLayout->setContentsMargins(10, 10, 10, 10);
        groupLayout->setSpacing(4);
        groupLayout->addWidget(makeLabel(title, "panelTitle"));
        for (auto *row : rows) groupLayout->addWidget(row);
        return frame;
    };
    QMap<QString, QLabel *> readings;
    const auto reading = [&readings](const QString &key, const QString &name, const QString &unit) {
        auto *row = new QWidget;
        auto *line = new QHBoxLayout(row); line->setContentsMargins(0,0,0,0); line->setSpacing(8);
        line->addWidget(makeLabel(name, "metadata")); line->addStretch();
        auto *value = makeLabel("—", "readoutValue"); value->setStyleSheet("font-size:18px;");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setProperty("displayUnit", unit);
        line->addWidget(value);
        row->setMinimumHeight(28);
        readings[key] = value;
        readings[key]->setProperty("telemetryKey", key);
        return row;
    };
    auto *pressureRow = new QWidget;
    auto *pressureLayout = new QHBoxLayout(pressureRow);
    pressureLayout->setContentsMargins(0, 0, 0, 0);
    pressureLayout->setSpacing(0);
    pressureLayout->addWidget(makeLabel("气压", "metadata"));
    auto *pressureMeasurement = new QHBoxLayout;
    pressureMeasurement->setContentsMargins(0, 0, 0, 0);
    pressureLayout->addStretch();
    alarmDetail->setStyleSheet("font-size:18px;");
    pressureMeasurement->addWidget(alarmDetail);
    pressureLayout->addLayout(pressureMeasurement);
    layout->addWidget(group("分子泵", {
        reading("pump", "转速", "RPM"),
        reading("pumpCurrent", "电流", "A"),
        reading("pumpVoltage", "电压", "V"),
        reading("pumpTemp", "温度", "℃"),
        reading("vacuum", "真空度", "mbar")
    }));
    layout->addWidget(group("基本信息", {
        reading("carrier", "载气模式", ""), pressureRow,
        reading("flow", "载气流速", "mL/min"),
        reading("trap", "离子阱", "℃"), reading("td", "TD温度", "℃"),
        reading("ion", "离子源电压", "V"), reading("multiplier", "倍增器", "V"),
        reading("extraction", "抽气流速", "%"), reading("syringe", "注射泵剩余", "%")
    }));
    // Read cached adapter state only while visible. No disk reads, model calls,
    // widget reconstruction, or repaint when values have not changed.
    const auto refreshReadings = [this, readings, alarmDetail] {
        const auto health = controller_->health();
        const auto telemetry = controller_->telemetry();
        const QMap<QString, QString> values{
            {"vacuum", measurementText(health.vacuumMbar, 'E', 2)},
            {"td", measurementText(health.tdTemperatureC, 'f', 1)},
            {"flow", measurementText(health.carrierGasMlMin, 'f', 2)},
            {"ion", measurementText(health.ionSourceKv * 1000.0, 'f', 1)},
            {"pump", measurementText(telemetry.molecularPumpRpm, 'f', 0)},
            {"pumpCurrent", measurementText(telemetry.molecularPumpCurrentA, 'f', 2)},
            {"pumpVoltage", measurementText(telemetry.molecularPumpVoltageV, 'f', 2)},
            {"trap", measurementText(telemetry.ionTrapTemperatureC, 'f', 1)},
            {"multiplier", measurementText(telemetry.multiplierVoltageV, 'f', 1)},
            {"extraction", measurementText(telemetry.extractionFlowPercent, 'f', 1)},
            {"pumpTemp", measurementText(telemetry.molecularPumpTemperatureC, 'f', 1)},
            {"syringe", measurementText(telemetry.syringeRemainingPercent, 'f', 0)},
            {"carrier", telemetry.carrierGasMode}};
        for (auto it = readings.begin(); it != readings.end(); ++it) {
            const QString value = health.connected ? values.value(it.key()) : "—";
            const QString unit = it.value()->property("displayUnit").toString();
            const QString display = value == "—" || unit.isEmpty() ? value : value + " " + unit;
            if (it.value()->text() != display) it.value()->setText(display);
        }
        const QString pressure = health.connected
            ? measurementText(telemetry.carrierGasPressureTorr, 'f', 1) + " Torr" : "未连接";
        if (alarmDetail->text() != pressure) alarmDetail->setText(pressure);
    };
    refreshReadings();
    connect(controller_, &AppController::instrumentSettingsChanged, panel, refreshReadings);
    auto *refreshTimer = new QTimer(panel);
    refreshTimer->setInterval(1000);
    connect(refreshTimer, &QTimer::timeout, panel, [this, panel, refreshReadings] {
        if (panel->isVisible() && !isMinimized()) refreshReadings();
    });
    refreshTimer->start();
    connect(close, &QToolButton::clicked, this, [this] { setInstrumentToolsVisible(false); });
    layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setObjectName("monitorScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->verticalScrollBar()->setSingleStep(24);
    scroll->setMinimumWidth(0);
    scroll->setMinimumWidth(280);
    scroll->setMaximumWidth(330);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::createSettingsPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    settingsStack_ = new CurrentPageStack;

    auto *detail = new QWidget;
    auto *detailLayout = new QHBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    auto *sidebar = new QWidget;
    sidebar->setObjectName("settingsSidebarRight");
    sidebar->setFixedWidth(300);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14, 16, 14, 16);
    sideLayout->setSpacing(8);
    auto *settingsHeading = new QHBoxLayout;
    settingsHeading->addWidget(makeLabel("设置与维护", "sectionTitle"), 1);
    auto *closeSettingsButton = new QToolButton;
    closeSettingsButton->setObjectName("closeSettingsButton");
    closeSettingsButton->setProperty("sciRole", "utility");
    closeSettingsButton->setAutoRaise(true);
    closeSettingsButton->setText("×");
    closeSettingsButton->setToolTip("收起设置，返回之前的工作页面");
    closeSettingsButton->setAccessibleName("收起设置与维护");
    settingsHeading->addWidget(closeSettingsButton);
    sideLayout->addLayout(settingsHeading);
    connect(closeSettingsButton, &QToolButton::clicked, this, &MainWindow::closeSettings);
    auto *section = new RoundedComboBox;
    section->setObjectName("settingsSection");
    section->setProperty("sciRole", "analysisInput");
    // 显式使用列表弹层，避免平台默认紧凑菜单破坏统一的大点击区样式。
    auto *sectionOptions = new QListView(section);
    sectionOptions->setObjectName("settingsSectionOptions");
    sectionOptions->setUniformItemSizes(true);
    sectionOptions->setSpacing(0);
    sectionOptions->setStyleSheet("QListView { background: #ffffff; color: #243331; border: 1px solid #b5c9c5; border-radius: 0; padding: 0; outline: 0; } QListView::item { min-height: 40px; padding: 0 10px; margin: 0; border: 0; border-radius: 0; } QListView::item:selected, QListView::item:hover { background: #d9efea; color: #007f80; }");
    section->setView(sectionOptions);
    section->addItems({"仪器控制", "分析校准", "系统"});
    section->setMinimumHeight(44);
    sideLayout->addWidget(section);
    settingsCategoryTree_ = new QTreeWidget;
    settingsCategoryTree_->setObjectName("settingsTree");
    settingsCategoryTree_->setHeaderHidden(true);
    settingsCategoryTree_->setRootIsDecorated(false);
    // Fixed headings and flat destinations keep navigation positions stable.
    settingsCategoryTree_->setIndentation(0);
    settingsCategoryTree_->setIconSize(QSize(18, 18));
    settingsCategoryTree_->setExpandsOnDoubleClick(false);
    settingsCategoryTree_->setItemsExpandable(false);
    settingsCategoryTree_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    settingsCategoryTree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    const QStringList modules{"仪器配置", "载气节省",
        "参考谱库", "定量曲线", "视图", "用户及参数设置", "帮助", "锁屏"};
    const QMap<QString, QString> moduleIcons{{"运行状态", "view"}, {"常用部件", "control-rf"},
            {"辅助部件", "settings"}, {"参数预设", "method"}, {"降温与关机", "power"},
            {"射频调谐", "rf-tuning"}, {"质量轴校准", "mass-calibration"}, {"注射泵", "syringe"},
            {"硬件接入说明", "help"}, {"离子源", "control-ion"},
            {"载气节省", "control-carrier"}, {"清洗模式", "clean"}, {"参考谱库", "library"},
            {"定量曲线", "curve"}, {"视图", "view"}, {"用户及参数设置", "user"},
            {"帮助", "help"}, {"锁屏", "lock"}};
    for (const auto &module : modules) {
        const auto pages = settingsPages_.value(module);
        for (const auto &subpage : pages) {
            if (subpage == "硬件接入说明") continue;
            const QString label = pages.size() > 1 ? subpage : module;
            auto *item = new QTreeWidgetItem(settingsCategoryTree_,
                {label == "用户及参数设置" ? QString("用户与参数")
                 : label == "载气节省" ? QString("气路与清洗") : label});
            const int sectionIndex = label == "射频调谐" || label == "质量轴校准"
                || module == "参考谱库" || module == "定量曲线" ? 1
                : module == "视图" || module == "用户及参数设置" || module == "帮助" || module == "锁屏" ? 2 : 0;
            item->setData(0, Qt::UserRole + 3, sectionIndex);
            item->setHidden(sectionIndex != 0);
            if(moduleIcons.contains(label))item->setIcon(0, commandIcon(moduleIcons.value(label)));
            item->setData(0, Qt::UserRole, module);
            item->setData(0, Qt::UserRole + 1, subpage);
            item->setSizeHint(0, QSize(0, 28));
        }
    }
    settingsCategoryTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sideLayout->addWidget(settingsCategoryTree_, 1);
    connect(section, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        for (int i = 0; i < settingsCategoryTree_->topLevelItemCount(); ++i) {
            auto *item = settingsCategoryTree_->topLevelItem(i);
            item->setHidden(item->data(0, Qt::UserRole + 3).toInt() != index);
        }
        settingsCategoryTree_->scrollToTop();
    });
    auto *detailContent = new QWidget;
    auto *contentLayout = new QVBoxLayout(detailContent);
    contentLayout->setContentsMargins(16, 16, 16, 20);
    contentLayout->setSpacing(12);
    settingsDetail_ = makeLabel("仪器控制", "pageTitle");
    auto *detailHeading = new QHBoxLayout;
    auto *chooseSettings = new QPushButton("全部设置"); chooseSettings->setObjectName("embeddedSettingsMenu");
    chooseSettings->setMinimumSize(110,44); detailHeading->addWidget(chooseSettings);
    detailHeading->addWidget(settingsDetail_,1);
    auto *back = new QPushButton("返回"); back->setObjectName("embeddedSettingsBack");back->setMinimumSize(80,44);detailHeading->addWidget(back);
    contentLayout->addLayout(detailHeading);
    connect(back,&QPushButton::clicked,this,&MainWindow::closeSettings);
    connect(chooseSettings,&QPushButton::clicked,this,[this] {
        auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("embeddedSettingsChooser");dialog->setWindowTitle("设置与维护");dialog->resize(720,620);
        auto *layout=new QVBoxLayout(dialog);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);auto *body=new QWidget;auto *grid=new QGridLayout(body);
        int count=0;
        for(int i=0;i<settingsCategoryTree_->topLevelItemCount();++i) {
            auto *item=settingsCategoryTree_->topLevelItem(i);if(!(item->flags()&Qt::ItemIsSelectable))continue;
            const auto module=item->data(0,Qt::UserRole).toString(), target=item->data(0,Qt::UserRole+1).toString();
            auto *button=new QPushButton(item->text(0));button->setMinimumHeight(52);button->setObjectName("settingsTile_"+target);
            button->setStyleSheet("QPushButton { min-height: 52px; font-size: 16px; }");
            grid->addWidget(button,count/2,count%2);++count;
            connect(button,&QPushButton::clicked,dialog,[this,dialog,module,target]{openSettingsModule(module,target);dialog->accept();});
        }
        scroll->setWidget(body);layout->addWidget(scroll);auto *close=new QPushButton("关闭");close->setMinimumHeight(44);layout->addWidget(close);connect(close,&QPushButton::clicked,dialog,&QDialog::reject);dialog->open();
    });

    settingsDetailStack_ = new CurrentPageStack;
    auto *placeholder = new QWidget;
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->setContentsMargins(0, 10, 0, 0);
    settingsDetailDescription_ = makeLabel("", "secondary");
    settingsDetailDescription_->setWordWrap(true);
    settingsDetailDescription_->setVisible(false);
    settingsStatusTable_ = new QTableWidget(0, 4);
    settingsStatusTable_->setObjectName("settingsStatusTable");
    polishDataTable(settingsStatusTable_);
    settingsStatusTable_->setWordWrap(true);
    settingsStatusTable_->setHorizontalHeaderLabels({"项目", "状态", "当前值", "备注"});
    settingsStatusTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    settingsStatusTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    settingsStatusTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    settingsStatusTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    settingsStatusTable_->verticalHeader()->hide();
    settingsStatusTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    settingsStatusTable_->setSelectionMode(QAbstractItemView::NoSelection);
    auto *communicationTabs = new QTabWidget;
    communicationTabs->setObjectName("communicationTabs");
    communicationTabs->addTab(new Rs485ConnectionPanel(controller_), "485串口");
    communicationTabs->addTab(new NetworkConnectionPanel(controller_), "网口TCP");
    communicationTabs->addTab(createDeviceWaveformPanel(controller_, false), "气压曲线");
    communicationTabs->hide();
    placeholderLayout->addWidget(communicationTabs);
    placeholderLayout->addWidget(settingsStatusTable_);
    settingsDetailAction_ = new QPushButton;
    settingsDetailAction_->setObjectName("settingsPrimaryAction");
    settingsDetailAction_->setProperty("sciRole", "primary");
    settingsDetailAction_->setVisible(false);
    placeholderLayout->addWidget(settingsDetailAction_, 0, Qt::AlignLeft);
    auto *screenButton = new QPushButton("进入全屏");
    screenButton->setObjectName("fullScreenAction");
    screenButton->hide();
    placeholderLayout->addWidget(screenButton, 0, Qt::AlignLeft);
    connect(screenButton, &QPushButton::clicked, this, [this] {
        if (isFullScreen()) showNormal();
        else showFullScreen();
        populateSettingsDetail("视图", "显示布局");
    });
    auto *exitButton = new QPushButton("退出软件");
    exitButton->setObjectName("exitWorkstationAction");
    exitButton->hide();
    placeholderLayout->addWidget(exitButton, 0, Qt::AlignLeft);
    connect(exitButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "退出软件", "请先保存编辑并停止检测。退出软件不会代替仪器安全关机。\n确定退出？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) close();
    });
    settingsGuidance_ = nullptr;
    placeholderLayout->addStretch();
    settingsDetailStack_->addWidget(placeholder);

    auto *controlPage = new QWidget;
    auto *controlLayout = new QVBoxLayout(controlPage);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(14);
    auto *oneKeyRow = new QHBoxLayout;
    oneKeyRow->addWidget(makeLabel("一键启停", "sectionTitle"));
    oneKeyRow->addSpacing(18);
    auto *on = new QPushButton("开");
    auto *off = new QPushButton("关");
    on->setObjectName("instrumentStartupOn");
    off->setObjectName("instrumentStartupOff");
    on->setCheckable(true);
    off->setCheckable(true);
    off->setChecked(true);
    on->setProperty("sciRole", "choice");
    off->setProperty("sciRole", "choice");
    auto *oneKeyGroup = new QButtonGroup(controlPage);
    // Both choices must be unchecked for partial/unknown confirmed states.
    oneKeyGroup->setExclusive(false);
    oneKeyGroup->addButton(on);
    oneKeyGroup->addButton(off);
    const auto initialSettings = controller_->instrumentSettings();
    const auto initialHealth = controller_->health();
    on->setChecked(initialSettings.value("powerOn").toBool());
    off->setChecked(!initialSettings.value("powerOn").toBool());
    oneKeyRow->addWidget(on);
    oneKeyRow->addWidget(off);
    auto *powerState = makeLabel({}, "secondary");
    powerState->setObjectName("instrumentStartupState");
    oneKeyRow->addWidget(powerState);
    const QString startupHint = "常用部件的启停状态以仪器回执为准。";
    on->setToolTip(startupHint); off->setToolTip(startupHint);
    oneKeyRow->addStretch();
    controlLayout->addLayout(oneKeyRow);
    auto *readiness = new QGridLayout;
    readiness->setSpacing(8);
    const QList<QPair<QString, QString>> readinessValues{
        {"真空系统", measurementText(initialHealth.vacuumMbar, 'E', 2) + " mbar"},
        {"腔内温度", measurementText(controller_->telemetry().ionTrapTemperatureC) + " ℃"},
        {"TD 温度", measurementText(initialHealth.tdTemperatureC, 'f', 1) + " ℃"},
        {"系统状态", initialHealth.ready ? "✓ 允许采集" : "需要复核"}
    };
    for (int i = 0; i < readinessValues.size(); ++i) {
        auto *tile = new QWidget;
        tile->setProperty("sciRole", "workspaceSection");
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(12, 10, 12, 10);
        tileLayout->setSpacing(3);
        tileLayout->addWidget(makeLabel(readinessValues[i].first, "metadata"));
        auto *value = makeLabel(readinessValues[i].second, "contextTitle");
        value->setObjectName(QString("instrumentReadiness%1").arg(i));
        if (initialHealth.ready) value->setProperty("sciState", "healthy");
        connect(controller_, &AppController::instrumentSettingsChanged, value,
            [this, value, i](const QVariantMap &settings) {
                const auto health = controller_->health();
                const QStringList values{measurementText(health.vacuumMbar, 'E', 2) + " mbar",
                    measurementText(controller_->telemetry().ionTrapTemperatureC) + " ℃",
                    measurementText(health.tdTemperatureC, 'f', 1) + " ℃",
                    health.ready ? "✓ 允许采集" : "未就绪"};
                value->setText(values[i]);
                value->setProperty("sciState", health.ready ? "healthy" : "");
                value->style()->unpolish(value); value->style()->polish(value);
            });
        tileLayout->addWidget(value);
        readiness->addWidget(tile, 0, i);
    }
    for (int column = 0; column < 4; ++column) readiness->setColumnStretch(column, 1);
    controlLayout->addLayout(readiness);
    controlLayout->addWidget(separator());
    auto *controlHeading = new QHBoxLayout;
    controlHeading->addWidget(makeLabel("手动控制", "sectionTitle"));
    controlHeading->addStretch();
    controlHeading->addWidget(makeLabel("设备控制 · 以回读为准", "secondary"));
    controlLayout->addLayout(controlHeading);
    auto *controlGroups = new QStackedWidget;
    controlGroups->setObjectName("instrumentControlPages");
    auto *commonControls = new QWidget;
    auto *manualGrid = new QGridLayout;
    manualGrid->setSpacing(8);
    const QStringList controls{"RF", "离子源高压", "隔膜泵", "分子泵", "夹管阀", "内载气"};
    const QStringList controlKeys{"rfOn", "ionHighVoltageOn", "diaphragmPumpOn",
        "molecularPumpOn", "pinchValveOn", "internalCarrierGasOn"};
    const QStringList controlIcons{"control-rf", "control-ion", "control-diaphragm",
        "control-turbo", "control-valve", "control-carrier"};
    for (int i = 0; i < controls.size(); ++i) {
        auto *button = new QToolButton;
        const bool enabled = initialSettings.value(controlKeys[i]).toBool();
        button->setObjectName("instrumentControl_" + controlKeys[i]);
        button->setText(controls[i] + (enabled ? "\n已开启" : "\n已关闭"));
        button->setCheckable(true);
        button->setIcon(commandIcon(controlIcons[i]));
        button->setIconSize(QSize(36, 36));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setProperty("sciRole", "controlTile");
        button->setChecked(enabled);
        button->setMinimumSize(110, 104);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        manualGrid->addWidget(button, i / 3, i % 3);
        button->setProperty("instrumentControl", true);
        button->setToolTip("点击提交操作；没有仪器回执不视为成功。");
        const auto refresh = [button, label = controls[i], key = controlKeys[i]](const QVariantMap &settings) {
            const QSignalBlocker blocker(button);
            const QVariant value = settings.value(key);
            button->setChecked(value.isValid() && value.toBool());
            button->setText(label + (!value.isValid() ? "\n状态未知" : value.toBool() ? "\n已开启" : "\n已关闭"));
        };
        refresh(initialSettings);
        connect(controller_, &AppController::instrumentSettingsChanged, button, refresh);
        connect(button, &QToolButton::toggled, this,
            [this, button, label = controls[i], key = controlKeys[i]](bool checked) {
            const QSignalBlocker blocker(button);
            button->setChecked(controller_->instrumentSettings().value(key).toBool());
            controller_->updateInstrumentSetting(key, checked);
        });
    }
    for (int column = 0; column < 3; ++column) manualGrid->setColumnStretch(column, 1);
    manualGrid->setRowStretch(0, 1);
    manualGrid->setRowStretch(1, 1);
    commonControls->setLayout(manualGrid);
    controlGroups->addWidget(commonControls);
    auto *auxiliary = new QWidget;
    auxiliary->setObjectName("auxiliaryControls");
    auto *auxLayout = new QVBoxLayout(auxiliary);
    auxLayout->setContentsMargins(0, 4, 0, 0); auxLayout->setSpacing(10);
    auto *part = new RoundedComboBox;
    part->setObjectName("auxiliaryControlKey");
    part->setProperty("sciRole", "analysisInput");
    for (const auto &entry : VendorControlCatalog::controls())
        if (entry.auxiliary) part->addItem(entry.title, entry.key);
    auxLayout->addWidget(part);
    auto *state = makeLabel({}, "contextTitle"); state->setObjectName("auxiliaryReadback");
    auxLayout->addWidget(state);
    auto *settingRow = new QHBoxLayout;
    auto *switchValue = new RoundedComboBox;
    switchValue->setObjectName("auxiliarySwitchValue");
    switchValue->setProperty("sciRole", "analysisInput");
    switchValue->addItem("关闭", false); switchValue->addItem("开启", true);
    auto *numericValue = new QSpinBox;
    numericValue->setObjectName("auxiliaryNumericValue");
    numericValue->setRange(0, 999); numericValue->setSuffix(" ℃");
    numericValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    numericValue->setProperty("sciRole", "analysisInput");
    numericValue->setKeyboardTracking(false);
    auto *apply = new QPushButton("应用设定");
    apply->setObjectName("applyAuxiliaryControl"); apply->setProperty("sciRole", "primary");
    apply->setProperty("instrumentControl", true);
    settingRow->addWidget(switchValue, 1); settingRow->addWidget(numericValue, 1); settingRow->addWidget(apply);
    auxLayout->addLayout(settingRow);
    auto *scope = makeLabel("部件状态以设备回读为准；未接入的接口不能执行。", "secondary");
    scope->setWordWrap(true); auxLayout->addWidget(scope);
    auto *contract = makeLabel({}, "secondary"); contract->setWordWrap(true); auxLayout->addWidget(contract);
    auxLayout->addStretch(); controlGroups->addWidget(auxiliary);
    controlLayout->addWidget(controlGroups, 1);
    const auto refreshAuxiliary = [this, part, state, switchValue, numericValue, contract] {
        const auto *entry = VendorControlCatalog::find(part->currentData().toString());
        if (!entry) return;
        part->setToolTip(VendorControlCatalog::sourceReference(*entry));
        switchValue->setVisible(entry->toggle); numericValue->setVisible(!entry->toggle);
        const auto current = controller_->instrumentSettings().value(entry->key);
        state->setText("当前状态：" + (!current.isValid() ? QString("未知") : entry->toggle
            ? (current.toBool() ? QString("开启") : QString("关闭")) : current.toString() + entry->unit));
        contract->setText(entry->transport + " · 厂家协议控制项；更改待设值不会直接下发。"
            + (entry->toggle ? QString() : QString("温度范围不代表设备安全范围，实际使用须按厂家限定。")));
    };
    connect(part, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, part, switchValue, numericValue, refreshAuxiliary] {
            const auto current = controller_->instrumentSettings().value(part->currentData().toString());
            switchValue->setCurrentIndex(current.isValid() && current.toBool() ? 1 : 0);
            numericValue->setValue(current.isValid() ? current.toInt() : 0);
            refreshAuxiliary();
        });
    connect(controller_, &AppController::instrumentSettingsChanged, auxiliary, refreshAuxiliary);
    connect(apply, &QPushButton::clicked, this, [this, part, switchValue, numericValue] {
        const auto *entry = VendorControlCatalog::find(part->currentData().toString());
        if (!entry) return;
        controller_->updateInstrumentSetting(entry->key, entry->toggle ? switchValue->currentData() : QVariant(numericValue->value()));
    });
    refreshAuxiliary();
    settingsDetailStack_->addWidget(controlPage);
    on->setProperty("instrumentControl", true);
    off->setProperty("instrumentControl", true);
    const auto refreshPower = [on, off, powerState, controlKeys](const QVariantMap &settings) {
        const QSignalBlocker a(on), b(off);
        const QVariant value = settings.value("powerOn");
        bool known = value.isValid(), allOn = value.toBool(), allOff = !value.toBool();
        for (const auto &key : controlKeys) {
            const auto part = settings.value(key);
            known = known && part.isValid();
            allOn = allOn && part.toBool();
            allOff = allOff && !part.toBool();
        }
        on->setChecked(known && allOn);
        off->setChecked(known && allOff);
        powerState->setText(!known ? "状态未知" : allOn ? "已开启" : allOff ? "已关闭" : "部分开启");
    };
    refreshPower(initialSettings);
    connect(controller_, &AppController::instrumentSettingsChanged, on, refreshPower);
    // Qt accessibility may invoke toggle rather than clicked. Keep that path
    // functional too; readback updates above are protected by signal blockers.
    connect(on, &QPushButton::toggled, this, [this, refreshPower](bool checked) {
        if (checked) controller_->updateInstrumentSetting("powerOn", true);
        refreshPower(controller_->instrumentSettings());
    });
    connect(off, &QPushButton::toggled, this, [this, refreshPower](bool checked) {
        if (checked) controller_->updateInstrumentSetting("powerOn", false);
        refreshPower(controller_->instrumentSettings());
    });

    auto *presetPage = new QWidget;
    auto *presetLayout = new QVBoxLayout(presetPage);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    auto *presetForm = new QFormLayout;
    presetForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    presetForm->setHorizontalSpacing(18);
    presetForm->setVerticalSpacing(10);
    auto *trapTemperature = new QSpinBox;
    trapTemperature->setObjectName("presetTrapTemperature");
    trapTemperature->setRange(0, 999);
    trapTemperature->setSuffix(" ℃");
    trapTemperature->setValue(initialSettings.value("trapTemperatureC").toInt());
    auto *inletFlow = new QSpinBox;
    inletFlow->setRange(0, 100);
    inletFlow->setSuffix(" %");
    inletFlow->setValue(initialSettings.value("inletFlowPercent").toInt());
    auto *pumpFlow = new QSpinBox;
    pumpFlow->setRange(0, 100);
    pumpFlow->setSuffix(" %");
    pumpFlow->setValue(initialSettings.value("pumpFlowPercent").toInt());
    auto *efc = new QDoubleSpinBox;
    efc->setRange(0.0, 50.0); // 485 specification, command 0x12.
    efc->setDecimals(2);
    efc->setSuffix(" ml/min");
    efc->setValue(initialSettings.value("efcMlMin").toDouble());
    presetForm->addRow("离子阱温度", trapTemperature);
    presetForm->addRow("进气流速", inletFlow);
    presetForm->addRow("抽气流速", pumpFlow);
    presetForm->addRow("EFC", efc);
    for (auto *input : {static_cast<QAbstractSpinBox *>(trapTemperature),
                       static_cast<QAbstractSpinBox *>(inletFlow),
                       static_cast<QAbstractSpinBox *>(pumpFlow),
                       static_cast<QAbstractSpinBox *>(efc)}) {
        // Editing a preset is local; only Save persists it. Never emit hardware
        // commands while a user is typing or holding a step control.
        input->setKeyboardTracking(false);
        input->setToolTip("直接输入数值，或点 − / + 调节；保存后保留预设，不直接下发仪器。");
    }
    trapTemperature->setAccessibleName("离子阱温度预设");
    inletFlow->setAccessibleName("进气流速预设");
    pumpFlow->setAccessibleName("抽气流速预设");
    efc->setAccessibleName("EFC 流速预设");
    QSettings savedPreset(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
    trapTemperature->setValue(savedPreset.value("preset/trapTemperatureC", trapTemperature->value()).toInt());
    inletFlow->setValue(savedPreset.value("preset/inletFlowPercent", inletFlow->value()).toInt());
    pumpFlow->setValue(savedPreset.value("preset/pumpFlowPercent", pumpFlow->value()).toInt());
    const double savedEfc = savedPreset.value("preset/efcMlMin", efc->value()).toDouble();
    if (savedEfc >= 0 && savedEfc <= 50.0) efc->setValue(savedEfc);
    else efc->setToolTip("旧预设超过协议规定的 0–50 mL/min，未载入；请核对后重新保存。未下发仪器。");
    presetLayout->addLayout(presetForm);
    auto *savePreset = new QPushButton("保存预设");
    savePreset->setProperty("sciRole", "primary");
    presetLayout->addWidget(savePreset, 0, Qt::AlignLeft);
    presetLayout->addStretch();
    settingsDetailStack_->addWidget(presetPage);
    connect(savePreset, &QPushButton::clicked, this,
        [this, trapTemperature, inletFlow, pumpFlow, efc] {
            controller_->saveInstrumentPreset({{"trapTemperatureC", trapTemperature->value()},
                {"inletFlowPercent", inletFlow->value()}, {"pumpFlowPercent", pumpFlow->value()},
                {"efcMlMin", efc->value()}});
        });

    auto *powerPage = new QWidget;
    auto *powerLayout = new QVBoxLayout(powerPage);
    powerLayout->setContentsMargins(0, 0, 0, 0);
    powerLayout->setSpacing(14);
    powerLayout->addWidget(makeLabel("安全关机顺序", "sectionTitle"));
    auto *powerSummary = new QGridLayout;
    powerSummary->setSpacing(8);
    const QList<QPair<QString, QString>> powerValues{
        {"当前阶段", "确认无采集任务"},
        {"TD 温度", measurementText(initialHealth.tdTemperatureC, 'f', 1) + " ℃"},
        {"降温状态", initialSettings.value("coolingModeOn").toBool() ? "进行中" : "未启动"},
        {"仪器电源", initialSettings.value("powerOn").toBool() ? "已开启" : "已关闭"}
    };
    for (int i = 0; i < powerValues.size(); ++i) {
        auto *tile = new QWidget;
        tile->setProperty("sciRole", "workspaceSection");
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(12, 10, 12, 10);
        tileLayout->addWidget(makeLabel(powerValues[i].first, "metadata"));
        auto *value = makeLabel(powerValues[i].second, "contextTitle");
        value->setObjectName(QString("instrumentPowerSummary%1").arg(i));
        const auto refresh = [this, value, i] {
            const auto values = controller_->instrumentSettings();
            const auto state = [&values](const QString &key, const QString &yes, const QString &no) {
                const auto value = values.value(key);
                return !value.isValid() ? QString("状态未知") : value.toBool() ? yes : no;
            };
            const QStringList labels{
                controller_->phase() == AppController::Phase::Acquiring ? "采集中" : "无采集任务",
                measurementText(controller_->health().tdTemperatureC) + " ℃",
                state("coolingModeOn", "进行中", "未启动"), state("powerOn", "已开启", "已关闭")};
            value->setText(labels[i]);
        };
        connect(controller_, &AppController::instrumentSettingsChanged, value, refresh);
        connect(controller_, &AppController::phaseChanged, value, refresh);
        refresh();
        tileLayout->addWidget(value);
        powerSummary->addWidget(tile, 0, i);
        powerSummary->setColumnStretch(i, 1);
    }
    powerLayout->addLayout(powerSummary);
    auto *steps = makeLabel(
        "1 结束采集并保存记录    2 启动降温并观察温度    3 达到仪器阈值后关闭电源", "secondary");
    steps->setWordWrap(true);
    powerLayout->addWidget(steps);
    auto *powerActions = new QHBoxLayout;
    auto *coolingButton = new QPushButton(initialSettings.value("coolingModeOn").toBool()
        ? "停止降温" : "开始降温");
    coolingButton->setProperty("sciRole", "primary");
    auto *instrumentPowerButton = new QPushButton(initialSettings.value("powerOn").toBool()
        ? "关闭仪器电源" : "开启仪器电源");
    powerActions->addWidget(coolingButton);
    powerActions->addWidget(instrumentPowerButton);
    powerActions->addStretch();
    powerLayout->addLayout(powerActions);
    powerLayout->addStretch();
    settingsDetailStack_->addWidget(powerPage);
    connect(coolingButton, &QPushButton::clicked, this, [this, coolingButton] {
        const bool next = !controller_->instrumentSettings().value("coolingModeOn").toBool();
        controller_->updateInstrumentSetting("coolingModeOn", next);
    });
    connect(instrumentPowerButton, &QPushButton::clicked, this, [this, instrumentPowerButton] {
        const bool next = !controller_->instrumentSettings().value("powerOn").toBool();
        controller_->updateInstrumentSetting("powerOn", next);
    });
    coolingButton->setProperty("instrumentControl", true);
    instrumentPowerButton->setProperty("instrumentControl", true);
    connect(controller_, &AppController::instrumentSettingsChanged, coolingButton,
        [coolingButton, instrumentPowerButton](const QVariantMap &settings) {
            coolingButton->setText(settings.value("coolingModeOn").toBool() ? "停止降温" : "开始降温");
            instrumentPowerButton->setText(settings.value("powerOn").toBool() ? "关闭仪器电源" : "开启仪器电源");
        });

    settingsDetailStack_->addWidget(createLibraryPage());
    settingsDetailStack_->addWidget(createQuantitationPage());
    settingsDetailStack_->addWidget(createDeviceWaveformPanel(controller_,true));
    settingsDetailStack_->addWidget(createInstrumentWorkbench("质量轴校准"));
    settingsDetailStack_->addWidget(createInstrumentWorkbench("注射泵"));
    auto *gasPage = new QWidget;
    auto *gasLayout = new QVBoxLayout(gasPage);
    gasLayout->addWidget(makeLabel("气路状态以仪器回读为准", "secondary"));
    for (const auto &entry : QList<QPair<QString,QString>>{{"gasSavingOn","载气节省"},{"cleaningModeOn","清洗模式"}}) {
        auto *toggle = new QPushButton;
        toggle->setObjectName("gasControl_" + entry.first);
        toggle->setMinimumHeight(64);
        toggle->setProperty("instrumentControl", true);
        gasLayout->addWidget(toggle);
        const auto refresh = [toggle, entry](const QVariantMap &values) {
            toggle->setText(entry.second + (values.value(entry.first).toBool() ? "：已开启" : "：已关闭"));
        };
        refresh(controller_->instrumentSettings());
        connect(controller_, &AppController::instrumentSettingsChanged, toggle, refresh);
        connect(toggle, &QPushButton::clicked, this, [this,entry] {
            controller_->updateInstrumentSetting(entry.first,!controller_->instrumentSettings().value(entry.first).toBool());
        });
    }
    gasLayout->addStretch();
    settingsDetailStack_->addWidget(gasPage);
    contentLayout->addWidget(settingsDetailStack_, 1);
    detailLayout->addWidget(detailContent, 1);
    detailLayout->addWidget(sidebar);
    settingsStack_->addWidget(detail);
    layout->addWidget(settingsStack_);

    connect(settingsCategoryTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const QString module = item->data(0, Qt::UserRole).toString();
        openSettingsModule(module, item->data(0, Qt::UserRole + 1).toString());
    });
    connect(settingsDetailAction_, &QPushButton::clicked, this, [this] {
        const QString target = settingsDetailAction_->property("target").toString();
        if (target == "records") { setWorkspaceSection(0); }
        else if (target == "methods") { refreshMethods(); setWorkspaceSection(5); }
        else if (target == "library") setWorkspaceSection(4);
        else if (target == "home") setWorkspaceSection(0);
        else if (target == "restoreLayout") {
            setAssistantVisible(false);
            setInstrumentToolsVisible(true);
            populateSettingsDetail("视图", "显示布局");
            statusBar()->showMessage("已恢复默认布局：仪器监控开启，智能台收起", 5000);
        } else if (target == "lock" || target == "guide" || target == "requirements" || target == "account") {
            const bool guard = target == "lock";
            const bool busy = controller_->phase() == AppController::Phase::Acquiring
                || controller_->phase() == AppController::Phase::Analyzing;
            if (guard && busy) {
                statusBar()->showMessage("检测进行中，暂不能锁定操作界面", 5000);
                return;
            }
            QDialog *dialog = guard ? static_cast<QDialog *>(new OperationGuardDialog(this)) : new QDialog(this);
            dialog->setObjectName(guard ? "operationGuard" : "settingsInformation");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(guard ? "界面防误触保护" : settingsDetailAction_->text());
            dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
            if (guard) dialog->setWindowFlag(Qt::WindowCloseButtonHint, false);
            auto *body = new QVBoxLayout(dialog);
            body->setContentsMargins(24, 24, 24, 24);
            body->setSpacing(16);
            auto *copy = makeLabel("", "body");
            copy->setWordWrap(true);
            copy->setTextInteractionFlags(Qt::TextSelectableByMouse);
            copy->setText(guard
                ? "界面操作已锁定。\n\n这是本软件的防误触保护，不是系统锁屏或身份验证。不会关闭仪器。点击下方按钮恢复操作。"
                : target == "guide"
                ? "1. 方法选择：选择并激活方法。\n\n2. 样品分析：导入已有数据，或填写样本信息后开始检测；完成后自动保存。\n\n3. 报告查看：复核结果并导出 PDF。"
                : target == "account"
                ? "当前会话：" + controller_->sessionSummary() + "\n\n操作权限由本地会话控制器校验。此页不提供未经认证的权限提升或账户切换。"
                : settingsDetailDescription_->text() + "\n\n接入前需提供厂家通信协议或 SDK、接口与量程、单位、安全互锁及操作回执。资料尚缺，不会向仪器发送猜测指令。");
            body->addWidget(copy);
            auto *done = new QPushButton(guard ? "恢复操作" : "关闭");
            done->setObjectName("settingsDialogDone");
            done->setProperty("sciRole", "primary");
            body->addWidget(done);
            connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
            dialog->resize(460, guard ? 220 : 420);
            dialog->open();
        }
        else if (target.startsWith("toggle:")) {
            const QString key = target.mid(7);
            controller_->updateInstrumentSetting(key, !controller_->instrumentSettings().value(key).toBool());
            populateSettingsDetail(settingsDetailAction_->property("module").toString(),
                settingsDetailAction_->property("subpage").toString());
        }
    });
    openSettingsModule("仪器配置", "仪器控制");
    return page;
}

QWidget *MainWindow::createReportPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *titles = new QWidget;
    auto *titleLayout = new QVBoxLayout(titles);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    titleLayout->addWidget(makeLabel("结果复核与报告", "pageTitle"));
    header->addWidget(titles);
    header->addStretch();
    reportReviewButton_ = new QPushButton("完成复核");
    reportExportButton_ = new QPushButton("生成 PDF");
    reportReviewViewButton_ = new QPushButton("复核");
    reportReviewViewButton_->setObjectName("reportReviewView");
    reportPreviewViewButton_ = new QPushButton("预览");
    reportPreviewViewButton_->setObjectName("reportPreviewView");
    auto *openSavedData = new QPushButton("打开数据");
    openSavedData->setObjectName("openSavedResult");
    auto *screeningDetails = new QPushButton("筛查详情");
    screeningDetails->setObjectName("screeningDetails");
    reportExportButton_->setProperty("sciRole", "primary");
    reportReviewButton_->setEnabled(false);
    reportExportButton_->setEnabled(false);
    reportReviewViewButton_->setProperty("sciState", "current");
    reportReviewViewButton_->setToolTip("查看可疑结果并进行人工复核");
    reportPreviewViewButton_->setToolTip("查看报告内容预览");
    header->addWidget(openSavedData);
    header->addWidget(reportReviewViewButton_);
    header->addWidget(screeningDetails);
    header->addWidget(reportReviewButton_);
    header->addWidget(reportPreviewViewButton_);
    header->addWidget(reportExportButton_);
    layout->addLayout(header);

    auto *summaryStrip = new QFrame;
    summaryStrip->setObjectName("reportSummaryStrip");
    summaryStrip->setProperty("sciRole", "summaryStrip");
    auto *summaryLayout = new QHBoxLayout(summaryStrip);
    summaryLayout->setContentsMargins(14, 10, 14, 10);
    summaryLayout->setSpacing(0);
    auto addMetric = [summaryLayout](const QString &title, QLabel *&value, int stretch = 1) {
        auto *metric = new QWidget;
        auto *metricLayout = new QVBoxLayout(metric);
        metricLayout->setContentsMargins(10, 0, 10, 0);
        metricLayout->setSpacing(1);
        metricLayout->addWidget(makeLabel(title, "metadata"));
        value = makeLabel("—", "metricValue");
        value->setWordWrap(false);
        metricLayout->addWidget(value);
        summaryLayout->addWidget(metric, stretch);
    };
    addMetric("质量门控", reportQualityValue_);
    summaryLayout->addWidget(separator(true));
    addMetric("候选结果", reportCandidateCount_);
    summaryLayout->addWidget(separator(true));
    addMetric("人工复核", reportReviewState_);
    summaryLayout->addWidget(separator(true));
    addMetric("数据类型", reportScope_, 2);
    layout->addWidget(summaryStrip);

    auto *body = new QSplitter;
    body->setChildrenCollapsible(false);

    auto *reviewWorkspace = new QWidget;
    reviewWorkspace->setObjectName("reportReviewWorkspace");
    auto *reviewLayout = new QVBoxLayout(reviewWorkspace);
    reviewLayout->setContentsMargins(0, 0, 10, 0);
    reviewLayout->setSpacing(10);
    auto *candidates = new QFrame;
    candidates->setObjectName("panel");
    auto *candidateLayout = new QVBoxLayout(candidates);
    candidateLayout->setContentsMargins(12, 10, 12, 10);
    candidateLayout->setSpacing(6);
    auto *candidateHeader = new QHBoxLayout;
    candidateHeader->addWidget(makeLabel("可疑结果", "sectionTitle"));
    candidateHeader->addStretch();
    reportCandidateSearch_ = new QLineEdit;
    reportCandidateSearch_->setObjectName("reportCandidateSearch");
    reportCandidateSearch_->setPlaceholderText("搜索名称");
    reportCandidateSearch_->setClearButtonEnabled(true);
    reportCandidateSearch_->setMaximumWidth(220);
    candidateHeader->addWidget(reportCandidateSearch_);
    reportSelectionHint_ = makeLabel("", "metadata");
    candidateHeader->addWidget(reportSelectionHint_);
    reportSelectionHint_->hide();
    candidateLayout->addLayout(candidateHeader);
    resultSource_ = makeLabel("", "metadata");
    resultSource_->setObjectName("resultDataSource");
    resultSource_->setWordWrap(true);
    candidateLayout->addWidget(resultSource_);
    resultSource_->hide();
    reportCandidateTable_ = new QTableWidget(0, 4);
    reportCandidateTable_->setObjectName("reportScreeningResults");
    polishDataTable(reportCandidateTable_);
    reportCandidateTable_->setHorizontalHeaderLabels({"物质名称", "浓度\nμg/mL", "证据", "筛查结果"});
    // Two text lines plus the shared header's 8px top/bottom padding.
    reportCandidateTable_->horizontalHeader()->setFixedHeight(58);
    reportCandidateTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    reportCandidateTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    reportCandidateTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    reportCandidateTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    reportCandidateTable_->verticalHeader()->hide();
    reportCandidateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportCandidateTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reportCandidateTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    reportCandidateTable_->setMinimumHeight(130);
    candidateLayout->addWidget(reportCandidateTable_, 1);
    reviewLayout->addWidget(candidates, 1);

    connect(reportCandidateSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString keyword = text.trimmed();
        for (int row = 0; row < reportCandidateTable_->rowCount(); ++row) {
            const auto *name = reportCandidateTable_->item(row, 0);
            reportCandidateTable_->setRowHidden(row, !keyword.isEmpty()
                && (!name || !name->text().contains(keyword, Qt::CaseInsensitive)));
        }
        reportCandidateTable_->clearSelection();
    });

    auto *evidencePanel = new QFrame;
    evidencePanel->setObjectName("panel");
    evidencePanel->setMaximumHeight(104);
    auto *evidenceLayout = new QHBoxLayout(evidencePanel);
    evidenceLayout->setContentsMargins(12, 10, 12, 10);
    evidenceLayout->setSpacing(14);
    auto *candidateEvidence = new QWidget;
    auto *candidateEvidenceLayout = new QVBoxLayout(candidateEvidence);
    candidateEvidenceLayout->setContentsMargins(0, 0, 0, 0);
    candidateEvidenceLayout->setSpacing(5);
    candidateEvidenceLayout->addWidget(makeLabel("所选候选证据", "sectionTitle"));
    reportEvidenceDetail_ = makeLabel("选择候选查看证据。", "secondary");
    reportEvidenceDetail_->setWordWrap(false);
    reportEvidenceDetail_->setProperty("sciRole", "reportDetail");
    reportEvidenceDetail_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    candidateEvidenceLayout->addWidget(reportEvidenceDetail_);
    evidenceLayout->addWidget(candidateEvidence, 1);
    evidenceLayout->addWidget(separator(true));
    auto *qualityEvidence = new QWidget;
    auto *qualityEvidenceLayout = new QVBoxLayout(qualityEvidence);
    qualityEvidenceLayout->setContentsMargins(0, 0, 0, 0);
    qualityEvidenceLayout->setSpacing(5);
    qualityEvidenceLayout->addWidget(makeLabel("质量检查", "sectionTitle"));
    reportQualityChecks_ = makeLabel("完成检测后显示确定性质量门控。", "secondary");
    reportQualityChecks_->setWordWrap(false);
    reportQualityChecks_->setProperty("sciRole", "reportDetail");
    reportQualityChecks_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    qualityEvidenceLayout->addWidget(reportQualityChecks_);
    evidenceLayout->addWidget(qualityEvidence, 1);
    reviewLayout->addWidget(evidencePanel);
    body->addWidget(reviewWorkspace);

    auto *preview = new QWidget;
    preview->setObjectName("reportPreviewWorkspace");
    connect(reportReviewViewButton_, &QPushButton::clicked, this, [this] {
        reportPreviewVisible_ = false;
        updateWorkspaceLayout();
    });
    connect(reportPreviewViewButton_, &QPushButton::clicked, this, [this] {
        reportPreviewVisible_ = true;
        updateWorkspaceLayout();
    });
    auto *previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(10, 0, 0, 0);
    previewLayout->setSpacing(10);

    auto *reportCard = new QFrame;
    reportCard->setObjectName("panel");
    auto *reportCardLayout = new QVBoxLayout(reportCard);
    reportCardLayout->setContentsMargins(14, 12, 14, 12);
    reportCardLayout->setSpacing(8);
    reportCardLayout->addWidget(makeLabel("报告内容", "sectionTitle"));
    reportRunId_ = makeLabel("等待检测", "sectionTitle");
    reportRunId_->setWordWrap(true);
    reportCardLayout->addWidget(reportRunId_);
    reportMeta_ = makeLabel("", "secondary");
    reportMeta_->setWordWrap(true);
    reportCardLayout->addWidget(reportMeta_);
    reportCardLayout->addWidget(separator());
    reportStatus_ = makeLabel("", "secondary");
    reportStatus_->setWordWrap(true);
    reportCardLayout->addWidget(reportStatus_);
    previewLayout->addWidget(reportCard);

    auto *flowCard = new QFrame;
    flowCard->setObjectName("panel");
    auto *flowLayout = new QVBoxLayout(flowCard);
    flowLayout->setContentsMargins(14, 12, 14, 12);
    flowLayout->setSpacing(7);
    flowLayout->addWidget(makeLabel("报告流程", "sectionTitle"));
    flowLayout->addWidget(makeLabel("① 选择需要写入报告的候选证据", "bodyStrong"));
    flowLayout->addWidget(makeLabel("② 操作员完成人工复核并留下状态", "bodyStrong"));
    flowLayout->addWidget(makeLabel("③ 生成可追溯 PDF，不覆盖原始记录", "bodyStrong"));
    previewLayout->addWidget(flowCard);

    previewLayout->addStretch();
    body->addWidget(preview);
    body->setSizes({760, 360});
    body->setStretchFactor(0, 1);
    body->setStretchFactor(1, 0);
    layout->addWidget(body, 1);

    connect(reportCandidateTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int count = reportCandidateTable_->selectionModel()->selectedRows().size();
        reportSelectionHint_->setText(count > 0
            ? QString("已选择 %1 项；PDF 只包含这些候选证据。").arg(count)
            : "未选择时将导出全部候选结果。");
        if (count == 1 && reportEvidenceDetail_) {
            const int row = reportCandidateTable_->selectionModel()->selectedRows().constFirst().row();
            const auto *nameItem = reportCandidateTable_->item(row, 0);
            const int candidateIndex = nameItem ? nameItem->data(Qt::UserRole).toInt() : row;
            const auto &candidates = controller_->result().candidates;
            if (candidateIndex >= 0 && candidateIndex < candidates.size()) {
                const auto &candidate = candidates[candidateIndex];
                const QString fullEvidence = QString(
                    "%1\n匹配分数 %2 · 质量误差 %3 ppm\n碎片证据 %4/%5 · %6")
                    .arg(candidate.name, QString::number(candidate.score, 'f', 1),
                        QString::number(candidate.massErrorPpm, 'f', 2))
                    .arg(candidate.matchedFragments).arg(candidate.requiredFragments)
                    .arg(candidate.evidence);
                reportEvidenceDetail_->setText(QString("%1 · 匹配 %2 · 误差 %3 ppm · 碎片 %4/%5")
                    .arg(candidate.name, QString::number(candidate.score, 'f', 1),
                        QString::number(candidate.massErrorPpm, 'f', 2))
                    .arg(candidate.matchedFragments).arg(candidate.requiredFragments));
                reportEvidenceDetail_->setToolTip(fullEvidence);
            }
        } else if (reportEvidenceDetail_) {
            reportEvidenceDetail_->setText(count > 1
                ? QString("已选择 %1 项。逐项选择可查看单条证据，导出时会保留全部已选项。").arg(count)
                : "选择上方候选后，这里显示匹配分数、质量误差和碎片证据。");
        }
        reportExportButton_->setEnabled(!controller_->currentRun().id.isEmpty()
            && controller_->currentRun().reviewStatus == "REVIEWED");
    });
    connect(reportReviewButton_, &QPushButton::clicked, controller_, &AppController::markCurrentRunReviewed);
    connect(openSavedData, &QPushButton::clicked, this, [this] {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SCIENTZ", "QITest01");
        const QString defaultFolder = PlatformPaths::documentsSubdirectory("飞秒检测数据");
        QDir().mkpath(defaultFolder);
        const QString initial = PlatformPaths::existingDirectoryOrDefault(
            settings.value("sampleSaveFolder").toString(), defaultFolder);
        const QString path = QFileDialog::getOpenFileName(this, "打开已保存的检测数据", initial,
            "检测数据 (*.qit.json *.scan.csv);;全部支持文件 (*.json *.csv)");
        if (path.isEmpty()) return;
        showReportAfterRunSaved_ = true;
        controller_->importRunArchive(path);
    });
    connect(screeningDetails, &QPushButton::clicked, this, [this] {
        auto *dialog = new QDialog(this);
        dialog->setObjectName("screeningDetailsDialog");
        dialog->setWindowTitle("筛查详情");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(900, 560);
        dialog->setMinimumSize(760, 440);
        auto *layout = new QVBoxLayout(dialog);
        auto *titleRow = new QHBoxLayout;
        titleRow->addWidget(makeLabel("完整筛查结果", "sectionTitle"));
        titleRow->addStretch();
        auto *statusFilter = new RoundedComboBox;
        statusFilter->setObjectName("screeningStatusFilter");
        statusFilter->addItems({"全部", "可疑", "未检出"});
        statusFilter->setMinimumWidth(120);
        auto *search = new QLineEdit;
        search->setObjectName("screeningSearch");
        search->setPlaceholderText("搜索名称");
        search->setClearButtonEnabled(true);
        search->setMaximumWidth(220);
        auto *previousPage = new QPushButton("上一页");
        auto *pageLabel = makeLabel("", "bodyStrong");
        auto *nextPage = new QPushButton("下一页");
        previousPage->setObjectName("screeningPreviousPage");
        nextPage->setObjectName("screeningNextPage");
        titleRow->addWidget(statusFilter);
        titleRow->addWidget(search);
        titleRow->addWidget(previousPage);
        titleRow->addWidget(pageLabel);
        titleRow->addWidget(nextPage);
        layout->addLayout(titleRow);
        auto *table = new QTableWidget(0, 6, dialog);
        table->setObjectName("screeningDetailsTable");
        polishDataTable(table);
        table->setHorizontalHeaderLabels({"序号", "名称", "母离子", "碎片离子", "实测强度比", "是否检出"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const auto &items = controller_->result().screeningItems;
        prepareTableRows(table, items.size());
        for (int row = 0; row < items.size(); ++row) {
            QStringList fragments, intensities;
            for (double value : items[row].fragmentMz) fragments << QString::number(value, 'f', value == std::floor(value) ? 0 : 2);
            for (double value : items[row].measuredRelativeIntensity) intensities << QString::number(value, 'f', 1);
            setTableText(table, row, 0, QString::number(row + 1));
            setTableText(table, row, 1, items[row].name);
            setTableText(table, row, 2, QString::number(items[row].precursorMz, 'f', items[row].precursorMz == std::floor(items[row].precursorMz) ? 0 : 1));
            setTableText(table, row, 3, fragments.join(", "));
            setTableText(table, row, 4, intensities.join(" : "));
            setTableText(table, row, 5, items[row].conclusion);
        }
        constexpr int rowsPerPage = 8;
        dialog->setProperty("screeningPage", 0);
        auto matchingRows = [table, statusFilter, search] {
            QVector<int> rows;
            const QString status = statusFilter->currentText();
            const QString keyword = search->text().trimmed();
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto *nameItem = table->item(row, 1);
                const auto *statusItem = table->item(row, 5);
                const bool statusMatches = status == "全部" || (statusItem && statusItem->text() == status);
                const bool nameMatches = keyword.isEmpty() || (nameItem
                    && nameItem->text().contains(keyword, Qt::CaseInsensitive));
                if (statusMatches && nameMatches) rows.append(row);
            }
            return rows;
        };
        auto showPage = [dialog, table, pageLabel, previousPage, nextPage, matchingRows] {
            const QVector<int> rows = matchingRows();
            const int pageCount = std::max(1, static_cast<int>((rows.size() + rowsPerPage - 1) / rowsPerPage));
            const int page = std::clamp(dialog->property("screeningPage").toInt(), 0, pageCount - 1);
            dialog->setProperty("screeningPage", page);
            for (int row = 0; row < table->rowCount(); ++row) table->setRowHidden(row, true);
            const int end = std::min(static_cast<int>(rows.size()), (page + 1) * rowsPerPage);
            for (int index = page * rowsPerPage; index < end; ++index)
                table->setRowHidden(rows[index], false);
            pageLabel->setText(rows.isEmpty() ? "0 / 0" : QString("%1 / %2").arg(page + 1).arg(pageCount));
            previousPage->setEnabled(page > 0);
            nextPage->setEnabled(!rows.isEmpty() && page + 1 < pageCount);
        };
        connect(previousPage, &QPushButton::clicked, dialog, [dialog, showPage] {
            dialog->setProperty("screeningPage", dialog->property("screeningPage").toInt() - 1);
            showPage();
        });
        connect(nextPage, &QPushButton::clicked, dialog, [dialog, showPage] {
            dialog->setProperty("screeningPage", dialog->property("screeningPage").toInt() + 1);
            showPage();
        });
        connect(statusFilter, &QComboBox::currentTextChanged, dialog, [dialog, showPage] {
            dialog->setProperty("screeningPage", 0);
            showPage();
        });
        connect(search, &QLineEdit::textChanged, dialog, [dialog, showPage] {
            dialog->setProperty("screeningPage", 0);
            showPage();
        });
        showPage();
        layout->addWidget(table, 1);
        auto *close = new QPushButton("关闭");
        close->setProperty("sciRole", "primary");
        layout->addWidget(close);
        connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
        dialog->open();
    });
    connect(reportExportButton_, &QPushButton::clicked, this, [this] {
        QVector<int> rows;
        for (const auto &index : reportCandidateTable_->selectionModel()->selectedRows()) {
            const auto *nameItem = reportCandidateTable_->item(index.row(), 0);
            rows.append(nameItem ? nameItem->data(Qt::UserRole).toInt() : index.row());
        }
        if (rows.isEmpty()) {
            const auto count = controller_->result().candidates.size();
            rows.reserve(count);
            for (int row = 0; row < count; ++row) rows.append(row);
        }
        std::sort(rows.begin(), rows.end());
        reportExportButton_->setText("生成中…");
        reportExportButton_->setEnabled(false);
        controller_->exportSelectedReport(rows);
        QTimer::singleShot(2500, this, [this] {
            if (!reportExportButton_ || reportExportButton_->text() != "生成中…") return;
            reportExportButton_->setText("生成 PDF");
            reportExportButton_->setEnabled(controller_->currentRun().reviewStatus == "REVIEWED");
        });
    });
    return page;
}


QWidget *MainWindow::createLibraryPage() {
    auto *tabs = new QWidget;
    tabs->setObjectName("standardLibraryTabs");
    tabs->setAttribute(Qt::WA_StyledBackground, true);
    auto *tabsLayout = new QVBoxLayout(tabs);
    tabsLayout->setContentsMargins(0, 8, 0, 0); tabsLayout->setSpacing(4);
    auto *selectors = new QHBoxLayout;
    selectors->setContentsMargins(20, 0, 20, 0); selectors->setSpacing(6);
    auto *group = new QButtonGroup(tabs);
    auto *pages = new QStackedWidget;
    pages->setObjectName("standardLibraryPages");
    for (int i = 0; i < 2; ++i) {
        auto *button = new QPushButton(i == 0 ? "公共谱库" : "我的标准");
        button->setObjectName(i == 0 ? "publicLibrarySelector" : "userLibrarySelector");
        button->setCheckable(true); button->setChecked(i == 0);
        button->setProperty("sciRole", "librarySelector");
        group->addButton(button); selectors->addWidget(button);
        connect(button, &QPushButton::clicked, pages, [pages, i] { pages->setCurrentIndex(i); });
    }
    selectors->addStretch(); tabsLayout->addLayout(selectors); tabsLayout->addWidget(pages, 1);
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 20);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    header->addWidget(makeLabel("参考谱库", "pageTitle"));
    header->addStretch();
    auto *libraryInfo = new QPushButton("详情");
    libraryInfo->setProperty("sciRole", "plotAction");
    header->addWidget(libraryInfo);
    connect(libraryInfo, &QPushButton::clicked, this, [this] {
        QStringList details{controller_->librarySummary()};
        if (libraryTable_ && libraryTable_->currentRow() >= 0) {
            for (int column = 0; column < libraryTable_->columnCount(); ++column) {
                const auto *item = libraryTable_->item(libraryTable_->currentRow(), column);
                if (item) details << libraryTable_->horizontalHeaderItem(column)->text() + "：" + item->text();
            }
        }
        QMessageBox::information(this, "谱库详情", details.join("\n"));
    });
    layout->addLayout(header);
    auto *searchRow = new QHBoxLayout;
    librarySearch_ = new QLineEdit;
    librarySearch_->setPlaceholderText("输入名称、CAS 或分子式");
    auto *searchButton = new QPushButton("检索");
    searchButton->setProperty("sciRole", "primary");
    searchRow->addWidget(librarySearch_, 1);
    searchRow->addWidget(searchButton);
    layout->addLayout(searchRow);
    librarySearchStatus_ = makeLabel("", "metadata");
    layout->addWidget(librarySearchStatus_);
    libraryTable_ = new QTableWidget(0, 7);
    libraryTable_->setObjectName("publicLibraryTable");
    polishDataTable(libraryTable_);
    libraryTable_->setHorizontalHeaderLabels({"物质名称", "分子式", "CAS", "标称质量", "峰数", "离子化", "来源"});
    // 主表只保留识别所需的三列；其余字段通过双击详情查看，避免名称被压成省略号。
    libraryTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int column : {1, 4, 5, 6}) libraryTable_->setColumnHidden(column, true);
    libraryTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    libraryTable_->setFrameShape(QFrame::NoFrame);
    connect(libraryTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QStringList details;
        for (int column = 0; column < libraryTable_->columnCount(); ++column) {
            const auto *item = libraryTable_->item(row, column);
            if (item) details << libraryTable_->horizontalHeaderItem(column)->text() + "：" + item->text();
        }
        QMessageBox::information(this, "谱图详情", details.join("\n"));
    });
    libraryTable_->verticalHeader()->hide();
    libraryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    libraryTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(libraryTable_, 1);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::performLibrarySearch);
    connect(librarySearch_, &QLineEdit::returnPressed, this, &MainWindow::performLibrarySearch);
    performLibrarySearch();
    pages->addWidget(page);
    QString workspacePath=qEnvironmentVariable("QITEST_WORKSPACE_DB");
    if (workspacePath.isEmpty()) workspacePath = PlatformPaths::appDataFile("workspace.sqlite");
    auto *standards = new UserStandardsPage(QFileInfo(workspacePath).absolutePath()+"/user-standards.sqlite");
    standards->setComparisonProvider([this] {
        StandardComparisonInput input;
        const auto &run=controller_->currentRun();
        input.recordId=run.id;
        input.description=QString("%1 · %2 · 记录分析峰（不是游标选中的扫描）")
            .arg(run.completedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"),dataScopeLabel(run.dataScope));
        for(const auto &peak:controller_->result().peaks) input.peaks.append({peak.mz,peak.relativeIntensity});
        std::sort(input.peaks.begin(),input.peaks.end(),[](const SpectrumPoint &a,const SpectrumPoint &b) { return a.mz<b.mz; });
        return input;
    });
    pages->addWidget(standards);
    return tabs;
}

QWidget *MainWindow::createMethodPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 18, 20, 20);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    header->addWidget(makeLabel("编辑方法", "pageTitle"));
    header->addStretch();
    layout->addLayout(header);

    auto *editor = new QFrame;
    editor->setObjectName("panel");
    auto *editorLayout = new QHBoxLayout(editor);
    editorLayout->setContentsMargins(14, 10, 14, 10);
    methodName_ = new QLineEdit("痕量筛查");
    methodName_->setPlaceholderText("方法名称");
    methodName_->setMaxLength(16);
    methodName_->setToolTip("方法名称为 2–16 个字符");
    methodRevisionNote_ = new QLineEdit;
    methodRevisionNote_->setPlaceholderText("版本说明");
    auto *create = new QPushButton("编辑参数");
    create->setObjectName("editMethodParameters");
    create->setProperty("sciRole", "primary");
    auto *activate = new QPushButton("激活所选版本");
    editorLayout->addWidget(methodName_, 1);
    methodRevisionNote_->setParent(editor);methodRevisionNote_->hide();
    editorLayout->addWidget(create);
    editorLayout->addWidget(activate);
    layout->addWidget(editor);

    methodTable_ = new QTableWidget(0, 6);
    methodTable_->setObjectName("methodTable");
    polishDataTable(methodTable_);
    methodTable_->setHorizontalHeaderLabels({"状态", "名称", "版本", "校验值", "创建人", "创建时间"});
    methodTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    methodTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    methodTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    methodTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    methodTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    methodTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    methodTable_->setColumnWidth(0, 92);
    methodTable_->setColumnWidth(2, 70);
    methodTable_->setColumnWidth(3, 126);
    methodTable_->setColumnWidth(4, 128);
    methodTable_->setColumnWidth(5, 174);
    methodTable_->verticalHeader()->hide();
    methodTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    methodTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    methodTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    activate->setText("设为当前方法");
    activate->setToolTip("将所选版本用于下一次检测，不会立即开始采集");
    activate->setEnabled(false);
    layout->addWidget(methodTable_, 1);
    connect(create, &QPushButton::clicked, this, [this] {
        if (methodName_->text().trimmed().size() < 2) {
            statusBar()->showMessage("方法名称至少需要两个字符", 3000);
            methodName_->setFocus();
            return;
        }
        QJsonObject values; QString name=methodName_->text();
        const auto *item=methodTable_->item(methodTable_->currentRow(),0);
        if(item) for(const auto &method:controller_->methods()) if(method.id==item->data(Qt::UserRole).toString()) {
            values=method.parameters.value("method_parameters").toObject(); name=method.name; break;
        }
        const QString baseId=item?item->data(Qt::UserRole).toString():QString{};
        if(!controller_->fullMethodAccess() && (baseId.isEmpty() || values.isEmpty())) {
            statusBar()->showMessage("请先选择管理员建立的方法",5000); return;
        }
        auto *dialog=new MethodEditorDialog(name,values,[this,baseId](const QString &title,const QJsonObject &parameters){
            return controller_->createMethodDraft(title,parameters,baseId);
        },this,controller_->fullMethodAccess()); dialog->open();
    });
    connect(methodTable_, &QTableWidget::itemSelectionChanged, this, [this, activate] {
        const int row = methodTable_->currentRow();
        const auto *item = row >= 0 ? methodTable_->item(row, 0) : nullptr;
        const bool valid = item && !item->data(Qt::UserRole).toString().isEmpty();
        const bool alreadyActive = item && item->data(Qt::UserRole + 1).toBool();
        activate->setEnabled(valid && !alreadyActive);
        if (valid && methodName_) {
            const QString methodId = item->data(Qt::UserRole).toString();
            for (const auto &method : controller_->methods())
                if (method.id == methodId) { methodName_->setText(method.name); break; }
        }
    });
    connect(activate, &QPushButton::clicked, this, [this, activate] {
        const int row = methodTable_->currentRow();
        const auto *item = row >= 0 ? methodTable_->item(row, 0) : nullptr;
        const QString methodId = item ? item->data(Qt::UserRole).toString() : QString{};
        if (methodId.isEmpty()) {
            activate->setEnabled(false);
            statusBar()->showMessage("请先选择一个有效的方法版本", 3000);
            return;
        }
        // Do not leave an accessibility-focused row selected while activation
        // emits methodsChanged. The delayed refresh will paint the new state.
        methodTable_->clearSelection();
        methodTable_->setCurrentItem(nullptr);
        activate->setEnabled(false);
        controller_->activateMethod(methodId);
    });
    refreshMethods();
    return page;
}

QWidget *MainWindow::createQuantitationPage() {
    return new CalibrationPage;
}

QWidget *MainWindow::createAiAssistantPage() {
    auto *page = new QWidget;
    page->setObjectName("assistantRail");
    page->setMinimumWidth(0);
    page->setMaximumWidth(340);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    auto *header = new QHBoxLayout;
    header->addWidget(makeLabel("智能台", "sectionTitle"));
    header->addStretch();
    auto *clear = new QToolButton;
    clear->setText("清空");
    clear->setToolTip("清空对话与操作记录");
    clear->setProperty("sciRole", "utility");
    clear->setFixedHeight(32);
    header->addWidget(clear);
    auto *close = new QToolButton;
    close->setText("×");
    close->setToolTip("收起智能台");
    close->setProperty("sciRole", "utility");
    close->setFixedSize(36, 36);
    header->addWidget(close);
    layout->addLayout(header);

    aiContext_ = nullptr;
    auto *aiMode = new RoundedComboBox;
    aiMode->setObjectName("aiModeSelector");
    aiMode->addItems({"自动", "手动", "常开", "关闭"});
    aiMode->setCurrentIndex(static_cast<int>(controller_->aiMode()));
    aiMode->setToolTip("自动：按需使用模型；手动：提问时使用模型；常开：模型保持就绪；关闭：仅保留页面操作。");
    layout->addWidget(aiMode);
    aiStatus_ = makeLabel("", "metadata");
    aiStatus_->setObjectName("assistantRequestStatus");
    aiStatus_->setWordWrap(true);
    aiStatus_->hide();
    layout->addWidget(aiStatus_);
    connect(aiMode, qOverload<int>(&QComboBox::currentIndexChanged), controller_,
        [this](int index) { controller_->setAiMode(static_cast<AppController::AiMode>(index)); });
    connect(controller_, &AppController::aiModeChanged, aiMode, [aiMode](AppController::AiMode mode) {
        const QSignalBlocker blocker(aiMode);
        aiMode->setCurrentIndex(static_cast<int>(mode));
    });
    aiConversation_ = new ChatTranscript;
    layout->addWidget(aiConversation_, 1);

    auto *composer = new QFrame;
    composer->setObjectName("assistantComposer");
    composer->setProperty("sciRole", "assistantComposer");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(8, 5, 5, 5);
    composerLayout->setSpacing(6);
    aiQuestion_ = new QLineEdit;
    aiQuestion_->setProperty("sciRole", "assistantInput");
    aiQuestion_->setPlaceholderText("输入问题或操作…");
    aiQuestion_->setClearButtonEnabled(true);
    aiSendButton_ = new QPushButton("发送");
    aiSendButton_->setObjectName("assistantSendOrStop");
    aiSendButton_->setProperty("sciRole", "primary");
    aiSendButton_->setFixedWidth(66);
    composerLayout->addWidget(aiQuestion_, 1);
    composerLayout->addWidget(aiSendButton_);
    layout->addWidget(composer);
    aiSendButton_->setToolTip("按 Return 发送");

    connect(close, &QToolButton::clicked, this, [this] { setAssistantVisible(false); });
    connect(clear, &QToolButton::clicked, aiConversation_, &ChatTranscript::clear);
    connect(aiSendButton_, &QPushButton::clicked, this, [this] {
        if (aiRequestBusy_) controller_->cancelAiQuestion();
        else submitAiQuestion();
    });
    connect(aiQuestion_, &QLineEdit::returnPressed, this, [this] { submitAiQuestion(); });

    return page;
}

void MainWindow::refreshAiContext() {
    if (aiContext_) aiContext_->setText(controller_->aiContextSummary());
}

void MainWindow::setAssistantVisible(bool visible) {
    if (!aiAssistantPanel_) return;
    if (visible && instrumentTargetVisible_) setInstrumentToolsVisible(false);
    assistantTargetVisible_ = visible;
    if (visible) {
        refreshAiContext();
        aiAssistantPanel_->setMinimumWidth(Scientz::Ui::Metrics::AssistantWidthMin);
        aiAssistantPanel_->setMaximumWidth(340);
        aiAssistantPanel_->show();
    } else {
        aiAssistantPanel_->setMinimumWidth(0);
        aiAssistantPanel_->setMaximumWidth(0);
        aiAssistantPanel_->hide();
    }
    if (assistantButton_) {
        assistantButton_->setProperty("sciState", visible ? "current" : QVariant{});
        Scientz::Ui::ThemeManager::refresh(assistantButton_);
    }
    updateWorkspaceLayout();
}

void MainWindow::setInstrumentToolsVisible(bool visible) {
    if (!instrumentTools_) return;
    if (visible && assistantTargetVisible_) setAssistantVisible(false);
    instrumentTargetVisible_ = visible;
    if (visible) {
        instrumentTools_->setMinimumWidth(Scientz::Ui::Metrics::MonitorWidthMin);
        instrumentTools_->setMaximumWidth(290);
        instrumentTools_->show();
    } else {
        instrumentTools_->setMinimumWidth(0);
        instrumentTools_->setMaximumWidth(0);
        instrumentTools_->hide();
    }
    if (instrumentToolsButton_) {
        instrumentToolsButton_->setProperty("sciState", visible ? "current" : QVariant{});
        Scientz::Ui::ThemeManager::refresh(instrumentToolsButton_);
    }
    updateWorkspaceLayout();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateWorkspaceLayout();
}

void MainWindow::updateWorkspaceLayout() {
    if (!workspaceBodyLayout_ || !instrumentTools_ || !aiAssistantPanel_) return;
    // One stable landscape arrangement; resizing must not replace the workspace
    // with an inspector or silently reset the user's panel visibility.
    const bool small = false;
    if (settingsCategoryTree_) {
        for (int i = 0; i < settingsCategoryTree_->topLevelItemCount(); ++i) {
            auto *item = settingsCategoryTree_->topLevelItem(i);
            if (!(item->flags() & Qt::ItemIsSelectable)) continue;
            if (!item->data(0, Qt::UserRole + 2).isValid())
                item->setData(0, Qt::UserRole + 2, item->icon(0));
            item->setIcon(0, small ? QIcon{} : qvariant_cast<QIcon>(item->data(0, Qt::UserRole + 2)));
            item->setSizeHint(0, QSize(0, 44));
        }
    }
    if (auto *intro = findChild<QWidget *>("loginIntro")) intro->setVisible(!small);
    if(auto *sidebar=findChild<QWidget *>("settingsSidebarRight"))sidebar->setVisible(!small);
    if(auto *menu=findChild<QWidget *>("embeddedSettingsMenu"))menu->setVisible(small);
    if(auto *back=findChild<QWidget *>("embeddedSettingsBack"))back->setVisible(small);
    if (auto *review = findChild<QWidget *>("reportReviewWorkspace"))
        review->setVisible(!reportPreviewVisible_);
    if (auto *preview = findChild<QWidget *>("reportPreviewWorkspace"))
        preview->setVisible(reportPreviewVisible_);
    if (reportReviewViewButton_ && reportPreviewViewButton_) {
        reportReviewViewButton_->setProperty("sciState", reportPreviewVisible_ ? QVariant{} : QVariant("current"));
        reportPreviewViewButton_->setProperty("sciState", reportPreviewVisible_ ? QVariant("current") : QVariant{});
        Scientz::Ui::ThemeManager::refresh(reportReviewViewButton_);
        Scientz::Ui::ThemeManager::refresh(reportPreviewViewButton_);
    }
    if (auto *body = findChild<QWidget *>("loginBody"))
        body->layout()->setContentsMargins(small ? 20 : 80, small ? 16 : 48, small ? 20 : 80, small ? 16 : 48);
    if (small != smallScreen_) {
        smallScreen_ = small;
        // Entering the embedded layout opens on the actual workspace, not an
        // inspector. Tools remain reachable from their existing top buttons.
        assistantTargetVisible_ = false;
        instrumentTargetVisible_ = !small;
        aiAssistantPanel_->hide();
        instrumentTools_->setVisible(!small);
        for (auto *button : {assistantButton_, instrumentToolsButton_}) {
            button->setProperty("sciState", button == instrumentToolsButton_ && !small
                ? QVariant("current") : QVariant{});
            Scientz::Ui::ThemeManager::refresh(button);
        }
    }
    const bool shortPlots = height() < 620;
    // Physical 8–9 inch target: primary plot controls must be touch-sized,
    // not the tiny desktop controls previously used at 1024×768.
    if(startButton_)startButton_->setFixedSize(112,36);
    if(auto *context=findChild<QWidget *>("contextHeader"))context->setFixedHeight(shortPlots?40:48);
    if(auto *tic=findChild<QWidget *>("runTicPanel"))
        if(auto *header=tic->findChild<QWidget *>("panelHeader"))header->setFixedHeight(shortPlots?32:48);
    // 图表工具条使用统一紧凑尺寸；缩放窗口时不能只把积分按钮和输入框再次撑大。
    if(eicMz_)eicMz_->setFixedSize(112,30);
    if(eicTolerance_)eicTolerance_->setFixedSize(90,30);
    for (const auto *name : {"loadPublicExample", "openTraceAnalysis"})
        if(auto *button=findChild<QPushButton *>(name))button->setFixedSize(120,30);
    if(auto *reset=findChild<QToolButton *>("resetRunPlots"))reset->setFixedSize(shortPlots?30:44,shortPlots?30:44);
    if (auto *choice = findChild<QWidget *>("smallScreenPlotChoice")) {
        choice->setVisible(shortPlots);
        const bool showEic = findChild<QPushButton *>("smallScreenEic")->isChecked();
        spectrumContainer_->setVisible(!shortPlots || !showEic);
        findChild<QWidget *>("runEicPanel")->setVisible(!shortPlots || showEic);
        for (auto *plot : {ticPlot_, spectrumPlot_, eicPlot_}) plot->setMinimumHeight(100);
    }
    if (emptyDataBanner_) emptyDataBanner_->setVisible(!shortPlots
        && controller_->phase() == AppController::Phase::Ready && controller_->liveSpectrum().isEmpty());
    if (auto *canvas = findChild<QWidget *>("analysisCanvas")) {
        canvas->layout()->setContentsMargins(shortPlots ? 8 : 14, shortPlots ? 4 : 12,
            shortPlots ? 8 : 14, shortPlots ? 4 : 12);
        canvas->layout()->setSpacing(shortPlots ? 4 : 10);
    }
    if (workflowLabel_) workflowLabel_->hide();
    workspaceStack_->setMinimumWidth(400);
    workspaceStack_->setVisible(!small || (!assistantTargetVisible_ && !instrumentTargetVisible_));
    // At narrow widths both tools share one vertical column. Neither overlays nor
    // squeezes the chart; reflow only at a breakpoint, without width animations.
    const int assistantWidth = 260;
    const int monitorWidth = 300;
    const bool compact = false;
    if (compact != compactRails_) {
        compactRails_ = compact;
        // In the shared-height arrangement retain the mode selector, conversation and input.
        aiAssistantPanel_->setSizePolicy(QSizePolicy::Preferred,
            compact ? QSizePolicy::Ignored : QSizePolicy::Preferred);
        aiAssistantPanel_->setMinimumHeight(compact ? 320 : 0);
        instrumentTools_->setMinimumHeight(compact ? 180 : 0);
        workspaceBodyLayout_->removeWidget(aiAssistantPanel_);
        workspaceBodyLayout_->removeWidget(instrumentTools_);
        workspaceBodyLayout_->addWidget(aiAssistantPanel_, 0, 0, compact ? 1 : 2, 1);
        workspaceBodyLayout_->addWidget(instrumentTools_, compact ? 1 : 0, compact ? 0 : 2,
                                       compact ? 1 : 2, 1);
        workspaceBodyLayout_->parentWidget()->setProperty("compactRails", compact);
    }
    if (small) {
        if (auto *navigation = findChild<QWidget *>("settingsSidebarRight")) navigation->setFixedWidth(208);
        aiAssistantPanel_->setMinimumHeight(0);
        instrumentTools_->setMinimumHeight(0);
        aiAssistantPanel_->setFixedWidth(assistantTargetVisible_ ? width() : 0);
        instrumentTools_->setFixedWidth(instrumentTargetVisible_ ? width() : 0);
        workspaceBodyLayout_->invalidate();
        return;
    }
    if (assistantTargetVisible_ && (aiAssistantPanel_->minimumWidth() != assistantWidth
            || aiAssistantPanel_->maximumWidth() != assistantWidth))
        aiAssistantPanel_->setFixedWidth(assistantWidth);
    if (instrumentTargetVisible_ && (instrumentTools_->minimumWidth() != (compact ? assistantWidth : monitorWidth)
            || instrumentTools_->maximumWidth() != (compact ? assistantWidth : monitorWidth)))
        instrumentTools_->setFixedWidth(compact ? assistantWidth : monitorWidth);
    if (auto *settingsNavigation = findChild<QWidget *>("settingsSidebarRight")) {
        const int centreWidth = width() - (assistantTargetVisible_ ? assistantWidth : 0)
            - (instrumentTargetVisible_ && !compact ? monitorWidth : 0);
        const int settingsWidth = 200;
        if (settingsNavigation->width() != settingsWidth)
            settingsNavigation->setFixedWidth(settingsWidth);
        if (auto *calibration = findChild<QSplitter *>("calibrationSplit")) {
            const auto orientation = centreWidth - settingsWidth < 680 ? Qt::Vertical : Qt::Horizontal;
            if (calibration->orientation() != orientation) {
                calibration->setOrientation(orientation);
                calibration->setSizes(orientation == Qt::Vertical ? QList<int>{160, 260} : QList<int>{300, 450});
            }
        }
    }
    workspaceBodyLayout_->invalidate();
}

void MainWindow::importRunArchiveFromDialog() {
    const QString initial = PlatformPaths::documentsSubdirectory("飞秒检测数据");
    QDir().mkpath(initial);
    const QStringList paths = QFileDialog::getOpenFileNames(this, "导入一批检测数据", initial,
        "检测数据 (*.qit.json *.scan.csv);;扫描 CSV (*.csv);;JSON (*.json)");
    if (paths.isEmpty()) return;
    setWorkspaceSection(3);
    controller_->importRunArchives(paths);
}

void MainWindow::submitAiQuestion(const QString &question) {
    const QString prompt = question.trimmed().isEmpty() && aiQuestion_
        ? aiQuestion_->text().trimmed() : question.trimmed();
    if (prompt.isEmpty()) {
        statusBar()->showMessage("请输入要询问本地模型的问题", 3000);
        return;
    }
    setAssistantVisible(true);
    if (aiRequestBusy_) {
        if (aiStatus_) aiStatus_->setText("已有问题正在处理；可点“停止”后重新提问。");
        return;
    }
    if (aiConversation_) aiConversation_->appendMessage(ChatTranscript::Role::User, prompt);
    if (handleAssistantCommand(prompt)) {
        if (aiQuestion_) aiQuestion_->clear();
        return;
    }
    if (aiQuestion_) aiQuestion_->clear();
    pendingChatQuestions_.append(prompt);
    if (pendingChatQuestions_.size() > 16) pendingChatQuestions_.removeFirst();
    controller_->askAiAssistant(prompt);
}

bool MainWindow::handleAssistantCommand(const QString &text) {
    QString contextTopic;
    if (workspaceStack_) {
        switch (workspaceStack_->currentIndex()) {
        case 0: contextTopic = "采集与分析"; break;
        case 2: contextTopic = "生成报告"; break;
        case 3: contextTopic = "方法版本"; break;
        default: break;
        }
    }
    const auto understood = AiCommandRouter::understand(text, contextTopic);
    if (!understood.feedback.isEmpty()) {
        if (aiConversation_) aiConversation_->appendMessage(ChatTranscript::Role::Assistant, understood.feedback);
        return true;
    }
    if (!understood.explanationTopic.isEmpty()) {
        // explainFeature emits its topic, not the user's original sentence.
        pendingChatQuestions_.append(understood.explanationTopic);
        controller_->explainFeature(understood.explanationTopic);
        return true;
    }
    if (understood.command == AssistantCommand::None) return false;
    return executeAssistantCommand(understood.command, text);
}

bool MainWindow::executeAssistantCommand(AssistantCommand command, const QString &source) {
    if (command == AssistantCommand::None) return false;
    const QString destination = AiCommandRouter::displayName(command);
    QString feedback = "已打开「" + destination + "」。";
    switch (command) {
    case AssistantCommand::OpenHome: setWorkspaceSection(0); break;
    case AssistantCommand::PrepareRun:
        setWorkspaceSection(0);
        feedback = "已打开运行准备。开始采集仍需操作人员确认。";
        break;
    case AssistantCommand::OpenResults: setWorkspaceSection(2); break;
    case AssistantCommand::OpenReport: refreshReport(controller_->currentRun()); setWorkspaceSection(2); break;
    case AssistantCommand::OpenLibrary: setWorkspaceSection(4); break;
    case AssistantCommand::OpenMethod: refreshMethods(); setWorkspaceSection(5); break;
    case AssistantCommand::OpenQuantitation: setWorkspaceSection(6); break;
    case AssistantCommand::OpenTraceAnalysis: {
        setWorkspaceSection(0);
        auto *dialog = new ChromatogramDialog(controller_->scans(), this);
        dialog->open();
        break;
    }
    case AssistantCommand::OpenInstrumentStatus:
        setInstrumentToolsVisible(true);
        feedback = "已打开仪器状态，并显示当前遥测。";
        break;
    case AssistantCommand::OpenInstrumentSettings:
        openSettingsModule("仪器配置", "仪器控制");
        break;
    case AssistantCommand::OpenInstrumentPresets:
        openSettingsModule("仪器配置", "参数预设");
        break;
    case AssistantCommand::OpenCalibration:
        openSettingsModule("仪器配置", "调谐与校准");
        feedback = "已打开离线质量轴校准；可拟合与保存，不会同步实际仪器。";
        break;
    case AssistantCommand::OpenSampling:
        openSettingsModule("仪器配置", "进样与注射泵");
        feedback = "已打开注射泵控制。";
        break;
    case AssistantCommand::OpenCarrierGas:
        openSettingsModule("载气节省");
        break;
    case AssistantCommand::OpenCleaning:
        openSettingsModule("清洗模式");
        break;
    case AssistantCommand::EnableIonSource:
    case AssistantCommand::DisableIonSource: {
        const bool enable = command == AssistantCommand::EnableIonSource;
        openSettingsModule("离子源", "离子源状态与设定");
        const bool simulation = controller_->instrumentDescriptor().simulation;
        if (simulation) {
            controller_->updateInstrumentSetting("ionSourceEnabled", enable);
            populateSettingsDetail("离子源", "离子源状态与设定");
            feedback = QString("离子源已%1，状态已回读。")
                .arg(enable ? "开启" : "关闭");
        } else {
            feedback = "已定位到离子源控制。真实仪器需核对当前值、安全范围和权限后，由操作人员确认。";
        }
        if (source.contains("分子源"))
            feedback += " 已将“分子源”按本仪器的“离子源”理解。";
        break;
    }
    case AssistantCommand::ReviewInstrumentAdjustment:
        openSettingsModule("仪器配置", "参数预设");
        feedback = "已打开参数页。请先确认参数、目标值和原因，并核对当前值、允许范围和操作权限。";
        break;
    case AssistantCommand::OpenPower:
        openSettingsModule("仪器配置", "降温与关机");
        break;
    case AssistantCommand::OpenSessionProtection:
        openSettingsModule("锁屏", "会话保护");
        break;
    case AssistantCommand::OpenSettings: setWorkspaceSection(1); break;
    case AssistantCommand::OpenHelp: openSettingsModule("帮助"); break;
    case AssistantCommand::None: return false;
    }
    refreshAiContext();
    if (aiConversation_) aiConversation_->appendMessage(ChatTranscript::Role::Assistant, feedback);
    statusBar()->showMessage("智能台已打开" + destination, 2500);
    return true;
}


void MainWindow::refreshReport(const RunSummary &run) {
    if (!reportRunId_ || !reportStatus_ || !reportCandidateTable_) return;
    if (run.id.isEmpty()) {
        reportRunId_->setText("等待检测");
        reportMeta_->clear();
        reportStatus_->clear();
        if (reportQualityValue_) reportQualityValue_->setText("等待检测");
        if (reportCandidateCount_) reportCandidateCount_->setText("0 项");
        if (reportReviewState_) reportReviewState_->setText("尚未开始");
        if (reportScope_) reportScope_->setText("—");
        if (reportEvidenceDetail_)
            reportEvidenceDetail_->setText("选择候选查看匹配证据");
        if (reportQualityChecks_) reportQualityChecks_->setText("等待检测");
        prepareTableRows(reportCandidateTable_, 0);
        reportSelectionHint_->clear();
        reportReviewButton_->setEnabled(false);
        reportExportButton_->setEnabled(false);
        return;
    }
    reportRunId_->setText(recordLabel(run.id));
    reportRunId_->setToolTip("完整记录编号：" + run.id);
    QString displayMethod = run.methodName;
    displayMethod.replace(" · 开发方法", "");
    reportMeta_->setText(QString("%1\n%2 · %3")
        .arg(run.completedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"),
            operatorLabel(run.operatorName), displayMethod));
    reportStatus_->setText(QString("%1%2")
        .arg(run.reviewStatus == "REVIEWED" ? "已完成人工复核" : "等待人工复核",
            run.reportPath.isEmpty() ? QString{} : "\n已生成：" + PlatformPaths::nativeDisplay(run.reportPath)));
    const auto &candidates = controller_->result().candidates;
    if (reportQualityValue_)
        reportQualityValue_->setText(QString("%1 · %2/100").arg(qualityLabel(run.qualityLevel)).arg(run.qualityScore));
    if (reportCandidateCount_) reportCandidateCount_->setText(QString("%1 项").arg(candidates.size()));
    if (reportReviewState_)
        reportReviewState_->setText(run.reviewStatus == "REVIEWED" ? "已完成" : "待人工复核");
    if (reportScope_)
        reportScope_->setText(dataScopeLabel(run.dataScope));

    QStringList qualitySummary, qualityDetails;
    for (const auto &check : controller_->result().quality.checks) {
        qualitySummary.append(QString("%1 %2").arg(check.passed ? "✓" : "△", check.title));
        qualityDetails.append(QString("%1 %2：%3").arg(check.passed ? "✓" : "△", check.title, check.detail));
    }
    if (reportQualityChecks_) {
        reportQualityChecks_->setText(qualitySummary.isEmpty() ? "无质量检查" : qualitySummary.join(" · "));
        reportQualityChecks_->setToolTip(qualityDetails.join("\n"));
    }
    prepareTableRows(reportCandidateTable_, candidates.size());
    for (int row = 0; row < candidates.size(); ++row) {
        const auto &candidate = candidates[row];
        const QStringList values{candidate.name, "—",
            QString("%1 / %2").arg(candidate.matchedFragments).arg(candidate.requiredFragments),
            "可疑"};
        for (int column = 0; column < values.size(); ++column)
            setTableText(reportCandidateTable_, row, column, values[column]);
        reportCandidateTable_->item(row, 0)->setData(Qt::UserRole, row);
    }
    const QString keyword = reportCandidateSearch_ ? reportCandidateSearch_->text().trimmed() : QString{};
    for (int row = 0; row < reportCandidateTable_->rowCount(); ++row) {
        const auto *name = reportCandidateTable_->item(row, 0);
        reportCandidateTable_->setRowHidden(row, !keyword.isEmpty()
            && (!name || !name->text().contains(keyword, Qt::CaseInsensitive)));
    }
    reportCandidateTable_->clearSelection();
    if (reportEvidenceDetail_)
        reportEvidenceDetail_->setText(candidates.isEmpty()
            ? "无候选证据"
            : "选择候选查看匹配证据");
    reportSelectionHint_->setText(candidates.isEmpty()
        ? "本次记录没有候选结果。"
        : "选择需要写入报告的候选结果。");
    const bool reviewed = run.reviewStatus == "REVIEWED";
    reportExportButton_->setEnabled(reviewed);
    reportReviewButton_->setEnabled(!reviewed);
    reportSelectionHint_->setText("未选择时将导出全部候选结果。");
}

void MainWindow::performLibrarySearch() {
    if (!libraryTable_ || !librarySearch_) return;
    const bool browsing = librarySearch_->text().trimmed().isEmpty();
    const auto results = controller_->searchLibrary(librarySearch_->text(), browsing ? 50 : 100);
    prepareTableRows(libraryTable_, results.size());
    for (int row = 0; row < results.size(); ++row) {
        const auto &item = results[row];
        const QStringList values{item.name, item.formula, item.cas,
            QString::number(item.nominalMass, 'f', 1), QString::number(item.peakCount),
            item.ionization, item.source};
        for (int column = 0; column < values.size(); ++column)
            setTableText(libraryTable_, row, column, values[column]);
    }
    librarySearchStatus_->setText(QString("%1 条记录").arg(results.size()));
}

void MainWindow::refreshMethods() {
    if (!methodTable_) return;
    QString selectedId;
    if (const auto *selected = methodTable_->item(methodTable_->currentRow(), 0))
        selectedId = selected->data(Qt::UserRole).toString();
    const auto methods = controller_->methods();
    prepareTableRows(methodTable_, methods.size());
    int rowToSelect = -1;
    for (int row = 0; row < methods.size(); ++row) {
        const auto &method = methods[row];
        QString displayName = method.name;
        displayName.replace(" · 开发方法", "");
        const QString createdBy = (method.createdBy == "offline-demo" || method.createdBy == "system-bootstrap")
            ? QString("系统") : method.createdBy;
        const QStringList values{method.active ? "● 活动" : "", displayName,
            QString::number(method.version), method.checksum.left(12), createdBy,
            method.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")};
        for (int column = 0; column < values.size(); ++column)
            setTableText(methodTable_, row, column, values[column]);
        auto *state = setTableText(methodTable_, row, 0, values[0]);
        state->setData(Qt::UserRole, method.id);
        state->setData(Qt::UserRole + 1, method.active);
        if (method.id == selectedId || (rowToSelect < 0 && selectedId.isEmpty() && method.active)) rowToSelect = row;
        for (int column = 0; column < values.size(); ++column)
            if (auto *item = methodTable_->item(row, column)) item->setToolTip(values[column]);
    }
    if (rowToSelect < 0 && !methods.isEmpty()) rowToSelect = 0;
    if (rowToSelect >= 0) {
        methodTable_->selectRow(rowToSelect);
        if (methodName_) methodName_->setText(methods[rowToSelect].name);
    }
}

QWidget *MainWindow::createPanel(const QString &technicalLabel, const QString &title, QWidget *content) {
    auto *panel = new QWidget;
    panel->setProperty("sciRole", "workspaceSection");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    header->addWidget(makeLabel(title, "sectionTitle"));
    header->addStretch();
    Q_UNUSED(technicalLabel);
    auto *help = new QToolButton;
    help->setText("说明");
    help->setAccessibleName(title + "操作说明");
    help->setToolTip("查看" + title + "的操作说明，不加载大模型");
    help->setProperty("sciRole", "utility");
    help->setFixedSize(48, 32);
    header->addWidget(help);
    connect(help, &QToolButton::clicked, this, [this, title] {
        setAssistantVisible(true);
        controller_->explainFeature(title);
    });
    layout->addLayout(header);
    layout->addWidget(content, 1);
    return panel;
}

QToolButton *MainWindow::createCommandButton(const QString &actionId, const QString &text, const QIcon &icon) {
    auto *action = actions_->registerAction(actionId, text, icon);
    auto *button = new QToolButton;
    button->setDefaultAction(action);
    button->setIconSize({20, 20});
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setProperty("sciRole", "command");
    button->setProperty("actionId", actionId);
    button->setFixedSize(62, 50);
    return button;
}

void MainWindow::showWorkspace() {
    rootStack_->setCurrentIndex(2);
    setWorkspaceSection(0);
    QTimer::singleShot(450, controller_, &AppController::prepareAiAssistant);
}

void MainWindow::closeSettings() {
    if (workspaceStack_->currentIndex() == 1) setWorkspaceSection(workspaceBeforeSettings_);
}

void MainWindow::setWorkspaceSection(int index) {
    if (smallScreen_) {
        setAssistantVisible(false);
        setInstrumentToolsVisible(false);
    }
    // Preserve old action/AI route IDs while giving each task one visible home.
    if (index == 4 || index == 6) {
        openSettingsModule("标准与校准", index == 4 ? "参考谱库" : "定量曲线");
        return;
    }
    if (index == 1 && workspaceStack_->currentIndex() != 1)
        workspaceBeforeSettings_ = workspaceStack_->currentIndex() == 3 ? 5 : workspaceStack_->currentIndex();
    if (index == 3) return; // 已删除的检测记录模块，没有隐藏页面。
    workspaceStack_->setCurrentIndex(index == 5 ? 3 : index);
    const QStringList sectionActions{
        "OpenHome", "OpenSettings", "OpenReport", "", "OpenLibrary", "OpenMethod", "OpenQuantitation"
    };
    const QString currentAction = index >= 0 && index < sectionActions.size()
        ? sectionActions[index] : QString{};
    for (auto *button : findChildren<QToolButton *>()) {
        if (button->property("sciRole").toString() != "command") continue;
        const QString actionId = button->property("actionId").toString();
        if (actionId == "OpenAssistant" || actionId == "OpenInstrumentStatus") continue;
        const QVariant nextState = actionId == currentAction ? QVariant("current") : QVariant{};
        if (button->property("sciState") == nextState) continue;
        button->setProperty("sciState", nextState);
        Scientz::Ui::ThemeManager::refresh(button);
    }
}

void MainWindow::populateSettingsDetail(const QString &module, const QString &subpage) {
    if (!settingsStatusTable_ || !settingsDetailAction_) return;
    settingsDetailStack_->setCurrentIndex(0);
    settingsDetailAction_->setVisible(false);
    if (auto *exitButton = findChild<QPushButton *>("exitWorkstationAction")) exitButton->setVisible(module == "视图");
    if (auto *screenButton = findChild<QPushButton *>("fullScreenAction")) {
        screenButton->setVisible(module == "视图");
        screenButton->setText(isFullScreen() ? "退出全屏" : "进入全屏");
    }
    settingsDetailAction_->setProperty("target", {});
    settingsDetailAction_->setProperty("module", module);
    settingsDetailAction_->setProperty("subpage", subpage);

    const bool serialPage = module == "仪器配置" && (subpage == "运行状态" || subpage == "硬件接入说明");
    if (auto *panel = findChild<QWidget *>("communicationTabs")) panel->setVisible(serialPage);
    settingsStatusTable_->setVisible(!serialPage);
    const auto health = controller_->health();
    const auto telemetry = controller_->telemetry();
    const auto descriptor = controller_->instrumentDescriptor();
    const auto configured = controller_->instrumentSettings();
    const auto result = controller_->result();
    QList<QStringList> rows;
    QString description;
    QString actionText;
    QString target;
    const auto onOff = [](const QVariant &value) {
        return !value.isValid() ? QString("状态未知") : value.toBool() ? QString("已开启") : QString("已关闭");
    };

    if (module == "离子源") {
        description = "显示仪器实测值与本地设定；所有操作以设备回执为准。";
        rows = {
            {"离子源", onOff(configured.value("ionSourceEnabled")),
                measurementText(health.ionSourceKv * 1000.0, 'f', 1) + " V",
                configured.value("ionSourceSetpointKv").isValid()
                    ? QString::number(configured.value("ionSourceSetpointKv").toDouble(), 'f', 1) + " kV"
                    : QString("设定值未确认")},
            {"适配器", health.connected ? "已连接" : "未连接", descriptor.model,
                descriptor.protocolVersion}
        };
        actionText = configured.value("ionSourceEnabled").toBool() ? "关闭离子源" : "开启离子源";
        target = "toggle:ionSourceEnabled";
    } else if (module == "载气节省") {
        description = "载气节省状态保存到本机设置；实际流量始终来自仪器适配器。";
        rows = {
            {"节省模式", onOff(configured.value("gasSavingOn")), "—", "设备回执"},
            {"载气流速", health.connected ? "实时" : "不可用",
                measurementText(health.carrierGasMlMin, 'f', 2) + " mL/min", "由正式方法确认"},
            {"内载气", onOff(configured.value("internalCarrierGasOn")), "—", "仪器控制联动"}
        };
        actionText = configured.value("gasSavingOn").toBool() ? "关闭载气节省" : "开启载气节省";
        target = "toggle:gasSavingOn";
    } else if (module == "清洗模式") {
        description = "清洗模式保留本地状态和审计记录；执行结果以设备回执为准。";
        rows = {
            {"清洗模式", onOff(configured.value("cleaningModeOn")), "—", "清洗进度"},
            {"安全互锁", health.ready ? "通过" : "未通过", health.ready ? "设备就绪" : "设备异常", "必须满足后才可执行"},
            {"操作记录", "已启用", controller_->sessionSummary(), "自动保存"}
        };
        actionText = configured.value("cleaningModeOn").toBool() ? "终止清洗" : "开始清洗";
        target = "toggle:cleaningModeOn";
    } else if (module == "数据处理") {
        description = "这里显示 C++ 确定性分析管线的真实运行状态，而不是由大语言模型计算的数值。";
        rows = {
            {"输入检查", controller_->liveSpectrum().isEmpty() ? "等待数据" : "已完成",
                QString::number(controller_->liveSpectrum().size()) + " 点", "数据范围检查"},
            {"基线与噪声", result.processedSpectrum.points.isEmpty() ? "未运行" : "已完成",
                result.processedSpectrum.points.isEmpty() ? "—" : QString::number(result.processedSpectrum.noiseMad, 'f', 3), "MAD 噪声"},
            {"峰检测", result.peaks.isEmpty() ? "未运行" : "已完成", QString::number(result.peaks.size()) + " 峰", "S/N 与峰距门限"},
            {"谱库匹配", result.processedSpectrum.points.isEmpty() ? "未运行" : "已完成",
                QString::number(result.candidates.size()) + " 个候选", controller_->librarySummary()},
            {"质量门控", result.processedSpectrum.points.isEmpty() ? "未运行" : "已完成",
                result.processedSpectrum.points.isEmpty() ? "—" : QString::number(result.quality.score) + "/100",
                AnalysisEngine::Version}
        };
    } else if (module == "文件") {
        description = "在样品分析页导入数据，检测完成后自动保存。";
        actionText = "样品分析"; target = "home";
    } else if (module == "编辑方法") {
        description = "方法以不可覆盖版本保存，只有明确激活的版本用于下一次检测。";
        const auto active = controller_->activeMethod();
        rows = {{"活动方法", active.id.isEmpty() ? "未设置" : "已激活", active.name,
            active.id.isEmpty() ? "—" : "v" + QString::number(active.version)}};
        actionText = "打开方法版本"; target = "methods";
    } else if (module == "谱库编辑") {
        description = "当前交付库为只读参考库，保留来源和版本；不会在界面中直接改写正式谱库。";
        rows = {{"参考谱库", controller_->librarySummary().contains("未加载") ? "未加载" : "已加载",
            controller_->librarySummary(), "只读、来源可追溯"}};
        actionText = "打开参考谱库"; target = "library";
    } else if (module == "用户及参数设置") {
        description = "当前会话、方法和记录都来自本地控制器与 SQLite 工作区。";
        rows = {
            {"当前会话", "已登录", controller_->sessionSummary(), "三级权限门控"},
            {"方法版本", "可用", QString::number(controller_->methods().size()) + " 个", "不可覆盖"},
            {"检测记录", "可用", QString::number(controller_->recentRuns().size()) + " 条", controller_->workspaceSummary()}
        };
        actionText = "查看当前权限"; target = "account";
    } else if (module == "定量曲线") {
        description = "定量曲线需要经确认的浓度点和校准数据；缺少数据时明确阻断，不生成伪造浓度。";
        rows = {
            {"已验证曲线", "未配置", "0 条", "等待正式校准数据"},
            {"当前浓度输出", "已阻断", "—", "不从定性候选推算浓度"},
            {"不确定度", "不可计算", "—", "需要校准与重复测量"}
        };
    } else if (module == "视图") {
        description = "采集与分析保留 TIC、质谱图、EIC 三张曲线；候选结果统一在生成报告中复核。";
        rows = {
            {"TIC 总离子流", "显示", "时间序列", "无扫描序列时保持空图"},
            {"质谱图", "显示", "矢量绘制", "自动适应窗口"},
            {"EIC 提取离子流", "显示", "目标 m/z 与质量窗口", "点选谱峰提取"},
            {"仪器工具", instrumentTargetVisible_ ? "显示" : "收起", "关键状态", "随窗口重排"},
            {"智能台", assistantTargetVisible_ ? "显示" : "收起", "对话与操作", "按需开启"}
        };
        rows.prepend({"屏幕模式", isFullScreen() ? "全屏" : "窗口", "F11 切换", "Windows 启动全屏"});
        actionText = "恢复默认布局"; target = "restoreLayout";
    } else if (module == "帮助") {
        description = "主流程保持为准备、采集、分析、复核与报告。";
        rows = {
            {"1 准备", "检查", "设备、样品与方法", "异常会阻断检测"},
            {"2 检测", "执行", "开始检测", "按当前方法与实际设备执行"},
            {"3 复核", "人工", "只显示可疑候选", "支持单选与多选"},
            {"4 报告", "输出", "PDF", "复核后生成"}
        };
        actionText = "打开操作说明"; target = "guide";
    } else if (module == "锁屏") {
        description = "仅锁定本软件的界面操作以防误触，不是系统锁屏或身份验证；检测期间保持停止入口可用，不允许锁定。";
        rows = {{"界面保护", "可用", "防误触", "不关闭仪器"},
            {"解锁方式", "本地操作", "恢复操作按钮", "不提供身份验证"}};
        actionText = "锁定界面操作"; target = "lock";
    } else if (module == "仪器配置") {
        if (subpage == "运行状态") {
            description = "统一读取仪器遥测；界面、检测引擎和智能台共享同一份只读状态。";
            rows = {
                {"分子泵", health.ready ? "运行" : "未就绪",
                    measurementText(telemetry.molecularPumpRpm, 'f', 0) + " RPM",
                    QString("%1 A · %2 V · %3 ℃").arg(measurementText(telemetry.molecularPumpCurrentA, 'f', 2))
                        .arg(measurementText(telemetry.molecularPumpVoltageV, 'f', 1))
                        .arg(measurementText(telemetry.molecularPumpTemperatureC, 'f', 1))},
                {"真空度", health.connected ? "实时" : "不可用",
                    measurementText(telemetry.vacuumMbar, 'E', 2) + " mbar", "实时采样"},
                {"载气", telemetry.carrierGasMode,
                    measurementText(telemetry.carrierGasFlowMlMin, 'f', 2) + " mL/min",
                    measurementText(telemetry.carrierGasPressureTorr, 'f', 1) + " Torr"},
                {"离子阱 / TD", "实时",
                    QString("%1 ℃ / %2 ℃").arg(measurementText(telemetry.ionTrapTemperatureC, 'f', 1))
                        .arg(measurementText(telemetry.tdTemperatureC, 'f', 1)), "只读遥测"},
                {"离子源 / 倍增器", "实时",
                    QString("%1 V / %2 V").arg(measurementText(telemetry.ionSourceVoltageV, 'f', 1))
                        .arg(measurementText(telemetry.multiplierVoltageV, 'f', 0)), "受控参数"},
                {"抽气 / 注射泵", "实时",
                    QString("%1 % / %2 %").arg(measurementText(telemetry.extractionFlowPercent, 'f', 1))
                        .arg(measurementText(telemetry.syringeRemainingPercent, 'f', 1)), "抽气流速 / 注射泵剩余"}
            };
        } else if (subpage == "降温与关机") {
            description = "降温与关机保持独立受控步骤，执行结果以仪器回执为准。";
            rows = {
                {"降温流程", onOff(configured.value("coolingModeOn")),
                    measurementText(health.tdTemperatureC, 'f', 1) + " ℃", "持续监测 TD 温度"},
                {"仪器电源", onOff(configured.value("powerOn")), "—", "降温完成后再关机"},
                {"关机条件", health.ready ? "可检查" : "已阻断",
                    descriptor.protocolVersion, "温度与任务状态联锁"}
            };
            actionText = configured.value("coolingModeOn").toBool() ? "停止降温" : "开始降温";
            target = "toggle:coolingModeOn";
        } else if (subpage == "硬件接入说明") {
            description = "485已实现只读状态查询，可在上方选择串口连接；9600、8N1、无流控。"
                "网口TCP可在同页切换标签，默认监听11000，读取倍增管高压、真空规原始值与实验状态。超时清除旧读数。";
            rows = {
                {"RS-485", "只读接入", controller_->instrumentConnectionSummary(), "0x30状态查询"},
                {"网口TCP", "只读接入", "默认11000", "可与485同时连接；谱图转换仍待确认"},
                {"硬件控制", "未开放", "—", "加热、电源、泵和载气控制需另行验证"}
            };
        }
    } else {
        description = QString("“%1 / %2”保留为文档要求的功能入口；当前没有足够的正式数据或协议执行该操作。")
            .arg(module, subpage);
        rows = {{"功能状态", "未配置", "—", "—"}};
    }

    if (target.isEmpty() && module == "数据处理") {
        actionText = "查看分析结果"; target = "home";
    } else if (target.isEmpty() && module == "仪器配置" && subpage != "运行状态") {
        actionText = "查看接入要求"; target = "requirements";
    }
    settingsDetailDescription_->setText(description);
    if (settingsGuidance_) {
        const QString nextStep = target.isEmpty()
            ? "当前页面以查看与核对为主。"
            : "下方只保留一个与当前任务直接相关的操作；执行结果会写入本地状态或记录。";
        settingsGuidance_->setText(description + "\n\n" + nextStep);
    }
    prepareTableRows(settingsStatusTable_, rows.size());
    for (int row = 0; row < rows.size(); ++row)
        for (int column = 0; column < rows[row].size(); ++column)
            setTableText(settingsStatusTable_, row, column, rows[row][column]);
    settingsStatusTable_->resizeRowsToContents();
    int tableHeight = settingsStatusTable_->horizontalHeader()->height()
        + 2 * settingsStatusTable_->frameWidth() + 2;
    for (int row = 0; row < settingsStatusTable_->rowCount(); ++row)
        tableHeight += settingsStatusTable_->rowHeight(row);
    // Fit short tables exactly; retain scrolling only for genuinely long data.
    settingsStatusTable_->setFixedHeight(qBound(80, tableHeight, 310));
    if (!target.isEmpty()) {
        settingsDetailAction_->setText(actionText);
        settingsDetailAction_->setProperty("target", target);
        settingsDetailAction_->setVisible(true);
        const bool busy = controller_->phase() == AppController::Phase::Acquiring
            || controller_->phase() == AppController::Phase::Analyzing;
        settingsDetailAction_->setEnabled((target != "lock" || !busy)
            && !(controller_->instrumentReadOnly() && target.startsWith("toggle:")));
        settingsDetailAction_->setToolTip(target == "lock" && busy ? "检测进行中，保持停止入口可用" : "");
    }
}

void MainWindow::openSettingsModule(const QString &requestedModule, const QString &requestedSubpage) {
    // Preserve old assistant routes without maintaining duplicate placeholder pages.
    const bool legacyHardwarePage = requestedModule == "抽取清洗液";
    const QString module = legacyHardwarePage ? QString("仪器配置")
        : requestedModule == "标准与校准" ? (requestedSubpage == "定量曲线" ? QString("定量曲线") : QString("参考谱库"))
        : requestedModule;
    const QString subpage = legacyHardwarePage ? QString("硬件接入说明")
        : requestedSubpage == "调谐与校准" ? QString("质量轴校准")
        : requestedSubpage == "进样与注射泵" ? QString("注射泵")
        : requestedSubpage == "仪器控制" ? QString("常用部件") : requestedSubpage;
    const auto pages = settingsPages_.value(module);
    if (pages.isEmpty()) return;
    const QString target = pages.contains(subpage) ? subpage : pages.first();
    if (workspaceStack_ && workspaceStack_->count() > 1) setWorkspaceSection(1);
    settingsDetail_->setText(target);
    settingsDetail_->setVisible(module != "参考谱库" && module != "定量曲线");
    if (module == "仪器配置" && (target == "常用部件" || target == "辅助部件")) {
        settingsDetailStack_->setCurrentIndex(1);
        if (auto *controls = settingsDetailStack_->findChild<QStackedWidget *>("instrumentControlPages"))
            controls->setCurrentIndex(target == "辅助部件" ? 1 : 0);
    }
    else if (module == "仪器配置" && target == "参数预设")
        settingsDetailStack_->setCurrentIndex(2);
    else if (module == "仪器配置" && target == "降温与关机")
        settingsDetailStack_->setCurrentIndex(3);
    else if (module == "参考谱库" || module == "定量曲线")
        settingsDetailStack_->setCurrentIndex(module == "参考谱库" ? 4 : 5);
    else if (module == "仪器配置" && target == "射频调谐") settingsDetailStack_->setCurrentIndex(6);
    else if (module == "仪器配置" && target == "质量轴校准") settingsDetailStack_->setCurrentIndex(7);
    else if (module == "仪器配置" && target == "注射泵") settingsDetailStack_->setCurrentIndex(8);
    else if (module == "载气节省" || module == "清洗模式") {
        settingsDetailStack_->setCurrentIndex(9);
        settingsDetail_->setText("气路与清洗");
    }
    else
        populateSettingsDetail(module, target);
    if (settingsCategoryTree_) {
        for (int index = 0; index < settingsCategoryTree_->topLevelItemCount(); ++index) {
            auto *item = settingsCategoryTree_->topLevelItem(index);
            if (item->data(0, Qt::UserRole).toString() == module
                && item->data(0, Qt::UserRole + 1).toString() == target) {
                if (auto *section = findChild<QComboBox *>("settingsSection"))
                    section->setCurrentIndex(item->data(0, Qt::UserRole + 3).toInt());
                settingsCategoryTree_->setCurrentItem(item);
                break;
            }
        }
    }
}

void MainWindow::updatePhase(AppController::Phase phase, const QString &label) {
    phaseLabel_->setText(label.startsWith("已打开历史记录 ") ? "历史记录" : label);
    phaseLabel_->setToolTip(label);
    const bool busy = phase == AppController::Phase::Acquiring || phase == AppController::Phase::Analyzing;
    if(phase==AppController::Phase::Failed || phase==AppController::Phase::Ready)
        showReportAfterRunSaved_=false;
    const bool locked=busy || showReportAfterRunSaved_ || detectionAwaitingConfirmation_;
    const QString buttonText=locked ? "检测中" : "开始检测";
    actions_->action("StartRun")->setEnabled(!locked);
    actions_->action("StartRun")->setText(buttonText);
    startButton_->setEnabled(!locked);
    startButton_->setText(buttonText);
    if (emptyDataBanner_)
        emptyDataBanner_->setVisible(height() >= 620 && phase == AppController::Phase::Ready && controller_->liveSpectrum().isEmpty());
    if (workflowLabel_) {
        const QString marker = phase == AppController::Phase::Acquiring
            ? "准备  ›  ● 采集  ›  分析  ›  复核/报告"
            : phase == AppController::Phase::Analyzing
                ? "准备  ›  采集  ›  ● 分析  ›  复核/报告"
                : phase == AppController::Phase::ResultReady
                    ? "准备  ›  采集  ›  分析  ›  ● 复核/报告"
                    : "● 准备  ›  采集  ›  分析  ›  复核/报告";
        workflowLabel_->setText(marker);
    }
    refreshAiContext();
}

void MainWindow::showResult(const AnalysisResult &result) {
    updateWorkspaceLayout();
    if (controller_->currentRun().dataScope == "PUBLIC_EXAMPLE") {
        phaseLabel_->setText("分析完成");
        eicMz_->setValue(391.284103);
        eicTolerance_->setValue(0.5);
        refreshRunEic();
        refreshAiContext();
        return;
    }
    const bool simulated = controller_->currentRun().dataScope == "DEMO_SIMULATION"
        || std::any_of(result.candidates.begin(), result.candidates.end(), [](const MatchCandidate &candidate) { return candidate.demo; });
    if (resultSource_) resultSource_->setText(simulated
        ? "数据来源：本机检测数据"
        : "数据来源：导入数据");
    refreshAiContext();
}

void MainWindow::refreshRunEic() {
    eicPlot_->setAxisLabels("时间 / s", "提取离子信号");
    eicPlot_->setPoints({});
    const auto ordinal = eicMz_->value() > 0
        ? controller_->bundledIntensityTrend(eicMz_->value(), eicTolerance_->value()) : QVector<SpectrumPoint>{};
    if (!ordinal.isEmpty()) {
        eicPlot_->setPoints(ordinal);
        eicPlot_->setAxisLabels("原始谱序号（非时间）", QString("m/z %1 ± %2 Da").arg(eicMz_->value()).arg(eicTolerance_->value()));
        return;
    }
    if (ticPlot_->points().size() < 2) {
        eicPlot_->setEmptyMessage("等待时间序列", "");
    } else if (eicMz_->value() <= 0) {
        eicPlot_->setEmptyMessage("点选质谱峰提取 EIC", "");
    } else {
        eicPlot_->setPoints(ChromatogramEngine::trace(controller_->scans(),
            ChromatogramEngine::Kind::Eic, 1, eicMz_->value(), eicTolerance_->value()));
        eicPlot_->setAxisLabels("时间 / s", QString("m/z %1 ± %2 Da").arg(eicMz_->value(), 0, 'g', 8).arg(eicTolerance_->value()));
    }
    eicPlot_->update();
}

void MainWindow::applyDesignSystem() {
    Scientz::Ui::ThemeManager::apply(*qApp, Scientz::Ui::Density::Standard);
}

} // namespace qitest
