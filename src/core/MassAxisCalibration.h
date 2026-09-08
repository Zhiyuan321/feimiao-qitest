#pragma once
#include <QVector>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>

namespace qitest {
struct MassAxisPair { double measured=0, theoretical=0; };
struct MassAxisFit {
    bool valid=false; QString error; int degree=1;
    double center=0,scale=1,minimum=0,maximum=0,rms=0;
    std::array<double,3> coefficients{{0,0,0}};
    double evaluate(double x) const { const double z=(x-center)/scale;return coefficients[0]+z*(coefficients[1]+z*coefficients[2]); }
    bool map(double x,double *out) const {
        if(!valid||!out||!std::isfinite(x)||x<minimum||x>maximum)return false;
        const double y=evaluate(x);if(!std::isfinite(y)||y<=0)return false;*out=y;return true;
    }
};
// Offline unweighted polynomial least squares measured m/z -> theoretical m/z.
// Centered/scaled QR, no manufacturer-specific calibration or extrapolation.
class MassAxisCalibration {
public:
    static MassAxisFit fit(QVector<MassAxisPair> pairs,int degree) {
        MassAxisFit out;out.degree=degree;
        const auto fail=[&](const QString &s){out.valid=false;out.error=s;return out;};
        if((degree!=1&&degree!=2)||pairs.size()<degree+1||pairs.size()>1000)return fail("线性至少 2 点，二次至少 3 点；最多 1000 点");
        for(const auto &p:pairs)if(!std::isfinite(p.measured)||!std::isfinite(p.theoretical)||p.measured<=0||p.theoretical<=0||p.measured>1e6||p.theoretical>1e6)return fail("质量数应为有限正值，且不超过 1000000");
        std::sort(pairs.begin(),pairs.end(),[](const MassAxisPair &a,const MassAxisPair &b){return a.measured<b.measured;});
        for(int i=1;i<pairs.size();++i)if(pairs[i].measured<=pairs[i-1].measured||pairs[i].theoretical<=pairs[i-1].theoretical)return fail("实测值不可重复，理论值须随实测值递增");
        out.minimum=pairs.first().measured;out.maximum=pairs.last().measured;out.center=(out.minimum+out.maximum)/2;out.scale=(out.maximum-out.minimum)/2;
        if(out.scale<1e-10)return fail("校准点范围过窄");
        std::array<QVector<double>,3> q; double r[3][3]={{0}};double projected[3]={0};
        for(int j=0;j<=degree;++j) {
            q[j].resize(pairs.size());
            for(int k=0;k<pairs.size();++k)q[j][k]=std::pow((pairs[k].measured-out.center)/out.scale,j);
            for(int i=0;i<j;++i){for(int k=0;k<pairs.size();++k)r[i][j]+=q[i][k]*q[j][k];for(int k=0;k<pairs.size();++k)q[j][k]-=r[i][j]*q[i][k];}
            for(double v:q[j])r[j][j]+=v*v;r[j][j]=std::sqrt(r[j][j]);
            if(r[j][j]<1e-10)return fail("校准点退化，无法可靠拟合");
            for(int k=0;k<pairs.size();++k){q[j][k]/=r[j][j];projected[j]+=q[j][k]*pairs[k].theoretical;}
        }
        for(int i=degree;i>=0;--i){double v=projected[i];for(int j=i+1;j<=degree;++j)v-=r[i][j]*out.coefficients[j];out.coefficients[i]=v/r[i][i];}
        if(out.coefficients[1]-2*std::abs(out.coefficients[2])<=0)return fail("拟合曲线在校准范围内不单调，拒绝使用");
        for(const auto &p:pairs){const double delta=out.evaluate(p.measured)-p.theoretical;out.rms+=delta*delta;}
        out.rms=std::sqrt(out.rms/pairs.size());out.valid=std::isfinite(out.rms);return out;
    }
};
}
