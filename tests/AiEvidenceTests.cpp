#include "ai/AiEvidenceBuilder.h"
#include "ai/AiCommandRouter.h"
#include "ai/AiSafetyGuard.h"
#include "ai/AiToolProtocol.h"
#include "ai/LocalKnowledgeStore.h"

#include <QtTest>
#include <limits>

using namespace qitest;

class AiEvidenceTests final : public QObject {
    Q_OBJECT

private slots:
    void missingHardwareValuesAreExplicit() {
        AiContextSnapshot snapshot;
        snapshot.instrumentHealth.vacuumMbar = std::numeric_limits<double>::quiet_NaN();
        snapshot.instrumentTelemetry.molecularPumpRpm = std::numeric_limits<double>::quiet_NaN();
        const auto evidence = AiEvidenceBuilder::buildEvidence(snapshot);
        QVERIFY(evidence.contains("真空度：未提供 mbar"));
        QVERIFY(evidence.contains("分子泵转速：未提供 RPM"));
    }
    void evidenceIsBoundedAndExplicit();
    void defaultQuestionTracksAvailableEvidence();
    void commandRouterAllowsOnlyWhitelistedNavigation();
    void localUnderstandingSeparatesQuestionsNegationAndTargets();
    void safetyGuardBlocksIdentificationClaims();
    void localManualRetrievalIsBoundedAndRelevant();
    void toolProtocolAcceptsOnlyHighConfidenceWhitelistedNavigation();
};

void AiEvidenceTests::evidenceIsBoundedAndExplicit() {
    AiContextSnapshot snapshot;
    snapshot.phaseLabel = "分析完成，等待人工复核";
    snapshot.dataScope = "DEMO_SIMULATION";
    snapshot.instrumentHealth = {true, true, 1.2e-5, 220.0, 1.3, 70.0};
    snapshot.librarySummary = "SWGDRUG 3.14 · 参考库";
    snapshot.workspaceSummary = "检测记录已启用";
    snapshot.aiSummary = "Qwen3.5-4B-Q4_K_M · 本地运行";
    snapshot.result.processedSpectrum.points = {{100.0, 10.0}, {101.0, 12.0}};
    snapshot.result.quality.score = 82;
    snapshot.result.candidates.push_back({"demo-1", "演示候选", "DEMO", 300.1, 2.0, 88.0,
        2, 3, "two demo fragments", true});

    const QString evidence = AiEvidenceBuilder::buildEvidence(snapshot);
    QVERIFY(evidence.contains("运行方式：本地离线智能台"));
    QVERIFY(evidence.contains("数据范围：预览数据"));
    QVERIFY(evidence.contains("候选 1：演示候选"));
    QVERIFY(evidence.contains("【边界】"));
    QVERIFY(!evidence.contains("phase_label"));
    QVERIFY(!evidence.contains("NO_COMPLETED_ANALYSIS"));
    QVERIFY(evidence.contains("正式阈值尚未"));
    QVERIFY(!evidence.contains("password", Qt::CaseInsensitive));
    QVERIFY(evidence.size() < 16000);
}

void AiEvidenceTests::defaultQuestionTracksAvailableEvidence() {
    AiContextSnapshot snapshot;
    snapshot.instrumentHealth.ready = true;
    QVERIFY(AiEvidenceBuilder::defaultQuestionForState(snapshot).contains("开始检测前"));
    snapshot.result.candidates.push_back({});
    QVERIFY(AiEvidenceBuilder::defaultQuestionForState(snapshot).contains("候选物"));
}

