#pragma once

#include "app/AppController.h"
#include "ai/AiCommandRouter.h"
#include "ui/SpectrumPlot.h"

#include <QMainWindow>
#include <QMap>
#include <QPointer>

class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QGridLayout;
class QResizeEvent;
class QDoubleSpinBox;

namespace Scientz::Ui {
class ActionRegistry;
}

namespace qitest {

class ChatTranscript;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppController *controller, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *createLoginPage();
    QWidget *createStartupPage();
    QWidget *createWorkspacePage();
    QWidget *createHomePage();
    QWidget *createSettingsPage();
    QWidget *createReportPage();
    QWidget *createLibraryPage();
    QWidget *createMethodPage();
    QWidget *createQuantitationPage();
    QWidget *createAiAssistantPage();
    QWidget *createMonitorPanel();
    QWidget *createPanel(const QString &technicalLabel, const QString &title, QWidget *content);
    QToolButton *createCommandButton(const QString &actionId, const QString &text, const QIcon &icon = {});
    void startStartupSequence();
    void showWorkspace();
    void setWorkspaceSection(int index);
    void closeSettings();
    int workspaceBeforeSettings_ = 0;
    void openSettingsModule(const QString &module, const QString &subpage = {});
    void populateSettingsDetail(const QString &module, const QString &subpage);
    void updatePhase(AppController::Phase phase, const QString &label);
    void showResult(const AnalysisResult &result);
    void refreshRunEic();
    void refreshReport(const RunSummary &run);
    void performLibrarySearch();
    void refreshMethods();
    void refreshAiContext();
    void setAssistantVisible(bool visible);
    void setInstrumentToolsVisible(bool visible);
    void updateWorkspaceLayout();
    void importRunArchiveFromDialog();
    void submitAiQuestion(const QString &question = {});
    bool handleAssistantCommand(const QString &text);
    bool executeAssistantCommand(qitest::AssistantCommand command, const QString &source);
    void applyDesignSystem();

    AppController *controller_;
    Scientz::Ui::ActionRegistry *actions_ = nullptr;
    QStackedWidget *rootStack_ = nullptr;
    QStackedWidget *workspaceStack_ = nullptr;
    QWidget *instrumentTools_ = nullptr;
    QWidget *aiAssistantPanel_ = nullptr;
    bool assistantTargetVisible_ = false;
    bool instrumentTargetVisible_ = true;
    bool compactRails_ = false;
    bool smallScreen_ = false;
    bool aiRequestBusy_ = false;
    bool showReportAfterRunSaved_ = false;
    bool detectionAwaitingConfirmation_ = false;
    QGridLayout *workspaceBodyLayout_ = nullptr;
    QToolButton *assistantButton_ = nullptr;
    QToolButton *instrumentToolsButton_ = nullptr;
    QStackedWidget *settingsStack_ = nullptr;
    QTreeWidget *settingsCategoryTree_ = nullptr;
    QLineEdit *username_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLabel *loginError_ = nullptr;
    QLabel *startupLabel_ = nullptr;
    QProgressBar *startupProgress_ = nullptr;
    QLabel *phaseLabel_ = nullptr;
    QLabel *workflowLabel_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QWidget *emptyDataBanner_ = nullptr;
    SpectrumPlot *ticPlot_ = nullptr;
    SpectrumPlot *spectrumPlot_ = nullptr;
    SpectrumPlot *eicPlot_ = nullptr;
    QDoubleSpinBox *eicMz_ = nullptr;
    QDoubleSpinBox *eicTolerance_ = nullptr;
    QTimer *eicRefreshTimer_ = nullptr;
    QWidget *spectrumContainer_ = nullptr;
    QLabel *resultSource_ = nullptr;
    QLabel *settingsDetail_ = nullptr;
    QLabel *settingsDetailDescription_ = nullptr;
    QLabel *settingsGuidance_ = nullptr;
    QPushButton *settingsDetailAction_ = nullptr;
    QTableWidget *settingsStatusTable_ = nullptr;
    QStackedWidget *settingsDetailStack_ = nullptr;
    QLabel *aiStatus_ = nullptr;
    QLabel *aiOutput_ = nullptr;
    QLabel *aiContext_ = nullptr;
    ChatTranscript *aiConversation_ = nullptr;
    QStringList pendingChatQuestions_;
    QLineEdit *aiQuestion_ = nullptr;
    QPushButton *aiSendButton_ = nullptr;
    QLabel *reportRunId_ = nullptr;
    QLabel *reportStatus_ = nullptr;
    QLabel *reportMeta_ = nullptr;
    QLabel *reportSelectionHint_ = nullptr;
    QLabel *reportQualityValue_ = nullptr;
    QLabel *reportCandidateCount_ = nullptr;
    QLabel *reportReviewState_ = nullptr;
    QLabel *reportScope_ = nullptr;
    QLabel *reportEvidenceDetail_ = nullptr;
    QLabel *reportQualityChecks_ = nullptr;
    QTableWidget *reportCandidateTable_ = nullptr;
    QLineEdit *reportCandidateSearch_ = nullptr;
    QPushButton *reportReviewButton_ = nullptr;
    QPushButton *reportExportButton_ = nullptr;
    QPushButton *reportReviewViewButton_ = nullptr;
    QPushButton *reportPreviewViewButton_ = nullptr;
    bool reportPreviewVisible_ = false;
    QLineEdit *librarySearch_ = nullptr;
    QTableWidget *libraryTable_ = nullptr;
    QLabel *librarySearchStatus_ = nullptr;
    QLineEdit *methodName_ = nullptr;
    QLineEdit *methodRevisionNote_ = nullptr;
    QTableWidget *methodTable_ = nullptr;
    QTimer *methodRefreshTimer_ = nullptr;
    QMap<QString, QStringList> settingsPages_;
};

} // namespace qitest
