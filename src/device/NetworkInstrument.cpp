#include "device/NetworkInstrument.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QFileInfo>
#include <QUuid>

namespace qitest {
NetworkInstrument::NetworkInstrument(std::unique_ptr<Rs485Instrument> serial, QObject *parent)
    : IInstrumentAdapter(parent), serial_(serial ? std::move(serial) : std::make_unique<Rs485Instrument>()) {
    connect(serial_.get(), &IInstrumentAdapter::stateChanged, this, [this] {
        if(shutdownBusy()) {
            if(!serial_->health().connected) finishShutdown(false,"485回读失效，关机流程已停止");
            else {
                if(shutdown_.stage()==ShutdownSequence::Stage::Cooling
                    && (!serial_->confirmedSettings().value("molecularPumpOn").isValid()
                        || !serial_->pumpStatusDetails().contains("molecularPumpRpm")))
                    finishShutdown(false,"分子泵回读失效，关机流程已停止");
                else if(shutdown_.stage()==ShutdownSequence::Stage::Cooling
                    && serial_->confirmedSettings().value("heatingOn").toBool())
                    finishShutdown(false,"加热状态重新开启，关机流程已停止");
                else {
                    shutdown_.observeTemperature(serial_->telemetry().ionTrapTemperatureC,true);
                    advanceShutdown();
                }
            }
        }
        startup_.observeCarrierFlow(serial_->health().carrierGasMlMin,serial_->health().connected);
        const auto stage=startup_.stage();
        if(stage==StartupSequence::Stage::PreparingRunning || stage==StartupSequence::Stage::WaitingForVacuum || stage==StartupSequence::Stage::StartingTrapHeating
            || stage==StartupSequence::Stage::Complete) {
            const auto pump=serial_->confirmedSettings().value("molecularPumpOn");
            if(!pump.isValid() || !pump.toBool()) {
                startup_.observeCarrierFlow(0,false);
                if(startupBusy()) stopStartup("分子泵电流为零或读数失效，已停止后续开机步骤");
            }
        }
        if(startupBusy() && !serial_->health().connected) stopStartup("485状态失效，已停止后续开机步骤");
        advanceStartup(); emit stateChanged();
    });
    connect(serial_.get(), &IInstrumentAdapter::settingFinished, this,
        [this](const QString &id,const QString &key,bool success,const QVariant &actual,const QString &error) {
            if(id==shutdownStepId_ && !id.isEmpty()) {
                shutdownStepId_.clear();
                if(!success) {finishShutdown(false,error);return;}
                shutdown_.confirmed(key,success,actual);advanceShutdown();emit stateChanged();return;
            }
            // Child shutdown requests never escape as separate user operations,
            // including a late failure emitted after stateChanged aborted the sequence.
            if(id.startsWith("shutdown-step/")) return;
            if(id==startupStepId_ && !id.isEmpty()) {
                startupStepId_.clear(); startupStepKey_.clear();
                if(!success) { stopStartup(key=="trapTemperatureC"?"离子阱85℃设定失败："+error:error); return; }
                if(startupPreparingMode_) { startupPreparingMode_=false; startup_.begin(); }
                else startup_.confirmed(key,actual,success);
                advanceStartup(); emit stateChanged(); return;
            }
            if(id.startsWith("startup-step/")) return;
            emit settingFinished(id,key,success,actual,error);
        });
    connect(serial_.get(), &Rs485Instrument::basicMethodParametersFinished, this,
        [this](const QString &id,bool success,const QJsonObject &,const QString &error) {
            if (id!=methodRequestId_ || id.isEmpty()) return;
            if (!success) { finishMethod(false,error); return; }
            sendFullscanMethod();
        });
    server_.setMaxPendingConnections(1);
    pinchTimer_.setSingleShot(true); pinchTimer_.setInterval(3000);
    connect(&pinchTimer_,&QTimer::timeout,this,[this] {
        closePeer("夹管阀应答超时，状态未知；已隔离旧连接，请重连后核对");
    });
    heartbeatTimer_.setInterval(2000);
    heartbeatTimer_.setTimerType(Qt::PreciseTimer);
    connect(&heartbeatTimer_, &QTimer::timeout, this, &NetworkInstrument::sendHeartbeat);
    acquisitionAckTimer_.setSingleShot(true); acquisitionAckTimer_.setInterval(3000);
    acquisitionDurationTimer_.setSingleShot(true); acquisitionDurationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&acquisitionAckTimer_,&QTimer::timeout,this,[this] {
        closePeer("检测指令应答超时，仪器运行状态未知，请核对仪器后重新连接");
    });
    connect(&acquisitionDurationTimer_,&QTimer::timeout,this,[this]{stopAcquisition(false);});
    stopRetryTimer_.setSingleShot(true); stopRetryTimer_.setInterval(500);
    stopRetryTimer_.setTimerType(Qt::PreciseTimer);
    stopReplyGuardTimer_.setSingleShot(true); stopReplyGuardTimer_.setInterval(3000);
    connect(&stopRetryTimer_,&QTimer::timeout,this,[this] {
        if(acquisitionState_==3 && stopCommandsSent_==1 && !sendDetection(false))
            closePeer("检测关闭重发失败，仪器可能仍在运行");
    });
    connect(&server_, &QTcpServer::newConnection, this, &NetworkInstrument::acceptConnection);
    staleTimer_.setSingleShot(true);
    connect(&staleTimer_, &QTimer::timeout, this, [this] {
        fresh_ = false; status_ = {}; if(!acquisitionBusy()) decoder_.reset();
        pinchConfirmed_=QVariant();
        if(!pinchRequestId_.isEmpty()) closePeer("网口状态失效，夹管阀操作未确认");
        startup_.observeVacuum(0,false); stopStartup("网口真空读数超时，开机流程已停止");
        if(shutdownBusy()) finishShutdown(false,"网口回读超时，关机流程已停止");
        recordConnectionEvent("status_timeout", peer_);
        message_ = "网口状态超时，读数已失效；等待新的有效状态报文";
        emit stateChanged();
    });
    pressureTimer_.setSingleShot(true);
    connect(&pressureTimer_, &QTimer::timeout, this, [this] {
        if(!pressureRunTrace_) {pressureVolts_.clear();pressureCycle_=-1;}
        emit stateChanged();
    });
    tuningTimer_.setSingleShot(true); tuningTimer_.setInterval(3000);
    connect(&tuningTimer_, &QTimer::timeout, this, [this] {
        tuningPending_=false;
        closePeer("调谐指令应答超时，设备状态未知；已隔离旧连接，请重连后核对");
        tuningMessage_="调谐应答超时，设备可能仍在运行；重连后可发送结束"; emit stateChanged();
    });
    methodTimer_.setSingleShot(true); methodTimer_.setInterval(3000);
    methodFollowupDelay_.setSingleShot(true); methodFollowupDelay_.setInterval(100);
    connect(&methodFollowupDelay_,&QTimer::timeout,this,&NetworkInstrument::sendLegacyMethodFollowup);
    connect(&methodTimer_,&QTimer::timeout,this,[this]{
        finishMethod(false,"方法设置应答超时，设备状态未知；已关闭网口连接");
        closePeer("方法设置应答超时，等待仪器重新连接");
    });
    // Raw spectra can arrive rapidly; keep GUI refresh bounded to 10 Hz.
    updateTimer_.setSingleShot(true); updateTimer_.setInterval(100);
    connect(&updateTimer_, &QTimer::timeout, this, &IInstrumentAdapter::stateChanged);
}
NetworkInstrument::~NetworkInstrument() {
    QObject::disconnect(this, nullptr, nullptr, nullptr);
    stopListening();
}
void NetworkInstrument::notify() { if (!updateTimer_.isActive()) updateTimer_.start(); }
void NetworkInstrument::sendHeartbeat() {
    if (!heartbeatTimer_.isActive() || heartbeatPausedForMethod_ || !methodRequestId_.isEmpty()
        || acquisitionState_==3
        || !peer_ || peer_->state()!=QAbstractSocket::ConnectedState) return;
    // Never accumulate heartbeats behind a stalled TCP write buffer.
    if (peer_->bytesToWrite()>0) return;
    const auto wire=NetworkProtocol::heartbeatCommand();
    recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x30},
        {"heartbeat",true},{"hex",QString::fromLatin1(wire.toHex(' '))}});
    if(recentFrames_.size()>256) recentFrames_.removeFirst();
    if(peer_->write(wire)!=wire.size()) {
        closePeer("心跳发送失败，等待仪器重新连接"); return;
    }
    ++heartbeatSent_;
}
void NetworkInstrument::recordConnectionEvent(const QString &event, const QTcpSocket *socket) {
    connectionEvents_.append(QJsonObject{
        {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"event", event},
        {"peer", socket ? socket->peerAddress().toString() : QString()},
        {"port", socket ? int(socket->peerPort()) : 0}});
    if (connectionEvents_.size() > 64) connectionEvents_.removeFirst();
}
bool NetworkInstrument::startListening(const QString &address, quint16 port, int staleMs) {
    QHostAddress local;
    if (!local.setAddress(address.trimmed()) || staleMs < 1000 || staleMs > 3600000) {
        message_ = "监听失败：请输入有效的本机IP和1～3600秒状态超时";
        emit stateChanged(); return false;
    }
    stopListening();
    receivedBytes_ = validFrames_ = unparsedFrames_ = 0;
    heartbeatSent_ = 0;
    heartbeatReplies_=0;recentRawReceives_.clear();errorRawReceives_.clear();
    waveformCrcMismatches_=0;
    recentRawBytes_=0;acquisitionParseFailure_={};
    acceptedConnections_ = replacedConnections_ = rejectedConnections_ = 0;
    connectionEvents_.clear();
    recentFrames_.clear(); decoder_ = {}; pressureFrameCount_=0;
    staleTimer_.setInterval(staleMs); pressureTimer_.setInterval(staleMs);
    if (!server_.listen(local, port)) {
        message_ = "网口监听失败：" + server_.errorString();
        emit stateChanged(); return false;
    }
    message_ = QString("正在监听 %1:%2，等待仪器连接 · 等待回读").arg(address).arg(server_.serverPort());
    emit stateChanged(); return true;
}
void NetworkInstrument::closePeer(const QString &message) {
    const QString pinchId=pinchRequestId_;
    pinchTimer_.stop(); pinchRequestId_.clear(); pinchConfirmed_=QVariant();
    startup_.observeVacuum(0,false); stopStartup(message);
    if(shutdownBusy()) finishShutdown(false,message);
    heartbeatTimer_.stop(); heartbeatPausedForMethod_=false;
    if(acquisitionBusy()) finishNetworkAcquisition(false,message);
    stopRetryTimer_.stop(); stopReplyGuardTimer_.stop();
    methodFollowupDelay_.stop();
    pressureTimer_.stop(); pressureVolts_.clear(); pressureCycle_=-1;
    pressureAcquisitionCompleted_=false;
    pressureRunTrace_=false;pressureDisplayLimit_=false;pressureSampleIntervalMinutes_=0;
    if(peer_ || !confirmedMethodParameters_.isEmpty())
        methodConfirmationReason_="网口连接变化后需重新确认方法："+message;
    confirmedMethodParameters_={};
    tuningTimer_.stop(); tuningPending_=false; tuningMessage_="连接已关闭，调谐状态未知";
    if (!methodRequestId_.isEmpty()) {
        serial_->cancelBasicMethodParameters(methodRequestId_);
        finishMethod(false,"网口连接中断，方法设置未确认");
    }
    staleTimer_.stop(); decoder_.reset(); fresh_ = false; status_ = {}; lastReadback_ = {};
    if (peer_) {
        recordConnectionEvent("closed", peer_);
        auto *old = peer_; peer_ = nullptr;
        QObject::disconnect(old, nullptr, this, nullptr);
        old->abort(); old->deleteLater();
    }
    peerAddress_.clear(); message_ = message; emit stateChanged();
    if(!pinchId.isEmpty()) emit settingFinished(pinchId,"pinchValveOn",false,{},message);
}
void NetworkInstrument::stopListening() {
    server_.close(); updateTimer_.stop(); closePeer("网口已停止监听");
}
void NetworkInstrument::acceptConnection() {
    while (server_.hasPendingConnections()) {
        auto *socket = server_.nextPendingConnection();
        if (!socket) continue;
        if (peer_) {
            if (socket->peerAddress() != peer_->peerAddress()) {
                ++rejectedConnections_;
                recordConnectionEvent("rejected_other_ip", socket);
                socket->abort(); socket->deleteLater(); continue;
            }
            // The observed firmware reconnects from a new source port without closing the old TCP session.
            // Only the same peer IP may replace it. Invalidate all old data and partial frames first.
            ++replacedConnections_;
            recordConnectionEvent("replaced_same_ip", socket);
            closePeer("仪器重新连接，旧网口读数已失效；等待新连接回读");
            if (!server_.isListening()) { socket->abort(); socket->deleteLater(); return; }
        }
        peer_ = socket; peer_->setReadBufferSize(65536);
        ++acceptedConnections_;
        recordConnectionEvent("accepted", socket);
        peerAddress_ = peer_->peerAddress().toString();
        decoder_.reset(); fresh_ = false; lastReadback_ = {};
        message_ = "TCP已连接 " + peerAddress_ + "，等待有效状态报文";
        connect(peer_, &QTcpSocket::readyRead, this, [this, socket] {
            if (peer_ == socket) receive();
        });
        connect(peer_, &QTcpSocket::disconnected, this, [this, socket] {
            if (peer_ == socket) closePeer("仪器TCP已断开，读数已失效；继续监听等待重连");
        });
        connect(peer_,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            &QTcpSocket::errorOccurred,
#else
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
#endif
            this,
            [this, socket](QAbstractSocket::SocketError) {
                if (peer_ == socket) closePeer("网口连接错误：" + peer_->errorString() + "；继续监听");
            });
        heartbeatPausedForMethod_=false; heartbeatTimer_.start();
        staleTimer_.start(); emit stateChanged();
        receive();
    }
}
void NetworkInstrument::receive() {
    if (!peer_) return;
    const auto bytes = peer_->read(65536);
    if(!bytes.isEmpty()) {
        recentRawReceives_.append({QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
            peerAddress_,int(peer_->peerPort()),receivedBytes_,bytes});
        recentRawBytes_+=bytes.size();
        while(recentRawReceives_.size()>128 || recentRawBytes_>1024*1024)
            recentRawBytes_-=recentRawReceives_.takeFirst().bytes.size();
    }
    receivedBytes_ += bytes.size();
    const auto rejectedBefore=decoder_.rejectedBytes();
    const auto frames=decoder_.feed(bytes);
    if(acquisitionBusy() && decoder_.rejectedBytes()!=rejectedBefore) {
        if(acquisitionParseFailure_.isEmpty()) {
            acquisitionParseFailure_=decoder_.lastFeedRejection();
            acquisitionParseFailure_.insert("time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            acquisitionParseFailure_.insert("rejectedBytesInRead",double(decoder_.rejectedBytes()-rejectedBefore));
            acquisitionParseFailure_.insert("methodParameters",confirmedMethodParameters_);
        }
        QString error="检测报文格式错误，本次数据不完整";
        const auto reason=acquisitionParseFailure_.value("reason").toString();
        if(reason=="crc_mismatch") {
            error=QString("网口0x%1 CRC校验不一致：收到%2，计算应为%3；本次检测未完成")
                .arg(acquisitionParseFailure_.value("command").toInt(),2,16,QLatin1Char('0'))
                .arg(acquisitionParseFailure_.value("receivedCrc").toInt(),4,16,QLatin1Char('0'))
                .arg(acquisitionParseFailure_.value("calculatedCrc").toInt(),4,16,QLatin1Char('0'));
        } else if(reason=="frame_tail_mismatch") error="网口报文帧尾不匹配，本次检测未完成";
        else if(reason=="length_out_of_range") error="网口报文长度超出协议范围，本次检测未完成";
        failAcquisition(error);
    }
    for (const auto &frame : frames) {
        ++validFrames_;
        if(!frame.crcRequired && frame.receivedCrc!=frame.calculatedCrc) ++waveformCrcMismatches_;
        NetworkStatus next;
        const bool decoded = NetworkProtocol::decodeStatus(frame, &next);
        QVector<double> pressure;
        const bool pressureDecoded=NetworkProtocol::decodePressure(frame,&pressure);
        if(pressureDecoded) {
            const bool collecting=acquisitionState_==2 || acquisitionState_==3;
            if(collecting && acquisitionError_.isEmpty() && frame.cycleIndex()!=nextPressureCycle_)
                failAcquisition(QString("气压周期编号不连续：应为%1，收到%2；本次检测未完成")
                    .arg(nextPressureCycle_).arg(frame.cycleIndex()));
            else if(!collecting || acquisitionError_.isEmpty()) {
                if(collecting) ++nextPressureCycle_;
                // Keep every sample of the run; do not overwrite earlier peaks with the last cycle.
                // Outside an acquisition, a reset/gap starts a new display segment.
                if(!pressureRunTrace_ && frame.cycleIndex()!=pressureCycle_ && frame.cycleIndex()!=pressureCycle_+1) pressureVolts_.clear();
                if(!pressureRunTrace_) pressureSampleIntervalMinutes_=NetworkProtocol::pressureSampleIntervalMinutes(confirmedMethodParameters_);
                if(frame.cycleIndex()!=pressureCycle_ && (!pressureRunTrace_ || frame.cycleIndex()==pressureCycle_+1)) {
                    constexpr int maximumPressurePoints=1000000;
                    const int room=maximumPressurePoints-pressureVolts_.size();
                    pressureVolts_+=pressure.mid(0,room);
                    if(room<pressure.size())pressureDisplayLimit_=true;
                    pressureCycle_=frame.cycleIndex();
                }
                ++pressureFrameCount_;
                // A final pressure frame may follow the stop acknowledgement in the same read.
                if(!pressureAcquisitionCompleted_) pressureTimer_.start();
            }
        }
        const bool tuningAck=frame.action==0x10 && frame.command==0x20 && frame.count==1 && frame.index==1
            && frame.payload.size()==1 && (quint8(frame.payload[0])==0x11 || quint8(frame.payload[0])==0x12);
        bool pinchSuccess=false;
        const bool pinchAck=NetworkProtocol::decodeCommandAcknowledgement(frame,0x22,&pinchSuccess);
        if(pinchAck && !pinchRequestId_.isEmpty())
            finishPinchValve(pinchSuccess,pinchSuccess?QString():QString("设备拒绝夹管阀指令，实际状态未知"));
        if(tuningAck && tuningPending_) {
            tuningTimer_.stop(); tuningPending_=false;
            tuningMessage_=quint8(frame.payload[0])==0x11
                ? (tuningTarget_ ? "设备已应答调谐开启成功；RF曲线换算待确认" : "设备已应答调谐关闭成功")
                : "设备拒绝调谐指令，实际状态未知";
        }
        bool methodSucceeded=false;
        bool heartbeatAccepted=false;
        const bool heartbeatReply=NetworkProtocol::decodeCommandAcknowledgement(frame,0x30,&heartbeatAccepted);
        if(heartbeatReply) ++heartbeatReplies_;
        const bool methodAck=NetworkProtocol::decodeCommandAcknowledgement(frame,0x81,&methodSucceeded);
        if (methodAck && !methodRequestId_.isEmpty() && methodStage_==1 && methodTimer_.isActive()) {
            if(!methodSucceeded) finishMethod(false,quint8(frame.payload[0])==0x29
                ? "设备返回0x29：冷却时间错误，方法设置失败" : "设备拒绝 Fullscan 方法参数");
            else {
                methodTimer_.stop(); methodStage_=2; methodFollowupDelay_.start();
            }
        }
        bool followupSucceeded=false;
        const bool followupAck=NetworkProtocol::decodeCommandAcknowledgement(frame,0x50,&followupSucceeded);
        if(followupAck && !methodRequestId_.isEmpty() && methodStage_==3 && methodTimer_.isActive()) {
            methodTimer_.stop();
            if(!followupSucceeded) {
                finishMethod(false,"设备拒绝方法后续0x50指令，方法未完整应用");
                closePeer("方法后续指令失败，请重连后重新设置方法");
            } else if(methodFollowupsSent_<3) sendLegacyMethodFollowup();
            else finishMethod(true);
        }
        const bool acquisitionFrame=(frame.action==0x20 && frame.command==0x81)
            || (frame.action==0x10 && frame.command==0x15);
        if (decoded) {
            status_ = next; fresh_ = true; lastReadback_ = QDateTime::currentDateTime();
            staleTimer_.start(); message_ = "网口状态回读正常；Fullscan 方法与定时采集可用";
            startup_.observeVacuum(NetworkProtocol::vacuumMbarFromRaw(status_.vacuumRaw),true);
            advanceStartup();
        } else if(!pressureDecoded && !tuningAck && !pinchAck && !methodAck && !followupAck && !acquisitionFrame && !heartbeatReply) ++unparsedFrames_;
        recentFrames_.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"peer", peerAddress_}, {"direction", "RX"}, {"decodedStatus", decoded}, {"decodedPressure", pressureDecoded}, {"tuningAck", tuningAck}, {"methodAck",methodAck},
            {"heartbeatReply",heartbeatReply},{"methodFollowupAck",followupAck},{"action", frame.action}, {"command", frame.command}, {"frameCount", frame.count},
            {"frameIndex", frame.index}, {"hex", QString::fromLatin1(frame.wire.toHex(' '))}});
        recentFrames_.last().insert("crcRequired",frame.crcRequired);
        recentFrames_.last().insert("crcMatches",frame.receivedCrc==frame.calculatedCrc);
        recentFrames_.last().insert("receivedCrc",int(frame.receivedCrc));
        recentFrames_.last().insert("calculatedCrc",int(frame.calculatedCrc));
        if(frame.action==0x20 && (frame.command==0x81 || frame.command==0x82)) {
            recentFrames_.last().remove("frameCount");recentFrames_.last().remove("frameIndex");
            recentFrames_.last().insert("cycleIndex",frame.cycleIndex());
        }
        if (recentFrames_.size() > 256) recentFrames_.removeFirst();
        if(acquisitionFrame) receiveAcquisition(frame);
    }
    notify();
    if (peer_ && peer_->bytesAvailable() > 0) QTimer::singleShot(0, this, &NetworkInstrument::receive);
}
bool NetworkInstrument::requestTuning(bool enabled, QString *error) {
    const auto fail=[error](const QString &message){if(error)*error=message;return false;};
    if(settingBusy()) return fail("请等待开机或部件操作完成");
    if(acquisitionBusy()) return fail("检测期间不能切换调谐");
    if(!peer_ || peer_->state()!=QAbstractSocket::ConnectedState) return fail("请先连接网口仪器");
    if(tuningPending_ || !methodRequestId_.isEmpty()) return fail("正在等待上一条设备指令应答");
    if(enabled && (!fresh_ || status_.experimentRunning)) return fail("需有效网口状态且实验已停止，才能开始调谐");
    tuningTarget_=enabled; tuningPending_=true; tuningMessage_=enabled?"等待设备确认调谐开启":"等待设备确认调谐关闭";
    const auto wire=NetworkProtocol::tuningCommand(enabled);
    recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x20},{"hex",QString::fromLatin1(wire.toHex(' '))}});
    if(recentFrames_.size()>256)recentFrames_.removeFirst();
    tuningTimer_.start();
    if(peer_->write(wire)!=wire.size()) {
        closePeer("调谐指令发送失败，设备状态未知"); return fail("调谐指令发送失败");
    }
    emit stateChanged();return true;
}
bool NetworkInstrument::setCalibrationProfile(const QJsonObject &profile,QString *error) {
    if(calibrationBusy()){if(error)*error="请结束检测、调谐或待确认操作后再同步校准";return false;}
    if(profile==calibrationProfile_)return true;
    // A damaged saved profile remains invalid, blocking method submission rather than silently using defaults.
    calibrationProfile_=profile;
    confirmedMethodParameters_={};
    methodConfirmationReason_="质量轴校准已变更，须重新设为当前方法";
    emit stateChanged();return true;
}
CommandValidation NetworkInstrument::validateMethodParameters(const QJsonObject &parameters) const {
    if(settingBusy()) return {false,"请等待开机或部件操作完成"};
    if(acquisitionBusy()) return {false,"检测期间不能修改方法"};
    if (!peer_ || peer_->state()!=QAbstractSocket::ConnectedState || !fresh_)
        return {false,"网口尚未收到有效状态回读"};
    if (status_.experimentRunning) return {false,"检测运行中不能修改方法"};
    if (tuningPending_ || !methodRequestId_.isEmpty()) return {false,"正在等待上一条设备指令应答"};
    QString error;
    if (NetworkProtocol::fullscanMethodCommand(parameters,&error,calibrationProfile_).isEmpty()) return {false,error};
    if (!parameters.value("source").isDouble()) return {false,"离子源电压须填写数值"};
    if (NetworkProtocol::ionSourceVoltageCommand(parameters.value("source").toDouble(),&error).isEmpty())
        return {false,error};
    const auto serialValidation=serial_->validateBasicMethodParameters(parameters);
    if (!serialValidation.allowed) return serialValidation;
    return {true,{}};
}
void NetworkInstrument::requestMethodParameters(const QString &requestId,const QJsonObject &parameters) {
    const auto validation=validateMethodParameters(parameters);
    if (requestId.isEmpty() || !validation.allowed) {
        emit methodParametersFinished(requestId,false,{},requestId.isEmpty()?"方法请求号为空":validation.reason); return;
    }
    QString error; const auto wire=NetworkProtocol::fullscanMethodCommand(parameters,&error,calibrationProfile_);
    if (wire.isEmpty()) { emit methodParametersFinished(requestId,false,{},error); return; }
    methodRequestId_=requestId; pendingMethodParameters_=parameters; pendingMethodWire_=wire;
    methodStage_=0; methodFollowupsSent_=0;
    heartbeatTimer_.stop(); heartbeatPausedForMethod_=true;
    recordConnectionEvent("heartbeat_paused_method",peer_);
    if (!serial_->requestBasicMethodParameters(requestId,parameters,&error)) finishMethod(false,error);
}
void NetworkInstrument::sendFullscanMethod() {
    if (methodRequestId_.isEmpty() || !peer_ || peer_->state()!=QAbstractSocket::ConnectedState
        || !fresh_ || status_.experimentRunning) {
        finishMethod(false,"网口状态已失效或检测已经开始，Fullscan 参数未发送"); return;
    }
    recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x81},
        {"hex",QString::fromLatin1(pendingMethodWire_.toHex(' '))}});
    if (recentFrames_.size()>256) recentFrames_.removeFirst();
    methodStage_=1; methodTimer_.start();
    if (peer_->write(pendingMethodWire_)!=pendingMethodWire_.size()) {
        finishMethod(false,"Fullscan 方法参数发送失败"); closePeer("方法参数发送失败，等待仪器重连");
    }
}
void NetworkInstrument::sendLegacyMethodFollowup() {
    if(methodRequestId_.isEmpty() || (methodStage_!=2 && methodStage_!=3)) return;
    if(!peer_ || peer_->state()!=QAbstractSocket::ConnectedState) {
        closePeer("方法后续指令发送失败，等待仪器重连"); return;
    }
    QString error;
    const auto wire=NetworkProtocol::ionSourceVoltageCommand(pendingMethodParameters_.value("source").toDouble(),&error);
    if(wire.isEmpty()) {finishMethod(false,error);return;}
    ++methodFollowupsSent_; methodStage_=3;
    recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x50},
        {"methodFollowup",methodFollowupsSent_},{"hex",QString::fromLatin1(wire.toHex(' '))}});
    if(recentFrames_.size()>256) recentFrames_.removeFirst();
    methodTimer_.start();
    if(peer_->write(wire)!=wire.size()) closePeer("方法后续指令发送失败，等待仪器重连");
}
void NetworkInstrument::finishMethod(bool success,const QString &error) {
    if (methodRequestId_.isEmpty()) return;
    const QString id=methodRequestId_; const QJsonObject values=pendingMethodParameters_;
    methodTimer_.stop(); methodFollowupDelay_.stop(); methodStage_=0;
    methodRequestId_.clear(); pendingMethodParameters_={}; pendingMethodWire_.clear();
    if (success) {confirmedMethodParameters_=values;methodConfirmationReason_="当前连接的方法已确认";}
    emit methodParametersFinished(id,success,success?values:QJsonObject{},error);
    // The synchronous controller slot publishes the success notice before restarting.
    // A rejection stays paused; a retry can resume only after its own success ACK.
    if(success && peer_ && peer_->state()==QAbstractSocket::ConnectedState && methodRequestId_.isEmpty()) {
        heartbeatPausedForMethod_=false; heartbeatTimer_.start();
        recordConnectionEvent("heartbeat_resumed_method",peer_);
    }
    emit stateChanged();
}
InstrumentDescriptor NetworkInstrument::descriptor() const {
    return {"便携式质谱 · 网口/485", {}, "tcp-v1.4-fullscan81+rs485-v2.1-method", false};
}
InstrumentHealth NetworkInstrument::health() const {
    auto value = serial_->health();
    value.connected = value.connected || fresh_;
    value.ready = fresh_ && !confirmedMethodParameters_.isEmpty() && !tuningPending_ && methodRequestId_.isEmpty();
    if (fresh_) value.vacuumMbar = NetworkProtocol::vacuumMbarFromRaw(status_.vacuumRaw);
    return value;
}
InstrumentTelemetry NetworkInstrument::telemetry() const {
    auto value = serial_->telemetry();
    if (fresh_) {
        value.multiplierVoltageV = status_.multiplierVoltageV;
        value.vacuumMbar = NetworkProtocol::vacuumMbarFromRaw(status_.vacuumRaw);
    }
    return value;
}
QString NetworkInstrument::connectionSummary() const {
    return message_ + (serial_->portOpen() ? "；" + serial_->connectionSummary() : QString());
}
CommandValidation NetworkInstrument::validate(const InstrumentCommand &command) const {
    if (command.id == "ReadHealth" && health().connected) return {true, {}};
    if(command.id=="CancelAcquisition" && acquisitionBusy()) return {true,{}};
    return {false, "此入口仅支持状态读取；调谐请使用射频页，定时采集请使用样品分析页"};
}
CommandValidation NetworkInstrument::validateSetting(const QString &key, const QVariant &value) const {
    if(settingBusy() || acquisitionBusy() || tuningPending_ || !methodRequestId_.isEmpty())
        return {false,"请等待当前设备操作完成"};
    if(key=="ionHighVoltageOn" || key=="rfOn") {
        if(fresh_ && status_.experimentRunning) return {false,"检测运行中不能切换电源"};
        return serial_->validateSetting(key,value);
    }
    if(!fresh_ || status_.experimentRunning) return {false,"需要有效网口状态且检测已停止"};
    if(key=="pinchValveOn") {
        if(value.userType()!=QMetaType::Bool) return {false,"夹管阀开关须为是或否"};
        return {true,{}};
    }
    if(key=="powerOn") {
        if(value.userType()!=QMetaType::Bool) return {false,"启停值须为是或否"};
        if(!value.toBool()) {
            const auto check=serial_->validateSetting("molecularPumpOn",false);
            if(!check.allowed) return check;
            const auto parts=serial_->confirmedSettings(), readings=serial_->pumpStatusDetails();
            if(!parts.value("molecularPumpOn").isValid() || !readings.contains("molecularPumpRpm"))
                return {false,"请等待分子泵电流和转速回读"};
            if(!parts.value("heatingOn").toBool() && !parts.value("diaphragmPumpOn").toBool()
                && !parts.value("molecularPumpOn").toBool() && readings.value("molecularPumpRpm").toDouble()==0)
                return {false,"加热和真空泵已关闭"};
            return {true,{}};
        }
        const auto pump=serial_->confirmedSettings().value("molecularPumpOn");
        if(!pump.isValid()) return {false,"请等待有效的分子泵电流回读，再判断是否需要开机"};
        if(pump.toBool()) {
            if(!observedPowerState().isValid()) return {false,"分子泵运行中，请先核对隔膜泵状态"};
            if(!runningStartupNeedsHeating()) return {false,startupTemperaturesConfirmed()
                ?"启动温度设定已确认，请核对实测温度；无需重复启动泵"
                :"设备已运行且无需补充升温，无需重复开机"};
            return serial_->validateSetting("tdTemperatureC",StartupSequence::TdTargetC);
        }
        return serial_->validateSetting("molecularPumpOn",true);
    }
    const double vacuum=health().vacuumMbar;
    if(key=="molecularPumpOn" && value.toBool() && !StartupSequence::molecularPumpStartPressureAllowed(vacuum))
        return {false,"真空度须小于8 mbar（8E0），才能开启分子泵"};
    if(key=="diaphragmPumpOn" && !value.toBool()) {
        const auto pump=serial_->confirmedSettings().value("molecularPumpOn");
        if(!pump.isValid() || pump.toBool()) return {false,"请先确认分子泵已关闭"};
    }
    if(key=="trapTemperatureC" && value.toDouble()>0) {
        const auto pump=serial_->confirmedSettings().value("molecularPumpOn");
        const auto health=serial_->health();
        if(!pump.isValid() || !pump.toBool() || !health.connected || !std::isfinite(health.carrierGasMlMin)
            || health.carrierGasMlMin<0 || !(vacuum>0 && vacuum<(health.carrierGasMlMin>0?1e-2:1e-4)))
            return {false,"真空系统尚未就绪，不能开启离子阱升温"};
    }
    return serial_->validateSetting(key,value);
}
void NetworkInstrument::requestSetting(const QString &id, const QString &key, const QVariant &value) {
    const auto check=validateSetting(key,value);
    if(id.isEmpty() || !check.allowed) { emit settingFinished(id,key,false,{},check.reason); return; }
    if(key=="pinchValveOn") {
        pinchRequestId_=id; pinchTarget_=value.toBool(); pinchConfirmed_=QVariant();
        const auto wire=NetworkProtocol::pinchValveCommand(pinchTarget_);
        recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x22},
            {"hex",QString::fromLatin1(wire.toHex(' '))}});
        if(recentFrames_.size()>256) recentFrames_.removeFirst();
        pinchTimer_.start();
        if(!peer_ || peer_->write(wire)!=wire.size()) closePeer("夹管阀指令发送失败，状态未知");
        emit stateChanged(); return;
    }
    if(key!="powerOn") { serial_->requestSetting(id,key,value); return; }
    if(!value.toBool()) {
        shutdown_=ShutdownSequence();shutdown_.begin();shutdownId_=id;
        advanceShutdown();emit stateChanged();return;
    }
    shutdown_=ShutdownSequence();
    startup_=StartupSequence(); startupId_=id;
    if(serial_->confirmedSettings().value("molecularPumpOn").toBool()) {
        startupPreparingMode_=false;
        const auto target=serial_->confirmedSettings().value("tdTemperatureC");
        startup_.resumeWithRunningPumps(target.isValid() && target.toDouble()==StartupSequence::TdTargetC);
        startup_.observeCarrierFlow(serial_->health().carrierGasMlMin,serial_->health().connected);
        startup_.observeVacuum(health().vacuumMbar,fresh_);
        advanceStartup();emit stateChanged();return;
    }
    startupPreparingMode_=true;
    startupStepId_="startup-step/"+QUuid::createUuid().toString(); startupStepKey_="internalCarrierGasOn";
    startup_.observeCarrierFlow(serial_->health().carrierGasMlMin,serial_->health().connected);
    serial_->requestSetting(startupStepId_,startupStepKey_,false); emit stateChanged();
}
QVariantMap NetworkInstrument::confirmedSettings() const {
    auto values=serial_->confirmedSettings();
    if(fresh_ && pinchConfirmed_.isValid()) values.insert("pinchValveOn",pinchConfirmed_);
    const auto power=observedPowerState();
    if(power.isValid()) values.insert("powerOn",power);
    return values;
}
void NetworkInstrument::finishPinchValve(bool success,const QString &error) {
    if(pinchRequestId_.isEmpty()) return;
    const QString id=pinchRequestId_; pinchRequestId_.clear(); pinchTimer_.stop();
    pinchConfirmed_=success?QVariant(pinchTarget_):QVariant();
    emit settingFinished(id,"pinchValveOn",success,pinchConfirmed_,error); emit stateChanged();
}
void NetworkInstrument::advanceShutdown() {
    if(!shutdownBusy() || !shutdownStepId_.isEmpty()) return;
    if(shutdown_.stage()==ShutdownSequence::Stage::Failed) {finishShutdown(false,shutdown_.error());return;}
    if(shutdown_.stage()==ShutdownSequence::Stage::Complete) {finishShutdown(true);return;}
    const auto key=shutdown_.pendingKey();if(key.isEmpty()) return;
    shutdownStepId_="shutdown-step/"+QUuid::createUuid().toString();
    const QString requestId=shutdownStepId_;
    if(key=="molecularPumpOn") serial_->requestPumpShutdown(requestId);
    else serial_->requestSetting(requestId,key,false);
}
void NetworkInstrument::finishShutdown(bool success,const QString &error) {
    if(!shutdownBusy()) return;
    const QString id=shutdownId_,step=shutdownStepId_;shutdownId_.clear();shutdownStepId_.clear();
    if(!success) {shutdown_.fail(error);serial_->cancelSetting(step,error);}
    emit settingFinished(id,"powerOn",success,success?QVariant(false):QVariant(),error);emit stateChanged();
}
void NetworkInstrument::advanceStartup() {
    if(!startupBusy() || !startupStepId_.isEmpty()) return;
    if(startup_.stage()==StartupSequence::Stage::Failed) { stopStartup(startup_.error()); return; }
    if(startup_.stage()==StartupSequence::Stage::Complete) {
        const QString id=startupId_; startupId_.clear();
        emit settingFinished(id,"powerOn",true,true,{}); return;
    }
    const auto actions=startup_.pendingActions();
    if(actions.isEmpty()) return;
    startupStepId_="startup-step/"+QUuid::createUuid().toString(); startupStepKey_=actions.first().key;
    serial_->requestSetting(startupStepId_,startupStepKey_,actions.first().value);
}
void NetworkInstrument::stopStartup(const QString &reason) {
    if(!startupBusy()) return;
    const QString id=startupId_,step=startupStepId_;
    startupId_.clear(); startupStepId_.clear(); startupStepKey_.clear(); startupPreparingMode_=false;
    startup_.fail(reason); serial_->cancelSetting(step,reason);
    emit settingFinished(id,"powerOn",false,{},reason); emit stateChanged();
}
void NetworkInstrument::cancelSetting(const QString &id) {
    if(!id.isEmpty() && id==pinchRequestId_) closePeer("夹管阀操作已取消，状态未知；需重新连接");
    else if(!id.isEmpty() && id==shutdownId_) finishShutdown(false,"已停止后续关机步骤，已执行的操作保持现状");
    else if(!id.isEmpty() && id==startupId_) stopStartup("已停止后续开机步骤；已启动的部件保持现状");
    else serial_->cancelSetting(id);
}
QVariant NetworkInstrument::observedPowerState() const {
    if(!fresh_ || !serial_->health().connected) return {};
    const auto parts=serial_->confirmedSettings();
    const auto diaphragm=parts.value("diaphragmPumpOn"), pump=parts.value("molecularPumpOn");
    if(!diaphragm.isValid() || !pump.isValid() || diaphragm.toBool()!=pump.toBool()) return {};
    return pump;
}
bool NetworkInstrument::startupTemperaturesConfirmed() const {
    const auto values=serial_->confirmedSettings();
    return values.value("tdTemperatureC").isValid() && values.value("trapTemperatureC").isValid()
        && values.value("tdTemperatureC").toDouble()==StartupSequence::TdTargetC
        && values.value("trapTemperatureC").toDouble()==StartupSequence::TrapTargetC;
}
bool NetworkInstrument::runningStartupNeedsHeating() const {
    const auto power=observedPowerState();const auto temperatures=serial_->telemetry();
    return power.isValid() && power.toBool() && !startupTemperaturesConfirmed()
        && std::isfinite(temperatures.tdTemperatureC) && std::isfinite(temperatures.ionTrapTemperatureC)
        // Residual heat after an interrupted startup does not confirm its settings.
        // Permit an explicit continuation without restarting already running pumps.
        && (startup_.stage()==StartupSequence::Stage::Failed
            || temperatures.tdTemperatureC<StartupSequence::TdTargetC-StartupSequence::TdToleranceC
            || temperatures.ionTrapTemperatureC<StartupSequence::TrapTargetC-StartupSequence::TrapToleranceC);
}
bool NetworkInstrument::observedVacuumReady() const {
    const auto power=observedPowerState();const auto state=serial_->health();
    const double pressure=health().vacuumMbar,flow=state.carrierGasMlMin;
    return power.isValid() && power.toBool() && std::isfinite(pressure) && pressure>0
        && std::isfinite(flow) && flow>=0 && pressure<(flow>0?1e-2:1e-4);
}
bool NetworkInstrument::operatingConditionsReady() const {
    const auto state=serial_->telemetry();
    return observedVacuumReady()
        && std::isfinite(state.tdTemperatureC) && std::isfinite(state.ionTrapTemperatureC)
        && std::abs(state.tdTemperatureC-StartupSequence::TdTargetC)<=StartupSequence::TdToleranceC
        && std::abs(state.ionTrapTemperatureC-StartupSequence::TrapTargetC)<=StartupSequence::TrapToleranceC;
}
QString NetworkInstrument::startupMessage() const {
    if(shutdownBusy()) {
        switch(shutdown_.stage()) {
        case ShutdownSequence::Stage::StoppingHeating:return "正在关闭TD和离子阱加热";
        case ShutdownSequence::Stage::Cooling:return QString("关机降温中，离子阱%1℃，等待低于75℃").arg(serial_->telemetry().ionTrapTemperatureC,0,'f',1);
        case ShutdownSequence::Stage::StoppingPump:return "分子泵正在停止，等待电流归零及转速0 RPM";
        case ShutdownSequence::Stage::StoppingDiaphragm:return "分子泵转速已归零，正在关闭隔膜泵";
        default:break;
        }
    }
    if(shutdown_.stage()==ShutdownSequence::Stage::Failed) return shutdown_.error();
    if(shutdown_.stage()==ShutdownSequence::Stage::Complete && observedPowerState().isValid()
        && !observedPowerState().toBool()) return "关机完成，加热和真空泵已关闭";
    if(startup_.stage()==StartupSequence::Stage::Failed) return startup_.error();
    if(!startupBusy()) {
        if(!fresh_ || !serial_->health().connected) return "等待网口和485有效回读";
        const auto parts=serial_->confirmedSettings();
        const auto pump=parts.value("molecularPumpOn");
        if(!pump.isValid()) return "等待分子泵电流回读";
        if(pump.toBool()) {
            if(!observedPowerState().isValid()) return "分子泵运行中，请核对隔膜泵状态";
            if(!observedVacuumReady()) return "设备运行中，真空尚未就绪";
            if(operatingConditionsReady()) return "设备运行中，真空和温度已达标";
            if(startupTemperaturesConfirmed())
                return QString("温度设定已确认（TD250℃/离子阱85℃），实测尚未达标");
            return runningStartupNeedsHeating()?"真空已就绪，点击继续开机完成升温":"设备运行中，真空已就绪，实测温度未达标";
        }
        if(startup_.stage()!=StartupSequence::Stage::Failed)
            return parts.value("diaphragmPumpOn").toBool()?"隔膜泵已开启，分子泵未开启":"真空泵已关闭";
    }
    if(startupPreparingMode_) return "正在设置外载气模式";
    switch(startup_.stage()) {
    case StartupSequence::Stage::Preparing:return "正在开启隔膜泵、TD升温至250℃";
    case StartupSequence::Stage::PreparingRunning:return "泵已运行，正在设置TD250℃，继续完成升温";
    case StartupSequence::Stage::WaitingForRoughVacuum:return "等待真空度小于8 mbar（8E0）";
    case StartupSequence::Stage::StartingPump:return "等待分子泵电流大于0";
    case StartupSequence::Stage::WaitingForVacuum:return serial_->health().carrierGasMlMin>0?"载气已开，等待E-03 mbar":"载气未开，等待E-05 mbar";
    case StartupSequence::Stage::StartingTrapHeating:return "真空已就绪，开启离子阱升温至85℃";
    case StartupSequence::Stage::Complete:return startup_.vacuumReady()?"真空已就绪，加热已启动":"加热已启动，真空尚未就绪";
    case StartupSequence::Stage::Failed:return startup_.error();
    default:return "尚未执行一键开机";
    }
}
QVariantMap NetworkInstrument::statusDetails() const {
    QVariantMap data{{"listening", server_.isListening()}, {"tcpConnected", peer_ != nullptr},
        {"connected", fresh_}, {"address", server_.serverAddress().toString()}, {"port", server_.serverPort()},
        {"peer", peerAddress_}, {"message", message_}, {"receivedBytes", receivedBytes_},
        {"peerPort", peer_ ? int(peer_->peerPort()) : 0},
        {"acceptedConnections", acceptedConnections_}, {"replacedConnections", replacedConnections_},
        {"rejectedConnections", rejectedConnections_},
        {"validFrames", validFrames_}, {"unparsedFrames", unparsedFrames_},
        {"rejectedBytes", decoder_.rejectedBytes()}, {"retainedFrames", recentFrames_.size()}};
    data.insert("startupStage",int(startup_.stage()));
    data.insert("massCalibrationProfile",calibrationProfile_.toVariantMap());
    data.insert("startupTemperaturesConfirmed",startupTemperaturesConfirmed());
    data.insert("resumeHeatingAvailable",runningStartupNeedsHeating() && validateSetting("powerOn",true).allowed);
    data.insert("shutdownBusy",shutdownBusy());
    data.insert("startupBusy",startupBusy()); data.insert("startupMessage",startupMessage());
    data.insert("pinchValvePending",!pinchRequestId_.isEmpty());
    data.insert("vacuumSystemReady",observedVacuumReady());
    data.insert("operatingConditionsReady",operatingConditionsReady());
    data.insert("pressureFrames",pressureFrameCount_); data.insert("pressurePoints",pressureVolts_.size());
    data.insert("pressureCycle",pressureCycle_);
    data.insert("pressureAcquisitionCompleted",pressureAcquisitionCompleted_);
    data.insert("pressureSampleIntervalMinutes",pressureSampleIntervalMinutes_);
    data.insert("pressureDisplayLimit",pressureDisplayLimit_);
    data.insert("waveformFormat","whole-cycle-u16be-legacy-crc-record-only");
    data.insert("waveformCrcPolicy","record_only");
    data.insert("waveformCrcMismatches",waveformCrcMismatches_);
    data.insert("tuningPending",tuningPending_);data.insert("tuningMessage",tuningMessage_);
    data.insert("methodPending",!methodRequestId_.isEmpty());
    data.insert("heartbeatIntervalMs",2000);
    data.insert("sendProfile","legacy-capture-20260916");
    data.insert("heartbeatPayload",0x23);
    data.insert("methodFollowupsSent",methodFollowupsSent_);
    data.insert("stopCommandsSent",stopCommandsSent_);
    data.insert("stopReplyGuardActive",stopReplyGuardTimer_.isActive());
    data.insert("heartbeatActive",heartbeatTimer_.isActive());
    data.insert("heartbeatPausedForMethod",heartbeatPausedForMethod_);
    data.insert("heartbeatSent",heartbeatSent_);
    data.insert("heartbeatReplies",heartbeatReplies_);
    data.insert("retainedRawReceiveBytes",recentRawBytes_);
    data.insert("acquisitionParseFailure",acquisitionParseFailure_);
    data.insert("methodConfirmed",!confirmedMethodParameters_.isEmpty());
    data.insert("methodConfirmationReason",methodConfirmationReason_);
    data.insert("confirmedMethodParameters",confirmedMethodParameters_);
    data.insert("acquisitionBusy",acquisitionBusy()); data.insert("acquiredScans",acquiredScans_);
    data.insert("acquisitionSeconds",acquisitionSeconds_); data.insert("acquisitionError",acquisitionError_);
    if (lastReadback_.isValid()) data.insert("lastReadback", lastReadback_.toString("HH:mm:ss"));
    if (fresh_) {
        data.insert("multiplierVoltageV", status_.multiplierVoltageV);
        data.insert("vacuumRaw", status_.vacuumRaw);
        data.insert("vacuumMbar", NetworkProtocol::vacuumMbarFromRaw(status_.vacuumRaw));
        data.insert("experimentRunning", status_.experimentRunning);
    }
    return data;
}
bool NetworkInstrument::exportFrames(const QString &path, QString *error) const {
    const auto rawJson=[](const QList<RawReceive> &records) {
        QJsonArray result;
        for(const auto &r:records) result.append(QJsonObject{{"time",r.time},{"peer",r.peer},{"port",r.port},
            {"offset",double(r.offset)},{"bytes",r.bytes.size()},{"hex",QString::fromLatin1(r.bytes.toHex(' '))}});
        return result;
    };
    QJsonArray frames; for (const auto &frame : recentFrames_) frames.append(frame);
    QJsonArray events; for (const auto &event : connectionEvents_) events.append(event);
    QByteArray bytes = QJsonDocument(QJsonObject{{"protocol", "质谱网口通讯协议(2).pdf · 2026-09-09 · 状态数据21字节，实验状态01/00"},
        {"note", "最近256条接收帧/发送记录：状态/控制帧强制校验CRC，上传81/82波形CRC仅记录；另含64条连接事件、解析前原始块（最多128块/1MiB）和采集首次错误现场；RX_RAW块边界不是协议帧边界；不是完整采集记录"},
        {"connectionEvents", events},
        {"rawReceives",rawJson(recentRawReceives_)},{"acquisitionErrorRawReceives",rawJson(errorRawReceives_)},
        {"status", QJsonObject::fromVariantMap(statusDetails())}, {"frames", frames}}).toJson();
    if (QFileInfo(path).suffix().compare("txt", Qt::CaseInsensitive) == 0) {
        QString text = "网口十六进制收发记录\r\n";
        text += "时间为UTC；TX=上位机提交发送（不代表设备接收或执行成功）；RX=接收帧。状态/控制帧强制校验CRC；上传0x81/0x82波形CRC仅记录，不作为接收条件。\r\n";
        text += "仅保留最近256条收发记录和64条连接事件；重新监听会清空。\r\n";
        text += "另附解析前RX_RAW原始接收块（含未通过校验字节，块边界不是协议帧边界）；最近最多128块/1MiB，并保存本次采集首次错误现场。\r\n";
        text += "导出时间：" + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + "\r\n";
        text += "当前状态：" + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(statusDetails())).toJson(QJsonDocument::Compact)) + "\r\n\r\n";
        text += "[收发报文]\r\n";
        if (recentFrames_.isEmpty()) text += "没有保留的收发报文。\r\n";
        for (const auto &frame : recentFrames_) {
            const auto hex = frame.value("hex").toString().toUpper();
            const auto wire = QByteArray::fromHex(hex.toLatin1());
            text += QString("[%1] %2 peer=%3 action=0x%4 command=0x%5 bytes=%6\r\n")
                .arg(frame.value("time").toString(), frame.value("direction").toString(), frame.value("peer").toString())
                .arg(frame.value("action").toInt(), 2, 16, QLatin1Char('0'))
                .arg(frame.value("command").toInt(), 2, 16, QLatin1Char('0')).arg(wire.size());
            text += hex + "\r\n";
            if(frame.contains("crcRequired") && !frame.value("crcRequired").toBool()) {
                text += QString("波形CRC仅记录：收到%1，计算%2，%3；周期%4。\r\n")
                    .arg(frame.value("receivedCrc").toInt(),4,16,QLatin1Char('0'))
                    .arg(frame.value("calculatedCrc").toInt(),4,16,QLatin1Char('0'))
                    .arg(frame.value("crcMatches").toBool()?"一致":"不一致")
                    .arg(frame.value("cycleIndex").toInt());
            }
            if (frame.value("direction").toString() == "RX" && frame.value("action").toInt() == 0x10
                && frame.value("command").toInt() == 0x81 && wire.size() == 11) {
                const auto code = quint8(wire[7]);
                text += QString("方法返回值：0x%1；%2\r\n").arg(code, 2, 16, QLatin1Char('0'))
                    .arg(code == 0x11 ? "协议定义成功" : code == 0x12 ? "协议定义失败" : code == 0x29 ? "冷却时间错误，设置失败" : "含义未确认");
            }
            text += "\r\n";
        }
        text += "[连接事件]\r\n";
        for (const auto &event : connectionEvents_)
            text += QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)) + "\r\n";
        const auto appendRaw=[&text](const QString &title,const QList<RawReceive> &records) {
            text += "\r\n["+title+"]\r\n";
            for(const auto &r:records) {
                text += QString("[%1] RX_RAW peer=%2 port=%3 offset=%4 bytes=%5\r\n")
                    .arg(r.time,r.peer).arg(r.port).arg(r.offset).arg(r.bytes.size());
                text += QString::fromLatin1(r.bytes.toHex(' ').toUpper())+"\r\n\r\n";
            }
        };
        appendRaw("采集首次错误现场（解析前原始字节）",errorRawReceives_);
        appendRaw("最近原始接收块（未经校验）",recentRawReceives_);
        // BOM lets the Win7 Notepad open Chinese text as UTF-8.
        bytes = QByteArray::fromHex("efbbbf") + text.toUtf8();
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

