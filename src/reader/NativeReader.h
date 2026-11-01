#pragma once
#include "Fb3Layout.h"
#include "ReaderTransport.h"
#include "ReaderProgress.h"
#include "ReaderHistory.h"
#include "OfflineBook.h"
#include "OfflineAccount.h"
#include <QCache>
#include <QWidget>
#include <QApplication>
#include <QPushButton>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QUrlQuery>
#include <QMenu>
#include <QMessageBox>
#include <QResizeEvent>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <memory>
#include <QStorageInfo>
class NativeReader:public QWidget {
    ReaderTransport *net_;QUrl readerUrl_;QString art_,base_,title_,userId_,statePath_,status_="Загрузка книги…";QJsonArray parts_;QMap<int,QJsonArray> cache_;QSet<int> fetching_;QCache<QString,QImage> imageCache_{8192};QSet<QString> imagePending_,imageFailed_,imageShown_;
    Fb3::Layout layout_;Fb3::Progress progress_;int part_=-1,line_=0,font_=30,total_=1;bool ready_=false,loading_=false,syncing_=false,closed_=false,failed_=false;int epoch_=0;QTimer save_,retry_,download_,connection_,frameTimer_;QImage frameImage_;Fb3::OfflineBook disk_;bool offline_=false,downloadBusy_=false,downloadPaused_=false,checking_=false,cacheComplete_=false;int offlineSaved_=0;int downloadPart_=0;QSet<QString> downloadImages_;QList<QString> backPages_;
    static bool networkFailure(int status){return status==0||status>=500;}
    void localState(const QString &uid){userId_=uid;QString dir="/mnt/ext1/litres-client/native-reader/"+uid;QDir().mkpath(dir);statePath_=dir+"/"+art_+".json";QFile f(statePath_);if(f.open(QIODevice::ReadOnly)&&f.size()<65536)progress_.restore(QJsonDocument::fromJson(f.readAll()).object());}
    bool openOffline(){QString uid=OfflineAccount::active("/mnt/ext1/litres-client");if(!ReaderHistory::numeric(uid)||!disk_.open("/mnt/ext1/litres-client",uid,art_)||disk_.savedParts()==0)return false;localState(uid);auto m=disk_.metadata();parts_=m["parts"].toArray();base_=m["base"].toString();title_=m["title"].toString();offline_=true;ready_=true;downloadPaused_=true;cacheComplete_=m["complete"].toBool();offlineSaved_=disk_.savedParts();total_=qMax(1,parts_.last().toObject()["e"].toInt());showPointer(progress_.local["xpointer"].toString());return true;}
    void downloadNext(){
        if(cacheComplete_||!ready_||offline_||downloadBusy_||downloadPaused_||!disk_.valid()||!isVisible())return;
        if(networkAvailable&&!networkAvailable()){offline_=true;status("Офлайн · место на книге");return;}
        QStorageInfo space("/mnt/ext1");if(space.isValid()&&space.bytesAvailable()>=0&&space.bytesAvailable()<20*1024*1024){downloadPaused_=true;status("Недостаточно места для офлайн-книги");return;}
        if(loading_||syncing_||save_.isActive()||!imagePending_.isEmpty()){download_.start(1000);return;}
        while(downloadPart_<parts_.size()){
            auto data=disk_.part(downloadPart_);if(data.isEmpty())break;downloadImages_.unite(Fb3::OfflineBook::images(data));++downloadPart_;if(downloadPart_<parts_.size()){download_.start(25);return;}
        }
        if(downloadPart_<parts_.size()){
            int n=downloadPart_;downloadBusy_=true;fetchPart(n,[this,n](bool ok){downloadBusy_=false;if(ok){downloadImages_.unite(Fb3::OfflineBook::images(disk_.part(n)));++downloadPart_;}else downloadPaused_=true;if(qAbs(n-part_)>1)cache_.remove(n);offlineSaved_=disk_.savedParts();download_.start(ok?250:60000);update(QRect(0,height()-64,width(),64));});return;
        }
        for(const auto &name:QSet<QString>(downloadImages_)){if(disk_.hasImage(name)){downloadImages_.remove(name);continue;}downloadBusy_=true;QPointer<NativeReader> self(this);net_->image(QUrl(base_+"json/"+name),[self,name](int status,const QByteArray &bytes){if(!self)return;self->downloadBusy_=false;if(status==200&&self->disk_.saveImage(name,bytes))self->downloadImages_.remove(name);else self->downloadPaused_=true;self->download_.start(status==200?250:60000);self->update(QRect(0,self->height()-64,self->width(),64));});return;}
        cacheComplete_=disk_.finish();update(QRect(0,height()-64,width(),64));
    }
    void checkConnection(){
        if(!ready_||syncing_||checking_||!isVisible()||(networkAvailable&&!networkAvailable()))return;
        if(!offline_){downloadPaused_=false;downloadNext();if(progress_.pending&&!progress_.conflict&&!save_.isActive())sync();return;}
        checking_=true;
        get(api("users/me"),[this](int code,const QString &text){bool ok=false;auto user=payload(code,text,&ok);if(!ok||QString::number(qint64(user["id"].toDouble()))!=userId_){checking_=false;return;}
            get(api("arts/"+art_),[this](int code,const QString &text){bool ok=false;auto art=payload(code,text,&ok);if(!ok||art["is_drm"].toBool()){checking_=false;if(code==401||code==403||art["is_drm"].toBool())disk_.revoke();return;}
                if(base_.contains("/download_book")&&QString::number(qint64(art["release_file_id"].toDouble()))!=base_.section('/',-2,-2)){checking_=false;status("Книга обновилась · откройте её заново");return;}
                get(base_+"json/toc.js",[this](int code,const QString &text){checking_=false;QString error;auto toc=Fb3::Parser().parse(text,&error).toObject();if(code!=200||!error.isEmpty()||toc["Parts"].toArray().isEmpty()){if(code==401||code==403){disk_.revoke();this->error("Литрес не подтвердил доступ к книге");}return;}
                    offline_=false;downloadPaused_=false;imageFailed_.clear();visibleImages();status("Связь восстановлена");if(progress_.pending&&!save_.isActive())sync();else if(!progress_.pending)remote([this](bool ok,const QJsonObject &r){if(ok&&!progress_.pending){QString before=progress_.local["xpointer"].toString();reconcile(r);if(progress_.local["xpointer"].toString()!=before)showPointer(progress_.local["xpointer"].toString());}});downloadNext();
                });
            });
        });
    }
    void visibleImages(){
        if(part_<0||layout_.lines.isEmpty())return;
        if(networkAvailable&&!networkAvailable())offline_=true;
        const QString anchor=layout_.pointer(layout_.lines[line_].start);bool updated=false;
        const int begin=layout_.previous(line_,textHeight());const int end=layout_.endLine(qMin(layout_.endLine(line_,textHeight()),layout_.lines.size()-1),textHeight());QSet<QString> wanted;
        for(const auto &i:layout_.images){int l=layout_.lineFor(i.position);if(l>=begin&&l<end)wanted.insert(i.name);}
        for(const auto &name:QSet<QString>(imageShown_))if(!wanted.contains(name)){layout_.setImage(name,{});imageShown_.remove(name);}
        for(const auto &name:wanted){if(imageShown_.contains(name)||imageFailed_.contains(name))continue;
            if(!imageCache_.contains(name)){auto image=disk_.image(name);if(!image.isNull())imageCache_.insert(name,new QImage(image),qMax(1,int(image.sizeInBytes()/1024)));}
            if(auto *image=imageCache_.object(name)){layout_.setImage(name,*image);imageShown_.insert(name);updated=true;continue;}
            if(offline_||imagePending_.contains(name)||imagePending_.size()>=2)continue;
            imagePending_.insert(name);QPointer<NativeReader> self(this);const int p=part_;
            net_->image(QUrl(base_+"json/"+name),[self,name,p](int status,const QByteArray &bytes){if(!self||self->closed_)return;self->imagePending_.remove(name);QImage image=status==200?Fb3::decodeImage(bytes):QImage();
                if(image.isNull()){self->imageFailed_.insert(name);if(self->part_==p)self->status("Иллюстрация не загрузилась · повторить через OK");}
                else{self->disk_.saveImage(name,bytes);int cost=qMax(1,int(image.sizeInBytes()/1024));self->imageCache_.insert(name,new QImage(image),cost);}
                if(self->part_==p){self->visibleImages();self->update();}
            });
        }
        if(updated){layout_.paginate(width()-44,font_,textHeight());line_=layout_.lineFor(qMax(0,layout_.position(anchor)));}
        update();
    }
    int textHeight()const{return qMax(100,height()-126);}
    static QJsonObject payload(int status,const QString &text,bool *ok=nullptr){auto d=QJsonDocument::fromJson(text.toUtf8()).object();bool good=status>=200&&status<300&&!d.isEmpty()&&d["error"].isNull()&&d["status"].toInt(status)>=200&&d["status"].toInt(status)<300;if(ok)*ok=good;return good?d["payload"].toObject()["data"].toObject():QJsonObject();}
    QString api(const QString &p)const{return "https://api.litres.ru/foundation/api/"+p;}
    void get(const QString &url,const std::function<void(int,const QString&)> &cb){QPointer<NativeReader> self(this);net_->request(QUrl(url),"GET",{},[self,cb](int s,const QString &t){if(self&&!self->closed_)cb(s,t);});}
    void status(const QString &s){status_=s;update(QRect(0,height()-64,width(),64));}
    void error(const QString &s){failed_=true;loading_=false;status(s+" · OK: меню");update();}
    bool persist(){if(statePath_.isEmpty())return false;QSaveFile f(statePath_);if(!f.open(QIODevice::WriteOnly)){status("Не удалось сохранить место на книге");return false;}f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);auto bytes=QJsonDocument(progress_.json()).toJson(QJsonDocument::Compact);if(f.write(bytes)!=bytes.size()||!f.commit()){status("Не удалось сохранить место на книге");return false;}return true;}
    void remote(const std::function<void(bool,const QJsonObject&)> &cb){get(api("users/me/arts/"+art_+"/fb3/autobookmark?is_epub=false"),[cb](int s,const QString &t){bool ok=false;auto obj=payload(s,t,&ok);if(s==404){cb(true,{});return;}cb(ok,obj);});}
    bool reconcile(const QJsonObject &remote){if(progress_.reconcile(remote)){persist();return true;}
        status("Есть позиция с другого устройства");if(!isVisible())return false;QMessageBox box(QMessageBox::Question,"Место чтения","На другом устройстве сохранено другое место. Как продолжить?",QMessageBox::NoButton,this);
        auto *there=box.addButton("С другого устройства",QMessageBox::AcceptRole);auto *here=box.addButton("С текущего места",QMessageBox::ActionRole);box.addButton("Решить позже",QMessageBox::RejectRole);box.setDefaultButton(there);box.exec();
        if(box.clickedButton()==there){progress_.chooseRemote(remote);persist();if(ready_)showPointer(remote["xpointer"].toString());return false;}
        if(box.clickedButton()==here){progress_.chooseLocal(remote);persist();return true;}persist();return false;
    }
    void initialize(){QUrlQuery q(readerUrl_);art_=q.queryItemValue("art");if(!QRegularExpression("^[0-9]+$").match(art_).hasMatch()){error("Неизвестная книга");return;}if(q.queryItemValue("format")=="epub"||q.queryItemValue("drm")=="1"){error("Этот формат доступен в веб-читалке");return;}
        get(api("users/me"),[this](int s,const QString &t){bool ok=false;auto user=payload(s,t,&ok);const QString uid=QString::number(qint64(user["id"].toDouble()));if(!ok||uid=="0"){if(networkFailure(s)&&openOffline())return;error(QString("Для чтения нужен вход в Литрес (HTTP %1)").arg(s));return;}
            localState(uid);OfflineAccount::remember("/mnt/ext1/litres-client",uid);
            get(api("arts/"+art_),[this](int s,const QString &t){bool ok=false;auto art=payload(s,t,&ok);if(!ok){if(networkFailure(s)&&openOffline())return;disk_.open("/mnt/ext1/litres-client",userId_,art_);if(s==401||s==403)disk_.revoke();error("Не удалось получить сведения о книге");return;}title_=art["title"].toString();if(art["is_drm"].toBool()||art["art_type"].toInt()!=0){error("Этот формат доступен в веб-читалке");return;}
                QUrlQuery q(readerUrl_);QString file=QString::number(qint64(art["release_file_id"].toDouble()));QString url=q.queryItemValue("baseurl");if(!url.isEmpty()){QUrl b=readerUrl_.resolved(QUrl(url));if(b.scheme()!="https"||b.host()!="www.litres.ru"||b.hasQuery()||b.hasFragment()){error("Неизвестный адрес данных книги");return;}base_=b.toString();}
                else if(q.queryItemValue("trials")=="1")base_="https://www.litres.ru/pub/t/"+art_+".";
                else base_="https://www.litres.ru/download_book"+QString(art["my_art_status"].toInt()>0||art["is_free"].toBool()?"":"_subscr")+"/"+art_+"/"+file+"/";
                get(base_+"json/toc.js",[this](int s,const QString &t){QString err;auto toc=Fb3::Parser().parse(t,&err).toObject();parts_=toc["Parts"].toArray();if(s!=200||!err.isEmpty()||parts_.isEmpty()){if(networkFailure(s)&&openOffline())return;if(s==401||s==403){disk_.open("/mnt/ext1/litres-client",userId_,art_);disk_.revoke();}error("Литрес не предоставил данные книги");return;}
                    for(int i=0;i<parts_.size();++i){auto p=parts_[i].toObject();if(!QRegularExpression("^[a-zA-Z0-9_.-]+\\.js$").match(p["url"].toString()).hasMatch()||p["url"].toString().contains("..")||p["xps"].toArray().isEmpty()){error("Неизвестный формат частей книги");return;}if(i+1<parts_.size())p["e"]=parts_[i+1].toObject()["s"].toInt()-1;parts_[i]=p;}
                    disk_.prepare("/mnt/ext1/litres-client",userId_,art_,title_,base_,parts_);offlineSaved_=disk_.savedParts();cacheComplete_=disk_.metadata()["complete"].toBool();total_=qMax(1,parts_.last().toObject()["e"].toInt());remote([this](bool ok,const QJsonObject &r){if(!ok){if(openOffline())return;error("Не удалось прочитать закладку Литрес");return;}reconcile(r);ready_=true;showPointer(progress_.local["xpointer"].toString());});
                });
            });
        });
    }
    int partFor(const QString &xp)const{auto a=Fb3::pointerPath(xp);if(a.isEmpty()||(!parts_.isEmpty()&&Fb3::compare(a,parts_.first().toObject()["xps"].toArray())<0))return 0;for(int i=0;i<parts_.size();++i){auto p=parts_[i].toObject();if(Fb3::compare(a,p["xps"].toArray())>=0&&Fb3::compare(a,p["xpe"].toArray())<=0)return i;}return -1;}
    void showPointer(const QString &xp,bool moved=false){if(!moved)backPages_.clear();int p=partFor(xp);if(p<0){error("Сохранённое место не найдено в этой версии книги");return;}loadPart(p,xp,false,moved);}
    void fetchPart(int p,const std::function<void(bool)> &done){if(cache_.contains(p)){if(disk_.valid()&&disk_.part(p).isEmpty()&&!disk_.savePart(p,cache_[p])){done(false);return;}done(true);return;}auto saved=disk_.part(p);if(!saved.isEmpty()){cache_[p]=saved;done(true);return;}if(offline_){done(false);return;}if(fetching_.contains(p)){QTimer::singleShot(100,this,[this,p,done](){fetchPart(p,done);});return;}fetching_.insert(p);
        get(base_+"json/"+parts_[p].toObject()["url"].toString(),[this,p,done](int s,const QString &t){fetching_.remove(p);QString err;auto data=Fb3::Parser().parse(t,&err);bool ok=s==200&&err.isEmpty()&&data.isArray()&&!data.toArray().isEmpty();if(ok){cache_[p]=data.toArray();if(!disk_.savePart(p,data.toArray()))downloadPaused_=true;offlineSaved_=disk_.savedParts();}else if(networkFailure(s))offline_=true;else if(s==401||s==403)disk_.revoke();done(ok);});
    }
    void loadPart(int p,const QString &xp,bool last,bool moved=false){if(p<0||p>=parts_.size()||loading_)return;loading_=true;status("Загрузка части книги…");const int e=++epoch_;
        fetchPart(p,[this,p,xp,last,moved,e](bool ok){if(e!=epoch_)return;loading_=false;if(!ok){error(offline_?"Часть ещё не сохранена · подключитесь к Wi-Fi":"Не удалось загрузить часть · повторите через меню");return;}
            imageShown_.clear();layout_.load(cache_[p],parts_[p].toObject()["s"].toInt());layout_.paginate(width()-44,font_,textHeight());int pos=xp.isEmpty()?0:layout_.position(xp);if(!xp.isEmpty()&&pos<0){error("Точное место не найдено · откройте веб-версию");return;}part_=p;failed_=false;line_=last?qMax(0,layout_.lines.size()-1):layout_.lineFor(pos);if(last)line_=layout_.previous(line_,textHeight());
            status(offline_?"Офлайн · место на книге":progress_.pending?"Место сохранено на книге":"Синхронизировано");if(diagnostic)diagnostic(QString("layout ready part=%1 lines=%2 firstLine=%3").arg(part_).arg(layout_.lines.size()).arg(line_));if(moved)changed();
            if(isVisible()&&!ReaderHistory::remember("/mnt/ext1/litres-client",userId_,art_,title_)&&diagnostic)diagnostic("last book save failed");
            visibleImages();download_.start(2000);
            for(auto i=cache_.begin();i!=cache_.end();)if(qAbs(i.key()-p)>1)i=cache_.erase(i);else ++i;
            if(p+1<parts_.size()&&!fetching_.contains(p+1))QTimer::singleShot(150,this,[this,p](){if(part_==p)fetchPart(p+1,[](bool){});});
        });
    }
    void changed(){if(part_<0||layout_.lines.isEmpty())return;int pos=layout_.lines[line_].start;progress_.change(layout_.pointer(pos),layout_.percent(pos,total_));if(persist())status(offline_?"Офлайн · место на книге":"Место сохранено на книге");save_.start(5000);visibleImages();}
    void resized(){backPages_.clear();if(part_<0||layout_.lines.isEmpty())return;const QString xp=layout_.pointer(layout_.lines[line_].start);layout_.paginate(width()-44,font_,textHeight());line_=layout_.lineFor(qMax(0,layout_.position(xp)));visibleImages();update();}
