#include "ai/AiSafetyGuard.h"

#include <QRegularExpression>
#include <QStringList>

namespace qitest {

QString AiSafetyGuard::enforce(const QString &modelText, const QString &deterministicEvidence) {
    QString cleaned = modelText;
    cleaned.remove(QRegularExpression(QStringLiteral("```(?:text|markdown)?\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    cleaned.replace("```", "");
    const QList<QPair<QString, QString>> internalTerms{
        {"phase_label", "当前阶段"},
        {"result_status", "分析状态"},
        {"NO_COMPLETED_ANALYSIS", "尚无已完成分析"},
        {"peak_count", "谱峰数量"},
        {"candidate_count", "候选数量"},
        {"data_scope", "数据范围"},
        {"DEMO_SIMULATION", "预览数据"},
        {"instrument_ready", "仪器就绪"},
        {"instrument_connected", "仪器已连接"}
    };
    for (const auto &term : internalTerms) cleaned.replace(term.first, term.second);
    cleaned.replace(QRegularExpression(QStringLiteral("“([^\u201d\\n]+)\"")), QStringLiteral("“\\1”"));
    cleaned.replace(QRegularExpression(QStringLiteral("[ \\t]+\\n")), "\n");
    cleaned.replace(QRegularExpression(QStringLiteral("\\n{3,}")), "\n\n");
    if (deterministicEvidence.contains("正式阈值尚未")) {
        const QString bounded = "已取得实时读数，但仍需按正式阈值复核";
        cleaned.replace(QRegularExpression(QStringLiteral("关键参数[^\uff0c\u3002\\n]*(?:均在|处于)[^\uff0c\u3002\\n]*范围内")), bounded);
        cleaned.replace(QRegularExpression(QStringLiteral("各项物理参数[^\uff0c\u3002\\n]*数值正常")), bounded);
        cleaned.replace(QRegularExpression(QStringLiteral("参数正常|数值正常|已正式达标")), bounded);
    }
    if (deterministicEvidence.contains("分析状态：尚无已完成分析"))
        cleaned.replace(QRegularExpression(QStringLiteral("分析质量得分为?\\s*0(?:\\s*分)?")), "尚无可用的分析质量得分");
    if (deterministicEvidence.contains("已选择，但不代表已开始运行")) {
        cleaned.replace("已正确加载", "已选择");
        cleaned.replace("运行队列", "当前方法状态");
    }
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) return {};
    const QStringList prohibited{
        "可以确认", "可确认", "确认为", "确认其为", "鉴定为", "确定为",
        "证实为", "最终结论", "无需复核"
    };
    for (const auto &phrase : prohibited) {
        if (!cleaned.contains(phrase)) continue;
        return "模型输出触发安全拦截：候选匹配不能写成物质鉴定结论。\n"
               "证据只能说明当前候选与已提供特征相符；请由具备权限的人员结合正式谱库、"
               "校准数据、原始谱图和复核流程作出结论。";
    }
    const QString notice = "说明：物质鉴定结论须经人工复核确认。";
    if (cleaned.contains("不构成物质鉴定结论")) return cleaned;
    return cleaned + "\n\n" + notice;
}

} // namespace qitest
