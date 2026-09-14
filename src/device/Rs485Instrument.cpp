#include "device/Rs485Instrument.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <cmath>

namespace qitest {
Rs485Instrument::Rs485Instrument(QObject *parent)
    : Rs485Instrument(new QSerialPort, parent) { transport_->setParent(this); }

Rs485Instrument::Rs485Instrument(QIODevice *transport, QObject *parent)
    : IInstrumentAdapter(parent), transport_(transport), serial_(qobject_cast<QSerialPort *>(transport)) {
    Q_ASSERT(transport_);
    pollTimer_.setSingleShot(true);
    mainFreshTimer_.setSingleShot(true); mainFreshTimer_.setInterval(5000);
    connect(&mainFreshTimer_, &QTimer::timeout, this, [this] {
        clearMainReadings(); message_ = "主控板读数超时，等待新的485状态"; emit stateChanged();
    });
    timeout_.setSingleShot(true);
    timeout_.setInterval(1500);
    connect(&pollTimer_, &QTimer::timeout, this, &Rs485Instrument::query);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!basicRequestId_.isEmpty()) {
            fail("485参数应答超时，已停止后续下发并关闭串口"); return;
        }
        if (activeQuery_ >= 0) {
            pending_ = false; pumpEnabled_ = false; nextQuery_ = -1;
            pump_.record("EVENT", {}, "分子泵查询超时 " + QString::fromLatin1(PumpProtocol::parameter(activeQuery_)));
            pump_.invalidate("分子泵超时，已暂停泵查询；主控板继续读取。可导出报文或重新连接。");
            pollTimer_.start(200); emit stateChanged(); return;
        }
        // No request ID exists on this protocol. Close on timeout; do not let a
        // late response be mistaken for the response to an automatic new query.
        fail("485查询超时，读数已失效；请检查设备后重新连接");
    });
    connect(transport_, &QIODevice::readyRead, this, &Rs485Instrument::receive);
    if (serial_) {
        serial_->setReadBufferSize(4096);
        connect(serial_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
            if (!closing_ && !basicRequestId_.isEmpty() && error != QSerialPort::NoError) {
                const QString message="485串口错误："+serial_->errorString();
                finishBasicParameters(false,message);fail(message);
            } else if (!closing_ && pending_ && error != QSerialPort::NoError)
                fail("485串口错误：" + serial_->errorString());
            else if (!closing_ && serial_->isOpen() && error == QSerialPort::ResourceError)
                fail("485设备已断开：" + serial_->errorString());
        });
    }
}

Rs485Instrument::~Rs485Instrument() {
    closing_ = true;
    pollTimer_.stop(); timeout_.stop(); mainFreshTimer_.stop();
    if (transport_->isOpen()) transport_->close();
}

QStringList Rs485Instrument::availablePorts() {
    QStringList ports;
    for (const auto &port : QSerialPortInfo::availablePorts()) ports << port.portName();
    ports.sort(Qt::CaseInsensitive);
    return ports;
}

bool Rs485Instrument::openPort(const QString &name, bool includePump) {
    closePort();
    pumpEnabled_ = includePump; nextQuery_ = activeQuery_ = -1;
    pump_.resetSession(includePump);
    portName_ = name.trimmed();
    if (portName_.isEmpty()) { fail("请选择485串口"); return false; }
    if (serial_) {
        serial_->setPortName(portName_);
        if (!serial_->setBaudRate(QSerialPort::Baud9600)
            || !serial_->setDataBits(QSerialPort::Data8)
            || !serial_->setParity(QSerialPort::NoParity)
            || !serial_->setStopBits(QSerialPort::OneStop)
            || !serial_->setFlowControl(QSerialPort::NoFlowControl)) {
            fail("无法设置9600、8N1、无流控：" + serial_->errorString()); return false;
        }
    }
    if (!transport_->open(QIODevice::ReadWrite)) {
        fail("无法打开" + portName_ + "：" + transport_->errorString()); return false;
    }
    message_ = portName_ + "已打开，等待485状态回读 · 只读";
    emit stateChanged();
    pollTimer_.start(0);
    return true;
}

