#include "core/AnalysisEngine.h"
#include "core/QuantitationEngine.h"
#include "core/CalibrationModel.h"
#include "core/ChromatogramEngine.h"
#include "core/SpectralComparison.h"
#include "device/SimulatedInstrument.h"

#include <QtTest>
#include <cmath>

using namespace qitest;

class CoreTests final : public QObject {
    Q_OBJECT
private slots:
    void processorRejectsUnsortedMassAxis();
    void syntheticPipelineFindsDemoCandidates();
    void matcherRejectsDistantReference();
    void qualityGateFailsDisconnectedInstrument();
    void simulatorRefusesHardwareCriticalCommands();
    void quantitationFitsVerifiedCalibrationPoints();
    void timeTracesSeparateLevelsAndIntegrateMeasuredTime();
    void internalStandardUsesPairedRatiosAndPreservesSafetyBounds();
    void userSpectraComparisonIsBoundedAndDoesNotReusePeaks();
};

void CoreTests::userSpectraComparisonIsBoundedAndDoesNotReusePeaks() {
    const QVector<SpectrumPoint> a{{10,3},{20,4}};
    auto match = SpectralComparison::compare(a, {{10,6},{20,8}}, 0);
    QVERIFY(match.valid); QCOMPARE(match.pairs.size(),2);
    QVERIFY(std::abs(match.cosine-1)<1e-12);
    match = SpectralComparison::compare(a, {{10,1}}, 0);
    QVERIFY(std::abs(match.cosine-0.6)<1e-12); // Includes unmatched query norm.
    match = SpectralComparison::compare({{10,1},{10.2,1}}, {{10.1,1}}, 0.3);
    QCOMPARE(match.pairs.size(),1); // Ambiguous neighbor cannot count twice.
    QVERIFY(std::abs(match.cosine-1/std::sqrt(2.0))<1e-12);
    match = SpectralComparison::compare(a, {{30,1}}, 0.5);
    QVERIFY(match.valid); QCOMPARE(match.cosine,0.0);
    match = SpectralComparison::compare({{10,1e300},{20,1e300}}, {{10,1},{20,1}}, 0);
    QVERIFY(match.valid); QVERIFY(std::abs(match.cosine-1)<1e-12);
    QVERIFY(!SpectralComparison::compare(a, {{10,0}}, 0).valid);
    QVERIFY(!SpectralComparison::compare(a, {{10,1},{10,2}}, 0).valid);
    QVERIFY(!SpectralComparison::compare(a, a, -1).valid);
    QVERIFY(!SpectralComparison::compare(a, {{10,qQNaN()}}, 0).valid);
    QVector<SpectrumPoint> large;
    for (int i=1;i<=10000;++i) large.append({double(i),1});
    match=SpectralComparison::compare(large,large,0);
    QCOMPARE(match.pairs.size(),10000); QVERIFY(std::abs(match.cosine-1)<1e-12);
    large.append({10001,1}); QVERIFY(!SpectralComparison::compare(large,a,0).valid);
}

void CoreTests::internalStandardUsesPairedRatiosAndPreservesSafetyBounds() {
    CalibrationModel model;
    model.standard=CalibrationModel::Standard::Internal;
    model.internalStandard="IS-A";
    // Vary both ISTD concentration and response; x = C/Cis, y = A/Ais.
    model.observations={{2,21,2,10,true},{8,82,4,20,true},{30,202,6,20,true}};
    auto evaluation=CalibrationCalculator::evaluate(model);
    QVERIFY(evaluation.fit.valid); QVERIFY(std::abs(evaluation.fit.slope-2)<1e-12);
    QVERIFY(std::abs(evaluation.fit.intercept-0.1)<1e-12);
    double result=-1; QString error;
    QVERIFY(CalibrationCalculator::calculate(model,61,10,2,&result,&error));
    QVERIFY(std::abs(result-6)<1e-12);
    QVERIFY(!CalibrationCalculator::calculate(model,61,0,2,&result,&error));
    QVERIFY(!CalibrationCalculator::calculate(model,61,10,0,&result,&error));
    QVERIFY(!CalibrationCalculator::calculate(model,6100,10,2,&result,&error));
    QCOMPARE(result,6.0); // Failed calculations must not return a new value.
    model.weight=QuantitationEngine::Weight::InverseXSquared;
    QVERIFY(CalibrationCalculator::evaluate(model).fit.valid);
    model.observations.push_back({0,0,2,10,true});
    QVERIFY(!CalibrationCalculator::evaluate(model).fit.valid);
    model.observations.back().included=false;
    QVERIFY(CalibrationCalculator::evaluate(model).fit.valid);
    QCOMPARE(model.observations.size(),4); // Exclusion preserves original data.
    model.internalStandard.clear(); QVERIFY(!CalibrationCalculator::evaluate(model).fit.valid);
}

QVector<SpectrumPoint> syntheticSpectrum() {
    QVector<SpectrumPoint> points;
    const QVector<QPair<double, double>> peaks{
        {121.0, 28.0}, {182.0, 48.0}, {276.0, 33.0}, {310.0, 100.0}
    };
    for (int i = 0; i <= 900; ++i) {
        const double mz = 50.0 + i * 0.5;
        double intensity = 10.0;
        for (const auto &peak : peaks) {
            const double delta = (mz - peak.first) / 0.34;
            intensity += peak.second * std::exp(-0.5 * delta * delta);
        }
        points.push_back({mz, intensity});
    }
    return points;
}

void CoreTests::processorRejectsUnsortedMassAxis() {
    auto spectrum = syntheticSpectrum();
    std::swap(spectrum[20], spectrum[21]);
    bool rejected = false;
    try {
        SpectrumProcessor().process(spectrum);
    } catch (const SpectrumError &) {
        rejected = true;
    }
    QVERIFY(rejected);
}

