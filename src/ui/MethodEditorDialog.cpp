#include "ui/MethodEditorDialog.h"
#include "ui/RoundedComboBox.h"
#include "core/MethodDraft.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QMap>
#include <QGuiApplication>
#include <QScreen>

namespace qitest {
MethodEditorDialog::MethodEditorDialog(const QString &initialName,const QJsonObject &initial,
        std::function<bool(const QString &,const QJsonObject &)> persist,QWidget *parent,bool fullAccess):QDialog(parent) {
    setObjectName("methodParameterEditor"); setAttribute(Qt::WA_DeleteOnClose); setWindowTitle("方法参数");
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(940, available.width() - 24), qMin(690, available.height() - 48));
    setMinimumSize(qMin(760, available.width() - 24), qMin(620, available.height() - 48));
    setStyleSheet("QDialog#methodParameterEditor { background: #e8efed; }"
                  "QGroupBox { color: #1d2422; font-weight: 600; border: 1px solid #c8d8d4;"
                  " border-radius: 10px; margin-top: 12px; padding-top: 10px; background: #f4f8f7; }"
                  "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; }"
                  "QLabel#methodDraftHint { color: #52615e; }"
                  "QLabel#methodDraftFeedback { color: #9a4e18; }");
    auto *layout=new QVBoxLayout(this);
    auto *titleRow=new QHBoxLayout;
    auto *name=new QLineEdit(initialName); name->setMaxLength(16); name->setObjectName("draftMethodName"); name->setPlaceholderText("方法名称（2–16 字）");
    name->setReadOnly(!fullAccess);
    titleRow->addWidget(name,1); layout->addLayout(titleRow);
    if (!fullAccess) {
        auto *notice=new QLabel("普通账号只可另存扫描模式和进样时间；其他参数沿用管理员方法。");
        notice->setObjectName("methodDraftHint"); notice->setWordWrap(true); layout->addWidget(notice);
    }
    QJsonObject displayed=fullAccess ? MethodDraft::defaultParameters() : initial;
    for(auto it=initial.begin();it!=initial.end();++it) displayed.insert(it.key(),it.value());
    auto *mode=new RoundedComboBox; mode->setObjectName("methodScanMode"); mode->addItems({"Fullscan","SIM","MS/MS"});
    mode->setCurrentText(displayed.value("scan_mode").toString("Fullscan"));
    QMap<QString,QLineEdit *> edits;
    const auto createGroup=[&](const QString &title,const QString &fieldGroup,int columns,bool includeMode=false) {
        auto *box=new QGroupBox(title); auto *grid=new QGridLayout(box);
        grid->setHorizontalSpacing(12); grid->setVerticalSpacing(7);
        int fieldIndex=includeMode ? 1 : 0;
        if(includeMode) {
            grid->addWidget(new QLabel("扫描模式"),0,0); grid->addWidget(mode,0,1);
            grid->setColumnStretch(1,1);
        }
        for(const auto &field:MethodDraft::fields()) if(field.group==fieldGroup && (fullAccess || field.key=="injection")) {
            const int row=fieldIndex/columns; const int column=(fieldIndex%columns)*2; ++fieldIndex;
            auto *label=new QLabel(field.unit.isEmpty() ? field.label : field.label+"（"+field.unit+"）");
            auto *edit=new QLineEdit; edit->setObjectName("method_"+field.key); edit->setMinimumHeight(44); edit->setMaxLength(32);
            if(displayed.contains(field.key)) edit->setText(QString::number(displayed.value(field.key).toDouble(),'g',17));
            grid->addWidget(label,row,column); grid->addWidget(edit,row,column+1); grid->setColumnStretch(column+1,1);
            edits.insert(field.key,edit); connect(edit,&QLineEdit::textChanged,this,[this]{dirty_=true;});
        }
        return box;
    };
    if(fullAccess) layout->addWidget(createGroup("基本设置","基本",2));
    layout->addWidget(createGroup(fullAccess ? "质谱设置" : "可调整参数","扫描",fullAccess ? 3 : 1,true),1);
    auto *extensions=new QStackedWidget; extensions->setObjectName("methodModeParameters");
    auto *fullscanPlaceholder=new QWidget;
    extensions->addWidget(fullscanPlaceholder);
    extensions->addWidget(createGroup("SIM 参数","SIM",2));
    extensions->addWidget(createGroup("MS/MS 参数","MS/MS",3));
    layout->addWidget(extensions); extensions->setVisible(false);
    const auto updateMode=[=](const QString &value){
        extensions->setCurrentIndex(value=="SIM" ? 1 : value=="MS/MS" ? 2 : 0);
        extensions->setVisible(fullAccess && value!="Fullscan");
    };
    updateMode(mode->currentText());
    connect(mode,&QComboBox::currentTextChanged,this,[=](const QString &value){dirty_=true;updateMode(value);});
    auto *feedback=new QLabel; feedback->setWordWrap(true); feedback->setObjectName("methodDraftFeedback"); layout->addWidget(feedback);
    auto *row=new QHBoxLayout; layout->addLayout(row);
    auto *load=new QPushButton("打开文件"); load->setObjectName("loadMethodDraft"); auto *exportButton=new QPushButton("导出文件"); exportButton->setObjectName("exportMethodDraft"); auto *save=new QPushButton("保存新版本"); auto *cancel=new QPushButton("取消");
    save->setObjectName("saveMethodDraft"); save->setProperty("sciRole","primary");
    for(auto *b:{load,exportButton,save,cancel}) { b->setMinimumHeight(44); row->addWidget(b); }
    connect(name,&QLineEdit::textChanged,this,[this]{dirty_=true;});
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
