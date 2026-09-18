#include "device/Rs485Instrument.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#include "device/VendorControlCatalog.h"

namespace qitest {
Rs485Instrument::Rs485Instrument(QObject *parent)
    : Rs485Instrument(new QSerialPort, parent) { transport_->setParent(this); }

Rs485Instrument::Rs485Instrument(QIODevice *transport, QObject *parent)
    : IInstrumentAdapter(parent), transport_(transport), serial_(qobject_cast<QSerialPort *>(transport)) {
    Q_ASSERT(transport_);
    controlDeadline_.setSingleShot(true); controlDeadline_.setInterval(4500);
    controlDeadline_.setTimerType(Qt::PreciseTimer);
    connect(&controlDeadline_, &QTimer::timeout, this, [this] {
        if(controlStage_==4 && !controlWaitForRest_ && !pumpTurnaround_
            && portOpen() && health_.connected && pumpFreshTimer_.isActive()
            && pump_.statusDetails().contains("molecularPumpCurrentA")) {
            pumpConfirmationExpired();return;
        }
        fail("控制回读未确认，已停止后续步骤；设备实际状态请核对");
    });
    pumpFreshTimer_.setSingleShot(true); pumpFreshTimer_.setInterval(5000);
    connect(&pumpFreshTimer_, &QTimer::timeout, this, [this] {
        pump_.invalidate("分子泵读数超时，状态未知"); emit stateChanged();
    });
    pollTimer_.setSingleShot(true);
    mainFreshTimer_.setSingleShot(true); mainFreshTimer_.setInterval(5000);
    connect(&mainFreshTimer_, &QTimer::timeout, this, [this] {
        clearMainReadings(); message_ = "主控板读数超时，等待新的485状态"; emit stateChanged();
    });
    timeout_.setSingleShot(true);
    timeout_.setInterval(1500);
    connect(&pollTimer_, &QTimer::timeout, this, &Rs485Instrument::query);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(pumpTurnaround_) {fail("分子泵启停后串口持续有数据，未能恢复查询；已停止后续步骤");return;}
        if (!controlId_.isEmpty() && controlStage_ >= 2) {
            fail("控制指令或状态回读超时，设备状态未知"); return;
        }
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
    controlHistory_.clear();
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
    message_ = portName_ + "已打开，等待485状态回读";
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
    controlDeadline_.stop(); pumpFreshTimer_.stop(); confirmedSetpoints_.clear();
    controlId_.clear(); controlKey_.clear(); controlStage_=0; controlWaitForRest_=false; pumpTurnaround_=false; controlWire_.clear();
}
void Rs485Instrument::clearMainReadings() {
    health_ = unavailableRs485Health();
    telemetry_ = unavailableRs485Telemetry();
    status_ = {};
    lastReadback_ = {};
}

void Rs485Instrument::closePort() { fail("485已断开",false); }

void Rs485Instrument::fail(const QString &message,bool abnormal) {
    if (closing_) return;
    if(abnormal) recordFailure(message);
    const QString interruptedRequest=basicRequestId_;
    const QString controlId=controlId_, controlKey=controlKey_;
    if(!controlId.isEmpty()) recordControl("FAILED",{{"error",message}});
    closing_ = true;
    pollTimer_.stop(); timeout_.stop(); mainFreshTimer_.stop();
    if (transport_->isOpen()) transport_->close();
    clearReadings();
    message_ = message;
    if(abnormal) saveFailureDiagnostics();
    closing_ = false;
    emit stateChanged();
    if(!controlId.isEmpty()) emit settingFinished(controlId,controlKey,false,{},message);
    if(!interruptedRequest.isEmpty())
        emit basicMethodParametersFinished(interruptedRequest,false,{},message);
}
void Rs485Instrument::recordFailure(const QString &message) {
        lastFailure_={{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"reason",message},{"activeQuery",activeQuery_},{"pendingQuery",pending_},
            {"controlKey",controlKey_},{"controlStage",controlStage_},
            {"lastReadback",lastReadback_.toString(Qt::ISODateWithMs)},
            {"controlHex",QString::fromLatin1(controlWire_.toHex(' '))}};
        pump_.record("EVENT",{},message);
}
void Rs485Instrument::saveFailureDiagnostics() {
        QString directory=qEnvironmentVariable("QITEST_DIAGNOSTICS_DIR");
        // In-memory test transports only write when explicitly given a temporary directory.
        if(directory.isEmpty() && serial_) directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/diagnostics/485";
        if(!directory.isEmpty()) {
            diagnosticError_.clear();
            diagnosticPath_=QDir(directory).filePath("485-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+".json");
            if(!QDir().mkpath(directory) || !exportFrames(diagnosticPath_,&diagnosticError_)) {
                if(diagnosticError_.isEmpty()) diagnosticError_="无法创建诊断目录";
                diagnosticPath_.clear();
            }
        }
}
void Rs485Instrument::pumpConfirmationExpired() {
    const QString error=controlValue_.toBool()
        ?"分子泵启动未确认：电流仍为0 A；已停止后续开机步骤，485保持连接"
        :"分子泵停止未确认：电流仍大于0 A；485保持连接，请核对设备";
    recordFailure(error);recordControl("FAILED",{{"error",error}});
    const QString id=controlId_,key=controlKey_;
    controlDeadline_.stop();
    controlId_.clear();controlKey_.clear();controlStage_=0;controlWaitForRest_=false;
    // Only the requested control failed. An in-flight status reply still owns
    // the bus: preserve its parser, pending flag and per-query timeout.
    if(!pending_) {timeout_.stop();nextQuery_=-1;pollTimer_.start(200);}
    message_=error;saveFailureDiagnostics();
    emit settingFinished(id,key,false,{},error);
    emit stateChanged();
}

void Rs485Instrument::query() {
    if (!transport_->isOpen() || pending_ || !basicRequestId_.isEmpty()) return;
    if(pumpTurnaround_) {pumpTurnaround_=false;timeout_.stop();}
    if(controlStage_==1) { sendControl(); return; }
    decoder_.reset(); pump_.resetDecoder();
    // Drop unsolicited / late bytes accumulated before this query.
    if (serial_) serial_->clear(QSerialPort::Input);
    else transport_->readAll();
    pending_ = true; activeQuery_ = controlStage_==3 ? -1 : nextQuery_;
    timeout_.start();
    const auto request = activeQuery_ < 0 ? Rs485Protocol::statusQuery() : PumpProtocol::query(activeQuery_);
    pump_.record("TX", request, activeQuery_ < 0 ? "主控板状态查询" : "分子泵原始值查询");
    if (transport_->write(request) != request.size()) fail("485查询发送失败，读数已失效");
}

void Rs485Instrument::receive() {
    if (!transport_->isOpen()) return;
    const auto bytes = transport_->read(4096);
    pump_.record("RX", bytes, "共用485接收字节块");
    if(pumpTurnaround_) {
        // Pump power writes may elicit bytes even though no dedicated ACK is
        // required. Let that traffic drain before another device owns the bus.
        // Every received chunk restarts the same 200 ms quiet gap as normal polling.
        if(!bytes.isEmpty()) pollTimer_.start(200);
        if(transport_->bytesAvailable()>0) QTimer::singleShot(0,this,&Rs485Instrument::receive);
        return;
    }
    if(controlStage_==2) {
        for(const auto &frame : decoder_.feed(bytes)) {
            bool success=false;
            if(!Rs485Protocol::decodeAcknowledgement(frame,controlCommand_,&success)) continue;
            recordControl("ACK",{{"command",frame.command},{"payloadHex",QString::fromLatin1(frame.payload.toHex(' '))},{"accepted",success}});
            timeout_.stop(); pending_=false;
            if(!success) { finishControl(false,"设备拒绝控制指令"); return; }
            if(controlKey_=="diaphragmPumpOn" || controlKey_=="internalCarrierGasOn" || controlKey_=="ionHighVoltageOn" || controlKey_=="rfOn" || controlKey_=="heatingOn") {
                controlStage_=3; pollTimer_.start(40);
            } else {
                confirmedSetpoints_.insert(controlKey_,controlValue_);
                finishControl(true);
            }
            return;
        }
        if(transport_->bytesAvailable()>0) QTimer::singleShot(0,this,&Rs485Instrument::receive);
        return;
    }
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
            if(activeQuery_==1) pumpFreshTimer_.start();
            if(controlStage_==4) {
                const auto readings=pump_.statusDetails();
                const double current=readings.value("molecularPumpCurrentA").toDouble();
                if(controlWaitForRest_) {
                    // Both readings were invalidated when STOP was sent. Zero current
                    // alone cannot stop the backing pump while the rotor coasts.
                    if(readings.contains("molecularPumpCurrentA") && readings.contains("molecularPumpRpm")
                        && current==0 && readings.value("molecularPumpRpm").toDouble()==0) {
                        finishControl(true);emit stateChanged();return;
                    }
                } else {
                    if((current>0)==controlValue_.toBool()) { finishControl(true); emit stateChanged(); return; }
                    // Keep main-board telemetry fresh while the pump current settles.
                    // Repeating only 310 could expire the board's five-second lease
                    // and make the startup coordinator cancel a healthy serial link.
                    nextQuery_=-1; pollTimer_.start(200); emit stateChanged(); return;
                }
            }
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
            mainFreshTimer_.start();
            nextQuery_ = controlStage_==4 && !controlWaitForRest_ ? 1 : pumpEnabled_ ? 0 : -1;
            pollTimer_.start(pumpEnabled_ ? 200 : 1000);
            message_ = portName_ + " · 485回读正常";
            if(controlStage_==3) {
                const bool matches=controlReadbackMatches();
                if(matches) finishControl(true);
                else pollTimer_.start(100); // Allow physical state to settle, within the original deadline.
            }
            emit stateChanged();
        }
    }
    if (transport_->isOpen() && transport_->bytesAvailable() > 0)
        QTimer::singleShot(0, this, &Rs485Instrument::receive);
}

