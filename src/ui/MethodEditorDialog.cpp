#include "ui/MethodEditorDialog.h"
#include "ui/RoundedComboBox.h"
#include "core/MethodDraft.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMap>
#include <QGuiApplication>
#include <QScreen>

namespace qitest {
MethodEditorDialog::MethodEditorDialog(const QString &initialName,const QJsonObject &initial,
        std::function<bool(const QString &,const QJsonObject &)> persist,QWidget *parent,bool fullAccess):QDialog(parent) {
    setObjectName("methodParameterEditor"); setAttribute(Qt::WA_DeleteOnClose); setWindowTitle("方法参数");
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(720, available.width() - 24), qMin(650, available.height() - 48));
    // 显式绘制页签和内容底色，避免旧 Windows 原生样式出现黑色背景。
    setStyleSheet("QDialog#methodParameterEditor { background: #e8efed; }"
                  "QTabWidget::pane { background: #e8efed; border: 0; }"
                  "QWidget#methodFields { background: #e8efed; }"
                  "QTabBar::tab { background: #eff5f3; color: #1d2422; padding: 8px 14px; }"
                  "QTabBar::tab:selected { background: #d9efea; color: #007f80; }");
    auto *layout=new QVBoxLayout(this);
    auto *name=new QLineEdit(initialName); name->setMaxLength(16); name->setObjectName("draftMethodName"); name->setPlaceholderText("方法名称（2–16 字）"); layout->addWidget(name);
    name->setReadOnly(!fullAccess);
    auto *notice=new QLabel("扫描模式"); notice->setToolTip("空白表示未指定；保存参数不会直接下发仪器。"); layout->addWidget(notice);
    auto *mode=new RoundedComboBox; mode->setObjectName("methodScanMode"); mode->addItems({"Fullscan","SIM","MS/MS"}); mode->setCurrentText(initial.value("scan_mode").toString("Fullscan")); layout->addWidget(mode);
    auto *tabs=new QTabWidget(this); tabs->setObjectName("methodParameterTabs");
    if(fullAccess) layout->addWidget(tabs,1); else tabs->hide();
    QMap<QString,QLineEdit *> edits;
    for(const QString &group:{"基本","扫描","SIM","MS/MS"}) {
        // 每页最多五项，底部操作始终在页签之外，不依赖拖动滚动条。
        QWidget *body=nullptr; QFormLayout *form=nullptr; int fieldIndex=0;
        for(const auto &field:MethodDraft::fields()) if(field.group==group && (fullAccess || field.key=="injection")) {
            if(fieldIndex % 5 == 0) {
                body=new QWidget; body->setObjectName("methodFields"); form=new QFormLayout(body);
                form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
                form->setRowWrapPolicy(QFormLayout::DontWrapRows);
                form->setVerticalSpacing(6);
                if(fullAccess) tabs->addTab(body, group + (fieldIndex ? QString::number(fieldIndex / 5 + 1) : QString{}));
                else layout->addWidget(body);
            }
            ++fieldIndex;
            auto *edit=new QLineEdit; edit->setObjectName("method_"+field.key); edit->setFixedHeight(38); edit->setMaxLength(32);
            edit->setPlaceholderText("未指定");
            if(initial.contains(field.key)) edit->setText(QString::number(initial.value(field.key).toDouble(),'g',17));
            form->addRow(field.label+"（"+(field.unit.isEmpty()?QString("单位待确认"):field.unit)+"）",edit); edits.insert(field.key,edit);
            connect(edit,&QLineEdit::textChanged,this,[this]{dirty_=true;});
        }
    }
    auto *feedback=new QLabel; feedback->setWordWrap(true); feedback->setObjectName("methodDraftFeedback"); layout->addWidget(feedback);
    auto *row=new QHBoxLayout; layout->addLayout(row);
    auto *load=new QPushButton("打开文件"); load->setObjectName("loadMethodDraft"); auto *exportButton=new QPushButton("导出文件"); exportButton->setObjectName("exportMethodDraft"); auto *save=new QPushButton("保存新版本"); auto *cancel=new QPushButton("取消");
    save->setObjectName("saveMethodDraft"); save->setProperty("sciRole","primary");
    for(auto *b:{load,exportButton,save,cancel}) { b->setMinimumHeight(44); row->addWidget(b); }
    connect(name,&QLineEdit::textChanged,this,[this]{dirty_=true;});
    connect(mode,&QComboBox::currentTextChanged,this,[this]{dirty_=true;});
    const auto collect=[=](QJsonObject *values) {
        if(name->text().trimmed().size()<2) {feedback->setText("请填写 2–16 字的方法名称");return false;}
        QJsonObject result=fullAccess ? QJsonObject{} : initial;
        result.insert("scan_mode",mode->currentText());
        for(auto i=edits.cbegin();i!=edits.cend();++i) {
            if(i.value()->text().trimmed().isEmpty()){result.remove(i.key());continue;}
            bool ok=false; const double value=i.value()->text().trimmed().toDouble(&ok);
            if(!ok || !std::isfinite(value)) {feedback->setText("参数不是有限数值："+i.key());return false;}
            result.insert(i.key(),value);
        }
        QString error; if(!MethodDraft::validate(result,&error)){feedback->setText(error);return false;}
        *values=result; return true;
    };
    connect(save,&QPushButton::clicked,this,[=]{QJsonObject values;if(collect(&values) && persist(name->text().trimmed(),values)){dirty_=false;accept();}});
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(exportButton,&QPushButton::clicked,this,[=]{
        QJsonObject values;if(!collect(&values))return;
        const auto path=QFileDialog::getSaveFileName(this,"导出方法",{},"方法文件 (*.qmethod.json)"); if(path.isEmpty())return;
        const auto bytes=QJsonDocument(QJsonObject{{"schema","qitest-method-draft-1"},{"name",name->text().trimmed()},{"parameters",values}}).toJson();
        QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())feedback->setText(file.errorString());
        else {feedback->setText("方法已导出；尚未启用或下发");dirty_=false;}
    });
    connect(load,&QPushButton::clicked,this,[=]{
        if(dirty_ && QMessageBox::question(this,"打开文件","放弃当前未保存修改？")!=QMessageBox::Yes)return;
        const auto path=QFileDialog::getOpenFileName(this,"打开方法",{},"方法文件 (*.qmethod.json)");if(path.isEmpty())return;
        QFile file(path);if(!file.open(QIODevice::ReadOnly)){feedback->setText(file.errorString());return;}
        const auto bytes=file.read(65537); QJsonParseError parse; const auto doc=QJsonDocument::fromJson(bytes,&parse); const auto root=doc.object();
        QString error; const auto values=root.value("parameters").toObject();const auto title=root.value("name").toString();
        if(bytes.size()>65536||parse.error!=QJsonParseError::NoError||root.value("schema")!="qitest-method-draft-1"||title.trimmed().size()<2||title.size()>16||!MethodDraft::validate(values,&error)){
            feedback->setText("方法文件无效："+error);return;
        }
        if(fullAccess) name->setText(title);
        mode->setCurrentText(values.value("scan_mode").toString());
        for(auto i=edits.cbegin();i!=edits.cend();++i)i.value()->setText(values.contains(i.key())?QString::number(values.value(i.key()).toDouble(),'g',17):QString{});
        dirty_=false;feedback->setText(fullAccess ? "方法已载入；编辑后可保存为新版本" : "仅载入扫描模式和进样时间；其他参数保留原方法值");
    });
}
void MethodEditorDialog::reject(){
    if(dirty_&&QMessageBox::question(this,"未保存的修改","放弃当前方法修改？")!=QMessageBox::Yes)return;
    QDialog::reject();
}
}
