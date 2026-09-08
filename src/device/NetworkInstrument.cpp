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
        message_ = "网口状态超时，读数已失效；等待新的有效状态报文";
        emit stateChanged();
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
bool NetworkInstrument::startListening(const QString &address, quint16 port, int staleMs) {
    QHostAddress local;
    if (!local.setAddress(address.trimmed()) || staleMs < 1000 || staleMs > 3600000) {
        message_ = "监听失败：请输入有效的本机IP和1～3600秒状态超时";
        emit stateChanged(); return false;
    }
    stopListening();
    receivedBytes_ = validFrames_ = unparsedFrames_ = 0;
    recentFrames_.clear(); decoder_ = {};
    staleTimer_.setInterval(staleMs);
    if (!server_.listen(local, port)) {
        message_ = "网口监听失败：" + server_.errorString();
        emit stateChanged(); return false;
    }
    message_ = QString("正在监听 %1:%2，等待仪器连接 · 只读").arg(address).arg(server_.serverPort());
    emit stateChanged(); return true;
}
void NetworkInstrument::closePeer(const QString &message) {
    staleTimer_.stop(); decoder_.reset(); fresh_ = false; status_ = {}; lastReadback_ = {};
    if (peer_) {
        auto *old = peer_; peer_ = nullptr;
        QObject::disconnect(old, nullptr, this, nullptr);
        old->abort(); old->deleteLater();
    }
    peerAddress_.clear(); message_ = message; emit stateChanged();
}
void NetworkInstrument::stopListening() {
    server_.close(); updateTimer_.stop(); closePeer("网口已停止监听 · 只读状态");
}
void NetworkInstrument::acceptConnection() {
    while (server_.hasPendingConnections()) {
        auto *socket = server_.nextPendingConnection();
        if (peer_) { socket->abort(); socket->deleteLater(); continue; }
        peer_ = socket; peer_->setReadBufferSize(65536);
        peerAddress_ = peer_->peerAddress().toString();
        decoder_.reset(); fresh_ = false; lastReadback_ = {};
        message_ = "TCP已连接 " + peerAddress_ + "，等待有效状态报文";
        connect(peer_, &QTcpSocket::readyRead, this, &NetworkInstrument::receive);
        connect(peer_, &QTcpSocket::disconnected, this, [this] {
            closePeer("仪器TCP已断开，读数已失效；继续监听等待重连");
        });
        connect(peer_,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            &QTcpSocket::errorOccurred,
#else
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
#endif
            this,
            [this](QAbstractSocket::SocketError) {
                if (peer_) closePeer("网口连接错误：" + peer_->errorString() + "；继续监听");
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
        if (decoded) {
            status_ = next; fresh_ = true; lastReadback_ = QDateTime::currentDateTime();
            staleTimer_.start(); message_ = "网口状态回读正常 · 只读，谱图与控制未开放";
        } else ++unparsedFrames_;
        recentFrames_.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"peer", peerAddress_}, {"direction", "RX"}, {"decodedStatus", decoded},
            {"action", frame.action}, {"command", frame.command}, {"frameCount", frame.count},
            {"frameIndex", frame.index}, {"hex", QString::fromLatin1(frame.wire.toHex(' '))}});
        if (recentFrames_.size() > 256) recentFrames_.removeFirst();
    }
    notify();
    if (peer_ && peer_->bytesAvailable() > 0) QTimer::singleShot(0, this, &NetworkInstrument::receive);
}
InstrumentDescriptor NetworkInstrument::descriptor() const {
    return {"便携式质谱 · 网口/485只读", {}, "tcp-v1.4-doc3-status14+rs485-status23", false};
}
InstrumentHealth NetworkInstrument::health() const {
    auto value = serial_->health(); value.connected = value.connected || fresh_; value.ready = false; return value;
}
InstrumentTelemetry NetworkInstrument::telemetry() const {
    auto value = serial_->telemetry();
    if (fresh_) value.multiplierVoltageV = status_.multiplierVoltageV;
    return value;
}
QString NetworkInstrument::connectionSummary() const {
    return message_ + (serial_->portOpen() ? "；" + serial_->connectionSummary() : QString());
}
CommandValidation NetworkInstrument::validate(const InstrumentCommand &command) const {
    if (command.id == "ReadHealth" && health().connected) return {true, {}};
    return {false, "网口/485当前仅支持只读状态，真实采集与控制未开放"};
}
CommandValidation NetworkInstrument::validateSetting(const QString &, const QVariant &) const {
    return {false, "网口/485当前为只读接入，不发送硬件控制命令"};
}
void NetworkInstrument::requestSetting(const QString &id, const QString &key, const QVariant &) {
    emit settingFinished(id, key, false, {}, validateSetting(key, {}).reason);
}
QVariantMap NetworkInstrument::statusDetails() const {
    QVariantMap data{{"listening", server_.isListening()}, {"tcpConnected", peer_ != nullptr},
        {"connected", fresh_}, {"address", server_.serverAddress().toString()}, {"port", server_.serverPort()},
        {"peer", peerAddress_}, {"message", message_}, {"receivedBytes", receivedBytes_},
        {"validFrames", validFrames_}, {"unparsedFrames", unparsedFrames_},
        {"rejectedBytes", decoder_.rejectedBytes()}, {"retainedFrames", recentFrames_.size()}};
    if (lastReadback_.isValid()) data.insert("lastReadback", lastReadback_.toString("HH:mm:ss"));
    if (fresh_) {
        data.insert("multiplierVoltageV", status_.multiplierVoltageV);
        data.insert("vacuumRaw", status_.vacuumRaw);
        data.insert("experimentRunning", status_.experimentRunning);
    }
    return data;
}
bool NetworkInstrument::exportFrames(const QString &path, QString *error) const {
    QJsonArray frames; for (const auto &frame : recentFrames_) frames.append(frame);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"protocol", "质谱网口通讯协议(3).pdf"},
        {"note", "最近256条CRC有效接收帧，包含未解析帧；不是完整采集记录"},
        {"status", QJsonObject::fromVariantMap(statusDetails())}, {"frames", frames}}).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}
}