CommandValidation Rs485Instrument::validateBasicMethodParameters(const QJsonObject &values) const {
    if (!transport_->isOpen() || !health_.connected) return {false,"485尚未收到有效状态回读"};
    if (!basicRequestId_.isEmpty() || settingBusy()) return {false,"485正在处理上一项设置"};
    const auto exact=[&values](const char *key,double minimum,double maximum,int scale=1) {
        const auto value=values.value(QLatin1String(key)); const double number=value.toDouble(-1);
        return value.isDouble() && std::isfinite(number) && number>=minimum && number<=maximum
            && std::abs(number*scale-std::round(number*scale))<1e-6;
    };
    if (!exact("carrier",0,50,10)) return {false,"载气流速需为 0～50 mL/min，分辨率 0.1"};
    if (!exact("extraction",0,100) || !exact("inlet",0,100))
        return {false,"抽气与进气流速需为 0～100 的整数百分比"};
    if (!exact("td",0,65535) || !exact("trap",0,65535))
        return {false,"TD 与离子阱温度需为协议范围内的整数"};
    // Ion-source voltage belongs to TCP 0x50 and is validated by NetworkInstrument.
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
                 {0x12,u16(int(std::llround(values.value("carrier").toDouble()*10.0)))},
                 {0x04,u16(int(std::llround(values.value("extraction").toDouble())))},
                 {0x14,u16(int(std::llround(values.value("inlet").toDouble())))}};
    pollTimer_.stop(); timeout_.stop(); pending_=false; decoder_.reset();
    if (serial_) serial_->clear(QSerialPort::Input); else transport_->readAll();
    basicRequestId_=requestId; basicParameters_=values; basicIndex_=0;
    basicParameters_.remove("source"); // A serial ACK cannot confirm a TCP voltage setting.
    confirmedSetpoints_.remove("tdTemperatureC"); confirmedSetpoints_.remove("trapTemperatureC");
    confirmedSetpoints_.remove("efcMlMin");
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
    if(success) {
        confirmedSetpoints_.insert("tdTemperatureC",values.value("td").toDouble());
        confirmedSetpoints_.insert("trapTemperatureC",values.value("trap").toDouble());
        confirmedSetpoints_.insert("efcMlMin",values.value("carrier").toDouble());
    }
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
    auto result=confirmedSetpoints_;
    result.insert("observationLightOn",status_.observationLightOn);
    result.insert("wastePumpOn",status_.wastePumpOn);
    result.insert("diaphragmPumpOn",status_.diaphragmPumpOn);
    result.insert("ionHighVoltageOn",status_.hv24VOn);
    result.insert("rfOn",status_.rf24VOn);
    result.insert("heatingOn",status_.heatingOn);
    result.insert("internalCarrierGasOn",!status_.externalCarrierGas);
    const auto pump=pump_.statusDetails();
    if(pump.contains("molecularPumpCurrentA"))
        result.insert("molecularPumpOn",pump.value("molecularPumpCurrentA").toDouble()>0);
    return result;
}

