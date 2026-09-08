#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QWidget;

namespace Scientz::Ui {

enum class Density { Standard, Compact };

struct Colors final {
    inline static const QColor Teal900{"#005C5C"};
    inline static const QColor Teal700{"#007A7A"};
    inline static const QColor Teal500{"#00A8A8"};
    inline static const QColor Teal100{"#DDEFEA"};
    inline static const QColor Teal50{"#EEF7F4"};
    inline static const QColor Blue500{"#3478C9"};
    inline static const QColor Violet500{"#7868D8"};
    inline static const QColor Rose500{"#D95D78"};
    inline static const QColor Orange500{"#D97706"};
    inline static const QColor Amber700{"#996A00"};
    inline static const QColor Amber500{"#E19D00"};
    inline static const QColor Amber50{"#FFF8E8"};
    inline static const QColor Graphite900{"#1D2422"};
    inline static const QColor Graphite700{"#4F5755"};
    inline static const QColor Graphite500{"#707372"};
    inline static const QColor Graphite200{"#D4D9D7"};
    inline static const QColor Graphite100{"#DDE5E1"};
    inline static const QColor Graphite50{"#F0F2F1"};
    inline static const QColor Success{"#2F7D67"};
    inline static const QColor Danger{"#C74646"};
    inline static const QColor Danger50{"#FDECEC"};
    inline static const QColor Offline{"#8B9491"};
    inline static const QColor Canvas{"#E9EEEC"};
    inline static const QColor Panel{"#FFFFFF"};
    inline static const QColor SurfaceSubtle{"#F1F5F3"};
    inline static const QColor Border{"#D6DBD9"};
    inline static const QColor BorderStrong{"#ADB6B3"};
    inline static const QColor TextPrimary{"#1D2422"};
    inline static const QColor TextSecondary{"#59625F"};
    inline static const QColor TextDisabled{"#98A19E"};
};

struct Metrics final {
    static constexpr int Space1 = 4;
    static constexpr int Space2 = 8;
    static constexpr int Space3 = 12;
    static constexpr int Space4 = 16;
    static constexpr int Space6 = 24;
    static constexpr int Space8 = 32;
    static constexpr int RadiusControl = 8;
    static constexpr int RadiusPanel = 12;
    static constexpr int ControlCompact = 32;
    static constexpr int PanelPaddingCompact = 12;
    static constexpr int InspectorWidthDefault = 300;
    static constexpr int CanvasWidthMin = 640;
    static constexpr int AssistantWidthMin = 280;
    static constexpr int AssistantWidthMax = 320;
    static constexpr int MonitorWidthMin = 224;
    static constexpr int MonitorWidthMax = 260;
    static constexpr int SettingsWidthMin = 176;
    static constexpr int SettingsWidthMax = 208;
    // Includes settings navigation; leaves at least 500 logical pixels for its
    // actual editor. These are measured content budgets, not a golden ratio.
    static constexpr int ParallelWorkspaceMin = 720;
};

class Theme final {
public:
    static QString buildStyleSheet(Density density);
};

class ThemeManager final {
public:
    static void apply(QApplication &app, Density density);
    static void refresh(QWidget *widget);
};

} // namespace Scientz::Ui
