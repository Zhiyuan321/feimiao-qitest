#include "core/IonThresholdScreening.h"
#include "core/ChromatogramEngine.h"
#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace qitest {
namespace {
QString text(const QJsonObject &entry,const char *key) {
    const auto v=entry.value(QLatin1String(key));
    return v.isDouble()?QString::number(v.toDouble(),'g',17):v.toString().trimmed();
}
bool numberList(const QString &text,bool positive,QVector<double> *values) {
    const auto parts=QString(text).replace(QChar(0xff0c),',').split(',');
    for(const auto &part:parts) {
        bool ok=false;const double v=part.trimmed().toDouble(&ok);
        if(!ok || !std::isfinite(v) || (positive?v<=0:v<0))return false;
        values->append(v);
    }
    return true;
}
}
QString IonThresholdScreening::apply(const QVector<SpectrumScan> &scans,const QJsonObject &snapshot,
                                     AnalysisResult *result,QString *error) {
    if(error)error->clear();
    result->candidates.clear();result->screeningItems.clear();
    result->engineVersion=Version;
    result->libraryVersion=snapshot.value("path").toString()+" · SHA256 "+snapshot.value("sha256").toString();
    const auto fail=[&](const QString &message){if(error)*error=message;return QString("FAILED");};
    if(!snapshot.value("error").toString().isEmpty())return fail(snapshot.value("error").toString());
    const auto rule=snapshot.value("rule").toString();
    // Reopen legacy snapshots using the corrected cardinality rule and record
    // the actual calculation version on the result; never reread a mutable .lib.
    if((rule!=Version && rule!=LegacyVersion) || snapshot.value("tolerance_da").toDouble()!=ToleranceDa)
        return fail("谱库筛查规则或质量窗口不受支持");
    const auto entries=snapshot.value("entries").toArray();
    if(entries.isEmpty() || entries.size()>10000)return fail("指定谱库为空或物质数超出限制");
    QString validation;
    if(!ChromatogramEngine::validate(scans,&validation))return fail(validation);
    if(std::none_of(scans.cbegin(),scans.cend(),[](const SpectrumScan &s){return s.msLevel==1;}))
        return fail("没有MS1扫描，无法执行一级阈值筛查");
    struct Row {ScreeningItem item;QString category;bool valid=false;};
    QVector<Row> rows;QMap<double,double> totals;
    for(int i=0;i<entries.size();++i) {
        const auto e=entries[i].toObject();Row row;row.category=text(e,"sample_category");
        if(row.category.isEmpty())row.category="未提供";
        row.item.referenceId=QString("lib-row-%1").arg(i+1); // Duplicate legacy IDs/names remain distinct rows.
        row.item.name=text(e,"name");row.item.demo=false;
        row.item.precursorMz=text(e,"parent_ion").toDouble();
        if(!std::isfinite(row.item.precursorMz) || row.item.precursorMz<0)row.item.precursorMz=0;
        const bool ionsValid=numberList(text(e,"qualitify_ion"),true,&row.item.fragmentMz);
        const bool thresholdsValid=numberList(text(e,"son_area"),false,&row.item.primaryThresholds);
        QStringList problems;
        if(row.item.name.isEmpty())problems<<"名称不能为空";
        if(!ionsValid)problems<<"定性离子须填写一个或多个正数，用逗号分隔，不能留空";
        if(!thresholdsValid)problems<<"一级阈值须填写一个或多个非负数，用逗号分隔，不能留空";
        if(ionsValid && thresholdsValid && row.item.fragmentMz.size()!=row.item.primaryThresholds.size())
            problems<<QString("定性离子%1个，一级阈值%2个，数量不一致；请按顺序一一对应")
                .arg(row.item.fragmentMz.size()).arg(row.item.primaryThresholds.size());
        row.valid=problems.isEmpty();
        if(row.valid)for(double ion:row.item.fragmentMz)totals.insert(ion,0);
        else {
            row.item.conclusion="未筛查";
            row.item.evidence=problems.join("；");
        }
        rows.append(row);
    }
    // Same per-frame window sum as EIC; cache repeated library ions. Search the
    // sorted spectrum instead of revalidating/scanning a million points per row.
    for(const auto &scan:scans) {
        if(scan.msLevel!=1)continue;
        for(auto ion=totals.begin();ion!=totals.end();++ion) {
            double frameSum=0;
            auto point=std::lower_bound(scan.points.cbegin(),scan.points.cend(),ion.key()-ToleranceDa,
                [](const SpectrumPoint &p,double mz){return p.mz<mz;});
            for(;point!=scan.points.cend() && point->mz<=ion.key()+ToleranceDa;++point)
                if(std::abs(point->mz-ion.key())<=ToleranceDa)frameSum+=point->intensity;
            ion.value()+=frameSum;
        }
    }
    int invalid=0;
    for(auto &row:rows) {
        if(!row.valid) {++invalid;result->screeningItems.append(row.item);continue;}
        bool all=true;int exceeded=0;QStringList evidence;
        for(int i=0;i<row.item.fragmentMz.size();++i) {
            const double sum=totals[row.item.fragmentMz[i]],threshold=row.item.primaryThresholds[i];
            row.item.accumulatedIntensities.append(sum);
            if(!std::isfinite(sum))row.valid=false;
            const bool pass=std::isfinite(sum) && sum>threshold;all=all&&pass;if(pass)++exceeded;
            evidence<<QString("m/z %1：累加 %2 %3 一级阈值 %4")
                .arg(row.item.fragmentMz[i],0,'g',10).arg(sum,0,'g',12).arg(pass?">":"≤").arg(threshold,0,'g',12);
        }
        row.item.evidence=evidence.join("；")+"（MS1，±0.5 Da，各帧直接相加）";
        row.item.conclusion=!row.valid?"未筛查":all?"可疑":"未检出";
        if(!row.valid) {++invalid;row.item.evidence="累加结果溢出，未完成判定";row.item.accumulatedIntensities.clear();}
        if(row.valid && all) {
            MatchCandidate c;c.referenceId=row.item.referenceId;c.name=row.item.name;c.category=row.category;
            c.measuredMz=row.item.precursorMz;c.matchedFragments=exceeded;c.requiredFragments=row.item.fragmentMz.size();
            c.evidence=row.item.evidence;c.demo=false;result->candidates.append(c);
        }
        result->screeningItems.append(row.item);
    }
    if(invalid && error)*error=QString("%1条物质未完成筛查，请在筛查详情核对定性离子和一级阈值").arg(invalid);
    return invalid?QString("PARTIAL"):QString("COMPLETE");
}
}
