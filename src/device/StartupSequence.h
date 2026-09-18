#pragma once

#include <QString>
#include <QVariant>
#include <QVector>
#include <cmath>

namespace qitest {
// Pure sequencing policy. It does not own a port or infer a pump acknowledgement.
// The transport must correlate each completion to its request and validate actual
// device replies. An accepted write is NOT a completed step.
class StartupSequence {
public:
    // Fixed startup temperatures confirmed by the user (not method/preset values).
    static constexpr int TdTargetC = 250;
    static constexpr int TdToleranceC = 30;
    static constexpr int TrapTargetC = 85;
    static constexpr int TrapToleranceC = 5;
    // User-confirmed rough-vacuum gate: strictly below 8E0 mbar.
    static bool molecularPumpStartPressureAllowed(double mbar) {
        return std::isfinite(mbar) && mbar > 0 && mbar < 8.0;
    }
    enum class Stage { Idle, Preparing, PreparingRunning, WaitingForRoughVacuum, StartingPump,
                       WaitingForVacuum, StartingTrapHeating, Complete, Failed };
    struct Action { QString key; QVariant value; };

    bool begin() {
        if (stage_ != Stage::Idle) return false;
        stage_ = Stage::Preparing;
        // These belong to the same initial phase. A shared serial bus dispatches
        // them without overlapping requests; neither waits for vacuum readiness.
        pending_ = {{"diaphragmPumpOn", true}, {"tdTemperatureC", TdTargetC}};
        return true;
    }
    // Explicit continuation after readback proves both pumps already running.
    // Do not repeat gas-mode or pump-start commands on this path.
    bool resumeWithRunningPumps(bool tdAlreadyConfirmed) {
        if(stage_!=Stage::Idle) return false;
        pumpConfirmed_=true;vacuumFresh_=false;
        stage_=tdAlreadyConfirmed?Stage::WaitingForVacuum:Stage::PreparingRunning;
        if(!tdAlreadyConfirmed) pending_={{"tdTemperatureC",TdTargetC}};
        return true;
    }
    Stage stage() const { return stage_; }
    QString error() const { return error_; }
    QVector<Action> pendingActions() const { return pending_; }
    bool vacuumReady() const { return vacuumReady_; }
    void observeCarrierFlow(double mlMin, bool fresh) {
        carrierKnown_ = fresh && std::isfinite(mlMin) && mlMin >= 0;
        carrierOn_ = carrierKnown_ && mlMin > 0;
        assessVacuum();
    }

    void confirmed(const QString &key, const QVariant &actual, bool success) {
        if (terminal()) return;
        int index = -1;
        for (int i = 0; i < pending_.size(); ++i)
            if (pending_[i].key == key) { index = i; break; }
        if (index < 0) return; // Unrelated/duplicate replies cannot advance stages.
        if (!success || !actual.isValid() || actual != pending_[index].value) {
            fail("开机步骤未确认：" + key); return;
        }
        pending_.removeAt(index);
        if (!pending_.isEmpty()) return;
        switch (stage_) {
        case Stage::Preparing: stage_ = Stage::WaitingForRoughVacuum; break;
        case Stage::PreparingRunning: stage_=Stage::WaitingForVacuum;break;
        case Stage::StartingPump:
            stage_ = Stage::WaitingForVacuum; pumpConfirmed_ = true;
            vacuumFresh_ = false; // Require a new measurement after pump confirmation.
            break;
        case Stage::StartingTrapHeating: stage_ = Stage::Complete; break;
        default: break;
        }
    }
    void observeVacuum(double mbar, bool fresh) {
        if (stage_ == Stage::Idle || stage_ == Stage::Failed) return;
        vacuumFresh_ = fresh && std::isfinite(mbar) && mbar > 0;
        vacuumMbar_ = mbar;
        if (!fresh || !std::isfinite(mbar) || mbar <= 0) {
            vacuumReady_ = false;
            if (stage_ != Stage::Complete) fail("真空读数失效，已停止后续开机步骤");
            return;
        }
        if (stage_ == Stage::WaitingForRoughVacuum && molecularPumpStartPressureAllowed(mbar)) {
            stage_ = Stage::StartingPump;
            pending_ = {{"molecularPumpOn", true}};
        }
        assessVacuum();
    }
    void fail(const QString &reason) {
        if (stage_ == Stage::Complete || stage_ == Stage::Failed) return;
        stage_ = Stage::Failed; error_ = reason; pending_.clear(); vacuumReady_ = false;
        // No automatic rollback: shutdown must be explicitly requested.
    }
private:
    void assessVacuum() {
        // User correction: actual valid EFC flow, NOT internal/external mode,
        // determines gas presence. Unknown/stale flow never means gas OFF.
        vacuumReady_ = stage_ != Stage::Failed && pumpConfirmed_ && vacuumFresh_
            && carrierKnown_ && vacuumMbar_ < (carrierOn_ ? 1e-2 : 1e-4);
        if (stage_ == Stage::WaitingForVacuum && vacuumReady_) {
            stage_ = Stage::StartingTrapHeating;
            pending_ = {{"trapTemperatureC", TrapTargetC}};
        }
        // Continue assessing after completion as gas flow changes. This updates
        // readiness only; no unconfirmed automatic shutdown/heater-stop policy.
    }
    bool terminal() const {
        return stage_ == Stage::Idle || stage_ == Stage::Complete || stage_ == Stage::Failed;
    }
    Stage stage_ = Stage::Idle;
    QVector<Action> pending_;
    QString error_;
    bool carrierKnown_ = false, carrierOn_ = false, pumpConfirmed_ = false;
    bool vacuumFresh_ = false, vacuumReady_ = false;
    double vacuumMbar_ = 0;
};
}
