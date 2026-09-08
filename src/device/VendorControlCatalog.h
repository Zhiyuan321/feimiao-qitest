#pragma once

#include <QByteArray>
#include <QVariant>
#include <QVector>
#include <cmath>

namespace qitest {

struct VendorControlSpec {
    QString key, title, transport, unit;
    unsigned char command;
    bool toggle, auxiliary;
    double maximum, scale;
};

// 这里只整理协议资料中的控制数据区，不是完整通信帧或串口/TCP 驱动。
// 帧头、CRC、状态布局等仍需厂家确认；不能把 controlPayload 的结果直接写入设备。
// 发送前还必须核对固件、地址/路由及安全条件。
class VendorControlCatalog {
public:
    static const QVector<VendorControlSpec> &controls() {
        static const QVector<VendorControlSpec> entries{
            {"observationLightOn", "喷雾观察灯", "RS-485", {}, 0x01, true, true, 1, 1},
            {"coolingFanOn", "风扇（FAN）", "RS-485", {}, 0x05, true, true, 1, 1},
            {"wastePumpOn", "废液泵", "RS-485", {}, 0x06, true, true, 1, 1},
            {"rf48VOn", "RF 48V 电源", "网口", {}, 0x32, true, true, 1, 1},
            {"highVoltageBoardOn", "高压板电源", "网口", {}, 0x33, true, true, 1, 1},
            {"tdTemperatureC", "TD 温度设定", "RS-485", " ℃", 0x02, false, true, 65535, 1},
            {"trapTemperatureC", "离子阱温度设定", "RS-485", " ℃", 0x13, false, false, 65535, 1},
            {"inletFlowPercent", "进气泵 PWM", "RS-485", " %", 0x14, false, false, 100, 1},
            {"pumpFlowPercent", "抽气泵 PWM", "RS-485", " %", 0x04, false, false, 100, 1},
            {"efcMlMin", "EFC 流量", "RS-485", " mL/min", 0x12, false, false, 50, 1000}
        };
        return entries;
    }
    static const VendorControlSpec *find(const QString &key) {
        for (const auto &entry : controls()) if (entry.key == key) return &entry;
        return nullptr;
    }
    static QString sourceReference(const VendorControlSpec &entry) {
        const QString source = entry.transport == "RS-485"
            ? QString("485 协议第 5 页") : QString("网口协议第 8 页");
        return source + QString(" · 命令 0x%1").arg(entry.command, 2, 16, QLatin1Char('0'))
            + (entry.key == "observationLightOn"
                ? QString("；485 协议第 8 页说明用于辅助观察 ESI 喷雾") : QString());
    }
    // Limits below describe representable protocol values, NOT safe operating
    // limits. Producing a payload never authorizes, sends or confirms an action.
    // Output remains unchanged on error so a caller cannot send partial bytes.
    static bool controlPayload(const QString &key, const QVariant &value,
                               QByteArray *output, QString *error = nullptr) {
        const auto fail = [error](const QString &message) {
            if (error) *error = message;
            return false;
        };
        const auto *entry = find(key);
        if (!entry || !output) return fail("未知协议控制项或空输出");
        QByteArray payload;
        if (entry->toggle) {
            if (value.userType() != QMetaType::Bool) return fail("开关必须为布尔值");
            const bool ethernet = entry->transport == "网口";
            payload.append(char(value.toBool() ? (ethernet ? 0x22 : 0x01) : (ethernet ? 0x23 : 0x02)));
        } else {
            if (value.userType() != QMetaType::Int && value.userType() != QMetaType::UInt
                && value.userType() != QMetaType::Double && value.userType() != QMetaType::LongLong)
                return fail("设定值必须为数值");
            const double number = value.toDouble(), scaled = number * entry->scale;
            if (!std::isfinite(number) || number < 0 || number > entry->maximum
                || scaled > 65535 || std::abs(scaled - std::round(scaled)) > 1e-7)
                return fail("数值超出协议编码范围或精度");
            const auto raw = static_cast<unsigned int>(std::round(scaled));
            payload.append(char(raw >> 8)); payload.append(char(raw & 0xff));
        }
        *output = payload;
        if (error) error->clear();
        return true;
    }
};
} // namespace qitest