bool NetworkInstrument::startAcquisition(int seconds,QString *error) {
    const auto fail=[error](const QString &text){if(error)*error=text;return false;};
    if(settingBusy()) return fail("请等待开机或部件操作完成");
    if(acquisitionBusy() || tuningPending_ || !methodRequestId_.isEmpty()) return fail("请等待当前设备操作完成");
    if(stopReplyGuardTimer_.isActive()) return fail("正在收尾上一轮关闭回复，请稍后开始检测");
    if(!peer_ || !fresh_ || status_.experimentRunning) return fail("需要有效网口回读且仪器已停止检测");
    if(confirmedMethodParameters_.isEmpty()) return fail("请先将 Fullscan 方法设置到仪器并确认成功");
    if(confirmedMethodParameters_.value("period").toInt()!=10000)
        return fail("当前采集按已确认的1秒周期工作，请将方法周期设为10000");
    acquisitionMassAxis_=NetworkProtocol::fullscanMassAxis(confirmedMethodParameters_,error,calibrationProfile_);
    if(acquisitionMassAxis_.isEmpty()) return false;
    if(acquisitionMassAxis_.size()*2>NetworkProtocol::MaximumWaveformPayload)
        return fail("当前方法的单周期质谱超过网口长度字段容量，请调整方法");
    if(seconds<1 || seconds>5000 || qint64(seconds)*acquisitionMassAxis_.size()>1000000)
        return fail("检测时间超出当前采集容量（最多5000周期、100万采样点），请缩短检测时间");
    acquisitionSeconds_=seconds; acquiredScans_=0;nextPressureCycle_=0;
    stopCommandsSent_=0;
    acquisitionCancelled_=false; acquisitionError_.clear(); acquisitionState_=1;
    pressureTimer_.stop(); pressureVolts_.clear(); pressureCycle_=-1;
    pressureAcquisitionCompleted_=false;
    pressureRunTrace_=true;pressureDisplayLimit_=false;
    pressureSampleIntervalMinutes_=NetworkProtocol::pressureSampleIntervalMinutes(confirmedMethodParameters_);
    errorRawReceives_.clear();acquisitionParseFailure_={};
    if(!sendDetection(true)) {closePeer("检测开启发送失败，设备状态未知");return false;}
    notify();
    return true;
}
bool NetworkInstrument::sendDetection(bool enabled) {
    if(!peer_ || peer_->state()!=QAbstractSocket::ConnectedState) return false;
    const auto wire=NetworkProtocol::detectionCommand(enabled);
    recentFrames_.append(QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"direction","TX"},{"peer",peerAddress_},{"action",0x10},{"command",0x15},
        {"hex",QString::fromLatin1(wire.toHex(' '))}});
    if(recentFrames_.size()>256) recentFrames_.removeFirst();
    // A single retry must not extend the original 3-second stop deadline.
    if(enabled || stopCommandsSent_==0) acquisitionAckTimer_.start();
    if(!enabled) ++stopCommandsSent_;
    return peer_->write(wire)==wire.size();
}
void NetworkInstrument::stopAcquisition(bool cancelled) {
    if(!acquisitionBusy() || acquisitionState_==3) return;
    acquisitionCancelled_=cancelled;
    if(acquisitionState_==1) {
        // Start and stop ACKs have identical payloads. Never have both outstanding.
        if(acquisitionError_.isEmpty()) acquisitionError_="在开启确认前取消，仪器状态未知";
        return; // After start ACK, send stop. On timeout isolate the connection.
    }
    acquisitionDurationTimer_.stop(); acquisitionState_=3;
    if(!sendDetection(false)) closePeer("检测关闭发送失败，仪器可能仍在运行");
    else stopRetryTimer_.start();
}
void NetworkInstrument::failAcquisition(const QString &error) {
    if(!acquisitionBusy()) return;
    if(acquisitionError_.isEmpty()) errorRawReceives_=recentRawReceives_;
    if(acquisitionError_.isEmpty()) acquisitionError_=error;
    stopAcquisition(false);
}
void NetworkInstrument::finishNetworkAcquisition(bool success,const QString &error) {
    if(!acquisitionBusy()) return;
    acquisitionAckTimer_.stop(); acquisitionDurationTimer_.stop();
    stopRetryTimer_.stop();
    // Start/stop replies have no request ID. Drain late replies to the duplicate
    // stop before permitting another start on this socket.
    if(stopCommandsSent_>1) stopReplyGuardTimer_.start();
    acquisitionState_=0;
    pressureAcquisitionCompleted_=success && !acquisitionCancelled_ && error.isEmpty();
    if(pressureAcquisitionCompleted_) pressureTimer_.stop();
    if(!error.isEmpty()) acquisitionError_=error;
    notify();
    emit acquisitionFinished(success,acquisitionCancelled_ && error.isEmpty(),error);
}
void NetworkInstrument::receiveAcquisition(const NetworkFrame &frame) {
    if(!acquisitionBusy()) return;
    bool accepted=false;
    if(NetworkProtocol::decodeCommandAcknowledgement(frame,0x15,&accepted)) {
        if(acquisitionState_!=1 && acquisitionState_!=3) return;
        acquisitionAckTimer_.stop();
        if(!accepted) {
            closePeer("设备拒绝检测指令，请核对仪器运行状态"); return;
        }
        if(acquisitionState_==1) {
            acquisitionState_=2;
            if(acquisitionCancelled_ || !acquisitionError_.isEmpty()) {stopAcquisition(acquisitionCancelled_);return;}
            acquisitionDurationTimer_.start(acquisitionSeconds_*1000); emit acquisitionStarted();
        } else {
            QString error=acquisitionError_;
            if(error.isEmpty() && !acquisitionCancelled_ && (acquiredScans_==0 || decoder_.bufferedBytes()!=0))
                error="设备已确认关闭，但没有完整质谱或仍有未收齐报文，本次检测未完成";
            finishNetworkAcquisition(error.isEmpty() && !acquisitionCancelled_,error);
        }
        return;
    }
    if(frame.action!=0x20 || frame.command!=0x81 || acquisitionState_==1 || !acquisitionError_.isEmpty()) return;
    if(frame.cycleIndex()!=acquiredScans_) {
        failAcquisition(QString("质谱周期编号不连续：应为%1，收到%2；本次检测未完成")
            .arg(acquiredScans_).arg(frame.cycleIndex()));return;
    }
    if(frame.payload.size()!=acquisitionMassAxis_.size()*2 || acquiredScans_>=5000
        || qint64(acquiredScans_+1)*acquisitionMassAxis_.size()>1000000) {
        failAcquisition("质谱采样点数与方法不符或超过容量，已请求关闭检测");return;
    }
    SpectrumScan scan;scan.timeSeconds=frame.cycleIndex();scan.msLevel=1;
    scan.points.reserve(acquisitionMassAxis_.size());
    for(int i=0;i<acquisitionMassAxis_.size();++i) {
        const quint16 raw=(quint16(quint8(frame.payload[i*2]))<<8)|quint8(frame.payload[i*2+1]);
        scan.points.append({acquisitionMassAxis_[i],double(raw)});
    }
    ++acquiredScans_;
    emit acquisitionScan(scan);
}
}
