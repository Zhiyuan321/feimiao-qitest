#include "ui/DeviceWaveformPanel.h"
#include "app/AppController.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMessageBox>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace qitest {
namespace {
// Concatenate received samples; time spacing is supplied by the confirmed method.
class VoltagePlot final : public QWidget {
public:
    explicit VoltagePlot(bool tuning):tuning_(tuning) {
        repaintTimer_.setSingleShot(true);repaintTimer_.setInterval(16);
        repaintTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&repaintTimer_,&QTimer::timeout,this,[this]{update();});
        setProperty("frameIntervalMs",16);setMinimumSize(240,220);setMouseTracking(true);
        setProperty("yMaximum",scaleMaximum());
    }
    void setValues(const QVector<double> &v,double minutesPerSample=0) {
        if(values_==v && minutesPerSample_==minutesPerSample)return;
        values_=v;minutesPerSample_=minutesPerSample;
        setProperty("sampleCount",v.size());setProperty("yMaximum",scaleMaximum());
        setProperty("xUnit",minutesPerSample_>0?"min":"采样点");
        setProperty("xMaximum",std::max(0,v.size()-1)*(minutesPerSample_>0?minutesPerSample_:1));
        if(!repaintTimer_.isActive())repaintTimer_.start();
    }
protected:
    QRectF area() const {return QRectF(66,24,std::max(1,width()-90),std::max(1,height()-82));}
    double scaleMaximum() const {
        if (!tuning_ || values_.isEmpty()) return 6.0;
        const double peak=*std::max_element(values_.cbegin(),values_.cend());
        return peak<=6.0 ? 6.0 : std::ceil(peak/5.0)*5.0;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#f7faf9"));
        const auto a=area();const double yMaximum=scaleMaximum();p.setPen(QColor("#344540"));p.drawLine(a.bottomLeft(),a.topLeft());p.drawLine(a.bottomLeft(),a.bottomRight());
        if(!tuning_) for(int i=0;i<=6;++i) {
            const double y=a.bottom()-a.height()*i/6;
            p.setPen(QColor("#dae5e0"));p.drawLine(QPointF(a.left(),y),QPointF(a.right(),y));
            p.setPen(QColor("#344540"));p.drawText(QRectF(2,y-10,55,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(yMaximum*i/6.0,'f',2));
        }
        p.drawText(QRectF(2,2,60,22),Qt::AlignCenter,tuning_?"mV":"V");
        p.drawText(QRectF(a.left(),a.bottom()+25,a.width(),36),Qt::AlignCenter,
            tuning_?"序列":minutesPerSample_>0?"时间 / min":"采样点（时间参数未确认）");
        if(values_.isEmpty()) {
            p.drawText(a.adjusted(8,8,-8,-8),Qt::AlignCenter|Qt::TextWordWrap,
                tuning_?"等待确认RF数据换算":"等待网口气压数据");return;
        }
        p.drawText(QRectF(a.left(),a.bottom()+2,55,20),Qt::AlignLeft,"0");
        for(int i=1;i<=5;++i) {
            const double index=(values_.size()-1)*i/5.0;
            p.drawText(QRectF(a.left()+a.width()*i/5-(i==5?80:40),a.bottom()+2,80,20),i==5?Qt::AlignRight:Qt::AlignCenter,
                QString::number(index*(minutesPerSample_>0?minutesPerSample_:1),'g',4));
        }
        p.save();p.setClipRect(a);p.setPen(QPen(QColor("#008580"),1.5));QPainterPath path;
        for(int i=0;i<values_.size();++i) {
            QPointF point(a.left()+a.width()*i/std::max(1,values_.size()-1),a.bottom()-a.height()*values_[i]/yMaximum);
            if(!i)path.moveTo(point);else path.lineTo(point);
            if(values_.size()==1)p.drawEllipse(point,2,2);
        }
        p.drawPath(path);p.restore();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        const auto a=area();
        if(values_.isEmpty()||!a.contains(event->pos())) {setToolTip({});return;}
        const int index=qBound(0,qRound((event->pos().x()-a.left())/a.width()*(values_.size()-1)),values_.size()-1);
        setToolTip(minutesPerSample_>0
            ?QString("时间 %1 min：%2 V").arg(index*minutesPerSample_,0,'g',6).arg(values_[index],0,'f',3)
            :QString("采样点 %1：%2 V").arg(index).arg(values_[index],0,'f',3));
    }
private:
    bool tuning_;QVector<double> values_;QTimer repaintTimer_;double minutesPerSample_=0;
};
}
QWidget *createDeviceWaveformPanel(AppController *controller,bool tuning,QWidget *parent) {
    auto *page=new QWidget(parent);page->setObjectName(tuning?"rfTuningPanel":"pressureWaveformPanel");
    auto *layout=new QVBoxLayout(page);
    auto *status=new QLabel(page);status->setWordWrap(true);status->setObjectName(tuning?"rfTuningStatus":"pressureWaveformStatus");
    QPushButton *start=nullptr,*stop=nullptr;
    if(tuning) {
        auto *buttons=new QHBoxLayout;start=new QPushButton("检测");stop=new QPushButton("结束");
        start->setObjectName("rfTuningStart");stop->setObjectName("rfTuningStop");
        start->setMinimumHeight(44);stop->setMinimumHeight(44);
        buttons->addWidget(start);buttons->addWidget(stop);buttons->addStretch();layout->addLayout(buttons);
        QObject::connect(start,&QPushButton::clicked,page,[=] {
            if(QMessageBox::question(page,"射频调谐","向仪器发送调谐开启指令？RF曲线换算仍待确认。") == QMessageBox::Yes)
                controller->requestRfTuning(true,true);
        });
        QObject::connect(stop,&QPushButton::clicked,page,[=]{controller->requestRfTuning(false);});
    }
    auto *plot=new VoltagePlot(tuning);plot->setObjectName(tuning?"rfVoltagePlot":"pressureVoltagePlot");
    layout->addWidget(plot,1);
    layout->addWidget(status);
    const auto refresh=[=] {
        const auto data=controller->networkStatus();
        if(tuning) {
            const bool connected=data.value("tcpConnected").toBool(),pending=data.value("tuningPending").toBool();
            start->setEnabled(controller->canTune()&&connected&&!pending&&data.value("connected").toBool()&&!data.value("experimentRunning").toBool());
            stop->setEnabled(controller->canTune()&&connected&&!pending);
            status->setText(data.value("tuningMessage","请先连接网口仪器").toString());
        } else {
            const auto values=controller->pressureVolts();plot->setValues(values,data.value("pressureSampleIntervalMinutes").toDouble());
            QString text=QString("已接收 %1 帧气压数据；当前周期 %2，共 %3 点。按接收顺序连续显示。")
                .arg(data.value("pressureFrames",0).toULongLong()).arg(data.value("pressureCycle",-1).toInt()).arg(values.size());
            if(!values.isEmpty()) {
                const auto high=*std::max_element(values.cbegin(),values.cend());
                text+=QString(" 峰值 %1 V，纵轴固定为0.00～6.00 V，超出范围的部分不显示。").arg(high,0,'f',2);
            }
            const bool completed=data.value("pressureAcquisitionCompleted").toBool();
            status->setText(completed
                ? (values.isEmpty()?QString("检测已结束，未收到气压数据")
                    :QString("检测已结束 · 保留整次检测气压曲线 · %1 点").arg(values.size()))
                : (values.isEmpty()?QString("等待网口气压数据")
                    :QString("气压曲线 · 连续保留至周期 %1 · %2 点").arg(data.value("pressureCycle").toInt()).arg(values.size())));
            if(!values.isEmpty()) {
                const auto bounds=std::minmax_element(values.cbegin(),values.cend());
                status->setText(status->text()+QString("\n最低 %1 V · 峰值 %2 V").arg(*bounds.first,0,'f',3).arg(*bounds.second,0,'f',3));
            }
            if(data.value("pressureDisplayLimit").toBool())status->setText(status->text()+" · 已达100万点显示上限，后续点未显示");
            status->setToolTip(text);
            plot->setToolTip(text);
        }
    };
    QObject::connect(controller,&AppController::instrumentSettingsChanged,page,refresh);
    QObject::connect(controller,&AppController::sessionChanged,page,refresh);
    refresh();return page;
}
}
