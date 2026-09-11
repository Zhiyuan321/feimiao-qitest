#include "device/NetworkInstrument.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace qitest {
NetworkInstrument::NetworkInstrument(std::unique_ptr<Rs485Instrument> serial, QObject *parent)
    : IInstrumentAdapter(parent), serial_(serial ? std::move(serial) : std::make_unique<Rs485Instrument>()) {
    connect(serial_.get(), &IInstrumentAdapter::stateChanged, this, &IInstrumentAdapter::stateChanged);
    server_.setMaxPendingConnections(1);
    connect(&server_, &QTcpServer::newConnection, this, &NetworkInstrument::acceptConnection);
    staleTimer_.setSingleShot(true);
    connect(&staleTimer_, &QTimer::timeout, this, [this] {
        fresh_ = false; status_ = {}; decoder_.reset();
        recordConnectionEvent("status_timeout", peer_);
        message_ = "网口状态超时，读数已失效；等待新的有效状态报文";
        emit stateChanged();
    });
    pressureTimer_.setSingleShot(true);
    connect(&pressureTimer_, &QTimer::timeout, this, [this] {pressureVolts_.clear();emit stateChanged();});
    tuningTimer_.setSingleShot(true); tuningTimer_.setInterval(3000);
    connect(&tuningTimer_, &QTimer::timeout, this, [this] {
        tuningPending_=false;
        closePeer("调谐指令应答超时，设备状态未知；已隔离旧连接，请重连后核对");
        tuningMessage_="调谐应答超时，设备可能仍在运行；重连后可发送结束"; emit stateChanged();
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
    pressureTimer_.stop(); pressureVolts_.clear(); pressureFrameIndex_=pressureFrameTotal_=0;
    tuningTimer_.stop(); tuningPending_=false; tuningMessage_="连接已关闭，调谐状态未知";
    staleTimer_.stop(); decoder_.reset(); fresh_ = false; status_ = {}; lastReadback_ = {};
    if (peer_) {
        recordConnectionEvent("closed", peer_);
        auto *old = peer_; peer_ = nullptr;
        QObject::disconnect(old, nullptr, this, nullptr);
        old->abort(); old->deleteLater();
    }
    peerAddress_.clear(); message_ = message; emit stateChanged();
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
        staleTimer_.start(); emit stateChanged();
        receive();
    }
}
void NetworkInstrument::receive() {
    if (!peer_) return;
    const auto bytes = peer_->read(65536);
    receivedBytes_ += bytes.size();
    for (const auto &frame : decoder_.feed(bytes)) {
        ++validFrames_;
        NetworkStatus next;
        const bool decoded = NetworkProtocol::decodeStatus(frame, &next);
        QVector<double> pressure;
        const bool pressureDecoded=NetworkProtocol::decodePressure(frame,&pressure);
        if(pressureDecoded) {
            pressureVolts_=pressure; ++pressureFrameCount_;
            pressureFrameIndex_=frame.index; pressureFrameTotal_=frame.count; pressureTimer_.start();
        }
        const bool tuningAck=frame.action==0x10 && frame.command==0x20 && frame.count==1 && frame.index==1
            && frame.payload.size()==1 && (quint8(frame.payload[0])==0x11 || quint8(frame.payload[0])==0x12);
        if(tuningAck && tuningPending_) {
            tuningTimer_.stop(); tuningPending_=false;
            tuningMessage_=quint8(frame.payload[0])==0x11
                ? (tuningTarget_ ? "设备已应答调谐开启成功；RF曲线换算待确认" : "设备已应答调谐关闭成功")
                : "设备拒绝调谐指令，实际状态未知";
        }
        if (decoded) {
            status_ = next; fresh_ = true; lastReadback_ = QDateTime::currentDateTime();
            staleTimer_.start(); message_ = "网口状态回读正常；已支持调谐启停，其他控制未开放";
        } else if(!pressureDecoded && !tuningAck) ++unparsedFrames_;
        recentFrames_.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"peer", peerAddress_}, {"direction", "RX"}, {"decodedStatus", decoded}, {"decodedPressure", pressureDecoded}, {"tuningAck", tuningAck},
            {"action", frame.action}, {"command", frame.command}, {"frameCount", frame.count},
            {"frameIndex", frame.index}, {"hex", QString::fromLatin1(frame.wire.toHex(' '))}});
        if (recentFrames_.size() > 256) recentFrames_.removeFirst();
    }
    notify();
    if (peer_ && peer_->bytesAvailable() > 0) QTimer::singleShot(0, this, &NetworkInstrument::receive);
}
bool NetworkInstrument::requestTuning(bool enabled, QString *error) {
    const auto fail=[error](const QString &message){if(error)*error=message;return false;};
    if(!peer_ || peer_->state()!=QAbstractSocket::ConnectedState) return fail("请先连接网口仪器");
    if(tuningPending_) return fail("正在等待上一条调谐指令应答");
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
InstrumentDescriptor NetworkInstrument::descriptor() const {
    return {"便携式质谱 · 网口/485", {}, "tcp-20260910-pressure82-tuning20+rs485-status23", false};
}
InstrumentHealth NetworkInstrument::health() const {
    auto value = serial_->health();
    value.connected = value.connected || fresh_; value.ready = false;
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
    return {false, "此入口仅支持状态读取；调谐启停请使用射频页，真实采集未开放"};
}
CommandValidation NetworkInstrument::validateSetting(const QString &, const QVariant &) const {
    return {false, "此入口不支持通用硬件设置；仅射频页支持调谐启停"};
}
void NetworkInstrument::requestSetting(const QString &id, const QString &key, const QVariant &) {
    emit settingFinished(id, key, false, {}, validateSetting(key, {}).reason);
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
    data.insert("pressureFrames",pressureFrameCount_); data.insert("pressurePoints",pressureVolts_.size());
    data.insert("pressureFrameIndex",pressureFrameIndex_);data.insert("pressureFrameTotal",pressureFrameTotal_);
    data.insert("tuningPending",tuningPending_);data.insert("tuningMessage",tuningMessage_);
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
    QJsonArray frames; for (const auto &frame : recentFrames_) frames.append(frame);
    QJsonArray events; for (const auto &event : connectionEvents_) events.append(event);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"protocol", "质谱网口通讯协议(2).pdf · 2026-09-09 · 状态数据21字节，实验状态01/00"},
        {"note", "最近256条CRC有效接收帧/调谐发送帧及64条连接事件；同IP新连接接替旧连接；不是完整采集记录"},
        {"connectionEvents", events},
        {"status", QJsonObject::fromVariantMap(statusDetails())}, {"frames", frames}}).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}
}
