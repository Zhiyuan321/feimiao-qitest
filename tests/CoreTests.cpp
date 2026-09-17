#include "core/AnalysisEngine.h"
#include "core/QuantitationEngine.h"
#include "core/CalibrationModel.h"
#include "core/ChromatogramEngine.h"
#include "core/IonThresholdScreening.h"
#include <QJsonArray>
#include "core/SpectralComparison.h"
#include "device/SimulatedInstrument.h"

#include <QtTest>
#include <cmath>

using namespace qitest;

class CoreTests final : public QObject {
    Q_OBJECT
private slots:
    void threeIonThresholdsRequireEveryIndependentSum();
    void variableIonCountsAndLegacySnapshots();
    void processorRejectsUnsortedMassAxis();
    void syntheticPipelineFindsDemoCandidates();
    void matcherRejectsDistantReference();
    void qualityGateFailsDisconnectedInstrument();
    void simulatorRefusesHardwareCriticalCommands();
    void quantitationFitsVerifiedCalibrationPoints();
    void timeTracesSeparateLevelsAndIntegrateMeasuredTime();
    void ticSumsEachOneSecondSpectrumWithoutMassWeighting();
    void eicAreaSumsFramesWithoutTimeWeighting();
    void internalStandardUsesPairedRatiosAndPreservesSafetyBounds();
    void userSpectraComparisonIsBoundedAndDoesNotReusePeaks();
};

void CoreTests::variableIonCountsAndLegacySnapshots() {
    const QVector<SpectrumScan> scans{{0,1,{{100,10},{200,20},{300,30},{400,40}}},
                                     {20,1,{{100,10},{200,20},{300,30},{400,40}}}};
    for(const auto rule:{IonThresholdScreening::Version,IonThresholdScreening::LegacyVersion}) {
        for(int count=1;count<=4;++count) {
            QStringList ions,thresholds;
            for(int i=1;i<=count;++i) {ions<<QString::number(i*100);thresholds<<QString::number(i*20-1);}
            QJsonObject entry{{"name","可变离子物质"},{"qualitify_ion",ions.join(',')},{"son_area",thresholds.join(',')}};
            QJsonObject snapshot{{"rule",rule},{"tolerance_da",0.5},{"entries",QJsonArray{entry}}};
            AnalysisResult result;QString error;
            QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("COMPLETE"));
            QCOMPARE(result.engineVersion,QString(IonThresholdScreening::Version));
            QCOMPARE(result.candidates.size(),1);QCOMPARE(result.candidates[0].requiredFragments,count);
            QCOMPARE(result.candidates[0].matchedFragments,count);
            for(int i=0;i<count;++i)QCOMPARE(result.screeningItems[0].accumulatedIntensities[i],double(20*(i+1)));
            // Each position must pass independently, including the fourth and later ions.
            for(int i=0;i<count;++i) {
                auto equal=thresholds;equal[i]=QString::number((i+1)*20);entry["son_area"]=equal.join(',');
                snapshot["entries"]=QJsonArray{entry};
                QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("COMPLETE"));
                QVERIFY(result.candidates.isEmpty());QCOMPARE(result.screeningItems[0].conclusion,QString("未检出"));
            }
        }
    }
    const auto entry=[](QString ions,QString thresholds){return QJsonObject{{"name","格式检查"},{"qualitify_ion",ions},{"son_area",thresholds}};};
    QJsonObject snapshot{{"rule",IonThresholdScreening::Version},{"tolerance_da",0.5},{"entries",QJsonArray{
        entry("100,200","19"),entry("100","19,39"),entry("",""),entry("100,","19,39"),
        entry("100","-1"),entry("0","0"),entry("100，200","19，39"),
        QJsonObject{{"name","单个数值字段"},{"qualitify_ion",100},{"son_area",19}}}}};
    AnalysisResult result;QString error;
    QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("PARTIAL"));
    QCOMPARE(result.candidates.size(),2);QCOMPARE(result.screeningItems.size(),8);
    QVERIFY(result.screeningItems[0].evidence.contains("定性离子2个，一级阈值1个"));
    for(int i=0;i<6;++i)QCOMPARE(result.screeningItems[i].conclusion,QString("未筛查"));
}

