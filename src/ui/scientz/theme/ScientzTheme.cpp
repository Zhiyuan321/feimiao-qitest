#include "ui/scientz/theme/ScientzTheme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

namespace Scientz::Ui {

QString Theme::buildStyleSheet(Density density) {
    const int controlHeight = density == Density::Compact ? 36 : 44;
    QString style = QString(R"QSS(
        * { font-family: "PingFang SC"; color: %1; }
        QWidget { background: transparent; font-size: 15px; }
        QMainWindow, QStackedWidget#rootStack { background: %2; }
        QDialog { background: %2; }
        QLabel { background: transparent; border: 0; }
        QLabel#userMessage { background: %17; color: %20; border-radius: 10px; padding: 10px 12px; }
        QLabel#assistantMessage { background: %4; color: %1; border-radius: 10px; padding: 10px 12px; }
        QWidget#brandHeader QWidget, QWidget#startupPage QWidget, QWidget#brandIdentity QWidget { background: transparent; }
        QWidget#brandHeader { background: %4; border-bottom: 1px solid %5; }
        QWidget#startupPage { background: %3; }
        QWidget#identityBar, QWidget#commandBar, QWidget#brandIdentity, QWidget#statusRail, QWidget#panelHeader { background: %4; }
        QWidget#commandBar, QWidget#contextHeader, QWidget#statusRail { border-bottom: 1px solid %5; }
        QWidget#contextHeader { background: %6; }
        QWidget#analysisCanvas { background: %2; }
        QWidget#standardLibraryTabs, QStackedWidget#standardLibraryPages { background: %2; border: 0; }
        QWidget#calibrationPage, QWidget#userStandardsPage { background: %2; }
        QTabWidget#communicationTabs::pane { background: %2; border: 0; }
        QTabWidget#communicationTabs QStackedWidget,
        QWidget#networkConnectionPanel, QWidget#rs485ConnectionPanel { background: %2; }
        QTabWidget#communicationTabs QTabBar::tab { background: %6; color: %1; border: 0; padding: 8px 16px; min-height: 24px; }
        QTabWidget#communicationTabs QTabBar::tab:selected { background: %17; color: %20; font-weight: 600; }
        QTabWidget#communicationTabs QTabBar::tab:hover { background: %19; }
        QPushButton[sciRole="librarySelector"] { min-height: 30px; background: %6; border: 0; padding: 0 14px; }
        QPushButton[sciRole="librarySelector"]:checked { background: %17; color: %20; font-weight: 600; }
        QPushButton[sciRole="librarySelector"]:hover { background: %19; }
        QWidget#monitorPanel { background: %6; border: 0; }
        QWidget#assistantRail { background: %6; border: 0; }
        QWidget#settingsSidebar { background: %7; border-right: 1px solid %5; }
        QWidget#settingsSidebarRight { background: %7; border: 0; }
        QFrame#separator { color: %5; }
        QFrame#panel, QWidget#panel { background: %4; border: 0; border-radius: 12px; }
        QWidget[sciRole="plotPanel"] { background: %4; border: 0; border-radius: 12px; }
        QWidget[sciRole="workspaceSection"] { background: %4; border: 0; border-radius: 12px; }
        QFrame[sciRole="monitorGroup"] { background: %4; border: 0; border-radius: 10px; padding: 6px; }
        QWidget#panelHeader { border-top-left-radius: 12px; border-top-right-radius: 12px; }
        QFrame[sciRole="aiEvidence"] { background: %6; border: 0; border-radius: 10px; }
        QFrame[sciRole="summaryStrip"] { background: %4; border: 0; border-radius: 12px; }
        QFrame[sciRole="assistantComposer"] { background: %4; border: 1px solid %15; border-radius: 10px; }
        QFrame[sciRole="emptyState"] { background: %6; border: 0; border-radius: 12px; }
        QFrame[sciState="warning"] { background: %8; border: 0; border-radius: 10px; }
        QLabel#brandLogo { background: transparent; border: 0; }
        QLabel[sciTone="onBrand"], QLabel[sciTone="startupDetail"], QLabel[sciTone="startupSteps"] { color: %4; }
        QLabel[sciTone="brandTitle"] { color: %4; font-size: 15px; font-weight: 600; }
        QLabel[sciTone="startupBrand"] { color: %4; font-size: 18px; font-weight: 600; letter-spacing: 2px; }
        QLabel[sciTone="startupTitle"] { color: %4; font-size: 30px; font-weight: 600; }
        QLabel[sciTone="loginTitle"] { font-size: 28px; font-weight: 600; }
        QLabel[sciTone="pageTitle"] { font-size: 20px; font-weight: 600; }
        QLabel[sciTone="sectionTitle"] { font-size: 17px; font-weight: 600; }
        QLabel[sciTone="panelTitle"], QLabel[sciTone="contextTitle"], QLabel[sciTone="identityTitle"], QLabel[sciTone="bodyStrong"] { font-weight: 600; }
        QLabel[sciTone="technical"] { color: %10; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
        QLabel[sciTone="secondary"], QLabel[sciTone="metadata"], QLabel[sciTone="user"] { color: #243331; font-size: 16px; font-weight: 600; }
        QLabel[sciTone="readoutValue"] { font-family: "Menlo"; }
        QLabel[sciRole="reportDetail"] { font-size: 13px; font-weight: 400; color: #243331; }
        QWidget#monitorPanel QLabel#readoutName, QWidget#monitorPanel QLabel[sciTone="metadata"] { font-size: 13px; font-weight: 400; color: #52615f; }
        QWidget#monitorPanel QLabel[sciTone="readoutValue"] { font-family: "Arial"; font-size: 16px; font-weight: 600; }
        QWidget#monitorPanel QLabel#readoutUnit { font-size: 12px; font-weight: 400; }
        QLabel[sciTone="metricValue"] { font-size: 14px; font-weight: 600; color: %20; }
        QLabel[sciState="healthy"] { color: %12; }
        QLabel[sciState="critical"], QLabel[sciTone="error"] { color: %13; }
        QLabel[sciTone="warningTitle"] { color: %14; font-weight: 600; }
        QLineEdit, QPlainTextEdit { background: %4; border: 1px solid %15; border-radius: 8px; min-height: %16px; padding: 0 8px; }
        QPlainTextEdit { padding: 8px; selection-background-color: %17; }
        QComboBox { background: %4; border: 1px solid %15; border-radius: 8px; min-height: %16px; padding: 0 30px 0 8px; }
        QComboBox::drop-down { width: 26px; border: 0; background: transparent; }
        QComboBox::down-arrow { image: url(:/qitest/resources/icons/chevron-down.svg); width: 16px; height: 16px; }
        QComboBox:hover, QComboBox:focus { border-color: %18; background: %19; }
        QComboBox:on { border: 2px solid %18; background: %19; }
        QComboBox::drop-down:hover { background: %17; border-radius: 0; }
        QComboBox:disabled { background: %6; color: %11; }
        QTabWidget#workbenchTabs::pane { border: 0; background: %2; }
        QTabWidget#workbenchTabs QTabBar::tab { background: %6; border: 0; border-radius: 8px; min-height: 44px; padding: 0 18px; margin-right: 6px; font-weight: 600; }
        QTabWidget#workbenchTabs QTabBar::tab:selected { background: %17; color: %20; }
        QTabWidget#workbenchTabs QTabBar::tab:hover { background: %19; }
        QComboBox QAbstractItemView { background: %4; color: %1; border: 1px solid %15; border-radius: 0; padding: 0; outline: 0; selection-background-color: %17; selection-color: %20; }
        QComboBox QAbstractItemView::item { min-height: 36px; padding: 0 10px; margin: 0; border: 0; border-radius: 0; }
        QComboBox QAbstractItemView::item:hover { background: %19; color: %20; }
        QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid %18; }
        QLineEdit[sciRole="assistantInput"] { background: transparent; border: 0; padding: 0 4px; min-height: 32px; }
        QLineEdit[sciRole="assistantInput"]:focus { border: 0; }
        QPlainTextEdit[sciRole="assistantTranscript"] { background: %4; border-color: %5; border-radius: 9px; padding: 10px; }
        QPushButton { background: %4; border: 1px solid %5; border-radius: 8px; min-height: %16px; padding: 0 12px; }
        QPushButton:hover { background: %19; border-color: %18; }
        QPushButton:pressed { background: %7; border-color: %15; padding-top: 1px; }
        QPushButton:focus { border: 2px solid %18; }
        QPushButton[sciRole="primary"] { background: %18; color: %4; border-color: %18; font-weight: 600; }
        QPushButton[sciRole="primary"]:hover { background: %20; }
        QPushButton:disabled, QToolButton:disabled { background: %7; color: %11; border-color: %5; }
        QPushButton[sciRole="primary"]:disabled { background: %7; color: %11; border-color: %5; }
        QPushButton[sciRole="quietAction"] { min-height: 28px; padding: 0 10px; background: transparent; color: %20; border-color: %5; }
        QPushButton[sciRole="plotAction"] { min-height: 0; padding: 0 8px; background: %4; color: %20; border: 1px solid %5; border-radius: 6px; font-size: 14px; }
        QPushButton[sciRole="plotAction"]:hover, QPushButton[sciRole="plotAction"]:focus { border: 1px solid %18; }
        QPushButton[sciRole="plotAction"]:pressed { background: %7; padding: 0 8px; }
        QPushButton[sciState="current"] { background: %17; color: %20; border-color: transparent; font-weight: 600; }
        QToolButton[sciRole="command"] { background: transparent; border: 0; border-radius: 9px; padding: 4px 8px; font-size: 14px; font-weight: 600; }
        QToolButton[sciRole="command"]:hover { background: %19; }
        QToolButton[sciRole="command"]:pressed { background: %7; padding-top: 5px; }
        QToolButton[sciRole="command"][sciState="current"] { background: %17; color: %20; font-weight: 600; }
        QToolButton[sciRole="command"][sciEmphasis="primary"] { color: %20; font-weight: 600; }
        QLabel#batteryStatus { background: transparent; border: 0; padding: 0 6px; }
        QToolButton[sciRole="utility"] { background: transparent; border: 0; border-radius: 8px; padding: 4px; color: %11; }
        QToolButton[sciRole="utility"]:hover { background: %19; color: %20; }
        QToolButton[sciRole="utility"]:pressed { background: %7; }
        QToolButton[sciRole="utility"][sciState="current"] { background: %17; color: %20; }
        QToolButton[sciRole="disclosure"] { background: transparent; border: 0; color: %20; text-align: left; padding: 6px 0; font-weight: 600; }
        QToolButton[sciRole="moduleTile"] { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 8px; font-weight: 500; }
        QToolButton[sciRole="moduleTile"]:hover { border-color: %5; background: %19; }
        QToolButton[sciRole="moduleTile"]:pressed { background: %7; border-color: %15; }
        QPushButton[sciRole="moduleTile"] { background: %4; border: 1px solid %5; border-radius: 8px; padding: 8px 12px; text-align: left; font-weight: 500; }
        QPushButton[sciRole="moduleTile"]:hover { border-color: %18; background: %19; }
        QToolButton[sciRole="controlTile"] { background: %6; border: 1px solid %5; border-radius: 6px; padding: 10px; font-weight: 600; }
        QToolButton[sciRole="controlTile"]:hover { border-color: %18; }
        QToolButton[sciRole="controlTile"]:pressed { background: %7; border-color: %15; }
        QToolButton[sciRole="controlTile"]:checked { background: %17; border-color: %18; color: %20; }
        QPushButton[sciRole="choice"] { min-width: 52px; }
        QPushButton[sciRole="choice"]:checked { background: %17; border-color: %18; color: %20; font-weight: 600; }
        QProgressBar { background: rgba(255,255,255,0.18); border: 0; border-radius: 4px; height: 8px; }
        QProgressBar::chunk { background: %4; border-radius: 4px; }
        QListWidget { background: transparent; border: 0; outline: 0; }
        QListWidget::item { min-height: 38px; padding: 0 10px; border-radius: 0; }
        QListWidget::item:hover { background: %19; }
        QListWidget::item:selected { background: %17; color: %20; }
        QTreeWidget { background: transparent; border: 0; outline: 0; }
        QTreeWidget::item { min-height: 36px; padding: 0 6px; border-radius: 0; }
        QTreeWidget::item:hover { background: %19; }
        QTreeWidget::item:selected { background: %17; color: %20; }
        QTreeWidget::branch { background: transparent; border: 0; image: none; }
        QTreeWidget#settingsTree::item { min-height: 28px; }
        QTreeWidget#settingsTree::item:selected { background: %18; color: %4; }
        QTableWidget { background: %4; alternate-background-color: %6; border: 0; gridline-color: transparent; border-radius: 10px; outline: 0; }
        QTableWidget::item { border: 0; padding: 6px; }
        QTableWidget::item:hover { background: %19; }
        QTableWidget::item:selected { background: %17; color: %20; }
        QHeaderView::section { background: %17; border: 0; padding: 8px; font-weight: 600; }
        QMenu { background: %4; border: 1px solid %5; border-radius: 0; padding: 0; }
        QMenu::item { min-width: 150px; padding: 8px 24px 8px 10px; margin: 0; border-radius: 0; }
        QMenu::item:selected { background: %17; color: %20; }
        QMenu::separator { height: 1px; background: %5; margin: 5px 8px; }
        QToolTip { background: %1; color: %4; border: 0; padding: 7px 9px; }
        QScrollArea { border: 0; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: %5; min-height: 32px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: transparent; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
        QScrollBar::handle:horizontal { background: %5; min-width: 32px; border-radius: 4px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; background: transparent; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
        QSpinBox, QDoubleSpinBox { background: %4; border: 1px solid %15; border-radius: 8px; min-height: 42px; padding: 0 88px 0 10px; selection-background-color: %17; }
        QSpinBox:focus, QDoubleSpinBox:focus { border-color: %18; }
        QLabel[sciRole="plotInputLabel"] { font-size: 12px; font-weight: normal; padding: 0; color: %20; }
        QDoubleSpinBox[sciRole="plotInput"] { min-height: 24px; padding: 0 6px; background: transparent; border: 0; border-radius: 0; font-size: 12px; font-weight: normal; color: %20; }
        QDoubleSpinBox[sciRole="plotInput"]:focus { border: 0; background: transparent; }
        QDoubleSpinBox[sciRole="plotInput"] QLineEdit { font-size: 12px; padding: 0; min-height: 0; border: 0; background: transparent; }
        QDoubleSpinBox[sciRole="plotInput"]::up-button, QDoubleSpinBox[sciRole="plotInput"]::down-button { width: 0; height: 0; border: 0; }
        QLineEdit[sciRole="analysisInput"] { min-height: 32px; padding: 0 10px; border-radius: 8px; }
        QDoubleSpinBox[sciRole="analysisInput"], QSpinBox[sciRole="analysisInput"] { min-height: 32px; padding: 0 10px; border-radius: 8px; }
        QComboBox[sciRole="analysisInput"] { min-height: 32px; padding: 0 28px 0 10px; }
        QComboBox[sciRole="analysisInput"]::drop-down { width: 22px; border: 0; background: transparent; }
        QComboBox[sciRole="analysisInput"]::down-arrow { image: url(:/qitest/resources/icons/chevron-down.svg); width: 16px; height: 16px; }
        QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: center right; right: 5px; width: 34px; height: 32px; border: 0; border-radius: 6px; background: %17; }
        QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: center right; right: 44px; width: 34px; height: 32px; border: 0; border-radius: 6px; background: %6; }
        QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: %7; }
        QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed { background: %15; }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/qitest/resources/icons/increment.svg); width: 16px; height: 16px; }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/qitest/resources/icons/decrement.svg); width: 16px; height: 16px; }
        QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:off { image: url(:/qitest/resources/icons/increment-disabled.svg); }
        QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:off { image: url(:/qitest/resources/icons/decrement-disabled.svg); }
        QFrame#separator { background: %5; border: 0; }
        QSplitter::handle { background: transparent; width: 1px; }
        QSplitter#calibrationSplit::handle { background: %5; width: 1px; height: 1px; }
    )QSS");
    const QStringList values{
        Colors::TextPrimary.name(), Colors::Canvas.name(), Colors::Graphite900.name(),
        Colors::Panel.name(), Colors::Border.name(), Colors::SurfaceSubtle.name(),
        Colors::Graphite100.name(), Colors::Amber50.name(), Colors::Amber500.name(),
        Colors::Graphite500.name(), Colors::TextSecondary.name(), Colors::Success.name(),
        Colors::Danger.name(), Colors::Amber700.name(), Colors::BorderStrong.name(),
        QString::number(controlHeight), Colors::Teal100.name(), Colors::Teal700.name(),
        Colors::Teal50.name(), Colors::Teal900.name()
    };
    for (int index = values.size(); index >= 1; --index)
        style.replace("%" + QString::number(index), values.at(index - 1));
