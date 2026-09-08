#include "ai/AiToolProtocol.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace qitest {
namespace {

struct ToolDefinition {
    const char *id;
    const char *description;
    AssistantCommand command;
    AiToolRisk risk;
};

constexpr ToolDefinition kTools[] = {
    {"open_home", "打开检测主页", AssistantCommand::OpenHome, AiToolRisk::Navigation},
    {"prepare_run", "打开运行方法准备页，不开始采集", AssistantCommand::PrepareRun, AiToolRisk::Navigation},
    {"open_results", "报告查看", AssistantCommand::OpenResults, AiToolRisk::ReadOnly},
    {"open_report", "打开复核与报告页", AssistantCommand::OpenReport, AiToolRisk::ReadOnly},
    {"open_library", "打开本地参考谱库", AssistantCommand::OpenLibrary, AiToolRisk::ReadOnly},
    {"open_method", "打开编辑方法与版本管理", AssistantCommand::OpenMethod, AiToolRisk::ReadOnly},
    {"open_quantitation", "打开定量曲线", AssistantCommand::OpenQuantitation, AiToolRisk::ReadOnly},
    {"open_trace_analysis", "打开当前记录的 TIC/EIC 与积分面板，不修改鉴定结论", AssistantCommand::OpenTraceAnalysis, AiToolRisk::ReadOnly},
    {"open_instrument_status", "显示仪器实时状态栏", AssistantCommand::OpenInstrumentStatus, AiToolRisk::ReadOnly},
    {"open_instrument_settings", "打开仪器控制开关页", AssistantCommand::OpenInstrumentSettings, AiToolRisk::Navigation},
    {"open_instrument_presets", "打开仪器参数预设编辑页，不下发参数", AssistantCommand::OpenInstrumentPresets, AiToolRisk::Navigation},
    {"open_calibration", "打开调谐与校准", AssistantCommand::OpenCalibration, AiToolRisk::Navigation},
    {"open_sampling", "打开进样器与注射泵", AssistantCommand::OpenSampling, AiToolRisk::Navigation},
    {"open_carrier_gas", "打开载气节省与载气模式", AssistantCommand::OpenCarrierGas, AiToolRisk::Navigation},
    {"open_cleaning", "打开清洗流程", AssistantCommand::OpenCleaning, AiToolRisk::Navigation},
    {"enable_ion_source", "提交离子源开启请求", AssistantCommand::EnableIonSource, AiToolRisk::SimulatedControl},
    {"disable_ion_source", "提交离子源关闭请求", AssistantCommand::DisableIonSource, AiToolRisk::SimulatedControl},
    {"open_power_page", "打开降温与关机页，不执行关机", AssistantCommand::OpenPower, AiToolRisk::Navigation},
    {"open_session_protection", "打开设置中的会话保护", AssistantCommand::OpenSessionProtection, AiToolRisk::Navigation},
    {"open_settings", "打开设置与维护", AssistantCommand::OpenSettings, AiToolRisk::Navigation},
    {"open_help", "打开本地操作帮助", AssistantCommand::OpenHelp, AiToolRisk::ReadOnly},
};

QRegularExpression envelopePattern() {
    return QRegularExpression(QStringLiteral("<tool_call>\\s*(\\{.*?\\})\\s*</tool_call>"),
        QRegularExpression::DotMatchesEverythingOption);
}

} // namespace

std::optional<AiToolCall> AiToolProtocol::parseEnvelope(const QString &text) {
    const auto match = envelopePattern().match(text);
    if (!match.hasMatch()) return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(match.captured(1).toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto object = document.object();
    const QString id = object.value("tool").toString();
    const double confidence = object.value("confidence").toDouble(0.0);
    if (confidence < 0.85 || !object.value("arguments").isObject()) return std::nullopt;
    for (const auto &tool : kTools) {
        if (id != QLatin1String(tool.id)) continue;
        return AiToolCall{id, object.value("arguments").toObject(), confidence, tool.command, tool.risk};
    }
    return std::nullopt;
}

std::optional<AiToolCall> AiToolProtocol::parseNativeToolCall(const QJsonObject &toolCall) {
    const auto function = toolCall.value("function").toObject();
    const QString id = function.value("name").toString();
    QJsonParseError error;
    QJsonObject arguments;
    const auto argumentValue = function.value("arguments");
    if (argumentValue.isObject()) arguments = argumentValue.toObject();
    else if (argumentValue.isString()) {
        const auto document = QJsonDocument::fromJson(argumentValue.toString().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
        arguments = document.object();
    } else return std::nullopt;
    for (const auto &tool : kTools)
        if (id == QLatin1String(tool.id))
            return AiToolCall{id, arguments, 1.0, tool.command, tool.risk};
    return std::nullopt;
}

QJsonArray AiToolProtocol::nativeTools() {
    QJsonArray tools;
    for (const auto &tool : kTools) {
        tools.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
            {"name", QLatin1String(tool.id)},
            {"description", QString::fromUtf8(tool.description)},
            {"parameters", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}},
                {"additionalProperties", false}}}
        }}});
    }
    return tools;
}

QString AiToolProtocol::removeEnvelope(const QString &text) {
    QString clean = text;
    clean.remove(envelopePattern());
    return clean.trimmed();
}

QString AiToolProtocol::toolInstructions() {
    QStringList ids;
    for (const auto &tool : kTools) ids << QLatin1String(tool.id);
    return QString(
        "如果用户明确要求打开或前往软件页面，可在回答末尾追加且只追加一个："
        "<tool_call>{\"tool\":\"工具ID\",\"confidence\":0.00,\"arguments\":{}}</tool_call>。"
        "允许的工具ID：%1。只有把握不低于0.85才可调用。禁止提出开始采集、修改仪器、删除、"
        "覆盖、关机或确认鉴定结论等工具。enable/disable_ion_source "
        "必须经过受控界面并等待人工确认和设备回执。")
        .arg(ids.join(','));
}

QString AiToolProtocol::idFor(AssistantCommand command) {
    for (const auto &tool : kTools)
        if (tool.command == command) return QLatin1String(tool.id);
    return {};
}

bool AiToolProtocol::mayApplyStateChange(const AiToolCall &call, bool simulation) {
    if (call.risk == AiToolRisk::ReadOnly || call.risk == AiToolRisk::Navigation) return true;
    return call.risk == AiToolRisk::SimulatedControl && simulation;
}

} // namespace qitest
