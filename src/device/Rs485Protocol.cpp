#include "device/Rs485Protocol.h"
#include <limits>

namespace qitest {
namespace {
quint16 u16(const QByteArray &data, int offset) {
    return (quint16(quint8(data[offset])) << 8) | quint8(data[offset + 1]);
}
}

QByteArray Rs485Protocol::statusQuery() {
    return QByteArray::fromHex("558830000101aa");
}

QVector<Rs485Frame> Rs485Protocol::feed(const QByteArray &bytes) {
    QVector<Rs485Frame> frames;
    // Process bytewise so even a very large caller input cannot grow the cache.
    for (char byte : bytes) {
        buffer_.append(byte);
        for (;;) {
            const int start = buffer_.indexOf(char(0x55));
            if (start < 0) { buffer_.clear(); break; }
            if (start > 0) buffer_.remove(0, start);
            if (buffer_.size() < 2) break;
            if (quint8(buffer_[1]) != 0x88) { buffer_.remove(0, 1); continue; }
            if (buffer_.size() < 5) break;
            const int length = u16(buffer_, 3);
            if (length < 1 || length > 1024) { buffer_.remove(0, 1); continue; }
            const int frameSize = length + 6;
            if (buffer_.size() < frameSize) break;
            if (quint8(buffer_[frameSize - 1]) != 0xaa) { buffer_.remove(0, 1); continue; }
            frames.push_back({quint8(buffer_[2]), buffer_.mid(5, length)});
            buffer_.remove(0, frameSize);
        }
    }
    return frames;
}

bool Rs485Protocol::decodeStatus(const QByteArray &data, Rs485Status *result) {
    // Seven one-byte flags followed by eight big-endian uint16 readings.
    // Reject unknown layouts rather than shifting offsets or padding with zeros.
    if (!result || data.size() != 23) return false;
    for (int i = 0; i < 7; ++i)
        if (quint8(data[i]) != 0xee && quint8(data[i]) != 0xff) return false;
    Rs485Status value;
    value.observationLightOn = quint8(data[0]) == 0xee;
    value.heatingOn = quint8(data[1]) == 0xee;
    value.wastePumpOn = quint8(data[2]) == 0xee;
    value.externalCarrierGas = quint8(data[3]) == 0xee;
    value.hv24VOn = quint8(data[4]) == 0xee;
    value.rf24VOn = quint8(data[5]) == 0xee;
    value.diaphragmPumpOn = quint8(data[6]) == 0xee;
    value.highVoltageV = u16(data, 7);
    value.highVoltageCurrentUa = u16(data, 9);
    value.vacuumGaugeMv = u16(data, 11);
    value.gasPressureTorr = u16(data, 13);
    value.tdTemperatureC = u16(data, 15) / 10.0;
    value.trapTemperatureC = u16(data, 17) / 10.0;
    value.efcMlMin = u16(data, 19) / 200.0;
    value.gasPumpPwmPercent = u16(data, 21);
    if (value.highVoltageV > 5000 || value.highVoltageCurrentUa > 1000
        || value.vacuumGaugeMv > 3300 || value.gasPumpPwmPercent > 100) return false;
    *result = value;
    return true;
}

InstrumentTelemetry unavailableRs485Telemetry() {
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    return {unknown, unknown, unknown, unknown, unknown, "未提供",
        unknown, unknown, unknown, unknown, unknown, unknown, unknown, unknown};
}

InstrumentHealth unavailableRs485Health() {
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    return {false, false, unknown, unknown, unknown, unknown};
}
} // namespace qitest