void AiEvidenceTests::commandRouterAllowsOnlyWhitelistedNavigation() {
    QCOMPARE(AiCommandRouter::route("打开报告。"), AssistantCommand::OpenReport);
    QCOMPARE(AiCommandRouter::route("我要生成报告"), AssistantCommand::OpenReport);
    QCOMPARE(AiCommandRouter::route("查询谱库"), AssistantCommand::OpenLibrary);
    QCOMPARE(AiCommandRouter::route("帮我打开谱库"), AssistantCommand::OpenLibrary);
    QCOMPARE(AiCommandRouter::route("带我去方法界面"), AssistantCommand::OpenMethod);
    QCOMPARE(AiCommandRouter::route("我想查看最近的检测记录"), AssistantCommand::OpenResults);
    QCOMPARE(AiCommandRouter::route("带我运行方法"), AssistantCommand::PrepareRun);
    QCOMPARE(AiCommandRouter::route("打开采集与分析"), AssistantCommand::PrepareRun);
    QCOMPARE(AiCommandRouter::route("查看筛查结果"), AssistantCommand::OpenReport);
    QCOMPARE(AiCommandRouter::route("打开结果复核"), AssistantCommand::OpenReport);
    QCOMPARE(AiCommandRouter::route("打开定量曲线"), AssistantCommand::OpenQuantitation);
    QCOMPARE(AiCommandRouter::route("查看仪器状态"), AssistantCommand::OpenInstrumentStatus);
    QCOMPARE(AiCommandRouter::route("打开参数预设"), AssistantCommand::OpenInstrumentPresets);
    QCOMPARE(AiCommandRouter::route("编辑仪器预设"), AssistantCommand::OpenInstrumentPresets);
    QCOMPARE(AiCommandRouter::route("打开仪器控制"), AssistantCommand::OpenInstrumentSettings);
    QCOMPARE(AiCommandRouter::route("解释参数预设"), AssistantCommand::None);
    QCOMPARE(AiToolProtocol::idFor(AssistantCommand::OpenInstrumentPresets), QString("open_instrument_presets"));
    QCOMPARE(AiCommandRouter::route("请解释当前仪器状态"), AssistantCommand::None);
    QCOMPARE(AiCommandRouter::route("请解释定量曲线"), AssistantCommand::None);
    QCOMPARE(AiCommandRouter::route("打开降温与关机"), AssistantCommand::OpenPower);
    QCOMPARE(AiCommandRouter::route("帮我找到调谐页面"), AssistantCommand::OpenCalibration);
    QCOMPARE(AiCommandRouter::route("调出质量轴校准"), AssistantCommand::OpenCalibration);
    QCOMPARE(AiCommandRouter::route("去注射泵设置"), AssistantCommand::OpenSampling);
    QCOMPARE(AiCommandRouter::route("看看省载气模式"), AssistantCommand::OpenCarrierGas);
    QCOMPARE(AiCommandRouter::route("打开清洁仪器流程"), AssistantCommand::OpenCleaning);
    QCOMPARE(AiCommandRouter::route("打开会话保护"), AssistantCommand::OpenSessionProtection);
    QCOMPARE(AiCommandRouter::route("请解释如何校准"), AssistantCommand::None);
    QCOMPARE(AiCommandRouter::route("帮我开启模拟离子源"), AssistantCommand::EnableIonSource);
    QCOMPARE(AiCommandRouter::route("打开分子源"), AssistantCommand::EnableIonSource);
    QCOMPARE(AiCommandRouter::route("关闭离子源"), AssistantCommand::DisableIonSource);
    QCOMPARE(AiCommandRouter::route("帮我把电压调高一点"), AssistantCommand::ReviewInstrumentAdjustment);
    QCOMPARE(AiCommandRouter::route("开始检测"), AssistantCommand::None);
    QCOMPARE(AiCommandRouter::route("删除所有记录"), AssistantCommand::None);
    QCOMPARE(AiCommandRouter::route("把候选改成阳性"), AssistantCommand::None);
}

void AiEvidenceTests::localUnderstandingSeparatesQuestionsNegationAndTargets() {
    const QStringList protectedPhrases{"不要打开离子源", "怎么打开离子源", "可以关闭离子源吗", "如果打开离子源",
        "刚才打开了离子源", "打开报告然后关闭离子源", "打开报告或者谱库", "把它关闭", "别关闭离子源"};
    for (const auto &phrase : protectedPhrases)
        QVERIFY2(AiCommandRouter::route(phrase) == AssistantCommand::None, qPrintable(phrase));
    QCOMPARE(AiCommandRouter::understand("怎么打开离子源").explanationTopic, QString("离子源"));
    QCOMPARE(AiCommandRouter::understand("这里怎么用", "生成报告").explanationTopic, QString("生成报告"));
    QVERIFY(!AiCommandRouter::understand("打开报告然后关闭离子源").feedback.isEmpty());
    QVERIFY(!AiCommandRouter::understand("关闭它", "离子源").feedback.isEmpty());
    QCOMPARE(AiCommandRouter::route("我想看历史数据"), AssistantCommand::OpenResults);
    QCOMPARE(AiCommandRouter::route("标曲在哪"), AssistantCommand::OpenQuantitation);
    QCOMPARE(AiCommandRouter::route("打开标准谱图"), AssistantCommand::OpenLibrary);
    QCOMPARE(AiCommandRouter::route("打开EIC积分"), AssistantCommand::OpenTraceAnalysis);
    QCOMPARE(AiCommandRouter::route("导入数据"), AssistantCommand::None); // explanation, not file selection by guess
    QVERIFY(!AiCommandRouter::understand(QString(3000, 'x')).feedback.isEmpty());
    LocalKnowledgeStore store;
    QVERIFY(store.loadMarkdown(QStringLiteral(QITEST_SOURCE_DIR) + "/resources/knowledge/operator_manual_zh.md"));
    QVERIFY(store.search("报告", -1).isEmpty());
    QVERIFY(store.contextFor("报告", 2, -100).isEmpty());
}

