#pragma once

#include "domain/Models.h"
#include <QColor>
#include <QRectF>
#include <QWidget>
#include <QPointer>
class QThread;
class QTimer;

namespace qitest {

class SpectrumPlot final : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Line, Sticks };

    explicit SpectrumPlot(Mode mode, QWidget *parent = nullptr);
    void setPoints(const QVector<SpectrumPoint> &points);
    void setEmptyMessage(QString title, QString detail);
    void setAxisLabels(QString xAxis, QString yAxis = {});
    void setAccentColor(const QColor &color);
    void setMode(Mode mode);
    void resetView();
    void zoomAt(double centerX, double factor);
    bool isZoomed() const { return zoomed_; }
    const QVector<SpectrumPoint> &points() const { return points_; }
    int renderedPointCount() const { return drawIndices_.size(); }
    int cacheBuildCount() const { return cacheBuildCount_; }
    bool exportCsv(const QString &path);
signals:
    void pointActivated(double x);
    void exportFinished(bool success, const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QRectF plotRect() const;
    double maximumIntensity() const;
    QPointF pointPosition(qsizetype index, const QRectF &plot, double maximum) const;
    qsizetype nearestPointIndex(double x, const QRectF &plot) const;
    void ensureDisplayCache() const;

    Mode mode_;
    QVector<SpectrumPoint> points_;
    QString emptyTitle_;
    QString emptyDetail_;
    QString xAxisLabel_ = "m/z";
    QString yAxisLabel_;
    QColor accentColor_{"#00A8A8"};
    qsizetype hoveredIndex_ = -1;
    double viewMinimum_ = 0, viewMaximum_ = 1;
    bool zoomed_ = false;
    mutable int cacheWidth_ = -1, cacheBuildCount_ = 0;
    mutable double cachedMaximum_ = 1;
    mutable QVector<qsizetype> drawIndices_;
    QPointer<QThread> exportThread_;
    QTimer *repaintTimer_ = nullptr;
};

} // namespace qitest
