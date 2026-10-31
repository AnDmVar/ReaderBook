#pragma once
#include "UpdateCore.h"
#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QTextBrowser>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QTimer>
#include <QDir>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <qwebenginedownloaditem.h>
class OnlineUpdate : public QDialog {
 QWebEngineProfile *profile_;QWebEnginePage *page_;QWebEngineDownloadItem *item_=nullptr;
 QTextBrowser *label_;QPushButton *close_;QProgressBar *progress_;QPushButton *action_;QTimer timer_;
 QString dir_,requested_,path_;qint64 limit_=0;int phase_=0;bool closed_=false;
 QByteArray raw_;AppUpdate::Manifest manifest_;
 QString target()const{return QCoreApplication::applicationFilePath();}
 QByteArray read(const QString &p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
 void fail(const QString &s){timer_.stop();phase_=-1;if(item_){auto i=item_;item_=nullptr;i->cancel();}label_->setPlainText(s);action_->setText("Закрыть");action_->setEnabled(true);}
 void fetch(const QUrl &url,const QString &name,qint64 limit){requested_=url.toString();path_=dir_+"/"+name;limit_=limit;item_=nullptr;timer_.start(180000);page_->download(url,name);}
 void complete(){
  timer_.stop();item_=nullptr;
  if(phase_==1){raw_=read(path_);phase_=2;fetch(QUrl("https://github.com/AnDmVar/ReaderBook/releases/latest/download/latest.json.sig"),"latest.json.sig",64);return;}
  if(phase_==2){QString e;if(!AppUpdate::parse(raw_,read(path_),QCoreApplication::applicationVersion(),manifest_,e)){fail(e);return;}phase_=3;label_->setPlainText("Доступна версия "+manifest_.version+"\n\n"+manifest_.notes+"\n\nСкачать и установить?");action_->setText("Установить");action_->setEnabled(true);return;}
  if(phase_==4){QString e;if(!AppUpdate::validImage(path_,manifest_)){fail("Размер, контрольная сумма или формат приложения не совпали.");return;}if(!AppUpdate::install(path_,target(),manifest_,e)){fail(e);return;}phase_=5;label_->setPlainText("Обновление установлено. Закройте и снова откройте приложение. Предыдущая версия сохранена для отката.");action_->setText("Закрыть приложение");action_->setEnabled(true);}
 }
public:
 explicit OnlineUpdate(QWidget *parent=nullptr):QDialog(parent){
  setWindowTitle("Обновление приложения");resize(700,700);setStyleSheet("QWidget{background:white;color:black;font-size:27px} QPushButton{min-height:76px;border:2px solid black} QPushButton:focus{background:#222;color:white}");
  auto l=new QVBoxLayout(this);label_=new QTextBrowser;label_->setReadOnly(true);label_->setOpenLinks(false);label_->setPlainText("Проверяем GitHub Releases…");label_->setMinimumSize(0,180);label_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);label_->setStyleSheet("QTextBrowser{border:1px solid #888;padding:8px}");l->addWidget(label_,1);progress_=new QProgressBar;progress_->setRange(0,0);l->addWidget(progress_);action_=new QPushButton("Проверка…");action_->setEnabled(false);l->addWidget(action_);close_=new QPushButton("Отмена");l->addWidget(close_);connect(close_,&QPushButton::clicked,this,&QDialog::reject);auto hint=new QLabel("↑/↓ Текст и кнопки · Листание: прокрутка · OK Выбрать");hint->setWordWrap(true);hint->setStyleSheet("font-size:20px");l->addWidget(hint);label_->installEventFilter(this);label_->viewport()->installEventFilter(this);action_->installEventFilter(this);close_->installEventFilter(this);label_->setFocus();
  dir_="/mnt/ext1/litres-client/updater/"+QUuid::createUuid().toString(QUuid::WithoutBraces);QDir().mkpath(dir_);
  profile_=new QWebEngineProfile(this);profile_->setHttpCacheType(QWebEngineProfile::NoCache);profile_->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);page_=new QWebEnginePage(profile_,this);
  timer_.setSingleShot(true);connect(&timer_,&QTimer::timeout,this,[this](){fail("Сервер не ответил вовремя. Проверьте Wi-Fi и повторите попытку.");});
  connect(profile_,&QWebEngineProfile::downloadRequested,this,[this](QWebEngineDownloadItem *d){
   QUrl incoming=d->url();bool allowed=incoming.scheme()=="https"&&(incoming.host()=="github.com"||incoming.host()=="release-assets.githubusercontent.com"||incoming.host()=="objects.githubusercontent.com");
   if(closed_||item_||!allowed){d->cancel();return;}item_=d;d->setPath(path_);
   connect(d,&QWebEngineDownloadItem::downloadProgress,this,[this,d](qint64 got,qint64 total){if(d!=item_)return;if(got>limit_||total>limit_){fail("Размер загрузки превышает допустимый.");return;}if(total>0){progress_->setRange(0,100);progress_->setValue(int(got*100/total));}});
   connect(d,&QWebEngineDownloadItem::finished,this,[this,d](){if(d!=item_||closed_)return;if(d->state()!=QWebEngineDownloadItem::DownloadCompleted){fail("Не удалось загрузить обновление. Проверьте Wi-Fi или наличие опубликованного релиза.");return;}if(QFileInfo(path_).size()>limit_){fail("Некорректный размер загрузки.");return;}complete();});d->accept();
  });
  connect(action_,&QPushButton::clicked,this,[this](){if(phase_==3){phase_=4;action_->setEnabled(false);label_->setPlainText("Загружаем и проверяем приложение…");fetch(QUrl(manifest_.url),"litres.app",manifest_.size);}else if(phase_==5){QCoreApplication::quit();}else reject();});
  QTimer::singleShot(0,this,[this](){QString t=QFileInfo(target()).absolutePath();if(t!="/mnt/ext1/applications"||QFileInfo(target()).isSymLink()){fail("Обновление доступно для приложения в /mnt/ext1/applications.");return;}QString check;if(!AppUpdate::probeWritable(target(),check)){fail(check);return;}phase_=1;fetch(QUrl("https://github.com/AnDmVar/ReaderBook/releases/latest/download/latest.json"),"latest.json",65536);});
 }
