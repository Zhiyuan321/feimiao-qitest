#pragma once

#include <QString>
#include <QVector>

namespace qitest {

struct CalibrationPoint {
    double concentration = 0.0;
    double response = 0.0;
};

struct LinearCalibrationFit {
    bool valid = false;
    QString error;
    double slope = 0.0;
    double intercept = 0.0;
    double rSquared = 0.0;
};

class QuantitationEngine final {
public:
    enum class Weight { None, InverseXSquared };
    static LinearCalibrationFit fitLinear(const QVector<CalibrationPoint> &points, Weight weight = Weight::None);
    static bool backCalculate(const LinearCalibrationFit &fit, double response,
        double minConcentration, double maxConcentration, double *concentration, QString *error);
};

} // namespace qitest
