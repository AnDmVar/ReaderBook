#pragma once
#include "OfflineBook.h"
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QKeyEvent>
class OfflineLibrary:public QDialog {
    QListWidget *list_;QList<QPushButton *> buttons_;
public:
    enum Result { Website=2,Home=3 };
    explicit OfflineLibrary(const QString &root,const QString &user,QWidget *parent=nullptr):QDialog(parent){
        setWindowTitle("Мои книги · офлайн");resize(700,740);setStyleSheet("QWidget{background:white;color:black;font-size:27px} QPushButton{min-height:64px;border:2px solid black} QPushButton:focus{background:#222;color:white} QListWidget::item{padding:14px} QListWidget::item:selected{background:#222;color:white}");auto *l=new QVBoxLayout(this);auto *title=new QLabel("Сохранённые книги");title->setWordWrap(true);l->addWidget(title);list_=new QListWidget;list_->setWordWrap(true);l->addWidget(list_,1);
        QDir books(root+"/native-reader/"+user+"/books");for(const auto &art:books.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){Fb3::OfflineBook b;if(!b.open(root,user,art)||b.savedParts()==0)continue;auto m=b.metadata();auto *item=new QListWidgetItem(m["title"].toString()+QString("\nСохранено частей: %1/%2").arg(b.savedParts()).arg(m["parts"].toArray().size()),list_);item->setData(Qt::UserRole,art);}
        auto add=[this,l](const QString &text,int result){auto *b=new QPushButton(text);buttons_.append(b);l->addWidget(b);b->installEventFilter(this);connect(b,&QPushButton::clicked,this,[this,result](){done(result);});return b;};
        auto *open=add("Читать",QDialog::Accepted);open->setEnabled(list_->count()>0);add("Назад в «Мои книги» ЛитРес",Website);add("Главное меню приложения",Home);
        auto *hint=new QLabel("↑/↓ Выбор · OK Открыть · Левая клавиша: в ЛитРес");hint->setWordWrap(true);hint->setStyleSheet("font-size:19px");l->addWidget(hint);
        if(list_->count()){list_->setCurrentRow(0);list_->setFocus();}else{title->setText("Пока нет сохранённых книг.\nОткройте книгу с Wi-Fi и дождитесь «Офлайн готово».");buttons_[1]->setFocus();}list_->installEventFilter(this);installEventFilter(this);
    }
    QString art()const{return list_->currentItem()?list_->currentItem()->data(Qt::UserRole).toString():QString();}
protected:
    bool eventFilter(QObject *o,QEvent *e)override{
        if(e->type()!=QEvent::KeyPress)return QDialog::eventFilter(o,e);auto *k=static_cast<QKeyEvent *>(e);int key=k->key();auto code=[k](int n){return int(k->nativeVirtualKey())==n||int(k->nativeScanCode())==n;};
        if(key==Qt::Key_Back||key==Qt::Key_Escape||key==Qt::Key_PageUp||code(0x18)||code(0x1c)||code(0x1b)){done(Website);return true;}
        bool ok=key==Qt::Key_Return||key==Qt::Key_Enter||key==Qt::Key_Space||code(0x0a),up=key==Qt::Key_Up||code(0x11),down=key==Qt::Key_Down||code(0x12);
        if(o==list_){if(ok){if(!k->isAutoRepeat()&&!art().isEmpty())accept();return true;}if(down&&list_->currentRow()==list_->count()-1){for(auto *b:buttons_)if(b->isEnabled()){b->setFocus();break;}return true;}if(up&&list_->currentRow()<=0){buttons_.last()->setFocus();return true;}}
        if(auto *button=qobject_cast<QPushButton *>(o)){if(ok){if(!k->isAutoRepeat())button->click();return true;}if(up||down){int i=buttons_.indexOf(button),next=i+(down?1:-1);while(next>=0&&next<buttons_.size()&&!buttons_[next]->isEnabled())next+=down?1:-1;if(next>=0&&next<buttons_.size())buttons_[next]->setFocus();else if(list_->count()){list_->setCurrentRow(down?0:list_->count()-1);list_->setFocus();}else buttons_[next<0?buttons_.size()-1:1]->setFocus();return true;}}
        return QDialog::eventFilter(o,e);
    }
};
