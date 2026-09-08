#include "ai/AiCommandRouter.h"

#include <initializer_list>
#include <QVector>

namespace qitest {
namespace {

QString normalized(QString text) {
    text = text.simplified().toLower();
    text.remove(QStringLiteral("。"));
    text.remove(QStringLiteral("，"));
    text.remove(QStringLiteral("！"));
    text.remove(QStringLiteral("？"));
    text.remove('.');
    text.remove(',');
    text.remove('!');
    text.remove('?');
    text.remove(' ');
    text.replace("分子源", "离子源");
    text.replace("质谱库", "谱库");
    text.replace("图谱库", "谱库");
    text.replace("毒品库", "谱库");
    text.replace("报表", "报告");
    text.replace("校正", "校准");
    text.replace("样品泵", "注射泵");
    text.replace("历史数据", "检测记录");
    text.replace("历史记录", "检测记录");
    text.replace("以前的结果", "检测记录");
    text.replace("标准谱图", "参考谱库");
    text.replace("标准曲线", "定量曲线");
    text.replace("标曲", "定量曲线");
    text.replace("检量线", "定量曲线");
    text.replace("仪器监控", "仪器状态");
    text.replace("设备状态", "仪器状态");
    text.replace("预置参数", "参数预设");
    return text;
}

bool matches(const QString &text, std::initializer_list<const char *> phrases) {
    for (const auto *phrase : phrases)
        if (text == QString::fromUtf8(phrase)) return true;
    return false;
}

bool containsAny(const QString &text, std::initializer_list<const char *> phrases) {
    for (const auto *phrase : phrases)
        if (text.contains(QString::fromUtf8(phrase))) return true;
    return false;
}

bool hasNavigationIntent(const QString &text) {
    return containsAny(text, {"打开", "进入", "返回", "回到", "带我", "查看", "查询", "编辑", "生成", "去",
        "切换", "跳转", "显示", "调出", "找到", "看看", "想看", "想查", "找一下", "在哪", "哪里"});
}

bool isQuestionOnly(const QString &text) {
    return containsAny(text, {"怎么", "如何", "为什么", "是什么", "说明", "解释", "介绍",
        "能不能", "可以吗", "吗", "有啥用", "有什么用", "什么意思", "步骤", "用法", "教程"});
}

} // namespace

AiCommandRouter::Understanding AiCommandRouter::understand(const QString &text, const QString &contextTopic) {
    Understanding result;
    if (text.size() > 2048) {
        result.feedback = "指令较长，请分开描述要做的事情，每次只处理一个操作。";
        return result;
    }
    const QString command = normalized(text);
    if (command.isEmpty()) return result;
    if (containsAny(command, {"删除所有", "绕过", "确认阳性", "改成阳性", "忽略互锁"})) {
        result.feedback = "不能通过智能台绕过安全校验、修改鉴定结论或批量删除数据。请使用相应页面的受控流程。";
        return result;
    }
    if (containsAny(command, {"如果", "假如", "假设", "刚才", "昨天", "已经", "曾经", "他说", "她说", "报错"})) {
        result.feedback = "这段描述没有作为新操作执行。可以问具体功能的用法，或明确说“打开”加页面名称。复杂问题可由自动模式进一步分析。";
        return result;
    }
    if (containsAny(command, {"不要", "别", "不能", "不许", "先不", "暂不", "不打开", "不关闭",
            "不开启", "不执行", "取消", "不是", "没有让", "不想", "不用", "不需要"})
            && !isQuestionOnly(command)) {
        result.feedback = "没有执行操作。若要停止正在进行的检测，请在运行页面使用停止按钮。";
        return result;
    }
    struct Topic { const char *title; std::initializer_list<const char *> aliases; AssistantCommand destination; };
    // Each task has one owner. Aliases describe vocabulary, not permission to execute.
    const Topic topics[] = {
        {"采集与分析", {"采集与分析", "运行方法", "开始检测", "准备检测", "准备运行", "启动检测", "采集"}, AssistantCommand::PrepareRun},
        {"检测记录与数据", {"检测记录", "历史结果", "最近结果", "导入数据", "导出数据", "导入检测", "导出检测", "归档"}, AssistantCommand::OpenResults},
        {"生成报告", {"报告", "筛查结果", "结果复核", "pdf", "word"}, AssistantCommand::OpenReport},
        {"参考谱库", {"谱库", "cas", "分子式"}, AssistantCommand::OpenLibrary},
        {"方法版本", {"编辑方法", "方法版本", "修改方法", "当前方法", "方法名称"}, AssistantCommand::OpenMethod},
        {"定量曲线", {"定量曲线", "校准曲线", "校准点", "浓度响应", "拟合", "校准公式"}, AssistantCommand::OpenQuantitation},
        {"TIC 与质谱图", {"tic", "eic", "bpc", "积分", "提取离子", "总离子曲线"}, AssistantCommand::OpenTraceAnalysis},
        {"离子源", {"离子源"}, AssistantCommand::OpenInstrumentSettings},
        {"仪器状态与仪器设置", {"仪器状态", "仪器栏", "实时监控", "仪器控制"}, AssistantCommand::OpenInstrumentStatus},
        {"参数预设", {"参数预设", "仪器预设"}, AssistantCommand::OpenInstrumentPresets},
        {"调谐与校准", {"调谐", "质量轴", "质量校准"}, AssistantCommand::OpenCalibration},
        {"进样与注射泵", {"进样", "注射泵"}, AssistantCommand::OpenSampling},
        {"载气节省", {"载气节省", "省载气", "载气模式"}, AssistantCommand::OpenCarrierGas},
        {"清洗模式", {"清洗", "清洁仪器"}, AssistantCommand::OpenCleaning},
        {"降温与关机", {"降温", "关机", "开关机"}, AssistantCommand::OpenPower},
        {"会话保护", {"锁屏", "会话保护", "防止误操作"}, AssistantCommand::OpenSessionProtection},
        {"深度问答", {"深度问答", "深度回答", "千问", "大模型", "量化"}, AssistantCommand::None},
        {"质量复核", {"质量门控", "质量检查", "质量复核", "复核质量"}, AssistantCommand::None},
        {"载气压力", {"载气压力", "气压"}, AssistantCommand::None}
    };
    QVector<int> hits;
    for (int i = 0; i < int(sizeof(topics) / sizeof(topics[0])); ++i)
        if (containsAny(command, topics[i].aliases)) hits.append(i);
    if (isQuestionOnly(command)) {
        if (hits.size() == 1) result.explanationTopic = QString::fromUtf8(topics[hits.first()].title);
        else if (hits.isEmpty() && containsAny(command, {"这个", "这里", "当前页面"}) && !contextTopic.isEmpty())
            result.explanationTopic = contextTopic;
        // Multi-topic questions remain questions and go to bounded manual retrieval.
        return result;
    }
    if (hits.size() > 1 || containsAny(command, {"然后", "接着", "再打开", "还是", "或者"})
            || (containsAny(command, {"开启", "打开", "启动"}) && containsAny(command, {"关闭", "关掉", "停止"}))) {
        result.feedback = "这句话涉及多个目标或操作，请一次选择一个。";
        QStringList choices;
        for (int i : hits) choices.append(QString::fromUtf8(topics[i].title));
        if (!choices.isEmpty()) result.feedback += "可分别说：" + choices.join("、") + "。";
        return result;
    }
    if (hits.isEmpty() && containsAny(command, {"打开它", "关闭它", "关掉它", "开启这个", "把它打开", "把它关"})) {
        result.feedback = "请明确要操作的对象，例如离子源、仪器监控或报告；不会根据“它”猜测仪器开关。";
        return result;
    }
    // Preserve the existing explicitly gated command lane.
    result.command = matchCommand(text);
    if (result.command == AssistantCommand::None && hits.size() == 1) {
        const auto &topic = topics[hits.first()];
        if (hasNavigationIntent(command) || command == QString::fromUtf8(topic.title))
            result.command = topic.destination;
        if (result.command == AssistantCommand::None) result.explanationTopic = QString::fromUtf8(topic.title);
    }
    return result;
}

AssistantCommand AiCommandRouter::route(const QString &text) {
    return understand(text).command;
}

AssistantCommand AiCommandRouter::matchCommand(const QString &text) {
    const QString command = normalized(text);
    if (text.size() > 2048 || command.isEmpty() || isQuestionOnly(command)
            || containsAny(command, {"不要", "别", "不许", "不能", "先不", "暂不", "不打开", "不关闭", "不开启",
                "不执行", "取消", "不是", "没有让", "不想", "不用", "不需要", "删除所有", "绕过", "确认阳性", "改成阳性"}))
        return AssistantCommand::None;
    if (containsAny(command, {"开启离子源", "打开离子源", "启动离子源", "开启模拟离子源",
            "打开分子源", "开启分子源", "启动分子源"}))
        return AssistantCommand::EnableIonSource;
    if (containsAny(command, {"关闭离子源", "停止离子源", "关掉离子源",
            "关闭分子源", "停止分子源", "关掉分子源"}))
        return AssistantCommand::DisableIonSource;
    if (containsAny(command, {"调高一点", "调低一点", "电压调高", "电压调低", "温度调高", "温度调低"}))
        return AssistantCommand::ReviewInstrumentAdjustment;
    if (containsAny(command, {"主页", "主界面"}) && hasNavigationIntent(command))
        return AssistantCommand::OpenHome;
    if ((matches(command, {"采集与分析", "运行方法", "准备检测", "准备运行"})
            || (containsAny(command, {"采集与分析", "运行方法", "准备检测", "准备运行"})
                && hasNavigationIntent(command)))
        && !containsAny(command, {"开始检测", "立即检测", "直接检测"}))
        return AssistantCommand::PrepareRun;
    if (containsAny(command, {"检测记录", "历史结果", "最近结果"}) && hasNavigationIntent(command))
        return AssistantCommand::OpenResults;
    if (containsAny(command, {"生成报告", "打开报告", "查看报告", "进入报告"}))
        return AssistantCommand::OpenReport;
    if (containsAny(command, {"筛查结果", "结果复核"}) && hasNavigationIntent(command))
        return AssistantCommand::OpenReport;
    if (command.contains("谱库") && hasNavigationIntent(command)) return AssistantCommand::OpenLibrary;
    if (command.contains("方法") && hasNavigationIntent(command)) return AssistantCommand::OpenMethod;
    if ((matches(command, {"定量曲线", "校准曲线"})
            || containsAny(command, {"定量曲线", "校准曲线"}))
        && hasNavigationIntent(command)) return AssistantCommand::OpenQuantitation;
    if (containsAny(command, {"仪器状态", "仪器栏", "实时监控"})
        && hasNavigationIntent(command))
        return AssistantCommand::OpenInstrumentStatus;
    if (containsAny(command, {"参数预设", "仪器预设"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenInstrumentPresets;
    if (containsAny(command, {"仪器设置", "仪器控制"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenInstrumentSettings;
    if (containsAny(command, {"调谐", "校准", "质量轴", "标定"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenCalibration;
    if (containsAny(command, {"进样", "注射泵", "进样器"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenSampling;
    if (containsAny(command, {"载气节省", "省载气", "载气模式"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenCarrierGas;
    if (containsAny(command, {"清洗模式", "抽清洗液", "清洁仪器", "清洗仪器"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenCleaning;
    if (containsAny(command, {"电源界面", "降温关机", "降温与关机", "开关机"}))
        return AssistantCommand::OpenPower;
    if (containsAny(command, {"锁屏", "会话保护", "防止误操作"}) && !isQuestionOnly(command))
        return AssistantCommand::OpenSessionProtection;
    if (command.contains("设置") && hasNavigationIntent(command)) return AssistantCommand::OpenSettings;
    if (command.contains("帮助") && hasNavigationIntent(command)) return AssistantCommand::OpenHelp;
    return AssistantCommand::None;
}

QString AiCommandRouter::displayName(AssistantCommand command) {
    switch (command) {
    case AssistantCommand::OpenHome: return "采集与分析";
    case AssistantCommand::PrepareRun: return "采集与分析";
    case AssistantCommand::OpenResults: return "报告查看";
    case AssistantCommand::OpenReport: return "报告";
    case AssistantCommand::OpenLibrary: return "参考谱库";
    case AssistantCommand::OpenMethod: return "编辑方法";
    case AssistantCommand::OpenQuantitation: return "定量曲线";
    case AssistantCommand::OpenTraceAnalysis: return "提取离子与积分";
    case AssistantCommand::OpenInstrumentStatus: return "仪器状态";
    case AssistantCommand::OpenInstrumentSettings: return "仪器控制";
    case AssistantCommand::OpenInstrumentPresets: return "参数预设";
    case AssistantCommand::OpenCalibration: return "调谐与校准";
    case AssistantCommand::OpenSampling: return "进样与注射泵";
    case AssistantCommand::OpenCarrierGas: return "载气设置";
    case AssistantCommand::OpenCleaning: return "清洗模式";
    case AssistantCommand::EnableIonSource: return "离子源";
    case AssistantCommand::DisableIonSource: return "离子源";
    case AssistantCommand::ReviewInstrumentAdjustment: return "仪器参数核对";
    case AssistantCommand::OpenPower: return "电源与降温";
    case AssistantCommand::OpenSessionProtection: return "会话保护";
    case AssistantCommand::OpenSettings: return "设置";
    case AssistantCommand::OpenHelp: return "帮助";
    case AssistantCommand::None: return {};
    }
    return {};
}

} // namespace qitest
