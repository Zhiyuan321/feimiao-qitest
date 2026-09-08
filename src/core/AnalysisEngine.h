#pragma once

#include "domain/Models.h"
#include <stdexcept>

namespace qitest {

class SpectrumError : public std::runtime_error {
public:
    explicit SpectrumError(const char *message) : std::runtime_error(message) {}
};

class SpectrumProcessor {
public:
    ProcessedSpectrum process(const QVector<SpectrumPoint> &raw) const;
};

class PeakDetector {
public:
    QVector<DetectedPeak> detect(const ProcessedSpectrum &spectrum) const;

private:
    double minimumRelativeIntensity_ = 2.0;
    double minimumSignalToNoise_ = 5.0;
    double minimumSeparationMz_ = 0.8;
};

class LibraryMatcher {
public:
    QVector<MatchCandidate> match(
        const QVector<DetectedPeak> &peaks,
        const QVector<SubstanceReference> &references) const;

private:
    double precursorTolerancePpm_ = 1800.0;
    double fragmentToleranceDa_ = 0.65;
};

class QualityGate {
public:
    QualityAssessment assess(
        qsizetype rawPointCount,
        const ProcessedSpectrum &spectrum,
        const QVector<DetectedPeak> &peaks,
        const InstrumentHealth &instrument) const;
};

class AnalysisEngine {
public:
    static constexpr const char *Version = "qitest-core-cpp-0.4.0";

    AnalysisEngine(QVector<SubstanceReference> references, QString libraryVersion);
    AnalysisResult analyze(const QVector<SpectrumPoint> &raw, const InstrumentHealth &instrument) const;

private:
    SpectrumProcessor processor_;
    PeakDetector detector_;
    LibraryMatcher matcher_;
    QualityGate qualityGate_;
    QVector<SubstanceReference> references_;
    QString libraryVersion_;
};

QVector<SubstanceReference> demoReferences();

} // namespace qitest
