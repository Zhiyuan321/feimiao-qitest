#pragma once
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <cmath>

namespace qitest {
struct MethodField { QString key, label, unit, group; };
// Document-backed names. Empty units are deliberately not inferred from sample
// numbers. These are editable records, not validated instrument settings.
class MethodDraft {
public:
    static const QVector<MethodField> &fields() {
        static const QVector<MethodField> values{
            {"carrier","载气流速","mL/min","基本"},{"td","TD 温度","℃","基本"},
            {"extraction","抽气流速","%","基本"},{"inlet","进气流速","%","基本"},
            {"source","离子源电压","V","基本"},{"trap","离子阱温度","℃","基本"},
            {"period","周期","","扫描"},{"speed","扫描速度","","扫描"},
            {"sampling","采样频率","","扫描"},{"storage_mass","存储质量数","m/z","扫描"},
            {"low_mass","低质量数","m/z","扫描"},{"high_mass","高质量数","m/z","扫描"},
            {"cooling","冷却时间","","扫描"},{"ac_frequency","AC 频率","","扫描"},
            {"injection","进样时间","","扫描"},{"multiplier","倍增器电压","V","扫描"},
            {"sim_start","隔离开始电压","","SIM"},{"sim_end","隔离结束电压","","SIM"},
            {"sim_rf","RF 质量数","m/z","SIM"},{"sim_width","隔离范围","","SIM"},
            {"fragment_period","碎裂周期","","MS/MS"},{"ac_interval","AC 震荡间隔","","MS/MS"},
            {"fragment_start","碎裂开始电压","","MS/MS"},{"fragment_end","碎裂结束电压","","MS/MS"},
            {"msms_start","隔离开始电压","","MS/MS"},{"msms_end","隔离结束电压","","MS/MS"},
            {"msms_rf","隔离 RF 质量数","m/z","MS/MS"},{"fragment_factor","碎裂系数","","MS/MS"},
            {"msms_width","隔离范围","","MS/MS"}};
        return values;
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
            if(field.unit=="%" && number>100) return fail(field.label+"：百分比不能超过 100");
        }
        for(const auto &pair:QVector<QPair<QString,QString>>{{"low_mass","high_mass"},{"sim_start","sim_end"},{"msms_start","msms_end"},{"fragment_start","fragment_end"}})
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
