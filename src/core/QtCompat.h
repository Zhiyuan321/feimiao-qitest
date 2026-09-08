#pragma once

#include <QString>

namespace qitest {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
inline constexpr auto skipEmptyParts = QString::SkipEmptyParts;
#else
inline constexpr auto skipEmptyParts = Qt::SkipEmptyParts;
#endif
} // namespace qitest