void CoreTests::syntheticPipelineFindsDemoCandidates() {
    AnalysisEngine engine(demoReferences(), "test-library");
    const auto result = engine.analyze(syntheticSpectrum(), {true, true, 1.8e-5, 245.0, 28.4, 3.2});
    QVERIFY(!result.peaks.isEmpty());
    QVERIFY(!result.candidates.isEmpty());
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.screeningItems.size(), demoReferences().size());
    QVERIFY(std::any_of(result.screeningItems.begin(), result.screeningItems.end(), [](const auto &item) {
        return item.conclusion == "可疑" && !item.measuredRelativeIntensity.isEmpty();
    }));
    QVERIFY(result.quality.level != QualityLevel::Fail);
}

void CoreTests::matcherRejectsDistantReference() {
    const QVector<DetectedPeak> peaks{{100.0, 100.0, 30.0}};
    const QVector<SubstanceReference> references{{"far", "far", "demo", 350.0, {}, true}};
    QVERIFY(LibraryMatcher().match(peaks, references).isEmpty());
}

void CoreTests::qualityGateFailsDisconnectedInstrument() {
    const auto processed = SpectrumProcessor().process(syntheticSpectrum());
    const auto peaks = PeakDetector().detect(processed);
    const auto quality = QualityGate().assess(syntheticSpectrum().size(), processed, peaks, {});
    QVERIFY(quality.level != QualityLevel::Pass);
}

void CoreTests::simulatorRefusesHardwareCriticalCommands() {
    SimulatedInstrument instrument;
    QVERIFY(instrument.descriptor().simulation);
    const auto telemetry = instrument.telemetry();
    QVERIFY(telemetry.molecularPumpRpm > 0.0);
    QVERIFY(telemetry.vacuumMbar > 0.0);
    QVERIFY(telemetry.syringeRemainingPercent >= 0.0 && telemetry.syringeRemainingPercent <= 100.0);
    QVERIFY(instrument.validate({"StartAcquisition", CommandRisk::Routine, {}}).allowed);
    QVERIFY(!instrument.validate({"SetIonSourceVoltage", CommandRisk::HardwareCritical, {{"kv", 4.5}}}).allowed);
}

void CoreTests::quantitationFitsVerifiedCalibrationPoints() {
    const auto fit = QuantitationEngine::fitLinear({{0.0, 0.1}, {1.0, 2.1}, {2.0, 4.1}, {5.0, 10.1}});
    QVERIFY(fit.valid);
    QVERIFY(std::abs(fit.slope - 2.0) < 1e-9);
    QVERIFY(std::abs(fit.intercept - 0.1) < 1e-9);
    QVERIFY(fit.rSquared > 0.999999);
    QVERIFY(!QuantitationEngine::fitLinear({{1.0, 2.0}, {1.0, 3.0}, {1.0, 4.0}}).valid);
    const auto weighted = QuantitationEngine::fitLinear({{1,2.1},{2,4.1},{5,10.1}}, QuantitationEngine::Weight::InverseXSquared);
    QVERIFY(weighted.valid); QVERIFY(std::abs(weighted.slope-2) < 1e-9);
    QVERIFY(!QuantitationEngine::fitLinear({{0,0.1},{1,2.1},{2,4.1}}, QuantitationEngine::Weight::InverseXSquared).valid);
    QVERIFY(!QuantitationEngine::fitLinear({{0,5},{1,5},{2,5}}).valid);
    double concentration = -1; QString error;
    QVERIFY(QuantitationEngine::backCalculate(weighted, 6.1, 1, 5, &concentration, &error));
    QVERIFY(std::abs(concentration-3) < 1e-9);
    QVERIFY(!QuantitationEngine::backCalculate(weighted, 1000, 1, 5, &concentration, &error));
}

void CoreTests::timeTracesSeparateLevelsAndIntegrateMeasuredTime() {
    const QVector<SpectrumScan> scans{{0,1,{{100,1},{101,2}}}, {0.5,2,{{100,500}}},
        {1,1,{{100,3},{101,4}}}, {3,1,{{100,5},{101,6}}}};
    QVERIFY(ChromatogramEngine::validate(scans));
    const auto tic = ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Tic);
    QCOMPARE(tic.size(), 3); QCOMPARE(tic[1].mz, 1.0); QCOMPARE(tic[1].intensity, 7.0);
    QCOMPARE(ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Bpc)[1].intensity, 4.0);
    QCOMPARE(ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Eic,1,100,0.1)[1].intensity, 3.0);
    QCOMPARE(ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Tic,2).size(), 1);
    QCOMPARE(ChromatogramEngine::integrate(tic, 0, 3, false).area, 23.0);
    QCOMPARE(ChromatogramEngine::integrate(tic, 0, 3, true).area, 2.0);
    QCOMPARE(ChromatogramEngine::integrate(tic, 0.5, 2, false).area, 11.0);
    QVERIFY(!ChromatogramEngine::integrate(tic, -1, 3, false).valid);
    QVERIFY(!ChromatogramEngine::integrate(tic, 1, 1, false).valid);
    auto bad = scans; bad[1].timeSeconds = 0;
    QVERIFY(!ChromatogramEngine::validate(bad));
    bad = scans; bad[0].points[0].intensity = std::numeric_limits<double>::infinity();
    QVERIFY(!ChromatogramEngine::validate(bad));
    QVERIFY(ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Eic, 1,100,-1).isEmpty());
}

QTEST_APPLESS_MAIN(CoreTests)
#include "CoreTests.moc"