void AiEvidenceTests::safetyGuardBlocksIdentificationClaims() {
    const QString guarded = AiSafetyGuard::enforce("根据匹配结果，可以确认其为目标物。");
    QVERIFY(guarded.contains("安全拦截"));
    QVERIFY(guarded.contains("物质鉴定结论"));
    const QString safe = AiSafetyGuard::enforce("候选与三个碎片相符，仍需人工复核。");
    QVERIFY(safe.contains("须经人工复核确认"));
    const QString internal = AiSafetyGuard::enforce(
        "phase_label=设备就绪，result_status=NO_COMPLETED_ANALYSIS，peak_count=0。");
    QVERIFY(internal.contains("当前阶段"));
    QVERIFY(internal.contains("尚无已完成分析"));
    QVERIFY(internal.contains("谱峰数量"));
    QVERIFY(!internal.contains("phase_label"));
    QCOMPARE(AiSafetyGuard::enforce("“方法 v3\"").section("\n", 0, 0), QString("“方法 v3”"));
    const QString thresholdEvidence = "正式阈值尚未从真实仪器协议接入";
    const QString thresholdGuarded = AiSafetyGuard::enforce(
        "关键参数均在设定范围内，各项物理参数数值正常。", thresholdEvidence);
    QVERIFY(!thresholdGuarded.contains("均在设定范围内"));
    QVERIFY(!thresholdGuarded.contains("数值正常"));
    QVERIFY(thresholdGuarded.contains("仍需按正式阈值复核"));
}

void AiEvidenceTests::localManualRetrievalIsBoundedAndRelevant() {
    LocalKnowledgeStore store;
    QString error;
    QVERIFY2(store.loadMarkdown(QStringLiteral(QITEST_SOURCE_DIR)
        + "/resources/knowledge/operator_manual_zh.md", &error), qPrintable(error));
    QVERIFY(store.chunkCount() >= 8);
    const auto initialChunks = store.chunkCount();
    QVERIFY(store.loadMarkdown(QStringLiteral(QITEST_SOURCE_DIR)
        + "/resources/knowledge/operator_manual_zh.md", &error));
    QCOMPARE(store.chunkCount(), initialChunks);
    const QString reportContext = store.contextFor("我怎么生成报告？", 3, 2400);
    QVERIFY(reportContext.contains("生成报告"));
    QVERIFY(reportContext.contains("人工") || reportContext.contains("复核"));
    QVERIFY(reportContext.size() <= 2400);
    const QString shutdownContext = store.contextFor("仪器如何降温关机？", 2, 1800);
    QVERIFY(shutdownContext.contains("降温与关机"));
    QVERIFY(shutdownContext.contains("互锁") || shutdownContext.contains("确认"));
}

void AiEvidenceTests::toolProtocolAcceptsOnlyHighConfidenceWhitelistedNavigation() {
    const auto report = AiToolProtocol::parseEnvelope(
        R"(<tool_call>{"tool":"open_report","confidence":0.93,"arguments":{}}</tool_call>)");
    QVERIFY(report.has_value());
    QCOMPARE(report->command, AssistantCommand::OpenReport);
    QCOMPARE(AiToolProtocol::idFor(report->command), QString("open_report"));
    QVERIFY(AiToolProtocol::removeEnvelope(
        "已为你准备。<tool_call>{\"tool\":\"open_report\",\"confidence\":0.93,\"arguments\":{}}</tool_call>")
        == "已为你准备。");
    QVERIFY(!AiToolProtocol::parseEnvelope(
        R"(<tool_call>{"tool":"start_acquisition","confidence":0.99,"arguments":{}}</tool_call>)"));
    QVERIFY(!AiToolProtocol::parseEnvelope(
        R"(<tool_call>{"tool":"open_report","confidence":0.61,"arguments":{}}</tool_call>)"));
    QVERIFY(!AiToolProtocol::parseEnvelope(
        R"(<tool_call>{"tool":"open_report","confidence":0.99,"arguments":"bad"}</tool_call>)"));
    const auto nativeCalibration = AiToolProtocol::parseNativeToolCall(QJsonObject{
        {"function", QJsonObject{{"name", "open_calibration"}, {"arguments", "{}"}}}});
    QVERIFY(nativeCalibration.has_value());
    QCOMPARE(nativeCalibration->command, AssistantCommand::OpenCalibration);
    QVERIFY(AiToolProtocol::nativeTools().size() >= 18);
    const auto simulatedControl = AiToolProtocol::parseEnvelope(
        R"(<tool_call>{"tool":"enable_ion_source","confidence":0.96,"arguments":{}}</tool_call>)");
    QVERIFY(simulatedControl.has_value());
    QVERIFY(AiToolProtocol::mayApplyStateChange(*simulatedControl, true));
    QVERIFY(!AiToolProtocol::mayApplyStateChange(*simulatedControl, false));
}

QTEST_MAIN(AiEvidenceTests)
#include "AiEvidenceTests.moc"
