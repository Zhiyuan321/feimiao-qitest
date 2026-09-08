#include "core/AnalysisEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace qitest {
namespace {

double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = std::clamp(
        static_cast<qsizetype>(std::llround((values.size() - 1) * fraction)),
        qsizetype{0}, qsizetype(values.size()) - 1);
    return values[index];
}

double median(QVector<double> values) {
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 == 0
        ? (values[middle - 1] + values[middle]) / 2.0
        : values[middle];
}

double measuredIntensityAt(const ProcessedSpectrum &spectrum, double targetMz) {
    const auto point = std::min_element(spectrum.points.begin(), spectrum.points.end(),
        [targetMz](const auto &a, const auto &b) {
            return std::abs(a.mz - targetMz) < std::abs(b.mz - targetMz);
        });
    return point != spectrum.points.end() && std::abs(point->mz - targetMz) <= 0.65
        ? point->intensity : 0.0;
}

QVector<ScreeningItem> buildScreeningItems(
    const QVector<SubstanceReference> &references,
    const QVector<MatchCandidate> &candidates,
    const ProcessedSpectrum &spectrum,
    QualityLevel qualityLevel) {
    QVector<ScreeningItem> items;
    items.reserve(references.size());
    for (const auto &reference : references) {
        const auto candidate = std::find_if(candidates.begin(), candidates.end(), [&](const auto &value) {
            return value.referenceId == reference.id;
        });
        QVector<double> measured;
        measured.reserve(reference.fragmentMz.size());
        for (double mz : reference.fragmentMz) measured.push_back(measuredIntensityAt(spectrum, mz));
        const bool matched = candidate != candidates.end();
        items.push_back({reference.id, reference.displayName, reference.precursorMz,
            reference.fragmentMz, measured, matched ? candidate->score : 0.0,
            matched ? QString("可疑")
                    : (qualityLevel == QualityLevel::Fail ? QString("未判定") : QString("未检出")),
            reference.demo});
    }
    return items;
}

} // namespace

ProcessedSpectrum SpectrumProcessor::process(const QVector<SpectrumPoint> &raw) const {
    if (raw.size() < 20) throw SpectrumError("insufficient spectrum points");
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (!std::isfinite(raw[i].mz) || !std::isfinite(raw[i].intensity) || raw[i].intensity < 0.0)
            throw SpectrumError("invalid spectrum point");
        if (i > 0 && raw[i - 1].mz >= raw[i].mz)
            throw SpectrumError("mass axis must be strictly increasing");
    }

    QVector<double> intensities;
    intensities.reserve(raw.size());
    for (const auto &point : raw) intensities.push_back(point.intensity);

    const double baseline = percentile(intensities, 0.12);
    QVector<double> corrected;
    corrected.reserve(raw.size());
    for (const double value : intensities) corrected.push_back(std::max(0.0, value - baseline));

    const double noiseLimit = percentile(corrected, 0.65);
    QVector<double> noiseSample;
    for (const double value : corrected) if (value <= noiseLimit) noiseSample.push_back(value);
    const double noiseMedian = median(noiseSample);
    QVector<double> deviations;
    for (const double value : noiseSample) deviations.push_back(std::abs(value - noiseMedian));
    const double noiseMad = std::max(0.000001, median(deviations) * 1.4826);
    const double maximum = std::max(0.000001, *std::max_element(corrected.begin(), corrected.end()));

    ProcessedSpectrum processed;
    processed.baseline = baseline;
    processed.noiseMad = noiseMad;
    processed.totalIonCurrent = std::accumulate(corrected.begin(), corrected.end(), 0.0);
    processed.points.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i)
        processed.points.push_back({raw[i].mz, corrected[i] / maximum * 100.0});
    return processed;
}

QVector<DetectedPeak> PeakDetector::detect(const ProcessedSpectrum &spectrum) const {
    QVector<DetectedPeak> peaks;
    const auto &points = spectrum.points;
    if (points.size() < 3) return peaks;
    const double normalizedNoise = std::max(
        0.02, spectrum.noiseMad / std::max(spectrum.totalIonCurrent, 0.000001) * 10000.0);

    for (qsizetype i = 1; i < points.size() - 1; ++i) {
        const auto &point = points[i];
        if (point.intensity < minimumRelativeIntensity_
            || point.intensity <= points[i - 1].intensity
            || point.intensity < points[i + 1].intensity) continue;
        const double snr = point.intensity / normalizedNoise;
        if (snr < minimumSignalToNoise_) continue;
        const DetectedPeak candidate{point.mz, point.intensity, snr};
        if (!peaks.isEmpty() && candidate.mz - peaks.last().mz < minimumSeparationMz_) {
            if (candidate.relativeIntensity > peaks.last().relativeIntensity) peaks.last() = candidate;
        } else {
            peaks.push_back(candidate);
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto &a, const auto &b) {
        return a.relativeIntensity > b.relativeIntensity;
    });
    return peaks;
}

