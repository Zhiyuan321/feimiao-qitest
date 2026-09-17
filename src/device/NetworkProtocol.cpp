#include "device/NetworkProtocol.h"
#include <cmath>
#include <limits>

namespace qitest {
namespace {
// Fullscan profile supplied in datafit.json on 2026-09-15. Both directions
// must use this one profile; never change only the displayed mass axis.
constexpr double fullscanA = 0.00013517703930585604;
constexpr double fullscanB = 5.882440951161457;
constexpr double fullscanC = 8.575019905095814;
quint16 u16(const QByteArray &bytes, int offset) {
    return (quint16(quint8(bytes[offset])) << 8) | quint8(bytes[offset + 1]);
}
void appendU16(QByteArray &bytes, quint16 value) {
    bytes.append(char(value >> 8)); bytes.append(char(value & 0xff));
}
QByteArray controlFrame(quint8 command, const QByteArray &payload) {
    const int length = payload.size() + 2; // count + index are part of LEN.
    QByteArray body;
    body.reserve(payload.size() + 8);
    body.append(char(0x10)); body.append(char(command));
    appendU16(body, quint16(length)); body.append(char(0x01)); body.append(char(0x01));
    body.append(payload);
    const auto crc = NetworkProtocol::crc16(body);
    QByteArray wire(1, char(0x55)); wire += body;
    wire.append(char(crc >> 8)); wire.append(char(crc & 0xff)); wire.append(char(0xaa));
    return wire;
}
bool exactInteger(const QJsonObject &values, const char *key, int minimum, int maximum,
                  int *result, QString *error) {
    const auto value = values.value(QLatin1String(key));
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || number < minimum || number > maximum
        || std::abs(number - std::round(number)) > 1e-6) {
        if (error) *error = QString::fromLatin1(key) + " 必须是协议范围内的整数";
        return false;
    }
    *result = int(std::llround(number)); return true;
}
bool calibratedVoltage(double mass, quint16 *result, QString *error) {
    // The supplied method code applies the Fullscan polynomial / 2.
    constexpr double a = fullscanA, b = fullscanB, c = fullscanC;
    const double raw = (c + b * mass + a * mass * mass) / 2.0;
    if (!std::isfinite(raw) || raw < 0 || raw > 65535) {
        if (error) *error = "质量数超出当前仪器 Fullscan 校准范围";
        return false;
    }
    *result = quint16(raw); return true; // Matches the supplied code's int truncation.
}
}
double NetworkProtocol::vacuumMbarFromRaw(quint16 value) {
    const double result = (value * 1.0 / 65536) * 2.5 * 5.7;
    return std::pow(10.0, (result - 6.143) / 1.286);
}
bool NetworkProtocol::decodePressure(const NetworkFrame &frame, QVector<double> *volts) {
    if (!volts || frame.action != 0x20 || frame.command != 0x82
        || frame.payload.isEmpty() || frame.payload.size() > MaximumWaveformPayload || frame.payload.size() % 2) return false;
    QVector<double> values; values.reserve(frame.payload.size()/2);
    // Exact conversion supplied by the user on 2026-09-10; not the vacuum formula.
    for(int i=0;i<frame.payload.size();i+=2) values.append(u16(frame.payload,i) / 65535.0 * 2.5 * 5.7);
    *volts=values; return true;
}
QByteArray NetworkProtocol::tuningCommand(bool enabled) {
    return controlFrame(0x20, QByteArray(1, enabled ? char(0x22) : char(0x23)));
}
QByteArray NetworkProtocol::detectionCommand(bool enabled) {
    return controlFrame(0x15,QByteArray(1,enabled ? char(0x22) : char(0x23)));
}
QByteArray NetworkProtocol::heartbeatCommand() {
    // Match the old-program capture authorized on 2026-09-16.
    return controlFrame(0x30,QByteArray(1,char(0x23)));
}
QByteArray NetworkProtocol::legacyMethodFollowupCommand() {
    // Captured post-method command; do not infer its physical meaning.
    return controlFrame(0x50,QByteArray(1,char(0x00)));
}
QJsonObject NetworkProtocol::fullscanCalibrationProfile() {
    return {{"source","datafit.json"},{"section","Fullscan"},
        {"source_sha256","f84190db1ebd30dc63eafcc8f69c726f3cefa73b65f07130378e79229b8e2233"},
        {"calibrate_a",fullscanA},{"calibrate_b",fullscanB},{"calibrate_c",fullscanC}};
}
QVector<double> NetworkProtocol::fullscanMassAxis(const QJsonObject &parameters, QString *error) {
    const auto wire=fullscanMethodCommand(parameters,error);
    if(wire.isEmpty()) return {};
    const auto fail=[error](const QString &message){if(error)*error=message;return QVector<double>{};};
    // Use the exact transmitted (truncated) slots and the same calibration as method setting.
    const int count=int(u16(wire,11))*int(u16(wire,23))/10;
    if(count<2 || count>127500) return fail("采样点数超出单周期分包容量");
    const double low=u16(wire,17)*2.0, high=u16(wire,19)*2.0;
    const double lowMass=parameters.value("low_mass").toDouble(),highMass=parameters.value("high_mass").toDouble();
    QVector<double> axis;axis.reserve(count);
    for(int i=0;i<count;++i) {
        const double voltage=low+(high-low)*i/(count-1);
        const double discriminant=fullscanB*fullscanB-4*fullscanA*(fullscanC-voltage);
        if(discriminant<0) return fail("质量轴校准反算失败");
        const double roots[]{(-fullscanB+std::sqrt(discriminant))/(2*fullscanA),
                            (-fullscanB-std::sqrt(discriminant))/(2*fullscanA)};
        double mz=0;
        for(double root:roots) if(root>lowMass-2 && root<highMass+2) {mz=root;break;}
        if(!std::isfinite(mz) || mz<=0 || (!axis.isEmpty() && mz<=axis.last()))
            return fail("质量轴不是有效递增序列，请核对校准文件");
        axis.append(mz);
    }
    return axis;
}
QByteArray NetworkProtocol::fullscanMethodCommand(const QJsonObject &values, QString *error) {
    const auto fail = [error](const QString &message) { if (error) *error = message; return QByteArray{}; };
    if (values.value("scan_mode").toString().compare("Fullscan", Qt::CaseInsensitive) != 0)
        return fail("真实方法下发当前仅开放 Fullscan");
    int period=0, speed=0, rf=0, ac=0, injection=0, cooling=0, multiplier=0;
    if (!exactInteger(values,"period",1,65535,&period,error)
        || !exactInteger(values,"speed",1,100000,&speed,error)
        || !exactInteger(values,"rf_frequency",1,100,&rf,error)
        // The supplied running workstation uses 590 although V1.4 prints 0-500;
        // preserve the observed firmware value while keeping the U16 wire bound.
        || !exactInteger(values,"ac_frequency",0,65535,&ac,error)
        || !exactInteger(values,"injection",0,10000,&injection,error)
        || !exactInteger(values,"cooling",0,65535,&cooling,error)
        || !exactInteger(values,"multiplier",0,2000,&multiplier,error)) return {};
    const double storageMass=values.value("storage_mass").toDouble(-1);
    const double lowMass=values.value("low_mass").toDouble(-1);
    const double highMass=values.value("high_mass").toDouble(-1);
    // The vendor method selects separate profiles above these mass ranges.
    if(highMass>801) return fail(highMass>1001
        ? "此质量范围需要 datafit_2000.json 校准文件，当前未配置"
        : "此质量范围需要 datafit_1000.json 校准文件，当前未配置");
    if (!std::isfinite(storageMass) || !std::isfinite(lowMass) || !std::isfinite(highMass)
        || storageMass < 0 || lowMass < 0 || highMass <= lowMass) return fail("Fullscan 质量数范围无效");
    quint16 storage=0, low=0, high=0;
    if (!calibratedVoltage(storageMass,&storage,error) || !calibratedVoltage(lowMass,&low,error)
        || !calibratedVoltage(highMass,&high,error)) return {};
    const double scanTimeRaw=(highMass-lowMass)*10000.0/speed;
    if (!std::isfinite(scanTimeRaw) || scanTimeRaw < 1 || scanTimeRaw > 65535)
        return fail("扫描时间超出协议范围");
    const int scanTime=int(scanTimeRaw); // supplied workstation code truncates to int
    constexpr int rfFastScanTime=30, rfLowVoltageDuration=50;
    // The supplied sum_time includes slot 8 (cooling), not slot 13 (injection).
    if (cooling + scanTime + rfFastScanTime + rfLowVoltageDuration + 1000 > period)
        return fail("扫描各阶段时间和不得超过总周期");
    constexpr double acK=0.01921598770176787, acB=544.8501152959262;
    const int acLow=int(acK*(low*2)+acB), acHigh=int(acK*(high*2)+acB);
    if (acLow < 0 || acLow > 65535 || acHigh < 0 || acHigh > 65535)
        return fail("AC 电压换算超出协议范围");

    // Fixed slots match the old-program capture authorized on 2026-09-16.
    // Editable fields and calibrated voltages still come from the current method.
    int data[45]{};
    data[0]=1; data[1]=1; data[2]=period; data[3]=rf; data[4]=3000;
    data[5]=storage; data[6]=low; data[7]=high; data[8]=cooling; data[9]=scanTime;
    data[10]=acLow; data[11]=acHigh; data[12]=ac; data[13]=injection; data[14]=0;
    data[15]=20; data[16]=20; data[17]=500; data[18]=500; data[19]=500;
    // User confirmed 2026-09-14: injection 380 sends 380, without x100 scaling.
    // Opening count/interval are a separate fixed pair, independent of cooling.
    data[20]=1; data[21]=3000; data[22]=multiplier;
    data[23]=rfFastScanTime; data[24]=rfLowVoltageDuration;
    const int legacyTail[]{10,100,100,0,0,802,446,207,651,674,0,0,9,1000,600,500,219,10,10,10};
    for(int i=0;i<20;++i) data[25+i]=legacyTail[i];
    QByteArray payload; payload.reserve(86);
    for (int i=0;i<45;++i) {
        if (i==0 || i==1 || i==14 || i==20) payload.append(char(data[i]));
        else appendU16(payload, quint16(data[i]));
    }
    if (payload.size()!=86) return fail("Fullscan 方法报文长度异常");
    if (error) error->clear();
    return controlFrame(0x81,payload);
}
bool NetworkProtocol::decodeCommandAcknowledgement(const NetworkFrame &frame, quint8 expectedCommand,
                                                   bool *success) {
    if (!success || frame.action!=0x10 || frame.command!=expectedCommand || frame.count!=1
        || frame.index!=1 || frame.payload.size()!=1) return false;
    const quint8 result=quint8(frame.payload[0]);
    // The updated 16-page protocol defines method error 0x29 as COOL_TIME_ERROR.
    if (result!=0x11 && result!=0x12 && !(expectedCommand==0x81 && result==0x29)) return false;
    *success=result==0x11; return true;
}
quint16 NetworkProtocol::crc16(const QByteArray &bytes) {
    quint16 crc = 0xffff;
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
QVector<NetworkFrame> NetworkProtocol::feed(const QByteArray &bytes) {
    QVector<NetworkFrame> frames;
    lastFeedRejection_={};
    const auto recordRejection=[this](const QString &reason) {
        if(!lastFeedRejection_.isEmpty()) return;
        lastFeedRejection_={{"reason",reason},{"bufferedBytes",buffer_.size()},
            {"candidateHex",QString::fromLatin1(buffer_.left(1034).toHex(' '))}};
        if(buffer_.size()>=5) lastFeedRejection_.insert("declaredLength",int(u16(buffer_,3)));
        if(buffer_.size()>=3) lastFeedRejection_.insert("command",quint8(buffer_[2]));
        if(buffer_.size()>=7) lastFeedRejection_.insert("cycleIndex",int(u16(buffer_,5)));
    };
    // Bytewise accumulation bounds the cache even for hostile or corrupt streams.
    for (char byte : bytes) {
        buffer_.append(byte);
        for (;;) {
            if(buffer_.isEmpty()) break;
            const int start = buffer_.indexOf(char(0x55));
            if (start < 0) { recordRejection("no_frame_header"); rejectedBytes_ += buffer_.size(); buffer_.clear(); break; }
            if (start > 0) { recordRejection("bytes_before_header"); rejectedBytes_ += start; buffer_.remove(0, start); }
            if (buffer_.size() < 5) break;
            const int length = u16(buffer_, 3);
            const int size = length + 8;
            const bool waveform=quint8(buffer_[1])==0x20
                && (quint8(buffer_[2])==0x81 || quint8(buffer_[2])==0x82);
            if (length < 3 || length > (waveform ? MaximumWaveformPayload+2 : 1026)) {
                recordRejection("length_out_of_range");
                ++rejectedBytes_; buffer_.remove(0, 1); continue;
            }
            if (buffer_.size() < size) break;
            const quint16 receivedCrc=u16(buffer_,size-3);
            const quint16 calculatedCrc=crc16(buffer_.mid(1,size-4));
            // User-confirmed legacy receiver compatibility: only uploaded waveforms
            // record CRC without using it as an acceptance condition. Control ACKs
            // and status frames remain strictly checked, including command 0x81/action 0x10.
            if (quint8(buffer_[size - 1]) != 0xaa
                || (!waveform && receivedCrc!=calculatedCrc)) {
                recordRejection(quint8(buffer_[size-1])!=0xaa ? "frame_tail_mismatch" : "crc_mismatch");
                if(lastFeedRejection_.value("candidateHex").toString()==QString::fromLatin1(buffer_.left(1034).toHex(' '))) {
                    lastFeedRejection_.insert("receivedCrc",int(u16(buffer_,size-3)));
                    lastFeedRejection_.insert("calculatedCrc",int(crc16(buffer_.mid(1,size-4))));
                }
                ++rejectedBytes_; buffer_.remove(0, 1); continue;
            }
            frames.push_back({quint8(buffer_[1]), quint8(buffer_[2]),
                quint8(buffer_[5]), quint8(buffer_[6]), buffer_.mid(7, length - 2), buffer_.left(size),
                receivedCrc,calculatedCrc,!waveform});
            buffer_.remove(0, size);
        }
    }
    return frames;
}
bool NetworkProtocol::decodeStatus(const NetworkFrame &frame, NetworkStatus *status) {
    // 2026-09-09 revised protocol: 21 payload bytes, LEN = 21 + 2.
    // Page 14's "data length 23" includes COUNT/INDEX; the table and capture agree on 21.
    if (!status || frame.action != 0x20 || frame.command != 0x01
        || frame.count != 1 || frame.index != 1 || frame.payload.size() != 21) return false;
    const auto &data = frame.payload;
    const auto experiment = quint8(data[9]);
    if ((experiment != 0x01 && experiment != 0x00) || u16(data, 0) > 3000) return false;
    *status = {u16(data, 0), u16(data, 2), experiment == 0x01};
    return true;
}
}