public:
    std::function<bool()> networkAvailable;
    std::function<void()> rotateScreen,openHome,openSaved;
    std::function<void(const QImage&,const QPoint&)> presentFrame;
    std::function<bool()> directOutput;
    void update(){if(directOutput&&directOutput()){frameTimer_.start();}else QWidget::update();}
    void update(const QRect &area){if(directOutput&&directOutput())update();else QWidget::update(area);}
    void synchronizeDisplay(){bool direct=directOutput&&directOutput();setUpdatesEnabled(!direct);resized();update();}
    std::function<void(const QString&)> diagnostic;
    std::function<void(bool)> leave; // true explicitly opens the web reader.
    explicit NativeReader(QWebEngineProfile *profile,const QUrl &url,QWidget *parent=nullptr,bool offlineStart=false):QWidget(parent),net_(new ReaderTransport(profile,this)),readerUrl_(url){QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);font_=qBound(22,prefs.value("reader/nativeFont",30).toInt(),48);frameTimer_.setSingleShot(true);frameTimer_.setInterval(20);connect(&frameTimer_,&QTimer::timeout,this,[this](){if(isVisible()&&directOutput&&directOutput()&&presentFrame&&!QApplication::activePopupWidget()&&!QApplication::activeModalWidget()){frameImage_=QImage(size(),QImage::Format_RGB32);{QPainter page(&frameImage_);drawPage(page);}presentFrame(frameImage_,mapToGlobal(QPoint()));}});setFocusPolicy(Qt::StrongFocus);setAutoFillBackground(true);setPalette(QPalette(Qt::white));save_.setSingleShot(true);connect(&save_,&QTimer::timeout,this,[this](){sync();});retry_.setInterval(60000);connect(&retry_,&QTimer::timeout,this,[this](){if(!progress_.conflict&&!save_.isActive()&&!offline_)sync();});retry_.start();download_.setSingleShot(true);connect(&download_,&QTimer::timeout,this,[this](){downloadNext();});connection_.setInterval(15000);connect(&connection_,&QTimer::timeout,this,[this](){checkConnection();});connection_.start();QTimer::singleShot(0,this,[this,offlineStart](){if(offlineStart){art_=QUrlQuery(readerUrl_).queryItemValue("art");if(!openOffline())error("Книга ещё не сохранена для офлайн-чтения");}else initialize();});}
    ~NativeReader()override{closed_=true;persist();}
    QJsonObject diagnostics()const{QJsonObject d;d["ready"]=ready_;d["failed"]=failed_;d["part"]=part_;d["line"]=line_;d["lines"]=layout_.lines.size();d["pending"]=progress_.pending;d["images"]=layout_.images.size();d["imagesLoaded"]=imageShown_.size();d["status"]=status_;d["offline"]=offline_;d["offlineComplete"]=cacheComplete_;d["bookmark"]=progress_.local["xpointer"];d["savedParts"]=disk_.savedParts();d["totalParts"]=parts_.size();d["syncDelay"]=5000;return d;}
    bool pending()const{return progress_.pending;}
    void sync(){
        if(!ready_||syncing_||!progress_.pending||progress_.conflict)return;
        if(offline_){checkConnection();return;}
        if(networkAvailable&&!networkAvailable()){offline_=true;status("Офлайн · место на книге");return;}
        syncing_=true;
        get(api("users/me"),[this](int code,const QString &text){bool ok=false;auto user=payload(code,text,&ok);if(!ok||QString::number(qint64(user["id"].toDouble()))!=userId_){syncing_=false;if(networkFailure(code))offline_=true;status("Место на книге · синхронизация позже");return;}
            remote([this](bool ok,const QJsonObject &r){if(!ok){syncing_=false;offline_=true;status("Офлайн · место на книге");return;}
                if(!reconcile(r)||!progress_.pending||save_.isActive()){syncing_=false;return;}auto body=progress_.begin();QPointer<NativeReader> self(this);
                net_->request(QUrl(api("users/me/arts/"+art_+"/fb3/autobookmark")),"POST",body,[self](int code,const QString &text){if(!self||self->closed_)return;self->syncing_=false;if(self->diagnostic)self->diagnostic(QString("bookmark response HTTP=%1").arg(code));bool ok=false;auto r=payload(code,text,&ok);
                    if(ok&&!r["xpointer"].toString().isEmpty()){self->progress_.ack(r);self->persist();self->status(self->progress_.pending?"Место сохранено на книге":"Синхронизировано");if(self->progress_.pending&&!self->save_.isActive())self->save_.start(5000);}
                    else if(code==409){self->progress_.conflict=true;self->status("Конфликт закладки · OK: меню");}
                    else{if(networkFailure(code))self->offline_=true;self->status("Место на книге · синхронизация позже");}
                });
            });
        });
    }
    void control(const QString &action){if(!ready_||loading_||failed_||layout_.lines.isEmpty())return;int target=line_;if(action=="pageUp"&&!backPages_.isEmpty()){auto xp=backPages_.takeLast();showPointer(xp,true);return;}if(action=="up"||action=="down")backPages_.clear();if(action=="pageDown"){target=layout_.next(line_,textHeight());if(target<layout_.lines.size()||part_+1<parts_.size()){backPages_.append(layout_.pointer(layout_.lines[line_].start));if(backPages_.size()>2048)backPages_.removeFirst();}}else if(action=="pageUp"){if(line_==0&&part_>0){loadPart(part_-1,{},true,true);return;}target=layout_.previous(line_,textHeight());}else if(action=="up")target=qMax(0,line_-1);else if(action=="down")target=line_+1;else return;
        if(target>=layout_.lines.size()){if(part_+1<parts_.size())loadPart(part_+1,{},false,true);return;}if(target!=line_){line_=target;changed();}}
    void menu(){QMenu m(this);auto *back=m.addAction("Мои книги ЛитРес");auto *saved=m.addAction("Сохранённые книги");auto *home=m.addAction("Главное меню приложения");auto *web=m.addAction("Открыть веб-версию");auto *larger=m.addAction("Увеличить шрифт");auto *smaller=m.addAction("Уменьшить шрифт");auto *rotate=m.addAction("Повернуть экран ↻");auto *retry=m.addAction("Повторить загрузку / синхронизацию");auto *a=m.exec(mapToGlobal(QPoint(24,70)));if(a==saved||a==home){pause();if(a==saved&&openSaved)openSaved();if(a==home&&openHome)openHome();}else if(a==back||a==web){save_.stop();persist();sync();if(leave)leave(a==web);}else if(a==larger||a==smaller){font_=qBound(22,font_+(a==larger?2:-2),48);QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);prefs.setValue("reader/nativeFont",font_);resized();}else if(a==rotate){if(rotateScreen)rotateScreen();}else if(a==retry){downloadPaused_=false;checkConnection();downloadNext();imageFailed_.clear();visibleImages();if(failed_){failed_=false;if(ready_)showPointer(progress_.local["xpointer"].toString());else initialize();}else{progress_.conflict=false;sync();}}update();}
    void pause(){save_.stop();persist();sync();}