void Rs485Instrument::clearReadings() {
    pending_ = false;
    decoder_.reset();
    pumpEnabled_ = false; nextQuery_ = activeQuery_ = -1;
    pump_.invalidate("485已断开，分子泵读数已失效");
    clearMainReadings();
    basicQueue_.clear(); basicIndex_=-1; basicRequestId_.clear(); basicParameters_={};
}
void Rs485Instrument::clearMainReadings() {
    health_ = unavailableRs485Health();
    telemetry_ = unavailableRs485Telemetry();
    status_ = {};
    lastReadback_ = {};
}

void Rs485Instrument::closePort() { fail("485未连接 · 只读状态"); }

void Rs485Instrument::fail(const QString &message) {
    if (closing_) return;
    const QString interruptedRequest=basicRequestId_;
    closing_ = true;
    pollTimer_.stop(); timeout_.stop(); mainFreshTimer_.stop();
    if (transport_->isOpen()) transport_->close();
    clearReadings();
    message_ = message;
    closing_ = false;
    emit stateChanged();
    if(!interruptedRequest.isEmpty())
        emit basicMethodParametersFinished(interruptedRequest,false,{},message);
}

void Rs485Instrument::query() {
    if (!transport_->isOpen() || pending_ || !basicRequestId_.isEmpty()) return;
    decoder_.reset(); pump_.resetDecoder();
    // Drop unsolicited / late bytes accumulated before this query.
    if (serial_) serial_->clear(QSerialPort::Input);
    else transport_->readAll();
    pending_ = true; activeQuery_ = nextQuery_;
    timeout_.start();
    const auto request = activeQuery_ < 0 ? Rs485Protocol::statusQuery() : PumpProtocol::query(activeQuery_);
    pump_.record("TX", request, activeQuery_ < 0 ? "主控板状态查询" : "分子泵原始值查询");
    if (transport_->write(request) != request.size()) fail("485查询发送失败，读数已失效");
}

void Rs485Instrument::receive() {
    if (!transport_->isOpen()) return;
    const auto bytes = transport_->read(4096);
    pump_.record("RX", bytes, "共用485接收字节块");
    if (!basicRequestId_.isEmpty()) {
        for (const auto &frame : decoder_.feed(bytes)) {
            if (basicIndex_<0 || basicIndex_>=basicQueue_.size()) continue;
            bool success=false;
            if (!Rs485Protocol::decodeAcknowledgement(frame,basicQueue_[basicIndex_].first,&success)) continue;
            timeout_.stop();
            if (!success) { finishBasicParameters(false,"485设备拒绝基本设置，后续参数未发送"); return; }
            ++basicIndex_;
            if (basicIndex_>=basicQueue_.size()) finishBasicParameters(true);
            else QTimer::singleShot(40,this,&Rs485Instrument::sendNextBasicParameter);
            return;
        }
        if (transport_->isOpen() && transport_->bytesAvailable()>0)
            QTimer::singleShot(0,this,&Rs485Instrument::receive);
        return;
    }
    if (!pending_) return;
    if (activeQuery_ >= 0) {
        if (pump_.consume(bytes, activeQuery_)) {
            pending_ = false; timeout_.stop();
            nextQuery_ = activeQuery_ == 3 ? -1 : activeQuery_ + 1;
            pollTimer_.start(200);
        }
        emit stateChanged();
    } else {
        for (const auto &frame : decoder_.feed(bytes)) {
            Rs485Status next;
            if (!pending_ || frame.command != 0x30 || !Rs485Protocol::decodeStatus(frame.payload, &next)) continue;
            pending_ = false; timeout_.stop();
            status_ = next;
            health_ = unavailableRs485Health();
            health_.connected = true; // ready remains false: 485 does not provide MS acquisition/interlocks.
            // User corrected 2026-09-10: raw HV readback / 10 is ion-source V.
            health_.ionSourceKv = next.highVoltageV / 10000.0; // Preserve the shared kV contract.
            health_.tdTemperatureC = next.tdTemperatureC;
            health_.carrierGasMlMin = next.efcMlMin;
            telemetry_ = unavailableRs485Telemetry();
            telemetry_.ionSourceVoltageV = next.highVoltageV / 10.0;
            telemetry_.tdTemperatureC = next.tdTemperatureC;
            telemetry_.ionTrapTemperatureC = next.trapTemperatureC;
            telemetry_.carrierGasFlowMlMin = next.efcMlMin;
            telemetry_.carrierGasPressureTorr = next.gasPressureTorr;
            telemetry_.carrierGasMode = next.externalCarrierGas ? "外载气" : "内载气";
            lastReadback_ = QDateTime::currentDateTime();
            mainFreshTimer_.start(); nextQuery_ = pumpEnabled_ ? 0 : -1;
            pollTimer_.start(pumpEnabled_ ? 200 : 1000);
            message_ = portName_ + " · 485回读正常 · 只读，采集未接入";
            emit stateChanged();
        }
    }
    if (transport_->isOpen() && transport_->bytesAvailable() > 0)
        QTimer::singleShot(0, this, &Rs485Instrument::receive);
}

