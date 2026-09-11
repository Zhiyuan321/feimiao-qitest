#include "device/SimulatedInstrument.h"
#include "core/MethodDraft.h"

#include <QRandomGenerator>
#include <cmath>

namespace qitest {

namespace {
const QStringList &startupControls() {
    static const QStringList keys{"rfOn", "ionHighVoltageOn", "diaphragmPumpOn",
        "molecularPumpOn", "pinchValveOn", "internalCarrierGasOn"};
    return keys;
}
}

SimulatedInstrument::SimulatedInstrument() {
    // The existing offline workflow starts with a warmed-up virtual instrument.
    // Its confirmed switches must agree with that state, not default to OFF.
    for (const auto &key : startupControls()) settings_[key] = true;
    settings_["powerOn"] = true;
    settings_["ionSourceEnabled"] = true;
}

InstrumentDescriptor SimulatedInstrument::descriptor() const {
    return {"QITest 01", "SIMULATOR", "sim-contract-1", true};
}

InstrumentHealth SimulatedInstrument::health() const {
    bool ready = settings_.value("powerOn").toBool();
    for (const auto &key : startupControls()) ready = ready && settings_.value(key).toBool();
    const auto values = telemetry();
    return {true, ready, values.vacuumMbar, values.tdTemperatureC,
        values.carrierGasFlowMlMin, values.ionSourceVoltageV / 1000.0};
}

InstrumentTelemetry SimulatedInstrument::telemetry() const {
    const bool pump = settings_.value("molecularPumpOn").toBool();
    const bool gas = settings_.value("internalCarrierGasOn").toBool()
        && settings_.value("pinchValveOn").toBool();
    const bool ion = settings_.value("ionHighVoltageOn").toBool();
    // Synthetic steady-state readings only, not a physical cooldown/vacuum model.
    // Temperature and supply pressure do not instantly become zero on shutdown.
    return {
        pump ? 60000.0 : 0.0, pump ? 1.2 : 0.0, pump ? 24.0 : 0.0, 42.0, 1.8e-5,
        gas ? "内载气" : "已关闭", 801.0, gas ? settings_.value("efcMlMin", 28.4).toDouble() : 0.0,
        settings_.value("trapTemperatureC", 85.0).toDouble(), settings_.value("tdTemperatureC", 245.0).toDouble(),
        ion ? settings_.value("ionSourceSetpointKv", 3.2).toDouble() * 1000.0 : 0.0,
        settings_.value("rfOn").toBool() ? settings_.value("multiplierVoltageV", 1450.0).toDouble() : 0.0,
        settings_.value("diaphragmPumpOn").toBool() ? settings_.value("pumpFlowPercent", 20.0).toDouble() : 0.0, 76.0
    };
}

CommandValidation SimulatedInstrument::validate(const InstrumentCommand &command) const {
    if (command.risk == CommandRisk::HardwareCritical)
        return {false, "当前设备接口禁止执行硬件关键命令"};
    if (command.id == "StartAcquisition" || command.id == "CancelAcquisition" || command.id == "ReadHealth")
        return {true, "命令已通过接口校验"};
    return {false, "当前设备接口不支持该命令"};
}

QVector<SpectrumPoint> SimulatedInstrument::acquireSpectrum() {
    cancelled_ = false;
    QVector<SpectrumPoint> points;
    const double lowMass = methodParameters_.value("low_mass").toDouble(50.0);
    const double highMass = methodParameters_.value("high_mass").toDouble(500.0);
    const double boundedLow = qBound(0.0, lowMass, 1999.5);
    const double boundedHigh = qBound(boundedLow + 0.5, highMass, 2000.0);
    const int sampleCount = qBound(2, static_cast<int>((boundedHigh - boundedLow) / 0.5) + 1, 4001);
    const double multiplierScale = qBound(0.1,
        methodParameters_.value("multiplier").toDouble(1000.0) / 1000.0, 5.0);
    points.reserve(sampleCount);
    const QVector<QPair<double, double>> peaks{
        {121.0, 28.0}, {182.0, 48.0}, {189.0, 22.0}, {276.0, 33.0},
        {284.0, 58.0}, {310.0, 100.0}, {410.0, 70.0}
    };
    for (int i = 0; i < sampleCount; ++i) {
        if (cancelled_) return {};
        const double mz = boundedLow + i * 0.5;
        double intensity = 8.0 + QRandomGenerator::global()->generateDouble() * 4.0;
        for (const auto &peak : peaks) {
            const double delta = (mz - peak.first) / 0.34;
            intensity += peak.second * std::exp(-0.5 * delta * delta);
        }
        points.push_back({mz, intensity * multiplierScale});
    }
    return points;
}

void SimulatedInstrument::cancel() { cancelled_ = true; }

CommandValidation SimulatedInstrument::validateSetting(const QString &, const QVariant &) const {
    return {true, "接口已受理，设备执行状态待确认"};
}

void SimulatedInstrument::requestSetting(const QString &requestId, const QString &key, const QVariant &value) {
    settings_[key] = value;
    // This aggregate behavior belongs exclusively to the simulator. A real
    // adapter must implement the vendor's sequenced start/stop and interlocks.
    if (key == "powerOn") {
        for (const auto &part : startupControls()) settings_[part] = value.toBool();
        settings_["ionSourceEnabled"] = value.toBool();
    } else {
        if (key == "ionSourceEnabled") settings_["ionHighVoltageOn"] = value.toBool();
        if (key == "ionHighVoltageOn") settings_["ionSourceEnabled"] = value.toBool();
        if (startupControls().contains(key) || key == "ionSourceEnabled") {
            bool anyOn = false;
            for (const auto &part : startupControls()) anyOn = anyOn || settings_.value(part).toBool();
            settings_["powerOn"] = anyOn;
        }
    }
    emit settingFinished(requestId, key, true, value, {});
}

CommandValidation SimulatedInstrument::validateMethodParameters(const QJsonObject &parameters) const {
    QString error;
    if (!MethodDraft::validate(parameters, &error)) return {false, error};
    return {true, "方法参数已通过校验"};
}

void SimulatedInstrument::requestMethodParameters(const QString &requestId,
                                                  const QJsonObject &parameters) {
    const auto validation = validateMethodParameters(parameters);
    if (!validation.allowed) {
        emit methodParametersFinished(requestId, false, {}, validation.reason);
        return;
    }
    methodParameters_ = parameters;
    // 模拟端把与现有仪器状态面板同义的参数同步为可见回读；这不是厂家协议映射。
    const auto copyNumber = [this, &parameters](const char *source, const char *target, double scale = 1.0) {
        if (parameters.contains(source)) settings_[target] = parameters.value(source).toDouble() * scale;
    };
    copyNumber("carrier", "efcMlMin");
    copyNumber("td", "tdTemperatureC");
    copyNumber("extraction", "pumpFlowPercent");
    copyNumber("inlet", "inletFlowPercent");
    copyNumber("source", "ionSourceSetpointKv", 0.001);
    copyNumber("trap", "trapTemperatureC");
    copyNumber("multiplier", "multiplierVoltageV");
    emit stateChanged();
    emit methodParametersFinished(requestId, true, methodParameters_, {});
}

} // namespace qitest
