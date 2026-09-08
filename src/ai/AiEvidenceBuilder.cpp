#include "domain/DisplayLabels.h"
#include "ai/AiEvidenceBuilder.h"

#include <QStringList>

namespace qitest {
namespace {

QString boolText(bool value) {
    return value ? "是" : "否";
}

QString qualityLevelText(QualityLevel level) {
    switch (level) {
    case QualityLevel::Pass: return "通过";
    case QualityLevel::Review: return "需复核";
    case QualityLevel::Fail: return "未通过";
    }
    return "未通过";
}

QString phaseFallback(const QString &phaseLabel) {
    return phaseLabel.trimmed().isEmpty() ? "状态未同步" : phaseLabel.trimmed();
}

QString dataScopeText(const QString &scope) {
    if (scope == "PUBLIC_EXAMPLE") return "OpenMS BSA 公开示例，不是客户检测结果，不用于浓度验证";
    if (scope == "DEMO_SIMULATION") return "本机检测数据";
    if (scope == "FORMAL_ACQUISITION") return "正式采集数据";
    return scope.trimmed().isEmpty() ? "未标注" : scope.trimmed();
}

QString reviewStatusText(const QString &status) {
    if (status == "REVIEWED") return "已复核";
    if (status == "PENDING_REVIEW") return "待复核";
    return status.trimmed().isEmpty() ? "未开始" : status.trimmed();
}

} // namespace

QString AiEvidenceBuilder::buildEvidence(const AiContextSnapshot &snapshot) {
    QStringList lines;
    lines << "【上下文】";
    lines << "运行方式：本地离线智能台";
    lines << "当前阶段：" + phaseFallback(snapshot.phaseLabel);
    lines << "数据范围：" + dataScopeText(snapshot.dataScope);
    lines << "参考谱库：" + snapshot.librarySummary;
    lines << "工作区：" + snapshot.workspaceSummary;
    lines << "本地模型：" + snapshot.aiSummary;
    lines << "" << "【仪器状态】";
    lines << "通讯连接：" + boolText(snapshot.instrumentHealth.connected);
    lines << "允许采集：" + boolText(snapshot.instrumentHealth.ready);
    lines << QString("真空度：%1 mbar").arg(measurementText(snapshot.instrumentHealth.vacuumMbar, 'E', 3));
    lines << QString("TD 温度：%1 ℃").arg(measurementText(snapshot.instrumentHealth.tdTemperatureC, 'f', 1));
    lines << QString("载气流速：%1 mL/min").arg(measurementText(snapshot.instrumentHealth.carrierGasMlMin, 'f', 2));
    lines << QString("离子源电压：%1 kV").arg(measurementText(snapshot.instrumentHealth.ionSourceKv, 'f', 2));
    lines << QString("分子泵转速：%1 RPM").arg(measurementText(snapshot.instrumentTelemetry.molecularPumpRpm, 'f', 0));
    lines << QString("分子泵电流：%1 A").arg(measurementText(snapshot.instrumentTelemetry.molecularPumpCurrentA, 'f', 2));
    lines << QString("泵体温度：%1 ℃").arg(measurementText(snapshot.instrumentTelemetry.molecularPumpTemperatureC, 'f', 1));
    lines << "载气模式：" + snapshot.instrumentTelemetry.carrierGasMode;
    lines << QString("载气压力：%1 Torr").arg(measurementText(snapshot.instrumentTelemetry.carrierGasPressureTorr, 'f', 1));
    lines << QString("离子阱温度：%1 ℃").arg(measurementText(snapshot.instrumentTelemetry.ionTrapTemperatureC, 'f', 1));
    lines << QString("倍增器电压：%1 V").arg(measurementText(snapshot.instrumentTelemetry.multiplierVoltageV, 'f', 1));
    lines << QString("抽气流速：%1%%").arg(measurementText(snapshot.instrumentTelemetry.extractionFlowPercent, 'f', 1));
    lines << QString("进样器余量：%1%%").arg(measurementText(snapshot.instrumentTelemetry.syringeRemainingPercent, 'f', 1));
    lines << QString("载气压力复核：%1 Torr；正式阈值尚未从真实仪器协议接入，不得判断为正常或在设定范围内。")
        .arg(measurementText(snapshot.instrumentTelemetry.carrierGasPressureTorr, 'f', 1));

    if (!snapshot.activeMethod.id.isEmpty()) {
        lines << QString("当前方法：%1 v%2；已选择，但不代表已开始运行。")
            .arg(snapshot.activeMethod.name).arg(snapshot.activeMethod.version);
    } else {
        lines << "当前方法：未选择";
    }

    lines << "" << "【检测记录】";
    if (!snapshot.currentRun.id.isEmpty()) {
        lines << "最近记录：" + snapshot.currentRun.id;
        lines << "记录数据范围：" + dataScopeText(snapshot.currentRun.dataScope);
        lines << QString("质量得分：%1/100").arg(snapshot.currentRun.qualityScore);
        lines << QString("候选数量：%1").arg(snapshot.currentRun.candidateCount);
        lines << "复核状态：" + reviewStatusText(snapshot.currentRun.reviewStatus);
    } else {
        lines << "最近记录：尚无已完成检测";
    }

    if (!snapshot.result.processedSpectrum.points.isEmpty()) {
        lines << QString("处理后数据点：%1").arg(snapshot.result.processedSpectrum.points.size());
        lines << QString("基线：%1").arg(snapshot.result.processedSpectrum.baseline, 0, 'f', 4);
        lines << QString("噪声中位绝对偏差：%1").arg(snapshot.result.processedSpectrum.noiseMad, 0, 'f', 4);
        lines << QString("总离子流：%1").arg(snapshot.result.processedSpectrum.totalIonCurrent, 0, 'f', 2);
        lines << "分析质量：" + qualityLevelText(snapshot.result.quality.level);
        lines << QString("分析质量得分：%1/100").arg(snapshot.result.quality.score);
        lines << QString("谱峰数量：%1").arg(snapshot.result.peaks.size());
        lines << QString("候选数量：%1").arg(snapshot.result.candidates.size());
    } else {
        lines << "分析状态：尚无已完成分析";
    }

    for (qsizetype i = 0; i < snapshot.result.candidates.size() && i < 5; ++i) {
        const auto &candidate = snapshot.result.candidates[i];
        lines << QString("候选 %1：%2；匹配分 %3；实测 m/z %4；质量误差 %5 ppm；证据 %6")
            .arg(i + 1)
            .arg(candidate.name)
            .arg(candidate.score, 0, 'f', 1)
            .arg(candidate.measuredMz, 0, 'f', 4)
            .arg(candidate.massErrorPpm, 0, 'f', 1)
            .arg(candidate.evidence);
    }

    for (qsizetype i = 0; i < snapshot.result.quality.checks.size() && i < 8; ++i) {
        const auto &check = snapshot.result.quality.checks[i];
        lines << QString("质量检查 %1：%2；通过 %3；%4")
            .arg(i + 1)
            .arg(check.title)
            .arg(boolText(check.passed))
            .arg(check.detail);
    }

    lines << "" << "【边界】只能解释上述确定性证据，不得创造科学数值或代替人工作出物质鉴定结论。";
    return lines.join('\n');
}

QString AiEvidenceBuilder::buildSummary(const AiContextSnapshot &snapshot) {
    QStringList lines;
    const QString method = snapshot.activeMethod.id.isEmpty()
        ? "未选择方法"
        : QString("%1 v%2").arg(snapshot.activeMethod.name).arg(snapshot.activeMethod.version);
    lines << phaseFallback(snapshot.phaseLabel) + " · " + method;
    lines << QString("仪器%1 · %2 · %3")
        .arg(snapshot.instrumentHealth.ready ? "就绪" : "未就绪",
             snapshot.instrumentHealth.connected ? "通讯正常" : "通讯中断",
             snapshot.librarySummary);
    if (!snapshot.currentRun.id.isEmpty()) {
        lines << QString("最近记录 %1 · 质量 %2/100 · %3")
            .arg(snapshot.currentRun.id).arg(snapshot.currentRun.qualityScore)
            .arg(snapshot.currentRun.reviewStatus == "REVIEWED" ? "已复核" : "待复核");
    } else {
        lines << "尚无已完成检测";
    }
    if (!snapshot.result.candidates.isEmpty()) {
        const auto &candidate = snapshot.result.candidates.first();
        lines << QString("首个候选 %1 · %2 分").arg(candidate.name).arg(candidate.score, 0, 'f', 1);
    }
    return lines.join('\n');
}

QString AiEvidenceBuilder::defaultQuestionForState(const AiContextSnapshot &snapshot) {
    if (!snapshot.result.candidates.isEmpty())
        return "请根据当前检测证据说明候选物、风险点、需要人工复核的原因和下一步。";
    if (snapshot.instrumentHealth.ready)
        return "请解释当前设备就绪状态、仍需注意的风险以及开始检测前的检查项。";
    return "请解释当前设备未就绪的原因、证据和恢复建议。";
}

} // namespace qitest
