#include "ui/InstrumentWorkbench.h"
#include "ui/RoundedComboBox.h"
#include "core/MassAxisCalibration.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QLabel>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSaveFile>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <functional>

namespace qitest {
namespace {
class WorkbenchPage final : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void()> onHidden;
protected:
    void hideEvent(QHideEvent *event) override {
        if(onHidden)onHidden();QWidget::hideEvent(event);
    }
};
QDoubleSpinBox *number(QFormLayout *form,const QString &label,const QString &id,double value) {
    auto *input=new QDoubleSpinBox; input->setObjectName(id); input->setRange(0,1e6);input->setDecimals(6);input->setValue(value);input->setMinimumHeight(44);
    form->addRow(label,input);return input;
}
QPushButton *button(QHBoxLayout *row,const QString &text,const QString &id) {
    auto *b=new QPushButton(text); b->setObjectName(id);b->setMinimumHeight(44);row->addWidget(b);return b;
}
QTableWidget *table(QVBoxLayout *layout,const QStringList &headers) {
    auto *t=new QTableWidget(0,headers.size());t->setHorizontalHeaderLabels(headers);t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(44);t->setAlternatingRowColors(true);t->setMinimumHeight(150);t->setSelectionBehavior(QAbstractItemView::SelectRows);layout->addWidget(t,1);return t;
}
bool pairsFromTable(QTableWidget *table,QVector<MassAxisPair> *pairs,QString *error) {
    QVector<MassAxisPair> result;
    for(int row=0;row<table->rowCount();++row) {
        bool a=false,b=false; const double x=table->item(row,0)?table->item(row,0)->text().toDouble(&a):0;
        const double y=table->item(row,1)?table->item(row,1)->text().toDouble(&b):0;
        if(!a||!b||!std::isfinite(x)||!std::isfinite(y)||x<=0||y<0||x>1e6||y>1e12){*error="请填写有效数值，第一列须为正值";return false;}
        result.append({x,y});
    }
    *pairs=result;return true;
}
}
QWidget *createInstrumentWorkbench(const QString &kind,QWidget *parent) {
    auto *page=new WorkbenchPage(parent);page->setObjectName("workbench_"+kind);page->setAttribute(Qt::WA_StyledBackground);
    auto *layout=new QVBoxLayout(page);layout->setContentsMargins(8,8,8,8);
    auto *body=new QWidget;auto *content=new QVBoxLayout(body);content->setContentsMargins(0,0,0,0);
    layout->addWidget(body,1);
    auto *status=new QLabel;status->setWordWrap(true);status->setObjectName("workbenchStatus");layout->addWidget(status);
    auto *files=new QHBoxLayout;layout->addLayout(files);
    auto *load=button(files,"打开记录","loadWorkbench");auto *save=button(files,"保存记录","saveWorkbench");
    auto *hardware=button(files,"仪器未接入","hardwareUnavailable");hardware->setEnabled(false);
    hardware->setToolTip("尚缺固件、通信回执和安全联锁验证；不会发送控制命令");
    auto dirty=std::make_shared<bool>(false); auto busy=std::make_shared<bool>(false);
    std::function<QJsonObject(QString *)> collect;std::function<bool(const QJsonObject &,QString *)> restore;
    if(kind=="注射泵") {
        auto *form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);content->addLayout(form);
        auto *withdraw=number(form,"抽取速度（µL/min）","syringeWithdraw",200);
        auto *volume=number(form,"计划体积（µL）","syringeVolume",100);
        auto *deliver=number(form,"执行速度（µL/min）","syringeDeliver",1);
        body->setToolTip("计划用时不表示泵实际位移、容量或余量，执行结果以设备回执为准。");
        auto *detail=new QLabel;detail->setWordWrap(true);detail->hide();content->addWidget(detail);
        auto *progress=new QProgressBar;progress->setRange(0,1000);progress->setValue(0);progress->setMinimumHeight(40);progress->setFormat("执行进度 %p%");content->addWidget(progress);
        auto *row=new QHBoxLayout;content->addLayout(row);
        auto *a=button(row,"抽取","simulateWithdraw");auto *b=button(row,"排液","simulateDrain");auto *c=button(row,"执行","simulateDeliver");auto *stop=button(row,"停止","stopSyringe"); stop->setEnabled(false);
        a->setIcon(QIcon(":/qitest/resources/icons/import.svg"));
        b->setIcon(QIcon(":/qitest/resources/icons/export.svg"));
        c->setIcon(QIcon(":/qitest/resources/icons/syringe.svg"));
        c->setProperty("sciRole", "primary");
        stop->setIcon(QIcon(":/qitest/resources/icons/power.svg"));
        for (auto *action : {a,b,c,stop}) action->setIconSize(QSize(18,18));
        auto timer=new QTimer(page);timer->setInterval(200);auto clock=std::make_shared<QElapsedTimer>();auto rate=std::make_shared<double>(0);auto planned=std::make_shared<double>(0);
        const auto controls=[=](bool running){*busy=running;for(auto *i:{withdraw,volume,deliver})i->setEnabled(!running);for(auto *i:{a,b,c,load,save})i->setEnabled(!running);stop->setEnabled(running);};
        const auto start=[=](double flow,const QString &operation){
            if(flow<=0||volume->value()<=0){status->setText("体积与速度必须大于 0");return;}
            *rate=flow;*planned=volume->value();progress->setValue(0);clock->start();controls(true);timer->start();
            detail->setText(operation+QString(" · 计划 %1 秒").arg(*planned/flow*60,0,'f',1));detail->show();status->setText("计划执行中；设备状态待确认");
        };
        QObject::connect(a,&QPushButton::clicked,page,[=]{start(withdraw->value(),"抽取");});
        QObject::connect(b,&QPushButton::clicked,page,[=]{start(deliver->value(),"排液");});
        QObject::connect(c,&QPushButton::clicked,page,[=]{start(deliver->value(),"执行");});
        QObject::connect(timer,&QTimer::timeout,page,[=]{const double done=qMin(*planned,clock->elapsed()/60000.0*(*rate));progress->setValue(qRound(done/(*planned)*1000));status->setText(QString("计划进度 %1 / %2 µL").arg(done,0,'f',3).arg(*planned));if(done>=*planned){timer->stop();controls(false);status->setText("计划完成；设备状态待确认");}});
        QObject::connect(stop,&QPushButton::clicked,page,[=]{timer->stop();controls(false);status->setText("操作已停止");});
        page->onHidden=[=]{if(*busy){timer->stop();controls(false);status->setText("操作已停止");}};
        for(auto *input:{withdraw,volume,deliver})QObject::connect(input,QOverload<double>::of(&QDoubleSpinBox::valueChanged),page,[=]{*dirty=true;});
        collect=[=](QString *error){if(withdraw->value()<=0||volume->value()<=0||deliver->value()<=0){*error="速度与体积必须大于 0";return QJsonObject{};}return QJsonObject{{"withdraw_ul_min",withdraw->value()},{"volume_ul",volume->value()},{"deliver_ul_min",deliver->value()}};};
        restore=[=](const QJsonObject &p,QString *error){
            for(const QString &k:{"withdraw_ul_min","volume_ul","deliver_ul_min"})if(!p.value(k).isDouble()||!std::isfinite(p.value(k).toDouble())||p.value(k).toDouble()<=0||p.value(k).toDouble()>1e6){*error="泵计划参数无效";return false;}
            withdraw->setValue(p.value("withdraw_ul_min").toDouble());volume->setValue(p.value("volume_ul").toDouble());deliver->setValue(p.value("deliver_ul_min").toDouble());return true;
        };
        content->addStretch();
    } else {
        const bool mass=kind=="质量轴校准";
        auto *tabs = new QTabWidget;
        tabs->setObjectName("workbenchTabs");
        tabs->setDocumentMode(true);
        content->addWidget(tabs, 1);
        auto *pointsPage = new QWidget;
        content = new QVBoxLayout(pointsPage);
        content->setContentsMargins(0, 4, 0, 0);
        tabs->addTab(pointsPage, "数据与计算");
        auto *resultPage = new QWidget;
        auto *resultLayout = new QVBoxLayout(resultPage);
        tabs->addTab(resultPage, mass ? "换算与结果" : "范围与结果");
        auto *options=new QHBoxLayout;content->addLayout(options);
        auto *degree=new RoundedComboBox;degree->addItems({"线性拟合","二次拟合"});degree->setProperty("sciRole","analysisInput");degree->setMinimumHeight(44);degree->setVisible(mass);options->addWidget(degree,1);
        auto *mode=new RoundedComboBox;mode->addItems({"Fullscan","SIM","MS/MS"});mode->setProperty("sciRole","analysisInput");mode->setMinimumHeight(44);mode->setVisible(mass);options->addWidget(mode,1);
        QMap<QString,QDoubleSpinBox *> rf;
        if(!mass){auto *form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);resultLayout->addLayout(form);for(const auto &k:QStringList{"RF低","RF高","AC低","AC高"})rf.insert(k,number(form,k+"（单位待确认）",k,0));}
        body->setToolTip(mass?"本地拟合：实测 m/z → 理论 m/z；仅在校准点范围内换算，不修改原始谱图。":"离线记录 RF/AC 范围与实测响应。最佳点仅为输入数据中响应最大的一点，不是厂家自动调谐算法。");
        auto *t=table(content,mass?QStringList{"实测 m/z","理论 m/z"}:QStringList{"RF 设定值","实测响应"});t->setObjectName("workbenchPoints");
        auto *row=new QHBoxLayout;content->addLayout(row);auto *add=button(row,"加一行","addWorkbenchRow");auto *remove=button(row,"删所选","removeWorkbenchRow");auto *calculate=button(row,mass?"拟合校准":"查找最大响应","calculateWorkbench");
        calculate->setProperty("sciRole","primary");
        auto fit=std::make_shared<MassAxisFit>();auto result=new QLabel;result->setWordWrap(true);result->setObjectName("workbenchResult");resultLayout->addWidget(result);
        auto *conversion=new QFormLayout;resultLayout->addLayout(conversion);auto *query=number(conversion,"待换算实测 m/z","massQuery",100);query->setVisible(mass);conversion->labelForField(query)->setVisible(mass);
        auto *apply=new QPushButton("计算校正质量数");apply->setObjectName("applyMassAxis");apply->setMinimumHeight(44);apply->setVisible(mass);resultLayout->addWidget(apply);
        apply->setProperty("sciRole","primary");
        auto *undo=new QPushButton("撤销拟合");undo->setMinimumHeight(44);undo->setVisible(mass);resultLayout->addWidget(undo);
        resultLayout->addStretch();
        const auto invalidate=[=]{*dirty=true;*fit=MassAxisFit{};result->clear();};
        QObject::connect(t,&QTableWidget::cellChanged,page,[=]{invalidate();});QObject::connect(degree,QOverload<int>::of(&QComboBox::currentIndexChanged),page,[=]{invalidate();});QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),page,[=]{invalidate();});
        for(auto *input:rf)QObject::connect(input,QOverload<double>::of(&QDoubleSpinBox::valueChanged),page,[=]{invalidate();});
        QObject::connect(add,&QPushButton::clicked,page,[=]{if(t->rowCount()>=1000){status->setText("最多 1000 行");return;}int r=t->rowCount();t->insertRow(r);for(int c=0;c<2;++c)t->setItem(r,c,new QTableWidgetItem);t->setCurrentCell(r,0);t->editItem(t->item(r,0));invalidate();});
        QObject::connect(remove,&QPushButton::clicked,page,[=]{if(t->currentRow()>=0){t->removeRow(t->currentRow());invalidate();}});
        QObject::connect(calculate,&QPushButton::clicked,page,[=]{
            QString error;QVector<MassAxisPair> points;if(!pairsFromTable(t,&points,&error)){status->setText(error);return;}
            if(mass){*fit=MassAxisCalibration::fit(points,degree->currentIndex()+1);if(!fit->valid){status->setText(fit->error);return;}
                result->setText(QString("局部校准范围 %1–%2 m/z；训练点 RMS 残差 %3 m/z\ny = %4 + %5·z + %6·z²，z = (实测值 − %7) / %8\n未做独立标准验证，未同步仪器。").arg(fit->minimum).arg(fit->maximum).arg(fit->rms,0,'g',8).arg(fit->coefficients[0],0,'g',10).arg(fit->coefficients[1],0,'g',10).arg(fit->coefficients[2],0,'g',10).arg(fit->center).arg(fit->scale));status->setText("本地拟合完成");
            }else{if(points.isEmpty()){status->setText("请录入实测响应数据");return;}
                const auto best=std::max_element(points.begin(),points.end(),[](const MassAxisPair &a,const MassAxisPair &b){return a.theoretical<b.theoretical;});result->setText(QString("输入数据最大响应：RF=%1，响应=%2；仅供人工复核，未设置仪器").arg(best->measured).arg(best->theoretical));}
        });
        QObject::connect(apply,&QPushButton::clicked,page,[=]{double y=0;if(!fit->map(query->value(),&y)){status->setText("请先拟合；只允许校准范围内换算");return;}status->setText(QString("校正后 %1 m/z · 原始数据未改变").arg(y,0,'g',12));});
        QObject::connect(undo,&QPushButton::clicked,page,[=]{*fit=MassAxisFit{};result->clear();status->setText("拟合已撤销，原始校准点保留");});
        collect=[=](QString *error){
            QVector<MassAxisPair> points;if(!pairsFromTable(t,&points,error))return QJsonObject{};QJsonArray rows;for(const auto &p:points)rows.append(QJsonArray{p.measured,p.theoretical});
            QJsonObject p{{"points",rows},{"degree",degree->currentIndex()+1},{"mode",mode->currentText()}};
            for(auto i=rf.cbegin();i!=rf.cend();++i)p.insert(i.key(),i.value()->value());
            if(!mass&&(rf["RF低"]->value()>rf["RF高"]->value()||rf["AC低"]->value()>rf["AC高"]->value())){*error="低值不能大于高值";return QJsonObject{};}
            return p;
        };
        restore=[=](const QJsonObject &p,QString *error){
            if(!p.value("points").isArray()||p.value("points").toArray().size()>1000||!p.value("degree").isDouble()||(p.value("degree").toDouble()!=1&&p.value("degree").toDouble()!=2)||!QStringList{"Fullscan","SIM","MS/MS"}.contains(p.value("mode").toString())){*error="记录格式错误";return false;}
            for(const auto &v:p.value("points").toArray()){const auto a=v.toArray();if(a.size()!=2||!a[0].isDouble()||!a[1].isDouble()||a[0].toDouble()<=0||a[0].toDouble()>1e6||a[1].toDouble()<0||a[1].toDouble()>1e12){*error="数据点无效";return false;}}
            for(auto i=rf.cbegin();i!=rf.cend();++i)if(!p.value(i.key()).isDouble()||p.value(i.key()).toDouble()<0||p.value(i.key()).toDouble()>1e6){*error="RF/AC 参数无效";return false;}
            if(!mass&&(p.value("RF低").toDouble()>p.value("RF高").toDouble()||p.value("AC低").toDouble()>p.value("AC高").toDouble())){*error="RF/AC 范围错误";return false;}
            t->setRowCount(0);for(const auto &v:p.value("points").toArray()){int r=t->rowCount();t->insertRow(r);for(int c=0;c<2;++c)t->setItem(r,c,new QTableWidgetItem(QString::number(v.toArray()[c].toDouble(),'g',17)));}
            degree->setCurrentIndex(p.value("degree").toInt()-1);mode->setCurrentText(p.value("mode").toString());for(auto i=rf.cbegin();i!=rf.cend();++i)i.value()->setValue(p.value(i.key()).toDouble());invalidate();return true;
        };
    }
    QObject::connect(save,&QPushButton::clicked,page,[=]{
        if(*busy)return;QString error;const auto payload=collect(&error);if(!error.isEmpty()){status->setText(error);return;}
        const auto path=QFileDialog::getSaveFileName(page,"保存离线记录",{},"工作记录 (*.qtool.json)");if(path.isEmpty())return;
        const auto bytes=QJsonDocument(QJsonObject{{"schema","qitest-workbench-1"},{"kind",kind},{"scope","OFFLINE"},{"payload",payload}}).toJson();QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){status->setText(file.errorString());return;}*dirty=false;status->setText("已保存离线记录；未发送仪器");
    });
    QObject::connect(load,&QPushButton::clicked,page,[=]{
        if(*busy)return;if(*dirty&&QMessageBox::question(page,"打开记录","放弃当前未保存的编辑？")!=QMessageBox::Yes)return;
        const auto path=QFileDialog::getOpenFileName(page,"打开离线记录",{},"工作记录 (*.qtool.json)");if(path.isEmpty())return;QFile file(path);
        if(!file.open(QIODevice::ReadOnly)){status->setText(file.errorString());return;}const auto bytes=file.read(1024*1024+1);QJsonParseError parse;const auto root=QJsonDocument::fromJson(bytes,&parse).object();QString error;
        if(bytes.size()>1024*1024||parse.error!=QJsonParseError::NoError||root.value("schema")!="qitest-workbench-1"||root.value("kind")!=kind||root.value("scope")!="OFFLINE"||!root.value("payload").isObject()||!restore(root.value("payload").toObject(),&error)){status->setText("未加载："+error+"（文件类型或内容不符合要求）");return;}
        *dirty=false;status->setText("离线记录已载入，请重新计算；未发送仪器");
    });
    return page;
}
}