#ifdef Q_OS_WIN
    style.replace("PingFang SC", "Microsoft YaHei");
    style.replace("Menlo", "Consolas");
#endif
    return style;
}

void ThemeManager::apply(QApplication &app, Density density) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette = app.style()->standardPalette();
    palette.setColor(QPalette::Window, Colors::Canvas);
    palette.setColor(QPalette::Base, Colors::Panel);
    palette.setColor(QPalette::AlternateBase, Colors::SurfaceSubtle);
    palette.setColor(QPalette::Button, Colors::Panel);
    palette.setColor(QPalette::WindowText, Colors::TextPrimary);
    palette.setColor(QPalette::Text, Colors::TextPrimary);
    palette.setColor(QPalette::ButtonText, Colors::TextPrimary);
    app.setPalette(palette);
#ifdef Q_OS_WIN
    QFont font("Microsoft YaHei");
    font.setHintingPreference(QFont::PreferFullHinting);
    // 低配 Windows 使用即时状态变化，避免菜单淡入和组合框动画占用绘制时间。
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
#else
    QFont font("PingFang SC");
#endif
    // Match the stylesheet's logical-pixel size on both preview and instrument.
    // Point sizes otherwise vary with the platform DPI in unstyled controls.
    font.setPixelSize(15);
    font.setWeight(QFont::Normal);
    app.setFont(font);
    app.setStyleSheet(Theme::buildStyleSheet(density));
}

void ThemeManager::refresh(QWidget *widget) {
    if (!widget) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace Scientz::Ui
