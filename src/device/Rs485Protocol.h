#pragma once

#include "domain/Models.h"
#include <QByteArray>

namespace qitest {

// 便携式质谱485通讯协议(2).pdf，第3至7页；无CRC，长度仅计数据区。
struct Rs485Frame {
    quint8 command = 0;
    QByteArray payload;
};

struct Rs485Status {
    // Legacy field name: raw HV integer; user-corrected ion-source V = raw / 10 (2026-09-10).
    // Keep wire data intact; physical conversion belongs to the adapter.
    quint16 highVoltageV = 0;
    quint16 highVoltageCurrentUa = 0;
    quint16 vacuumGaugeMv = 0;
    quint16 gasPressureTorr = 0;
    double tdTemperatureC = 0;
    double trapTemperatureC = 0;
    double efcMlMin = 0;
    quint16 gasPumpPwmPercent = 0;
    bool observationLightOn = false;
    bool heatingOn = false;
    bool wastePumpOn = false;
    bool externalCarrierGas = false;
    bool hv24VOn = false;
    bool rf24VOn = false;
    bool diaphragmPumpOn = false;
};

class Rs485Protocol {
public:
    static QByteArray statusQuery();
    static bool decodeStatus(const QByteArray &payload, Rs485Status *result);
    // Handles fragmented/coalesced reads; rejects oversized lengths and bad tails.
    QVector<Rs485Frame> feed(const QByteArray &bytes);
    void reset() { buffer_.clear(); }
    int bufferedBytes() const { return buffer_.size(); }
private:
    QByteArray buffer_;
};

InstrumentTelemetry unavailableRs485Telemetry();
InstrumentHealth unavailableRs485Health();

} // namespace qitest
