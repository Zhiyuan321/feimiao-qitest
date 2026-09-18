#pragma once
#include <QString>
#include <QVariant>
#include <cmath>

namespace qitest {
// Physical waits have no whole-sequence timeout; transport replies remain bounded.
class ShutdownSequence {
public:
    enum class Stage { Idle, StoppingHeating, Cooling, StoppingPump, StoppingDiaphragm, Complete, Failed };
    Stage stage() const { return stage_; }
    QString error() const { return error_; }
    void begin() { stage_=Stage::StoppingHeating; error_.clear(); }
    QString pendingKey() const {
        if(stage_==Stage::StoppingHeating) return "heatingOn";
        if(stage_==Stage::StoppingPump) return "molecularPumpOn";
        if(stage_==Stage::StoppingDiaphragm) return "diaphragmPumpOn";
        return {};
    }
    void confirmed(const QString &key,bool success,const QVariant &actual) {
        if(key!=pendingKey() || key.isEmpty()) return;
        if(!success || actual.userType()!=QMetaType::Bool || actual.toBool()) {
            fail("关机步骤未确认："+key);return;
        }
        if(stage_==Stage::StoppingHeating) stage_=Stage::Cooling;
        else if(stage_==Stage::StoppingPump) stage_=Stage::StoppingDiaphragm;
        else if(stage_==Stage::StoppingDiaphragm) stage_=Stage::Complete;
    }
    void observeTemperature(double celsius,bool fresh) {
        if(stage_!=Stage::Cooling) return;
        if(!fresh || !std::isfinite(celsius)) {fail("离子阱温度回读失效，关机流程已停止");return;}
        if(celsius<75.0) stage_=Stage::StoppingPump;
    }
    void fail(const QString &reason) {stage_=Stage::Failed;error_=reason;}
private:
    Stage stage_=Stage::Idle;
    QString error_;
};
}
