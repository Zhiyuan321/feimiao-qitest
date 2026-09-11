#include "ui/DeviceWaveformPanel.h"
#include "app/AppController.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMessageBox>
#include <algorithm>

namespace qitest {
namespace {
// A packet is never joined to another packet until cycle boundaries/time parameters are known.
class VoltagePlot final : public QWidget {
public:
    explicit VoltagePlot(bool tuning):tuning_(tuning) {setMinimumSize(240,220);setMouseTracking(true);}
    void setValues(const QVector<double> &v) {if(values_==v)return;values_=v;setProperty("sampleCount",v.size());update();}
protected:
    QRectF area() const {return QRectF(66,24,std::max(1,width()-90),std::max(1,height()-82));}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#f7faf9"));
        const auto a=area();p.setPen(QColor("#344540"));p.drawLine(a.bottomLeft(),a.topLeft());p.drawLine(a.bottomLeft(),a.bottomRight());
        if(!tuning_) for(int i=0;i<=6;++i) {
            const double y=a.bottom()-a.height()*i/6;
            p.setPen(QColor("#dae5e0"));p.drawLine(QPointF(a.left(),y),QPointF(a.right(),y));
            p.setPen(QColor("#344540"));p.drawText(QRectF(2,y-10,55,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(i,'f',2));
        }
        p.drawText(QRectF(2,2,60,22),Qt::AlignCenter,tuning_?"mV":"V");
        p.drawText(QRectF(a.left(),a.bottom()+25,a.width(),36),Qt::AlignCenter,
            tuning_?"序列":"单包采样点序号（时间参数待确认）");
        if(values_.isEmpty()) {
            p.drawText(a.adjusted(8,8,-8,-8),Qt::AlignCenter|Qt::TextWordWrap,
                tuning_?"等待确认RF数据换算":"等待网口气压数据");return;
        }
        p.drawText(QRectF(a.left(),a.bottom()+2,55,20),Qt::AlignLeft,"0");
        p.drawText(QRectF(a.right()-70,a.bottom()+2,70,20),Qt::AlignRight,QString::number(values_.size()-1));
        p.save();p.setClipRect(a);p.setPen(QPen(QColor("#008580"),1.5));QPainterPath path;
        for(int i=0;i<values_.size();++i) {
            QPointF point(a.left()+a.width()*i/std::max(1,values_.size()-1),a.bottom()-a.height()*values_[i]/6.0);
            if(!i)path.moveTo(point);else path.lineTo(point);
            if(values_.size()==1)p.drawEllipse(point,2,2);
        }
        p.drawPath(path);p.restore();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        const auto a=area();
        if(values_.isEmpty()||!a.contains(event->pos())) {setToolTip({});return;}
        const int index=qBound(0,qRound((event->pos().x()-a.left())/a.width()*(values_.size()-1)),values_.size()-1);
        setToolTip(QString("采样点 %1：%2 V").arg(index).arg(values_[index],0,'f',2));
    }
private:
    bool tuning_;QVector<double> values_;
};
}
QWidget *createDeviceWaveformPanel(AppController *controller,bool tuning,QWidget *parent) {
    auto *page=new QWidget(parent);page->setObjectName(tuning?"rfTuningPanel":"pressureWaveformPanel");
    auto *layout=new QVBoxLayout(page);
    auto *status=new QLabel;status->setWordWrap(true);status->setObjectName(tuning?"rfTuningStatus":"pressureWaveformStatus");
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
    layout->addWidget(plot,1);layout->addWidget(status);
    const auto refresh=[=] {
        const auto data=controller->networkStatus();
        if(tuning) {
            const bool connected=data.value("tcpConnected").toBool(),pending=data.value("tuningPending").toBool();
            start->setEnabled(controller->canTune()&&connected&&!pending&&data.value("connected").toBool()&&!data.value("experimentRunning").toBool());
            stop->setEnabled(controller->canTune()&&connected&&!pending);
            status->setText(!controller->canTune()?"调谐启停需管理员或工程师账号":data.value("tuningMessage","请先连接网口仪器").toString());
        } else {
            const auto values=controller->pressureVolts();plot->setValues(values);
            QString text=QString("已接收 %1 帧气压数据；当前单包 %2 点。时间参数和周期边界待确认，暂不拼接周期。")
                .arg(data.value("pressureFrames",0).toULongLong()).arg(values.size());
            if(!values.isEmpty()) {
                const auto high=*std::max_element(values.cbegin(),values.cend());
                text+=QString(" 单包峰值 %1 V。").arg(high,0,'f',2);
                if(high>6)text+="部分数值超出0～6 V显示范围，原始数值未截断。";
            }
            status->setText(text);
        }
    };
    QObject::connect(controller,&AppController::instrumentSettingsChanged,page,refresh);
    QObject::connect(controller,&AppController::sessionChanged,page,refresh);
    refresh();return page;
}
}
