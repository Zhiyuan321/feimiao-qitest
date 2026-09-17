#include "ui/LibraryFilesPage.h"
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qitest {
namespace {
QString libExtension(QString path) {
    if(!path.isEmpty() && QFileInfo(path).suffix().isEmpty())path+=".lib";
    return path;
}
void sizeDialog(QDialog *dialog,int w,int h) {
    const auto available=QGuiApplication::primaryScreen()->availableGeometry().size()-QSize(24,50);
    dialog->resize(qMin(w,available.width()),qMin(h,available.height()));
}
void setupTable(QTableWidget *table,const QStringList &labels) {
    table->setColumnCount(labels.size());table->setHorizontalHeaderLabels(labels);
    table->setAlternatingRowColors(true);table->setShowGrid(false);table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->horizontalHeader()->setMinimumSectionSize(72);
}
QString flagDisplay(const QString &value) {
    if(value=="1" || value=="true")return "是";
    if(value=="0" || value=="false" || value.isEmpty())return "否";
    return value;
}
int confirmLibraryAction(QWidget *parent,const QString &title,const QString &text,bool saving=false) {
    QDialog dialog(parent);dialog.setObjectName("libraryConfirmDialog");dialog.setWindowTitle(title);
    auto *layout=new QVBoxLayout(&dialog);auto *label=new QLabel(text);label->setWordWrap(true);layout->addWidget(label);
    auto *row=new QHBoxLayout;row->addStretch();
    auto *accept=new QPushButton(saving?"保存":"删除");accept->setObjectName("confirmLibraryAction");
    row->addWidget(accept);QObject::connect(accept,&QPushButton::clicked,&dialog,[&dialog,saving]{dialog.done(saving?QMessageBox::Save:QMessageBox::Yes);});
    if(saving){auto *discard=new QPushButton("不保存");discard->setObjectName("discardLibraryChanges");row->addWidget(discard);
        QObject::connect(discard,&QPushButton::clicked,&dialog,[&dialog]{dialog.done(QMessageBox::Discard);});}
    auto *cancel=new QPushButton("取消");cancel->setDefault(true);row->addWidget(cancel);
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);layout->addLayout(row);dialog.resize(440,150);
    const int result=dialog.exec();return result==QDialog::Rejected?int(QMessageBox::Cancel):result;
}
}
LibraryFilesPage::LibraryFilesPage(QString catalogPath,QWidget *parent):QWidget(parent),catalog_(std::move(catalogPath)) {
    setObjectName("libraryFilesPage");setAttribute(Qt::WA_StyledBackground,true);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);
    auto *toolbar=new QHBoxLayout;
    search_=new QLineEdit;search_->setObjectName("libraryFileSearch");search_->setPlaceholderText("谱库名称");
    auto *search=new QPushButton("检索");auto *create=new QPushButton("新建谱库");
    create->setObjectName("newLibraryFile");create->setProperty("sciRole","primary");
    auto *import=new QPushButton("导入");import->setObjectName("importLibraryFile");
    toolbar->addWidget(search_,1);toolbar->addWidget(search);toolbar->addWidget(create);toolbar->addWidget(import);layout->addLayout(toolbar);
    table_=new QTableWidget;table_->setObjectName("libraryFilesTable");setupTable(table_,{"名称","日期","操作"});
    table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table_->setColumnWidth(1,160);table_->setColumnWidth(2,150);table_->verticalHeader()->setDefaultSectionSize(56);
    layout->addWidget(table_,1);
    status_=new QLabel;status_->setObjectName("libraryFilesStatus");status_->setWordWrap(true);layout->addWidget(status_);
    connect(search,&QPushButton::clicked,this,&LibraryFilesPage::refresh);
    connect(search_,&QLineEdit::returnPressed,this,&LibraryFilesPage::refresh);
    connect(create,&QPushButton::clicked,this,[this] {
        const auto path=libExtension(QFileDialog::getSaveFileName(this,"新建谱库","未命名.lib","谱库文件 (*.lib)"));
        if(path.isEmpty())return;QString error;
        if(!createFile(path,&error))status_->setText(error);else openEditor(path);
    });
    connect(import,&QPushButton::clicked,this,[this] {
        QFileDialog dialog(this,"导入谱库");dialog.setNameFilter("谱库文件 (*.lib)");
        dialog.setFileMode(QFileDialog::ExistingFile);dialog.setAcceptMode(QFileDialog::AcceptOpen);
        dialog.setLabelText(QFileDialog::Accept,"导入");
        if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty())return;
        QString error;if(!importFile(dialog.selectedFiles().first(),&error))status_->setText(error);
    });
    connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int row,int){
        if(auto *item=table_->item(row,0))openEditor(item->data(Qt::UserRole).toString());
    });
    QString error;if(!catalog_.open(&error)){setEnabled(false);status_->setText(error);return;}refresh();
}
void LibraryFilesPage::refresh() {
    table_->setRowCount(0);
    for(const auto &path:catalog_.paths()) {
        const QFileInfo info(path);if(!info.fileName().contains(search_->text().trimmed(),Qt::CaseInsensitive))continue;
        const int row=table_->rowCount();table_->insertRow(row);
        auto *name=new QTableWidgetItem(info.fileName());name->setData(Qt::UserRole,path);name->setToolTip(path);table_->setItem(row,0,name);
        auto *date=new QTableWidgetItem(info.exists()?info.lastModified().toString("yyyy-MM-dd HH:mm"):"文件不存在");
        date->setToolTip("文件修改日期");table_->setItem(row,1,date);
        auto *cell=new QWidget;auto *buttons=new QHBoxLayout(cell);buttons->setContentsMargins(3,3,3,3);buttons->setSpacing(4);
        auto *edit=new QPushButton("编辑");edit->setObjectName("editLibraryFile");
        auto *remove=new QPushButton("删除");remove->setObjectName("removeLibraryFile");
        edit->setProperty("sciRole","quietAction");remove->setProperty("sciRole","quietAction");
        buttons->addWidget(edit);buttons->addWidget(remove);table_->setCellWidget(row,2,cell);
        connect(edit,&QPushButton::clicked,this,[this,path]{openEditor(path);});
        connect(remove,&QPushButton::clicked,this,[this,path] {
            if(confirmLibraryAction(this,"删除谱库","从我的谱库中移除“"+QFileInfo(path).fileName()+"”？原始 .lib 文件将保留。")!=QMessageBox::Yes)return;
            QString error;if(!removeFile(path,&error))status_->setText(error);
        });
    }
    status_->setText(QString("共 %1 个谱库").arg(table_->rowCount()));
}
bool LibraryFilesPage::importFile(const QString &path,QString *error) {
    if(!catalog_.add(path,error))return false;search_->clear();refresh();status_->setText("谱库已导入："+QFileInfo(path).fileName());return true;
}
bool LibraryFilesPage::createFile(const QString &path,QString *error) {
    if(QFileInfo::exists(path)){if(error)*error="文件已存在，请换一个名称或导入已有谱库";return false;}
    if(!LibraryFile::write(path,{},error))return false;
    return importFile(path,error);
}
bool LibraryFilesPage::removeFile(const QString &path,QString *error) {
    if(!catalog_.remove(path,error))return false;refresh();return true;
}
void LibraryFilesPage::openEditor(const QString &path) {
    auto *dialog=new LibraryFileEditor(path,this);dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&LibraryFileEditor::fileSaved,this,[this](const QString &file){QString error;if(!importFile(file,&error))status_->setText(error);});
    dialog->open();
}
LibraryFileEditor::LibraryFileEditor(QString path,QWidget *parent):QDialog(parent),path_(QFileInfo(path).absoluteFilePath()) {
    setObjectName("libraryFileEditor");setWindowTitle("谱库编辑");setWindowModality(Qt::WindowModal);sizeDialog(this,960,610);
    auto *layout=new QVBoxLayout(this);auto *toolbar=new QHBoxLayout;
    const QStringList names{"保存","另存为","导出","添加","修改","删除"};
    const QStringList ids{"saveLibrary","saveLibraryAs","exportLibrary","addLibraryEntry","editLibraryEntry","deleteLibraryEntry"};
    QList<QPushButton*> buttons;
    for(int i=0;i<names.size();++i){auto *b=new QPushButton(names[i]);b->setObjectName(ids[i]);toolbar->addWidget(b);buttons.append(b);}
    toolbar->addStretch();layout->addLayout(toolbar);
    pathLabel_=new QLabel;pathLabel_->setWordWrap(true);layout->addWidget(pathLabel_);
    table_=new QTableWidget;table_->setObjectName("libraryEntriesTable");setupTable(table_,LibraryFile::labels());
    for(int i=0;i<9;++i)table_->setColumnWidth(i,i==0?170:(i==1?125:110));
    layout->addWidget(table_,1);
    status_=new QLabel;status_->setObjectName("libraryEditorStatus");status_->setWordWrap(true);layout->addWidget(status_);
    auto *close=new QPushButton("关闭");auto *bottom=new QHBoxLayout;bottom->addStretch();bottom->addWidget(close);layout->addLayout(bottom);
    QString error;loaded_=LibraryFile::read(path_,&entries_,&error,&digest_);refresh();
    if(!loaded_){status_->setText(error);for(auto *b:buttons)b->setEnabled(false);}
    connect(close,&QPushButton::clicked,this,&LibraryFileEditor::reject);
    connect(buttons[0],&QPushButton::clicked,this,[this]{QString error;if(!saveTo(path_,true,&error))status_->setText(error);});
    connect(buttons[1],&QPushButton::clicked,this,[this]{chooseSave(true);});
    connect(buttons[2],&QPushButton::clicked,this,[this]{chooseSave(false);});
    connect(buttons[3],&QPushButton::clicked,this,[this]{editEntry(true);});
    connect(buttons[4],&QPushButton::clicked,this,[this]{editEntry(false);});
    connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int,int){editEntry(false);});
    connect(buttons[5],&QPushButton::clicked,this,[this] {
        const int row=table_->currentRow();if(row<0){status_->setText("请先选择要删除的物质");return;}
        if(confirmLibraryAction(this,"删除物质","删除所选物质？点击保存后写入谱库文件。")!=QMessageBox::Yes)return;
        entries_.removeAt(row);dirty_=true;refresh();
    });
}
void LibraryFileEditor::refresh() {
    pathLabel_->setText(QFileInfo(path_).fileName()+(dirty_?" · 未保存":""));pathLabel_->setToolTip(path_);
    table_->setRowCount(entries_.size());const auto keys=LibraryFile::keys();
    for(int row=0;row<entries_.size();++row)for(int col=0;col<keys.size();++col) {
        auto text=LibraryFile::text(entries_[row].toObject(),keys[col]);if(col==6)text=flagDisplay(text);
        auto *item=new QTableWidgetItem(text);item->setToolTip(text);table_->setItem(row,col,item);
    }
    status_->setText(QString("%1 个物质%2").arg(entries_.size()).arg(dirty_?" · 修改后请保存":""));
}
bool LibraryFileEditor::saveTo(const QString &path,bool switchFile,QString *error) {
    if(!loaded_){if(error)*error="谱库未成功读取，不能保存";return false;}
    const QFileInfo target(path),current(path_);
    const bool same=target.absoluteFilePath()==current.absoluteFilePath()
        || (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath()==current.canonicalFilePath());
    if(same && LibraryFile::digest(path_)!=digest_){if(error)*error="库文件已被其他程序修改或移走，请关闭后重新打开；当前修改可另存为";return false;}
    if(!LibraryFile::write(path,entries_,error))return false;
    if(switchFile || same){path_=target.absoluteFilePath();digest_=LibraryFile::digest(path_);dirty_=false;refresh();emit fileSaved(path_);}
    status_->setText((switchFile?"已保存：":"已导出：")+target.fileName());return true;
}
void LibraryFileEditor::chooseSave(bool switchFile) {
    const auto path=libExtension(QFileDialog::getSaveFileName(this,switchFile?"谱库另存为":"导出谱库",path_,"谱库文件 (*.lib)"));
    if(path.isEmpty())return;QString error;if(!saveTo(path,switchFile,&error))status_->setText(error);
}
void LibraryFileEditor::reject() {
    if(dirty_) {
        const auto answer=confirmLibraryAction(this,"未保存的谱库","保存修改后关闭吗？",true);
        if(answer==QMessageBox::Cancel)return;
        if(answer==QMessageBox::Save){QString error;if(!saveTo(path_,true,&error)){status_->setText(error);return;}}
    }
    QDialog::reject();
}
void LibraryFileEditor::editEntry(bool create) {
    const int row=table_->currentRow();if(!create && row<0){status_->setText("请先选择要修改的物质");return;}
    if(create && entries_.size()>=LibraryFile::MaximumEntries){status_->setText("谱库最多10000个物质");return;}
    const auto original=create?QJsonObject{}:entries_[row].toObject();
    auto *dialog=new QDialog(this);dialog->setObjectName("libraryEntryDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(create?"添加物质":"修改物质");dialog->setWindowModality(Qt::WindowModal);sizeDialog(dialog,570,590);
    auto *layout=new QVBoxLayout(dialog);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto *body=new QWidget;auto *form=new QFormLayout(body);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto keys=LibraryFile::keys(),labels=LibraryFile::labels();QList<QWidget*> inputs;
    for(int i=0;i<keys.size();++i) {
        QWidget *input=nullptr;const auto value=LibraryFile::text(original,keys[i]);
        if(i==5 || i==6) {
            auto *combo=new QComboBox;combo->addItems(i==5?QStringList{"农药","毒品","精神药物"}:QStringList{"否","是"});
            const auto display=i==6?flagDisplay(value):value;
            if(!display.isEmpty()){if(combo->findText(display)<0)combo->addItem(display);combo->setCurrentText(display);}input=combo;
        } else {auto *edit=new QLineEdit(value);edit->setMaxLength(4096);if(i==3 || i==4)edit->setPlaceholderText("多个离子用逗号分隔，如 265,310");input=edit;}
        input->setObjectName("lib_"+keys[i]);form->addRow(labels[i],input);inputs.append(input);
    }
    scroll->setWidget(body);layout->addWidget(scroll,1);
    auto *feedback=new QLabel;feedback->setObjectName("libraryEntryFeedback");feedback->setWordWrap(true);layout->addWidget(feedback);
    auto *actions=new QHBoxLayout;actions->addStretch();auto *ok=new QPushButton(create?"添加":"保存");ok->setObjectName("acceptLibraryEntry");
    ok->setProperty("sciRole","primary");auto *cancel=new QPushButton("取消");actions->addWidget(ok);actions->addWidget(cancel);layout->addLayout(actions);
    connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(ok,&QPushButton::clicked,dialog,[=] {
        auto entry=original;
        if(create) {
            QSet<QString> ids;for(const auto &v:entries_)ids.insert(LibraryFile::text(v.toObject(),"id"));
            int id=1;while(ids.contains(QString::number(id)))++id;
            entry["id"]=QString::number(id);
        }
        for(int i=0;i<inputs.size();++i) {
            const auto *combo=qobject_cast<QComboBox*>(inputs[i]);
            QString value=combo?combo->currentText():qobject_cast<QLineEdit*>(inputs[i])->text().trimmed();
            // Preserve the legacy value/type when the displayed field was not edited.
            const QString before=i==6?flagDisplay(LibraryFile::text(original,keys[i])):LibraryFile::text(original,keys[i]);
            if(create || value!=before)entry[keys[i]]=value;
        }
        QString error;if(!LibraryFile::validateEntry(entry,&error)){feedback->setText(error);return;}
        if(create)entries_.append(entry);else entries_[row]=entry;
        dirty_=true;refresh();table_->selectRow(create?entries_.size()-1:row);dialog->accept();
    });dialog->open();
}
}
