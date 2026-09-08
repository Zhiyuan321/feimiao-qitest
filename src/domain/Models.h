#pragma once

#include <QString>
#include <QVector>
#include <QMetaType>
#include <QVariantMap>

namespace qitest {

struct SpectrumPoint {
    double mz = 0.0;
    double intensity = 0.0;
};

// Seconds from the start of acquisition. A scan is not a progressive drawing
// of one spectrum. MS levels are kept separate when constructing traces.
struct SpectrumScan {
    double timeSeconds = 0.0;
    int msLevel = 1;
    QVector<SpectrumPoint> points;
};

struct ProcessedSpectrum {
    QVector<SpectrumPoint> points;
    double baseline = 0.0;
    double noiseMad = 0.0;
    double totalIonCurrent = 0.0;
};

struct DetectedPeak {
    double mz = 0.0;
    double relativeIntensity = 0.0;
    double signalToNoise = 0.0;
};

struct SubstanceReference {
    QString id;
    QString displayName;
    QString category;
    double precursorMz = 0.0;
    QVector<double> fragmentMz;
    bool demo = true;
};

struct MatchCandidate {
    QString referenceId;
    QString name;
    QString category;
    double measuredMz = 0.0;
    double massErrorPpm = 0.0;
    double score = 0.0;
    int matchedFragments = 0;
    int requiredFragments = 0;
    QString evidence;
    bool demo = true;
};

// 筛查面板中的逐项结果。候选列表只保留达到阈值的项目；本结构同时保留
// 未达到阈值的项目，供操作员在“筛查详情”中核对完整面板。
struct ScreeningItem {
    QString referenceId;
    QString name;
    double precursorMz = 0.0;
    QVector<double> fragmentMz;
    QVector<double> measuredRelativeIntensity;
    double score = 0.0;
    QString conclusion;
    bool demo = true;
};

enum class QualityLevel { Pass, Review, Fail };

struct QualityCheck {
    QString id;
    QString title;
    QString detail;
    bool passed = false;
};

struct QualityAssessment {
    QualityLevel level = QualityLevel::Fail;
    int score = 0;
    QVector<QualityCheck> checks;
};

struct InstrumentHealth {
    bool connected = false;
    bool ready = false;
    double vacuumMbar = 0.0;
    double tdTemperatureC = 0.0;
    double carrierGasMlMin = 0.0;
    double ionSourceKv = 0.0;
};

// Complete read-only telemetry contract shared by the instrument adapter, UI,
// audit/data engine and local AI evidence layer. Values must originate from the
// adapter; the UI and language model never invent missing measurements.
struct InstrumentTelemetry {
    double molecularPumpRpm = 0.0;
    double molecularPumpCurrentA = 0.0;
    double molecularPumpVoltageV = 0.0;
    double molecularPumpTemperatureC = 0.0;
    double vacuumMbar = 0.0;
    QString carrierGasMode;
    double carrierGasPressureTorr = 0.0;
    double carrierGasFlowMlMin = 0.0;
    double ionTrapTemperatureC = 0.0;
    double tdTemperatureC = 0.0;
    double ionSourceVoltageV = 0.0;
    double multiplierVoltageV = 0.0;
    double extractionFlowPercent = 0.0;
    double syringeRemainingPercent = 0.0;
};

struct InstrumentDescriptor {
    QString model;
    QString serialNumber;
    QString protocolVersion;
    bool simulation = true;
};

enum class CommandRisk { ReadOnly, Routine, HardwareCritical };

struct InstrumentCommand {
    QString id;
    CommandRisk risk = CommandRisk::ReadOnly;
    QVariantMap parameters;
};

struct CommandValidation {
    bool allowed = false;
    QString reason;
};

struct AnalysisResult {
    ProcessedSpectrum processedSpectrum;
    QVector<DetectedPeak> peaks;
    QVector<MatchCandidate> candidates;
    QVector<ScreeningItem> screeningItems;
    QualityAssessment quality;
    QString engineVersion;
    QString libraryVersion;
};

} // namespace qitest

Q_DECLARE_METATYPE(qitest::InstrumentHealth)
Q_DECLARE_METATYPE(qitest::InstrumentTelemetry)
Q_DECLARE_METATYPE(qitest::AnalysisResult)