CommandValidation Rs485Instrument::validateBasicMethodParameters(const QJsonObject &values) const {
    if (!transport_->isOpen() || !health_.connected) return {false,"485尚未收到有效状态回读"};
    if (!basicRequestId_.isEmpty()) return {false,"485正在处理上一套参数"};
    const auto exact=[&values](const char *key,double minimum,double maximum,int scale=1) {
        const auto value=values.value(QLatin1String(key)); const double number=value.toDouble(-1);
        return value.isDouble() && std::isfinite(number) && number>=minimum && number<=maximum
            && std::abs(number*scale-std::round(number*scale))<1e-6;
    };
    if (!exact("carrier",0,50,1000)) return {false,"载气流速需为 0～50 mL/min，分辨率 0.001"};
    if (!exact("extraction",0,100) || !exact("inlet",0,100))
        return {false,"抽气与进气流速需为 0～100 的整数百分比"};
    if (!exact("td",0,65535) || !exact("trap",0,65535))
        return {false,"TD 与离子阱温度需为协议范围内的整数"};
    if (!exact("source",0,0))
        return {false,"当前485协议未定义离子源电压写命令，请将其保持为 0"};
    return {true,{}};
}

bool Rs485Instrument::requestBasicMethodParameters(const QString &requestId,
                                                   const QJsonObject &values, QString *error) {
    const auto validation=validateBasicMethodParameters(values);
    if (requestId.isEmpty() || !validation.allowed) {
        if (error) *error=requestId.isEmpty()?"方法请求号为空":validation.reason;
        return false;
    }
    const auto u16=[](int value){QByteArray b;b.append(char(value>>8));b.append(char(value&0xff));return b;};
    // Deterministic order: stop on the first negative/timeout ACK.
    basicQueue_={{0x02,u16(int(std::llround(values.value("td").toDouble())))},
                 {0x13,u16(int(std::llround(values.value("trap").toDouble())))},
                 {0x12,u16(int(std::llround(values.value("carrier").toDouble()*1000.0)))},
                 {0x04,u16(int(std::llround(values.value("extraction").toDouble())))},
                 {0x14,u16(int(std::llround(values.value("inlet").toDouble())))}};
    pollTimer_.stop(); timeout_.stop(); pending_=false; decoder_.reset();
    if (serial_) serial_->clear(QSerialPort::Input); else transport_->readAll();
    basicRequestId_=requestId; basicParameters_=values; basicIndex_=0;
    sendNextBasicParameter();
    return true;
}

void Rs485Instrument::cancelBasicMethodParameters(const QString &requestId) {
    if(requestId.isEmpty() || requestId!=basicRequestId_)return;
    timeout_.stop();basicRequestId_.clear();basicParameters_={};basicQueue_.clear();basicIndex_=-1;
    decoder_.reset();pending_=false;
    if(transport_->isOpen())pollTimer_.start(200);
}

void Rs485Instrument::sendNextBasicParameter() {
    if (basicRequestId_.isEmpty() || basicIndex_<0 || basicIndex_>=basicQueue_.size()
        || !transport_->isOpen()) return;
    decoder_.reset();
    const auto item=basicQueue_[basicIndex_]; const auto wire=Rs485Protocol::controlCommand(item.first,item.second);
    pump_.record("TX",wire,QString("主控板参数设置 0x%1").arg(item.first,2,16,QLatin1Char('0')));
    timeout_.start();
    if (transport_->write(wire)!=wire.size()) {
        const QString message="485参数发送失败，后续参数未发送";
        finishBasicParameters(false,message);fail(message);
    }
}

