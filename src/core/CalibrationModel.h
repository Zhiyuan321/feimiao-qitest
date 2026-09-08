#pragma once
#include "core/QuantitationEngine.h"
#include <vector>

namespace qitest {
struct CalibrationObservation {
    double concentration = 0;
    double response = 0;
    double internalConcentration = 0;
    double internalResponse = 0;
    bool included = true;
};

// All concentrations in one document use the same explicit unit. Response is
// consistently peak area OR height, never an implicit mixture of both.
struct CalibrationModel {
    enum class Standard { External, Internal };
    QString name;
    QString concentrationUnit = "µg/mL";
    QString responseUnit = "强度×秒";
    QString internalStandard;
    Standard standard = Standard::External;
    QuantitationEngine::Weight weight = QuantitationEngine::Weight::None;
    // Plain C++ owned storage: independent of Qt 5/6 container allocation ABI.
    std::vector<CalibrationObservation> observations;
};

struct CalibrationEvaluation {
    LinearCalibrationFit fit;
    QVector<CalibrationPoint> points;
    double minimum = 0;
    double maximum = 0;
};

class CalibrationCalculator final {
public:
    // Internal standard: x = analyte concentration / ISTD concentration,
    // y = analyte response / ISTD response. No inferred pair or blank removal.
    static CalibrationEvaluation evaluate(const CalibrationModel &model);
    static bool calculate(const CalibrationModel &model, double response,
        double internalResponse, double internalConcentration,
        double *concentration, QString *error);
};
}
