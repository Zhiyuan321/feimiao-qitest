#include "ui/SpectrumPlot.h"
#include "ui/scientz/theme/ScientzTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QCoreApplication>
#include <memory>
#include <algorithm>
#include <cmath>

namespace qitest {

SpectrumPlot::SpectrumPlot(Mode mode, QWidget *parent) : QWidget(parent), mode_(mode) {
    // Coalesce data-driven repaint requests at about 30 FPS without dropping source points.
    repaintTimer_ = new QTimer(this);
    repaintTimer_->setSingleShot(true);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, [this] { update(); });
    setMinimumHeight(mode == Mode::Line ? 145 : 210);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip("滚轮缩放 · 双击复位 · 右键切换图形或导出原始绘图数据");
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint &position) {
        QMenu menu(this);
        auto *reset=menu.addAction("复位视图");
        auto *mode=menu.addAction(mode_==Mode::Line ? "切换棒图":"切换轮廓图");
        auto *exportAction=menu.addAction("导出全分辨率数据 CSV");
        exportAction->setEnabled(!points_.isEmpty() && !exportThread_);
        const auto action=menu.exec(mapToGlobal(position));
        if(action==reset) resetView();
        else if(action==mode) setMode(mode_==Mode::Line ? Mode::Sticks:Mode::Line);
        else if(action==exportAction) {
            const auto path=QFileDialog::getSaveFileName(this,"导出当前图的全分辨率数据","spectrum.csv","CSV (*.csv)");
            if(!path.isEmpty()) exportCsv(path);
        }
    });
    connect(this,&SpectrumPlot::exportFinished,this,[this](bool ok,const QString &message) {
        setToolTip(message);
        if(!ok && isVisible()) QMessageBox::warning(this,"导出未完成",message);
    });
}

bool SpectrumPlot::exportCsv(const QString &path) {
    if(points_.isEmpty() || exportThread_ || path.isEmpty()) return false;
    const auto data=points_; // Implicitly shared immutable snapshot, never the reduced render cache.
    const auto xLabel=QString(xAxisLabel_).replace('\n',' ').replace('\r',' ');
    const auto yLabel=QString(yAxisLabel_).replace('\n',' ').replace('\r',' ');
    struct Result { bool ok=false; QString message; };
    auto result=std::make_shared<Result>();
    auto *thread=QThread::create([=] {
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)) { result->message=file.errorString(); return; }
        QByteArray buffer=("# x_axis: "+xLabel+"\n# y_axis: "+yLabel+"\nx,y\n").toUtf8();
        for(const auto &point:data) {
            if(QThread::currentThread()->isInterruptionRequested()) { result->message="已取消，原文件未改变"; return; }
            buffer+=QByteArray::number(point.mz,'g',17)+','+QByteArray::number(point.intensity,'g',17)+'\n';
            if(buffer.size()>=65536) {
                if(file.write(buffer)!=buffer.size()) { result->message=file.errorString(); return; }
                buffer.clear();
            }
        }
        if(QThread::currentThread()->isInterruptionRequested()) { result->message="已取消，原文件未改变"; return; }
        result->ok=file.write(buffer)==buffer.size() && file.commit();
        result->message=result->ok ? "已导出："+path:file.errorString();
    });
    exportThread_=thread;
    connect(this,&QObject::destroyed,thread,[thread] { thread->requestInterruption(); });
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,thread,[thread] { thread->requestInterruption(); thread->wait(5000); });
    connect(thread,&QThread::finished,this,[this,result] { exportThread_=nullptr; emit exportFinished(result->ok,result->message); });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    thread->start(QThread::LowPriority); return true;
}

void SpectrumPlot::setPoints(const QVector<SpectrumPoint> &points) {
    points_ = points;
    zoomed_=false; cacheWidth_=-1; hoveredIndex_=-1;
    viewMinimum_=points_.isEmpty() ? 0:points_.first().mz;
    viewMaximum_=points_.isEmpty() ? 1:points_.last().mz;
    if (!repaintTimer_->isActive()) repaintTimer_->start();
}

