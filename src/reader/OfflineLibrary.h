#pragma once
#include "OfflineBook.h"
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QKeyEvent>
class OfflineLibrary:public QDialog {
    QListWidget *list_;
public:
    explicit OfflineLibrary(const QString &root,const QString &user,QWidget *parent=nullptr):QDialog(parent){setWindowTitle("Мои книги · офлайн");resize(700,740);setStyleSheet("QWidget{background:white;color:black;font-size:27px} QPushButton{min-height:64px} QListWidget::item{padding:14px} QListWidget::item:selected{background:#222;color:white}");auto *l=new QVBoxLayout(this);auto *title=new QLabel("Сохранённые книги");l->addWidget(title);list_=new QListWidget;list_->setWordWrap(true);l->addWidget(list_,1);
        QDir books(root+"/native-reader/"+user+"/books");for(const auto &art:books.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){Fb3::OfflineBook b;if(!b.open(root,user,art)||b.savedParts()==0)continue;auto m=b.metadata();auto *item=new QListWidgetItem(m["title"].toString()+QString("\nСохранено частей: %1/%2").arg(b.savedParts()).arg(m["parts"].toArray().size()),list_);item->setData(Qt::UserRole,art);}
        auto *open=new QPushButton("Читать");open->setEnabled(list_->count()>0);l->addWidget(open);auto *close=new QPushButton("Назад");l->addWidget(close);connect(open,&QPushButton::clicked,this,&QDialog::accept);connect(close,&QPushButton::clicked,this,&QDialog::reject);connect(list_,&QListWidget::itemActivated,this,[this](QListWidgetItem*){accept();});if(list_->count()){list_->setCurrentRow(0);list_->setFocus();}else title->setText("Пока нет сохранённых книг.\nОткройте книгу с Wi-Fi и дождитесь надписи «Офлайн готово».");list_->installEventFilter(this);
    }
    QString art()const{return list_->currentItem()?list_->currentItem()->data(Qt::UserRole).toString():QString();}
protected:
    bool eventFilter(QObject *o,QEvent *e)override{if(o==list_&&e->type()==QEvent::KeyPress){auto *k=static_cast<QKeyEvent *>(e);if(k->key()==Qt::Key_Space||k->key()==Qt::Key_Return||k->nativeVirtualKey()==0x0a){if(!art().isEmpty())accept();return true;}}return QDialog::eventFilter(o,e);}
};
