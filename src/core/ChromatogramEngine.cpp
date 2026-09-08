#include "core/ChromatogramEngine.h"
#include <algorithm>
#include <cmath>

namespace qitest {
bool ChromatogramEngine::validate(const QVector<SpectrumScan> &scans, QString *error) {
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (scans.isEmpty() || scans.size() > MaximumScans) return fail("扫描数必须为 1–5000，请分段导入");
    double previousTime = -1.0;
    qint64 count = 0;
    for (const auto &scan : scans) {
        if (!std::isfinite(scan.timeSeconds) || scan.timeSeconds <= previousTime || scan.timeSeconds < 0.0)
            return fail("扫描时间必须有限、非负且严格递增，单位为秒");
        if (scan.msLevel != 1 && scan.msLevel != 2) return fail("扫描级别仅支持 MS1 或 MS2");
        if (scan.points.isEmpty() || (count += scan.points.size()) > MaximumPoints)
            return fail("扫描为空或谱点总数超过 100 万，请分段导入");
        double previousMz = 0.0, sum = 0.0;
        for (const auto &point : scan.points) {
            if (!std::isfinite(point.mz) || !std::isfinite(point.intensity)
                    || point.mz <= previousMz || point.intensity < 0.0)
                return fail("每次扫描的 m/z 必须正值递增，强度必须有限且非负");
            previousMz = point.mz;
            sum += point.intensity;
            if (!std::isfinite(sum)) return fail("总离子信号溢出");
        }
        previousTime = scan.timeSeconds;
    }
    return true;
}

QVector<SpectrumPoint> ChromatogramEngine::trace(const QVector<SpectrumScan> &scans,
        Kind kind, int level, double target, double tolerance) {
    QVector<SpectrumPoint> result;
    if (!validate(scans) || (level != 1 && level != 2)) return result;
    if (kind == Kind::Eic && (!std::isfinite(target) || target <= 0.0
            || !std::isfinite(tolerance) || tolerance <= 0.0)) return result;
    for (const auto &scan : scans) {
        if (scan.msLevel != level) continue;
        double value = 0.0;
        for (const auto &point : scan.points) {
            if (kind == Kind::Bpc) value = std::max(value, point.intensity);
            else if (kind == Kind::Tic || std::abs(point.mz - target) <= tolerance) value += point.intensity;
        }
        result.append({scan.timeSeconds, value});
    }
    return result;
}

ChromatogramEngine::Integral ChromatogramEngine::integrate(const QVector<SpectrumPoint> &points,
        double from, double to, bool baseline) {
    Integral result;
    if (points.size() < 2 || !std::isfinite(from) || !std::isfinite(to)
            || from >= to || from < points.first().mz || to > points.last().mz) {
        result.error = "请选择曲线范围内两个不同的时间边界"; return result;
    }
    double previous = -1.0;
    for (const auto &p : points) {
        if (!std::isfinite(p.mz) || !std::isfinite(p.intensity) || p.mz < 0.0
                || p.mz <= previous || p.intensity < 0.0) {
            result.error = "时间序列无效"; return result;
        }
        previous = p.mz;
    }
    const auto interpolate = [&points](double t) {
        const auto right = std::lower_bound(points.cbegin(), points.cend(), t,
            [](const SpectrumPoint &p, double x) { return p.mz < x; });
        if (right == points.cbegin()) return right->intensity;
        const auto left = right - 1;
        return left->intensity + (right->intensity - left->intensity)
            * (t - left->mz) / (right->mz - left->mz);
    };
    QVector<SpectrumPoint> selected{{from, interpolate(from)}};
    for (const auto &p : points) if (p.mz > from && p.mz < to) selected.append(p);
    selected.append({to, interpolate(to)});
    for (int i = 1; i < selected.size(); ++i)
        result.area += (selected[i].mz - selected[i-1].mz)
            * (selected[i].intensity + selected[i-1].intensity) / 2.0;
    if (baseline) result.area -= (to - from) * (selected.first().intensity + selected.last().intensity) / 2.0;
    // Signed endpoint-baseline area is preserved, not silently clipped to zero.
    result.valid = std::isfinite(result.area);
    if (!result.valid) result.error = "积分结果溢出";
    return result;
}
}
