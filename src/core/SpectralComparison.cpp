#include "core/SpectralComparison.h"
#include <algorithm>
#include <cmath>

namespace qitest {
namespace {
double maximum(const QVector<SpectrumPoint> &points) {
    if (points.isEmpty() || points.size() > SpectralComparison::MaximumPeaks) return 0;
    double last = 0, result = 0;
    for (const auto &p : points) {
        if (!std::isfinite(p.mz) || p.mz <= last || p.mz > 1e6
            || !std::isfinite(p.intensity) || p.intensity < 0) return 0;
        last = p.mz; result = std::max(result, p.intensity);
    }
    return result;
}
int nearest(const QVector<SpectrumPoint> &points, double mz) {
    auto found = std::lower_bound(points.begin(), points.end(), mz,
        [](const SpectrumPoint &p, double x) { return p.mz < x; });
    int i = int(found - points.begin());
    if (i == points.size()) return i - 1;
    if (i > 0 && mz - points[i-1].mz <= points[i].mz - mz) --i;
    return i;
}
}
SpectralComparisonResult SpectralComparison::compare(const QVector<SpectrumPoint> &query,
    const QVector<SpectrumPoint> &reference, double toleranceDa) {
    SpectralComparisonResult result;
    const double qmax = maximum(query), rmax = maximum(reference);
    if (!qmax || !rmax || !std::isfinite(toleranceDa) || toleranceDa < 0 || toleranceDa > 100) {
        result.error = "两侧须为 1–10000 个递增且不重复的有效峰，至少一个正强度；容差为 0–100 Da";
        return result;
    }
    // Scale before squaring: finite but very large instrument intensities must
    // not overflow the cosine norm. Scaling does not change the cosine.
    double qnorm = 0, rnorm = 0, dot = 0;
    for (const auto &p : query) qnorm += std::pow(p.intensity / qmax, 2);
    for (const auto &p : reference) rnorm += std::pow(p.intensity / rmax, 2);
    for (int i = 0; i < query.size(); ++i) {
        if (query[i].intensity <= 0) continue;
        const int j = nearest(reference, query[i].mz);
        const double delta = query[i].mz - reference[j].mz;
        if (reference[j].intensity <= 0 || std::abs(delta) > toleranceDa
            || nearest(query, reference[j].mz) != i) continue;
        result.pairs.append({i, j, delta});
        dot += (query[i].intensity / qmax) * (reference[j].intensity / rmax);
    }
    result.cosine = std::clamp(dot / (std::sqrt(qnorm) * std::sqrt(rnorm)), 0.0, 1.0);
    result.valid = true;
    return result;
}
}
