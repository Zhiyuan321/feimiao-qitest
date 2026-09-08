#pragma once

#include <QString>

namespace qitest {

enum class AssistantCommand {
    None,
    OpenHome,
    PrepareRun,
    OpenResults,
    OpenReport,
    OpenLibrary,
    OpenMethod,
    OpenQuantitation,
    OpenInstrumentStatus,
    OpenInstrumentSettings,
    OpenCalibration,
    OpenSampling,
    OpenCarrierGas,
    OpenCleaning,
    EnableIonSource,
    DisableIonSource,
    ReviewInstrumentAdjustment,
    OpenPower,
    OpenSessionProtection,
    OpenSettings,
    OpenHelp,
    OpenInstrumentPresets,
    OpenTraceAnalysis
};

class AiCommandRouter final {
public:
    struct Understanding {
        AssistantCommand command = AssistantCommand::None;
        QString explanationTopic;
        QString feedback;
    };
    // Context is only used for explanations, never to infer a hardware target.
    static Understanding understand(const QString &text, const QString &contextTopic = {});
    static AssistantCommand route(const QString &text);
    static QString displayName(AssistantCommand command);
private:
    static AssistantCommand matchCommand(const QString &text);
};

} // namespace qitest