protected:
 bool eventFilter(QObject *watched,QEvent *event)override{
  if(event->type()!=QEvent::KeyPress)return QDialog::eventFilter(watched,event);auto *k=static_cast<QKeyEvent *>(event);auto code=[k](int qt,int a,int b){return k->key()==qt||int(k->nativeVirtualKey())==a||int(k->nativeScanCode())==a||(b&&int(k->nativeVirtualKey())==b);};auto *bar=label_->verticalScrollBar();
  if(code(Qt::Key_PageDown,0x19,0x1d)||code(Qt::Key_PageUp,0x18,0x1c)){int dir=code(Qt::Key_PageDown,0x19,0x1d)?1:-1;bar->setValue(bar->value()+dir*qMax(40,label_->viewport()->height()-40));return true;}
  bool text=watched==label_||watched==label_->viewport();bool up=code(Qt::Key_Up,0x11,0),down=code(Qt::Key_Down,0x12,0),ok=k->key()==Qt::Key_Return||k->key()==Qt::Key_Enter||k->key()==Qt::Key_Space||k->nativeVirtualKey()==0x0a;
  if(text){if(up||down){if(down&&bar->value()==bar->maximum())(action_->isEnabled()?action_:close_)->setFocus();else bar->setValue(bar->value()+(down?44:-44));return true;}if(ok){(action_->isEnabled()?action_:close_)->setFocus();return true;}}
  else{if(up){if(watched==close_&&action_->isEnabled())action_->setFocus();else label_->setFocus();return true;}if(down){if(watched==action_)close_->setFocus();else label_->setFocus();return true;}if(ok){if(!k->isAutoRepeat())static_cast<QPushButton *>(watched)->click();return true;}}
  return QDialog::eventFilter(watched,event);
 }
public:
 ~OnlineUpdate(){closed_=true;timer_.stop();if(item_){item_->disconnect(this);item_->cancel();}delete page_;delete profile_;QDir(dir_).removeRecursively();}
};
