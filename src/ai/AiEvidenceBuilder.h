#pragma once

#include "domain/Models.h"
#include "storage/WorkspaceRepository.h"

namespace qitest {

struct AiContextSnapshot {
    QString phaseLabel;
    QString dataScope;
    InstrumentHealth instrumentHealth;
    InstrumentTelemetry instrumentTelemetry;
    QString librarySummary;
    QString workspaceSummary;
    QString aiSummary;
    MethodDefinition activeMethod;
    RunSummary currentRun;
    AnalysisResult result;
};

class AiEvidenceBuilder final {
public:
    static QString buildEvidence(const AiContextSnapshot &snapshot);
    static QString buildSummary(const AiContextSnapshot &snapshot);
    static QString defaultQuestionForState(const AiContextSnapshot &snapshot);
};

} // namespace qitest