void SpectrumPlot::setMode(Mode mode) { mode_=mode; update(); }
void SpectrumPlot::resetView() {
    zoomed_=false; cacheWidth_=-1; hoveredIndex_=-1;
    viewMinimum_=points_.isEmpty() ? 0:points_.first().mz;
    viewMaximum_=points_.isEmpty() ? 1:points_.last().mz;
    update();
}
void SpectrumPlot::zoomAt(double centerX, double factor) {
    if(points_.size()<2 || !std::isfinite(centerX) || !std::isfinite(factor) || factor<=0) return;
    const double full=points_.last().mz-points_.first().mz;
    if(full<=0) return;
    const double span=std::clamp((viewMaximum_-viewMinimum_)*factor,full/10000.0,full);
    if(span>=full) { resetView(); return; }
    const double ratio=std::clamp((centerX-viewMinimum_)/(viewMaximum_-viewMinimum_),0.0,1.0);
    viewMinimum_=std::clamp(centerX-ratio*span,points_.first().mz,points_.last().mz-span);
    viewMaximum_=viewMinimum_+span; zoomed_=true; cacheWidth_=-1; hoveredIndex_=-1; update();
}
void SpectrumPlot::wheelEvent(QWheelEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto position=event->position();
#else
    const auto position=event->posF();
#endif
    if(!plotRect().contains(position) || !event->angleDelta().y()) { event->ignore(); return; }
    const double x=viewMinimum_+(position.x()-plotRect().left())/plotRect().width()*(viewMaximum_-viewMinimum_);
    zoomAt(x,event->angleDelta().y()>0 ? 0.8:1.25); event->accept();
}
void SpectrumPlot::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button()==Qt::LeftButton) resetView();
    QWidget::mouseDoubleClickEvent(event);
}

void SpectrumPlot::ensureDisplayCache() const {
    const int columns=std::max(1,int(plotRect().width()));
    if(cacheWidth_==columns) return;
    cacheWidth_=columns; ++cacheBuildCount_; drawIndices_.clear(); cachedMaximum_=0.000001;
    if(points_.size()<2) return;
    const auto first=std::lower_bound(points_.cbegin(),points_.cend(),viewMinimum_,[](const SpectrumPoint &p,double x){ return p.mz<x; });
    const auto last=std::upper_bound(points_.cbegin(),points_.cend(),viewMaximum_,[](double x,const SpectrumPoint &p){ return x<p.mz; });
    const qsizetype begin=std::max<qsizetype>(0,std::distance(points_.cbegin(),first)-1);
    const qsizetype end=std::min<qsizetype>(points_.size(),std::distance(points_.cbegin(),last)+1);
    int bucket=-1; qsizetype start=begin, low=begin, high=begin, previous=begin;
    const auto flush=[&] {
        QVector<qsizetype> selected{start,low,high,previous};
        std::sort(selected.begin(),selected.end());
        for(auto index:selected) if(drawIndices_.isEmpty() || drawIndices_.last()!=index) drawIndices_.append(index);
    };
    for(qsizetype i=begin;i<end;++i) {
        const double relative=(points_[i].mz-viewMinimum_)/std::max(1e-12,viewMaximum_-viewMinimum_);
        const int next=int(std::floor(std::clamp(relative,-1.0,2.0)*columns));
        if(next!=bucket) { if(i>begin) flush(); bucket=next; start=low=high=i; }
        if(points_[i].intensity<points_[low].intensity) low=i;
        if(points_[i].intensity>points_[high].intensity) high=i;
        cachedMaximum_=std::max(cachedMaximum_,points_[i].intensity); previous=i;
    }
    if(end>begin) flush();
    // At most four original points per display column (extrema and endpoints).
    // Full resolution stays in points_ for export, selection and computation.
}

void SpectrumPlot::setEmptyMessage(QString title, QString detail) {
    emptyTitle_ = std::move(title);
    emptyDetail_ = std::move(detail);
}

void SpectrumPlot::setAxisLabels(QString xAxis, QString yAxis) {
    xAxisLabel_ = std::move(xAxis);
    yAxisLabel_ = std::move(yAxis);
    update();
}

void SpectrumPlot::setAccentColor(const QColor &color) {
    accentColor_ = color;
    update();
}

QRectF SpectrumPlot::plotRect() const {
    // Reserve space for scientific notation and the last x tick on both Qt
    // font backends. Fixed 45 px clipped million-scale TIC labels on macOS.
    const QFontMetricsF ticks(QFont(font().family(), 10));
    const double left = std::max(64.0, ticks.horizontalAdvance("9.99e+099") + 8.0);
    return QRectF(left, 16.0, std::max(1.0, width() - left - 30.0),
                  std::max(1.0, height() - 54.0));
}

double SpectrumPlot::maximumIntensity() const {
    ensureDisplayCache(); return cachedMaximum_;
}

QPointF SpectrumPlot::pointPosition(qsizetype index, const QRectF &plot, double maximum) const {
    if (index < 0 || index >= points_.size()) return {};
    const double minMz = viewMinimum_;
    const double range = std::max(1e-12, viewMaximum_ - minMz);
    return {plot.left() + (points_[index].mz - minMz) / range * plot.width(),
            plot.bottom() - points_[index].intensity / maximum * plot.height()};
}

