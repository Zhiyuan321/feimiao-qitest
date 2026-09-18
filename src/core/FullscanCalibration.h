#pragma once
#include "core/MassAxisCalibration.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace qitest {
// The hardware polynomial maps theoretical m/z to scan voltage, not m/z to m/z.
// Recover each observed voltage with the OLD profile, then fit theory -> voltage.
class FullscanCalibration {
public:
    static QJsonObject defaults() {
        return {{"source","datafit.json"},{"section","Fullscan"},
            {"source_sha256","f84190db1ebd30dc63eafcc8f69c726f3cefa73b65f07130378e79229b8e2233"},
            {"calibrate_a",0.00013517703930585604},{"calibrate_b",5.882440951161457},
            {"calibrate_c",8.575019905095814}};
    }
    static double voltage(const QJsonObject &p,double mz) {
        return p["calibrate_c"].toDouble()+mz*(p["calibrate_b"].toDouble()+mz*p["calibrate_a"].toDouble());
    }
    static bool validate(const QJsonObject &p,QString *error=nullptr) {
        const auto fail=[&](const QString &s){if(error)*error=s;return false;};
        if(p["section"]!="Fullscan")return fail("校准配置须为Fullscan");
        for(const auto &k:{"calibrate_a","calibrate_b","calibrate_c"})
            if(!p[k].isDouble() || !std::isfinite(p[k].toDouble()))return fail("校准系数缺失或无效");
        const double a=p["calibrate_a"].toDouble(),b=p["calibrate_b"].toDouble();
        if(b<=1e-9 || b+2*a*801<=1e-9 || !std::isfinite(voltage(p,801))
            || voltage(p,801)>131070 || voltage(p,801)<=0)
            return fail("校准曲线在Fullscan量程内非递增或电压超出范围");
        if(error)error->clear();return true;
    }
    static bool mass(const QJsonObject &p,double v,double *out) {
        if(!out || !validate(p) || !std::isfinite(v) || v<voltage(p,0) || v>voltage(p,803))return false;
        const double a=p["calibrate_a"].toDouble(),b=p["calibrate_b"].toDouble(),c=p["calibrate_c"].toDouble();
        double lo=0,hi=801;
        if(v>voltage(p,801))return false;
        for(int i=0;i<64;++i){const double mid=(lo+hi)/2;if(c+mid*(b+mid*a)<v)lo=mid;else hi=mid;}
        *out=(lo+hi)/2;return std::isfinite(*out) && *out>0;
    }
    static QJsonObject fit(const QVector<MassAxisPair> &pairs,const QJsonObject &base,QString *error) {
        if(!validate(base,error))return {};
        const auto sanity=MassAxisCalibration::fit(pairs,2);
        if(!sanity.valid){if(error)*error=sanity.error;return {};}
        QVector<MassAxisPair> voltages;QJsonArray rows;
        for(const auto &p:pairs) {
            if(p.measured>801 || p.theoretical>801){if(error)*error="当前Fullscan校准质量数不得超过801";return {};}
            voltages.append({p.theoretical,voltage(base,p.measured)});
            rows.append(QJsonArray{p.measured,p.theoretical});
        }
        const auto f=MassAxisCalibration::fit(voltages,2);
        if(!f.valid){if(error)*error=f.error;return {};}
        const double a=f.coefficients[2]/(f.scale*f.scale);
        const double b=f.coefficients[1]/f.scale-2*a*f.center;
        const double c=f.coefficients[0]-f.coefficients[1]*f.center/f.scale+a*f.center*f.center;
        QJsonObject profile{{"section","Fullscan"},{"source","manual-quadratic-v1"},
            {"calibrate_a",a},{"calibrate_b",b},{"calibrate_c",c},
            {"points",rows},{"base_coefficients",QJsonObject{{"section","Fullscan"},
                {"calibrate_a",base["calibrate_a"]},{"calibrate_b",base["calibrate_b"]},{"calibrate_c",base["calibrate_c"]}}},
            {"minimum",f.minimum},{"maximum",f.maximum},{"voltage_rms",f.rms}};
        if(!validate(profile,error))return {};
        double rms=0;
        for(const auto &p:pairs){double corrected=0;if(!mass(profile,voltage(base,p.measured),&corrected)){
            if(error)*error="校准点反算失败";return {};}
            rms+=(corrected-p.theoretical)*(corrected-p.theoretical);}
        profile["mass_rms"]=std::sqrt(rms/pairs.size());
        return profile;
    }
};
}