void Rs485Instrument::finishBasicParameters(bool success, const QString &error) {
    const QString id=basicRequestId_; const QJsonObject values=basicParameters_;
    timeout_.stop(); basicRequestId_.clear(); basicParameters_={}; basicQueue_.clear(); basicIndex_=-1;
    decoder_.reset();
    emit basicMethodParametersFinished(id,success,success?values:QJsonObject{},error);
    if (transport_->isOpen()) pollTimer_.start(200);
}

InstrumentTelemetry Rs485Instrument::telemetry() const {
    auto result = telemetry_;
    const auto pump = pump_.statusDetails();
    // Compose at read time: a main-board reply must not clear pump values,
    // and a pump timeout must not leave converted values in the main cache.
    if (pump.contains("molecularPumpRpm")) result.molecularPumpRpm = pump.value("molecularPumpRpm").toDouble();
    if (pump.contains("molecularPumpCurrentA")) result.molecularPumpCurrentA = pump.value("molecularPumpCurrentA").toDouble();
    if (pump.contains("molecularPumpVoltageV")) result.molecularPumpVoltageV = pump.value("molecularPumpVoltageV").toDouble();
    if (pump.contains("molecularPumpTemperatureC")) result.molecularPumpTemperatureC = pump.value("molecularPumpTemperatureC").toDouble();
    return result;
}

InstrumentDescriptor Rs485Instrument::descriptor() const {
    return {"便携式质谱 · 485只读", {}, "rs485-status-23-v1", false};
}

QVariantMap Rs485Instrument::confirmedSettings() const {
    if (!health_.connected) return {};
    // Only unambiguous matching UI switches. Temperatures/flows are measurements,
    // not setpoint acknowledgements; carrier selection is not a gas on/off switch.
    return {{"observationLightOn", status_.observationLightOn}, {"wastePumpOn", status_.wastePumpOn}};
}

CommandValidation Rs485Instrument::validate(const InstrumentCommand &command) const {
    if (command.id == "ReadHealth" && health_.connected) return {true, {}};
    return {false, "485当前仅支持只读状态；采集与硬件控制未开放"};
}
CommandValidation Rs485Instrument::validateSetting(const QString &, const QVariant &) const {
    return {false, "485当前为只读接入，不发送硬件控制命令"};
}
void Rs485Instrument::requestSetting(const QString &id, const QString &key, const QVariant &) {
    emit settingFinished(id, key, false, {}, "485当前为只读接入，不发送硬件控制命令");
}

QVariantMap Rs485Instrument::statusDetails() const {
    QVariantMap result{{"port", portName_}, {"open", transport_->isOpen()},
        {"connected", health_.connected}, {"message", message_}, {"pumpEnabled", pumpEnabled_}};
    if (!health_.connected) return result;
    result.insert("lastReadback", lastReadback_.toString("HH:mm:ss"));
    result.insert("highVoltageV", status_.highVoltageV); // Legacy key: raw board value, not volts.
    result.insert("ionSourceKv", health_.ionSourceKv);
    result.insert("ionSourceVoltageV", telemetry_.ionSourceVoltageV);
    result.insert("highVoltageCurrentUa", status_.highVoltageCurrentUa);
    result.insert("vacuumGaugeMv", status_.vacuumGaugeMv);
    result.insert("gasPumpPwmPercent", status_.gasPumpPwmPercent);
    result.insert("observationLightOn", status_.observationLightOn);
    result.insert("heatingOn", status_.heatingOn);
    result.insert("wastePumpOn", status_.wastePumpOn);
    result.insert("externalCarrierGas", status_.externalCarrierGas);
    result.insert("hv24VOn", status_.hv24VOn);
    result.insert("rf24VOn", status_.rf24VOn);
    result.insert("diaphragmPumpOn", status_.diaphragmPumpOn);
    return result;
}
QVariantMap Rs485Instrument::pumpStatusDetails() const {
    auto data = pump_.statusDetails();
    data.insert("open", portOpen()); data.insert("enabled", pumpEnabled_); data.insert("port", portName_);
    return data;
}
} // namespace qitest