qsizetype SpectrumPlot::nearestPointIndex(double x, const QRectF &plot) const {
    if (points_.size() < 2 || !plot.contains(QPointF(x, plot.center().y()))) return -1;
    const double ratio = std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0);
    const double targetMz = viewMinimum_ + ratio * (viewMaximum_ - viewMinimum_);
    const auto it = std::lower_bound(points_.cbegin(), points_.cend(), targetMz,
        [](const SpectrumPoint &point, double value) { return point.mz < value; });
    if (it == points_.cbegin()) return 0;
    if (it == points_.cend()) return points_.size() - 1;
    const qsizetype right = std::distance(points_.cbegin(), it);
    const qsizetype left = right - 1;
    return std::abs(points_[left].mz - targetMz) <= std::abs(points_[right].mz - targetMz)
        ? left : right;
}

void SpectrumPlot::mouseMoveEvent(QMouseEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const qsizetype next = nearestPointIndex(event->position().x(), plotRect());
#else
    const qsizetype next = nearestPointIndex(event->localPos().x(), plotRect());
#endif
    if (next != hoveredIndex_) {
        hoveredIndex_ = next;
        if (hoveredIndex_ >= 0) {
            setToolTip(QString("%1 %2 · %3 %4").arg(xAxisLabel_)
                .arg(points_[hoveredIndex_].mz, 0, 'g', 6).arg(yAxisLabel_)
                .arg(points_[hoveredIndex_].intensity, 0, 'g', 6));
        } else {
            setToolTip({});
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SpectrumPlot::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto pos = event->position();
#else
        const auto pos = event->localPos();
#endif
        const auto index = nearestPointIndex(pos.x(), plotRect());
        if (plotRect().contains(pos) && index >= 0) emit pointActivated(points_[index].mz);
    }
    QWidget::mousePressEvent(event);
}

void SpectrumPlot::leaveEvent(QEvent *event) {
    if (hoveredIndex_ >= 0) {
        hoveredIndex_ = -1;
        setToolTip({});
        update();
    }
    QWidget::leaveEvent(event);
}

void SpectrumPlot::paintEvent(QPaintEvent *) {
    QPainter painter(this);
#ifdef QITEST_WIN7
    // Integer-pixel Win7 displays benefit more from predictable CPU cost than
    // sub-pixel curve smoothing. Text rendering remains handled by Qt/fonts.
    painter.setRenderHint(QPainter::Antialiasing, false);
#else
    painter.setRenderHint(QPainter::Antialiasing);
#endif
    const QRectF plot = plotRect();
    const double maximum = maximumIntensity();
    QColor plotBackground = Scientz::Ui::Colors::Teal50;
    plotBackground.setAlpha(80);
    painter.fillRect(plot, plotBackground);
    painter.setPen(QPen(Scientz::Ui::Colors::Graphite100, 0.8));
    const int yIntervals = plot.height() < 100 ? 2 : 4;
    for (int i = 0; i <= yIntervals; ++i) {
        const double y = plot.top() + plot.height() * i / yIntervals;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        if (points_.size() >= 2) {
            painter.save();
            painter.setPen(Scientz::Ui::Colors::TextSecondary);
            painter.setFont(QFont(painter.font().family(), 10));
            painter.drawText(QRectF(0, y - 10, plot.left() - 4, 20), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(maximum * (yIntervals-i)/yIntervals, 'g', 3));
            painter.restore();
        }
    }
    const double axisMin = points_.size() >= 2 ? viewMinimum_ : (xAxisLabel_ == "m/z" ? 50.0 : 0.0);
    const double axisMax = points_.size() >= 2 ? viewMaximum_ : (xAxisLabel_ == "m/z" ? 500.0 : 1.0);
    for (int i = 0; i <= 5; ++i) {
        const double x = plot.left() + plot.width() * i / 5.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(Scientz::Ui::Colors::TextSecondary);
        painter.setFont(QFont(painter.font().family(), 10));
        painter.drawText(QRectF(x - 32, plot.bottom() + 3, 64, 18), Qt::AlignCenter,
                         QString::number(axisMin + (axisMax - axisMin) * i / 5.0, 'g', 4));
        painter.setPen(QPen(Scientz::Ui::Colors::Graphite100, 0.8));
    }
    painter.setPen(Scientz::Ui::Colors::TextSecondary);
    painter.drawText(QRectF(plot.center().x() - 80, plot.bottom() + 19, 160, 15), Qt::AlignCenter, xAxisLabel_);
    if (!yAxisLabel_.isEmpty()) {
        painter.save();
        painter.setFont(QFont(painter.font().family(), 8));
        painter.drawText(QRectF(plot.left(), 0, plot.width(), 12), Qt::AlignRight, yAxisLabel_);
        painter.restore();
    }

    if (points_.size() < 2) {
        painter.setPen(Scientz::Ui::Colors::Teal700);
        QFont titleFont = painter.font();
        titleFont.setPointSize(12);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(QRectF(plot.left(), plot.center().y() - 18, plot.width(), 22), Qt::AlignCenter, emptyTitle_);
        painter.setPen(Scientz::Ui::Colors::TextSecondary);
        painter.setFont(QFont(painter.font().family(), 9));
        painter.drawText(QRectF(plot.left(), plot.center().y() + 5, plot.width(), 18), Qt::AlignCenter, emptyDetail_);
        return;
    }

    const double minMz = viewMinimum_;
    const double maxMz = viewMaximum_;
    const double range = std::max(1e-12, maxMz - minMz);
    painter.setPen(QPen(accentColor_, mode_ == Mode::Line ? 1.9 : 1.2,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    painter.save();
    painter.setClipRect(plot);
    if (mode_ == Mode::Line) {
        QPainterPath path;
        bool firstDraw=true;
        for (qsizetype i : drawIndices_) {
            const double x = plot.left() + (points_[i].mz - minMz) / range * plot.width();
            const double y = plot.bottom() - points_[i].intensity / maximum * plot.height();
            firstDraw ? path.moveTo(x,y):path.lineTo(x,y); firstDraw=false;
        }
        QPainterPath fillPath(path);
        fillPath.lineTo(plot.right(), plot.bottom());
        fillPath.lineTo(plot.left(), plot.bottom());
        fillPath.closeSubpath();
        QColor fillTop = accentColor_;
        fillTop.setAlpha(44);
        QColor fillBottom = accentColor_;
        fillBottom.setAlpha(3);
        QLinearGradient fill(plot.topLeft(), plot.bottomLeft());
        fill.setColorAt(0.0, fillTop);
        fill.setColorAt(1.0, fillBottom);
        painter.fillPath(fillPath, fill);
        painter.setPen(QPen(accentColor_, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    } else {
        for (const auto index : drawIndices_) {
            const auto &point=points_[index];
            const double x = plot.left() + (point.mz - minMz) / range * plot.width();
            const double y = plot.bottom() - point.intensity / maximum * plot.height();
            painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, y));
        }
    }

    painter.restore();
    if (hoveredIndex_ >= 0 && hoveredIndex_ < points_.size()) {
        const QPointF position = pointPosition(hoveredIndex_, plot, maximum);
        if(!plot.contains(position)) return;
        QColor guide = Scientz::Ui::Colors::Graphite500;
        guide.setAlpha(150);
        painter.setPen(QPen(guide, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(position.x(), plot.top()), QPointF(position.x(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), position.y()), QPointF(plot.right(), position.y()));

        painter.setPen(QPen(Scientz::Ui::Colors::Panel, 2.0));
        painter.setBrush(accentColor_);
        painter.drawEllipse(position, 4.5, 4.5);

        const QString value = QString("%1 %2  ·  %3")
            .arg(xAxisLabel_).arg(points_[hoveredIndex_].mz, 0, 'g', 6)
            .arg(points_[hoveredIndex_].intensity, 0, 'g', 6);
        QFont valueFont = painter.font();
        valueFont.setPointSize(9);
        valueFont.setWeight(QFont::DemiBold);
        painter.setFont(valueFont);
        const qreal bubbleWidth = std::min(plot.width(), qreal(painter.fontMetrics().horizontalAdvance(value) + 20.0));
        QRectF bubble(position.x() + 10.0, position.y() - 34.0, bubbleWidth, 27.0);
        if (bubble.right() > plot.right() - 4.0) bubble.moveRight(position.x() - 10.0);
        if (bubble.left() < plot.left() + 4.0) bubble.moveLeft(plot.left() + 4.0);
        if (bubble.top() < plot.top() + 4.0) bubble.moveTop(position.y() + 10.0);
        if (bubble.bottom() > plot.bottom()) bubble.moveBottom(plot.bottom());
        painter.setPen(Qt::NoPen);
        painter.setBrush(Scientz::Ui::Colors::Graphite900);
        painter.drawRoundedRect(bubble, 7.0, 7.0);
        painter.setPen(Scientz::Ui::Colors::Panel);
        painter.drawText(bubble, Qt::AlignCenter, painter.fontMetrics().elidedText(value, Qt::ElideRight, int(bubbleWidth - 12)));
    }
}

} // namespace qitest