protected:
    void resizeEvent(QResizeEvent *e)override{QWidget::resizeEvent(e);resized();}
    void drawPage(QPainter &p){p.fillRect(rect(),Qt::white);QFont f=font();f.setPixelSize(20);p.setFont(f);p.setPen(Qt::black);p.drawText(QRect(22,10,width()-44,30),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(title_.isEmpty()?"Литрес · Чтение":title_,Qt::ElideRight,width()-44));p.drawLine(22,48,width()-22,48);if(!failed_)layout_.draw(p,line_,textHeight(),QPoint(22,62));else p.drawText(QRect(24,100,width()-48,height()-180),Qt::TextWordWrap|Qt::AlignTop,"Не удалось открыть лёгкую читалку.\n\nНажмите OK, чтобы повторить загрузку или открыть веб-версию.");f.setPixelSize(17);p.setFont(f);QString pct=progress_.local.contains("percent")?QString::number(progress_.local["percent"].toDouble(),'f',1)+"% · ":"";p.drawText(QRect(22,height()-57,width()-44,25),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(pct+status_+(disk_.valid()&&!offline_?(cacheComplete_?QString(" · Офлайн готово"):QString(" · Офлайн %1/%2").arg(offlineSaved_).arg(parts_.size())):QString()),Qt::ElideRight,width()-44));p.drawText(QRect(22,height()-29,width()-44,24),Qt::AlignLeft|Qt::AlignVCenter,"↑/↓ строка · Листать экран · OK меню");}
    void paintEvent(QPaintEvent*)override{if(directOutput&&directOutput()){frameTimer_.start();return;}QPainter widget(this);drawPage(widget);}

};
