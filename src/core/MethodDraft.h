#pragma once
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <cmath>

namespace qitest {
struct MethodField { QString key, label, unit, group; };
class MethodDraft {
public:
    static const QVector<MethodField> &fields() {
        static const QVector<MethodField> values{
            {"carrier","载气流速","mL/min","基本"},{"td","TD 温度","℃","基本"},
            {"extraction","抽气流速","%","基本"},{"inlet","进气流速","%","基本"},
            {"source","离子源电压","V","基本"},{"trap","离子阱温度","℃","基本"},
            {"period","周期","","扫描"},{"speed","扫描频率","","扫描"},
            {"rf_frequency","RF 频率","","扫描"},
            {"storage_mass","存储质量数","m/z","扫描"},
            {"low_mass","低质量数","m/z","扫描"},{"high_mass","高质量数","m/z","扫描"},
            {"cooling","冷却时间","","扫描"},{"ac_frequency","AC 频率","","扫描"},
            {"injection","进样时间","ms","扫描"},{"multiplier","倍增器电压","V","扫描"},
            {"sim_ac_voltage","SIM AC 电压","","SIM"},
            {"sim_ac_voltage_high","SIM AC 上限","","SIM"},
            {"isolate_rf","隔离 RF 质量数","m/z","SIM"},
            {"isolate_Q","隔离 Q 值","","SIM"},
            {"isolate_swift_retain_sim","SIM 保留段","","SIM"},
            {"isolate_ac_voltage","隔离 AC 电压","","MS/MS"},
            {"isolate_ac_voltage_high","隔离 AC 上限","","MS/MS"},
            {"isolate_rf_ms","MS/MS 隔离 RF","m/z","MS/MS"},
            {"isolate_swift_retain","隔离保留段","","MS/MS"},
            {"lc_resonance_period","LC 共振周期","","MS/MS"},
            {"ac_oscillation_amplitude_low","AC 振荡下限","","MS/MS"},
            {"ac_oscillation_amplitude_high","AC 振荡上限","","MS/MS"},
            {"ac_oscillation_amplitude_interval","AC 振荡间隔","","MS/MS"}};
        return values;
    }
    static QJsonObject defaultParameters() {
        // 这些值逐项来自用户提供的旧软件 Fullscan 参考界面，不是协议推测值。
        return {{"scan_mode","Fullscan"},{"carrier",1.0},{"extraction",0.0},{"inlet",50.0},
            {"td",0.0},{"source",0.0},{"trap",85.0},{"period",10000.0},{"speed",8000.0},
            {"rf_frequency",50.0},{"storage_mass",30.0},{"low_mass",40.0},{"high_mass",300.0},
            {"cooling",5000.0},{"ac_frequency",590.0},{"injection",380.0},{"multiplier",1000.0},
            // 扩展模式值来自 QitVenture 6.1.0.1 随包配置，仅作模拟驱动参考。
            {"sim_ac_voltage",600.0},{"sim_ac_voltage_high",500.0},{"isolate_rf",100.0},
            {"isolate_Q",0.4},{"isolate_swift_retain_sim",4.0},
            {"isolate_ac_voltage",600.0},{"isolate_ac_voltage_high",500.0},{"isolate_rf_ms",100.0},
            {"isolate_swift_retain",4.0},{"lc_resonance_period",1000.0},
            {"ac_oscillation_amplitude_low",500.0},{"ac_oscillation_amplitude_high",550.0},
            {"ac_oscillation_amplitude_interval",10.0}};
    }
    static bool validate(const QJsonObject &values, QString *error) {
        const auto fail=[error](const QString &s){ if(error)*error=s; return false; };
        const QString mode=values.value("scan_mode").toString();
        if(mode!="Fullscan" && mode!="SIM" && mode!="MS/MS") return fail("请选择扫描模式");
        for(const auto &field:fields()) {
            if(!values.contains(field.key)) continue; // unspecified is not zero
            const auto v=values.value(field.key); const double number=v.toDouble();
            if(!v.isDouble() || !std::isfinite(number) || number<0 || number>1e9)
                return fail(field.label+"：需有限非负数，不能超过软件记录上限 1e9");
            if(field.key=="injection" && (number>600 || std::abs(number*100-std::round(number*100))>1e-6))
                return fail("进样时间：范围0～600 ms，分辨率0.01 ms");
            if(field.unit=="%" && number>100) return fail(field.label+"：百分比不能超过 100");
        }
        for(const auto &pair:QVector<QPair<QString,QString>>{{"low_mass","high_mass"}})
            if(values.contains(pair.first) && values.contains(pair.second) && values.value(pair.first).toDouble()>values.value(pair.second).toDouble())
                return fail("起始值不能大于结束值："+pair.first);
        for(const QString &key:values.keys()) {
            if(key=="scan_mode") continue;
            bool known=false; for(const auto &field:fields()) if(field.key==key) known=true;
            if(!known) return fail("包含不支持的参数："+key);
        }
        if(error)error->clear(); return true;
    }
};
}