CommandValidation Rs485Instrument::validate(const InstrumentCommand &command) const {
    if(command.id=="ReadHealth" && health_.connected) return {true,{}};
    return {false,"此入口不支持该仪器操作"};
}
bool Rs485Instrument::supportsSetting(const QString &key) {
    return key=="diaphragmPumpOn" || key=="internalCarrierGasOn" || key=="molecularPumpOn"
        || key=="tdTemperatureC" || key=="trapTemperatureC" || key=="efcMlMin" || key=="ionHighVoltageOn" || key=="rfOn" || key=="heatingOn";
}
CommandValidation Rs485Instrument::validateSetting(const QString &key,const QVariant &value) const {
    if(!supportsSetting(key)) return {false,"该部件控制尚未接入"};
    if(!portOpen() || !health_.connected) return {false,"485尚未收到有效状态回读"};
    if(settingBusy() || !basicRequestId_.isEmpty()) return {false,"485正在等待上一项设置"};
    const bool toggle=key.endsWith("On");
    if(toggle && value.userType()!=QMetaType::Bool) return {false,"开关值须为是或否"};
    if(key=="heatingOn" && value.toBool()) return {false,"加热开启请使用温度设定"};
    if(key=="molecularPumpOn" && !pumpEnabled_) return {false,"请在485连接中启用分子泵读取"};
    if(!toggle) {
        QByteArray bytes; QString error;
        if(!VendorControlCatalog::controlPayload(key,value,&bytes,&error)) return {false,error};
        if(key!="efcMlMin" && value.toDouble()>999) return {false,"温度设定超过允许范围"};
    }
    return {true,{}};
}
void Rs485Instrument::requestSetting(const QString &id,const QString &key,const QVariant &value) {
    queueSetting(id,key,value,false);
}
void Rs485Instrument::requestPumpShutdown(const QString &id) {
    queueSetting(id,"molecularPumpOn",false,true);
}
void Rs485Instrument::queueSetting(const QString &id,const QString &key,const QVariant &value,bool waitForRest) {
    const auto validation=validateSetting(key,value);
    if(id.isEmpty() || !validation.allowed) {
        emit settingFinished(id,key,false,{},id.isEmpty()?"请求号为空":validation.reason); return;
    }
    controlId_=id; controlKey_=key; controlValue_=value; controlStage_=1;
    controlWaitForRest_=waitForRest;
    controlDeadline_.start(key=="molecularPumpOn" && value.toBool()?10000:4500);
    // A query already in flight owns the bus until its reply has been consumed.
    if(!pending_) { pollTimer_.stop(); sendControl(); }
}
void Rs485Instrument::sendControl() {
    if(controlStage_!=1 || pending_ || !transport_->isOpen()) return;
    if(!health_.connected || (controlKey_=="molecularPumpOn" && !pumpEnabled_)) {
        finishControl(false,"有效设备回读已失效，未发送控制指令"); return;
    }
    decoder_.reset(); pump_.resetDecoder();
    if(serial_) serial_->clear(QSerialPort::Input); else transport_->readAll();
    if(controlKey_=="molecularPumpOn") {
        controlWire_=PumpProtocol::powerCommand(controlValue_.toBool());
        controlStage_=4;
        pumpTurnaround_=true;timeout_.start();
        nextQuery_=1;
        if(controlWaitForRest_) {controlDeadline_.stop();nextQuery_=-1;}
        pump_.invalidate("启停指令已发送，等待新的电流回读"); pumpFreshTimer_.stop();
    } else {
        QByteArray payload;
        if(controlKey_=="heatingOn") {
            controlCommand_=0x08;payload.append(char(0x02));
            confirmedSetpoints_.remove("tdTemperatureC");confirmedSetpoints_.remove("trapTemperatureC");
        } else if(controlKey_=="diaphragmPumpOn") {
            controlCommand_=0x11; payload.append(char(controlValue_.toBool()?0x01:0x02));
        } else if(controlKey_=="internalCarrierGasOn") {
            controlCommand_=0x03; payload.append(char(controlValue_.toBool()?0x02:0x01));
        } else {
            const auto *spec=VendorControlCatalog::find(controlKey_);
            controlCommand_=spec->command; VendorControlCatalog::controlPayload(controlKey_,controlValue_,&payload);
        }
        controlWire_=Rs485Protocol::controlCommand(controlCommand_,payload);
        controlStage_=2; timeout_.start();
    }
    pump_.record("TX",controlWire_,"部件控制："+controlKey_);
    recordControl("TX_ATTEMPT",{{"hex",QString::fromLatin1(controlWire_.toHex(' '))}});
    if(transport_->write(controlWire_)!=controlWire_.size()) { fail("部件控制发送失败"); return; }
    if(controlStage_==4) pollTimer_.start(200);
}
bool Rs485Instrument::controlReadbackMatches() const {
    if(controlKey_=="heatingOn") return status_.heatingOn==controlValue_.toBool();
    if(controlKey_=="rfOn") return status_.rf24VOn==controlValue_.toBool();
    if(controlKey_=="ionHighVoltageOn") return status_.hv24VOn==controlValue_.toBool();
    return controlKey_=="diaphragmPumpOn" ? status_.diaphragmPumpOn==controlValue_.toBool()
        : !status_.externalCarrierGas==controlValue_.toBool();
}
void Rs485Instrument::finishControl(bool success,const QString &error) {
    recordControl("FINISHED",{{"success",success},{"error",error}});
    const QString id=controlId_,key=controlKey_; const QVariant value=controlValue_;
    controlDeadline_.stop(); timeout_.stop(); pending_=false;
    controlId_.clear(); controlKey_.clear(); controlStage_=0; controlWaitForRest_=false; pumpTurnaround_=false; decoder_.reset();
    if(!success) confirmedSetpoints_.remove(key);
    nextQuery_=-1; if(transport_->isOpen()) pollTimer_.start(40);
    emit settingFinished(id,key,success,success?value:QVariant(),error);
    emit stateChanged();
}
void Rs485Instrument::recordControl(const QString &event,const QVariantMap &details) {
    QVariantMap item=details;
    item.insert("time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    item.insert("event",event);item.insert("requestId",controlId_);
    item.insert("key",controlKey_);item.insert("target",controlValue_);
    controlHistory_.append(item);
    if(controlHistory_.size()>96) controlHistory_.removeFirst();
}
void Rs485Instrument::cancelSetting(const QString &id) {
    cancelSetting(id,"控制确认已取消，设备状态请核对后重新连接");
}
void Rs485Instrument::cancelSetting(const QString &id,const QString &reason) {
    if(id.isEmpty() || id!=controlId_) return;
    if(controlStage_==1) finishControl(false,"控制请求已取消");
    else fail(reason);
}

QVariantMap Rs485Instrument::statusDetails() const {
    QVariantMap result{{"port", portName_}, {"open", transport_->isOpen()},
        {"connected", health_.connected}, {"message", message_}, {"pumpEnabled", pumpEnabled_}};
    // Preserve control evidence after a timeout and independently of polling's ring buffer.
    result.insert("controlHistory",controlHistory_);
    result.insert("confirmedSetpoints",confirmedSetpoints_);
    result.insert("lastFailure",lastFailure_);
    result.insert("diagnosticPath",diagnosticPath_);
    result.insert("diagnosticError",diagnosticError_);
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
