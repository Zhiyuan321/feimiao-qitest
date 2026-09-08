#pragma once

#include "ai/AiCommandRouter.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <optional>

namespace qitest {

enum class AiToolRisk { ReadOnly, Navigation, SimulatedControl };

struct AiToolCall {
    QString id;
    QJsonObject arguments;
    double confidence = 0.0;
    AssistantCommand command = AssistantCommand::None;
    AiToolRisk risk = AiToolRisk::Navigation;
};

// The model may propose an operation, but only this C++ protocol can turn it
// into an application command. Hardware, acquisition and destructive commands
// are deliberately absent from the registry.
class AiToolProtocol final {
public:
    static std::optional<AiToolCall> parseEnvelope(const QString &text);
    static std::optional<AiToolCall> parseNativeToolCall(const QJsonObject &toolCall);
    static QString removeEnvelope(const QString &text);
    static QJsonArray nativeTools();
    static QString toolInstructions();
    static QString idFor(AssistantCommand command);
    static bool mayApplyStateChange(const AiToolCall &call, bool simulation);
};

} // namespace qitest