QVector<MatchCandidate> LibraryMatcher::match(
    const QVector<DetectedPeak> &peaks,
    const QVector<SubstanceReference> &references) const {
    QVector<MatchCandidate> matches;
    for (const auto &reference : references) {
        if (peaks.isEmpty()) continue;
        const auto precursor = std::min_element(peaks.begin(), peaks.end(), [&](const auto &a, const auto &b) {
            return std::abs(a.mz - reference.precursorMz) < std::abs(b.mz - reference.precursorMz);
        });
        const double ppm = (precursor->mz - reference.precursorMz) / reference.precursorMz * 1000000.0;
        if (std::abs(ppm) > precursorTolerancePpm_) continue;
        int fragmentMatches = 0;
        for (const double target : reference.fragmentMz) {
            if (std::any_of(peaks.begin(), peaks.end(), [&](const auto &peak) {
                return std::abs(peak.mz - target) <= fragmentToleranceDa_;
            })) ++fragmentMatches;
        }
        const int requiredDiagnosticFragments = std::min(2, static_cast<int>(reference.fragmentMz.size()));
        if (fragmentMatches < requiredDiagnosticFragments) continue;
        const double massScore = std::max(0.0, 1.0 - std::abs(ppm) / precursorTolerancePpm_);
        const double fragmentScore = reference.fragmentMz.isEmpty()
            ? 1.0 : static_cast<double>(fragmentMatches) / reference.fragmentMz.size();
        const double intensityScore = std::min(1.0, precursor->relativeIntensity / 35.0);
        const double score = (massScore * 0.45 + fragmentScore * 0.35 + intensityScore * 0.20) * 100.0;
        if (score < 55.0) continue;
        matches.push_back({
            reference.id, reference.displayName, reference.category, precursor->mz, ppm, score,
            fragmentMatches, static_cast<int>(reference.fragmentMz.size()),
            QString("母离子与 %1/%2 个诊断碎片匹配")
                .arg(fragmentMatches).arg(reference.fragmentMz.size()),
            reference.demo
        });
    }
    std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
    return matches;
}

QualityAssessment QualityGate::assess(
    qsizetype rawPointCount,
    const ProcessedSpectrum &spectrum,
    const QVector<DetectedPeak> &peaks,
    const InstrumentHealth &instrument) const {
    QVector<QualityCheck> checks{
        {"instrument-ready", "设备状态", instrument.ready ? "真空和温度满足检测条件" : "设备尚未达到检测条件", instrument.ready},
        {"point-count", "谱图完整性", QString("采集 %1 个数据点").arg(rawPointCount), rawPointCount >= 500},
        {"signal", "有效信号", QString("总离子流 %1").arg(std::llround(spectrum.totalIonCurrent)), spectrum.totalIonCurrent > 1000.0},
        {"peak-count", "峰识别", QString("识别 %1 个候选峰").arg(peaks.size()), peaks.size() >= 3}
    };
    const int passed = std::count_if(checks.begin(), checks.end(), [](const auto &check) { return check.passed; });
    const QualityLevel level = passed == checks.size() ? QualityLevel::Pass
        : (passed >= 2 ? QualityLevel::Review : QualityLevel::Fail);
    return {level, static_cast<int>(static_cast<double>(passed) / checks.size() * 100.0), checks};
}

AnalysisEngine::AnalysisEngine(QVector<SubstanceReference> references, QString libraryVersion)
    : references_(std::move(references)), libraryVersion_(std::move(libraryVersion)) {}

AnalysisResult AnalysisEngine::analyze(
    const QVector<SpectrumPoint> &raw,
    const InstrumentHealth &instrument) const {
    const auto processed = processor_.process(raw);
    const auto peaks = detector_.detect(processed);
    const auto candidates = matcher_.match(peaks, references_);
    const auto quality = qualityGate_.assess(raw.size(), processed, peaks, instrument);
    const auto screeningItems = buildScreeningItems(references_, candidates, processed, quality.level);
    return {processed, peaks, candidates, screeningItems, quality, Version, libraryVersion_};
}

QVector<SubstanceReference> demoReferences() {
    return {
        {"demo-a", "质控标准物 A", "质控标准", 310.0, {121.0, 182.0, 276.0}, true},
        {"demo-b", "质控标准物 B", "质控标准", 410.0, {189.0, 284.0}, true},
        // 下列筛查项目沿用客户提供的旧版结果表字段，供结果复核页面展示。
        // 正式判定前必须由客户确认离子列表、阈值及仪器适配验证结果。
        {"panel-methamphetamine", "甲基苯丙胺", "筛查项目", 150.0, {119.0, 91.0, 150.1}, true},
        {"panel-medetomidine", "美托咪酯", "筛查项目", 231.0, {127.0, 231.0}, true},
        {"panel-ketamine", "氯胺酮", "筛查项目", 238.0, {220.0, 221.0, 238.0}, true},
        {"panel-fluoroketamine", "氟胺酮", "筛查项目", 222.0, {109.0, 222.0}, true},
        {"panel-dextromethorphan", "右美沙芬", "筛查项目", 272.0, {215.0, 147.0, 272.0}, true},
        {"panel-methadone", "美沙酮", "筛查项目", 310.0, {266.0, 265.0, 310.0}, true},
        {"panel-cocaine", "可卡因", "筛查项目", 304.0, {182.0, 304.0}, true},
        {"panel-tiletamine", "替来他明", "筛查项目", 224.1, {179.05, 224.10}, true},
        {"panel-thc", "四氢大麻酚", "筛查项目", 315.0, {231.0, 315.0}, true},
        {"panel-morphine", "吗啡", "筛查项目", 286.0, {201.0, 209.0, 286.0}, true},
        {"panel-6mam", "O6-单乙酰吗啡", "筛查项目", 328.0, {268.0, 328.0}, true},
        {"panel-amphetamine", "苯丙胺", "筛查项目", 136.0, {119.0, 136.0}, true},
        {"panel-etomidate", "依托咪酯", "筛查项目", 245.0, {141.0, 245.0}, true},
        {"panel-heroin", "海洛因", "筛查项目", 371.0, {328.0, 371.0}, true}
    };
}

} // namespace qitest