void CoreTests::threeIonThresholdsRequireEveryIndependentSum() {
    const QVector<SpectrumPoint> points{{99.49,10000},{99.5,2},{100,3},{100.5,5},{100.51,10000},{200,20},{300,30}};
    const QVector<SpectrumScan> scans{{0,1,points},{2,2,points},{20,1,points}};
    const auto row=[](QString name,QString thresholds,QString ions="100,200,300") {
        return QJsonObject{{"id","duplicate"},{"name",name},{"qualitify_ion",ions},{"son_area",thresholds}};
    };
    QJsonObject snapshot{{"rule",IonThresholdScreening::Version},{"tolerance_da",0.5},{"entries",QJsonArray{
        row("全通过","19,39,59"),row("等于不通过","20,39,59"),row("单项不足","0,0,100"),
        row("顺序不同","59,39,19"),row("全部为零阈值","0,0,0"),row("缺少离子","0,0,0","100,200")}}};
    AnalysisResult result;QString error;
    QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("PARTIAL"));
    QCOMPARE(result.candidates.size(),2);QCOMPARE(result.screeningItems.size(),6);
    QCOMPARE(result.screeningItems[0].accumulatedIntensities,QVector<double>({20,40,60}));
    QCOMPARE(result.screeningItems[0].primaryThresholds,QVector<double>({19,39,59}));
    QVERIFY(!result.candidates[0].demo);QCOMPARE(result.candidates[0].matchedFragments,3);
    QVERIFY(result.candidates[0].referenceId!=result.candidates[1].referenceId);
    for(int i=1;i<=3;++i)QCOMPARE(result.screeningItems[i].conclusion,QString("未检出"));
    QCOMPARE(result.screeningItems[5].conclusion,QString("未筛查"));
    for(int i=0;i<3;++i) {
        const auto trace=ChromatogramEngine::trace(scans,ChromatogramEngine::Kind::Eic,1,100*(i+1),0.5);
        QCOMPARE(trace.size(),2);
        QCOMPARE(trace[0].intensity,trace[1].intensity); // Graph still displays individual frames.
        QCOMPARE(ChromatogramEngine::sumIntensities(trace).value,result.screeningItems[0].accumulatedIntensities[i]);
    }
    snapshot["entries"]=QJsonArray{row("缺失信号","0,0,0","100,200,400")};
    QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("COMPLETE"));
    QVERIFY(result.candidates.isEmpty());QCOMPARE(result.screeningItems[0].accumulatedIntensities[2],0.0);
    snapshot["entries"]=QJsonArray{row("非法阈值","-1,0,0"),row("非法数值","nan,0,0")};
    QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("PARTIAL"));
    QVERIFY(result.candidates.isEmpty());
    snapshot["rule"]="unknown";
    QCOMPARE(IonThresholdScreening::apply(scans,snapshot,&result,&error),QString("FAILED"));
    QVERIFY(result.screeningItems.isEmpty());QVERIFY(!error.isEmpty());
    snapshot["rule"]=IonThresholdScreening::Version;
    snapshot["entries"]=QJsonArray{row("溢出","0,0,0","100,100,100")};
    QCOMPARE(IonThresholdScreening::apply({{0,1,{{100,1e308}}},{1,1,{{100,1e308}}}},snapshot,&result,&error),QString("PARTIAL"));
    QVERIFY(result.candidates.isEmpty());
}

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

void CoreTests::ticSumsEachOneSecondSpectrumWithoutMassWeighting() {
    // Synthetic complete scans, not captured packets. Unequal mass spacing catches
    // accidental m/z integration; unequal sums catch accumulation across cycles.
    const QVector<SpectrumScan> scans{
        {0,1,{{40,10},{41,20},{300,30}}},
        {1,1,{{40,1},{41,2},{300,3}}},
        {2,1,{{40,0},{41,0},{300,0}}}};
    const auto tic = ChromatogramEngine::trace(scans, ChromatogramEngine::Kind::Tic);
    QCOMPARE(tic.size(), 3);
    QCOMPARE(tic[0].mz, 0.0); QCOMPARE(tic[0].intensity, 60.0);
    QCOMPARE(tic[1].mz, 1.0); QCOMPARE(tic[1].intensity, 6.0);
    QCOMPARE(tic[2].mz, 2.0); QCOMPARE(tic[2].intensity, 0.0);
}

void CoreTests::eicAreaSumsFramesWithoutTimeWeighting() {
    const QVector<SpectrumScan> scans{{0,1,{{237.9,2},{238,3},{239,100}}},
        {2,1,{{237.9,4},{238,6},{239,200}}},{20,1,{{237.9,0},{238,5},{239,300}}}};
    const auto trace=ChromatogramEngine::trace(scans,ChromatogramEngine::Kind::Eic,1,238,0.5);
    QCOMPARE(trace[0].intensity,5.0);QCOMPARE(trace[1].intensity,10.0);QCOMPARE(trace[2].intensity,5.0);
    const auto sum=ChromatogramEngine::sumIntensities(trace);QVERIFY(sum.valid);QCOMPARE(sum.value,20.0);
    QCOMPARE(ChromatogramEngine::integrate(trace,0,20,false).area,150.0);
    QVERIFY(!ChromatogramEngine::sumIntensities({}).valid);
    QVERIFY(!ChromatogramEngine::sumIntensities({{0,1},{1,-1}}).valid);
    QVERIFY(!ChromatogramEngine::sumIntensities({{0,1},{0,2}}).valid);
    QVERIFY(!ChromatogramEngine::sumIntensities({{0,1e308},{1,1e308}}).valid);
}

QTEST_APPLESS_MAIN(CoreTests)
#include "CoreTests.moc"
