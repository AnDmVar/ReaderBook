#pragma once
#include "Fb3Layout.h"
#include "ReaderTransport.h"
#include "ReaderProgress.h"
#include "ReaderHistory.h"
#include <QCache>
#include <QWidget>
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
class NativeReader:public QWidget {
    ReaderTransport *net_;QUrl readerUrl_;QString art_,base_,title_,userId_,statePath_,status_="Загрузка книги…";QJsonArray parts_;QMap<int,QJsonArray> cache_;QSet<int> fetching_;QCache<QString,QImage> imageCache_{8192};QSet<QString> imagePending_,imageFailed_,imageShown_;
    Fb3::Layout layout_;Fb3::Progress progress_;int part_=-1,line_=0,font_=30,total_=1;bool ready_=false,loading_=false,syncing_=false,closed_=false,failed_=false;int epoch_=0;QTimer save_,retry_;
    void visibleImages(){
        if(part_<0||layout_.lines.isEmpty())return;
        const QString anchor=layout_.pointer(layout_.lines[line_].start);bool updated=false;
        const int begin=layout_.previous(line_,textHeight());const int end=layout_.endLine(qMin(layout_.endLine(line_,textHeight()),layout_.lines.size()-1),textHeight());QSet<QString> wanted;
        for(const auto &i:layout_.images){int l=layout_.lineFor(i.position);if(l>=begin&&l<end)wanted.insert(i.name);}
        for(const auto &name:QSet<QString>(imageShown_))if(!wanted.contains(name)){layout_.setImage(name,{});imageShown_.remove(name);}
        for(const auto &name:wanted){if(imageShown_.contains(name)||imageFailed_.contains(name))continue;
            if(auto *image=imageCache_.object(name)){layout_.setImage(name,*image);imageShown_.insert(name);updated=true;continue;}
            if(imagePending_.contains(name)||imagePending_.size()>=2)continue;
            imagePending_.insert(name);QPointer<NativeReader> self(this);const int p=part_;
            net_->image(QUrl(base_+"json/"+name),[self,name,p](int status,const QByteArray &bytes){if(!self||self->closed_)return;self->imagePending_.remove(name);QImage image=status==200?Fb3::decodeImage(bytes):QImage();
                if(image.isNull()){self->imageFailed_.insert(name);if(self->part_==p)self->status("Иллюстрация не загрузилась · повторить через OK");}
                else{int cost=qMax(1,int(image.sizeInBytes()/1024));self->imageCache_.insert(name,new QImage(image),cost);}
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
    void status(const QString &s){status_=s;update();}
    void error(const QString &s){failed_=true;loading_=false;status(s+" · OK: меню");}
    bool persist(){if(statePath_.isEmpty())return false;QSaveFile f(statePath_);if(!f.open(QIODevice::WriteOnly)){status("Не удалось сохранить место на книге");return false;}f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);auto bytes=QJsonDocument(progress_.json()).toJson(QJsonDocument::Compact);if(f.write(bytes)!=bytes.size()||!f.commit()){status("Не удалось сохранить место на книге");return false;}return true;}
    void remote(const std::function<void(bool,const QJsonObject&)> &cb){get(api("users/me/arts/"+art_+"/fb3/autobookmark?is_epub=false"),[cb](int s,const QString &t){bool ok=false;auto obj=payload(s,t,&ok);if(s==404){cb(true,{});return;}cb(ok,obj);});}
    bool reconcile(const QJsonObject &remote){if(progress_.reconcile(remote)){persist();return true;}
        status("Есть позиция с другого устройства");if(!isVisible())return false;QMessageBox box(QMessageBox::Question,"Место чтения","На другом устройстве сохранено другое место. Как продолжить?",QMessageBox::NoButton,this);
        auto *there=box.addButton("С другого устройства",QMessageBox::AcceptRole);auto *here=box.addButton("С текущего места",QMessageBox::ActionRole);box.addButton("Решить позже",QMessageBox::RejectRole);box.setDefaultButton(there);box.exec();
        if(box.clickedButton()==there){progress_.chooseRemote(remote);persist();if(ready_)showPointer(remote["xpointer"].toString());return false;}
        if(box.clickedButton()==here){progress_.chooseLocal(remote);persist();return true;}persist();return false;
    }
    void initialize(){QUrlQuery q(readerUrl_);art_=q.queryItemValue("art");if(!QRegularExpression("^[0-9]+$").match(art_).hasMatch()){error("Неизвестная книга");return;}if(q.queryItemValue("format")=="epub"||q.queryItemValue("drm")=="1"){error("Этот формат доступен в веб-читалке");return;}
        get(api("users/me"),[this](int s,const QString &t){bool ok=false;auto user=payload(s,t,&ok);const QString uid=QString::number(qint64(user["id"].toDouble()));if(!ok||uid=="0"){error(QString("Для чтения нужен вход в Литрес (HTTP %1)").arg(s));return;}
            userId_=uid;QString dir="/mnt/ext1/litres-client/native-reader/"+uid;QDir().mkpath(dir);statePath_=dir+"/"+art_+".json";QFile f(statePath_);if(f.open(QIODevice::ReadOnly)&&f.size()<65536)progress_.restore(QJsonDocument::fromJson(f.readAll()).object());
            get(api("arts/"+art_),[this](int s,const QString &t){bool ok=false;auto art=payload(s,t,&ok);if(!ok){error("Не удалось получить сведения о книге");return;}title_=art["title"].toString();if(art["is_drm"].toBool()||art["art_type"].toInt()!=0){error("Этот формат доступен в веб-читалке");return;}
                QUrlQuery q(readerUrl_);QString file=QString::number(qint64(art["release_file_id"].toDouble()));QString url=q.queryItemValue("baseurl");if(!url.isEmpty()){QUrl b=readerUrl_.resolved(QUrl(url));if(b.scheme()!="https"||b.host()!="www.litres.ru"||b.hasQuery()||b.hasFragment()){error("Неизвестный адрес данных книги");return;}base_=b.toString();}
                else if(q.queryItemValue("trials")=="1")base_="https://www.litres.ru/pub/t/"+art_+".";
                else base_="https://www.litres.ru/download_book"+QString(art["my_art_status"].toInt()>0||art["is_free"].toBool()?"":"_subscr")+"/"+art_+"/"+file+"/";
                get(base_+"json/toc.js",[this](int s,const QString &t){QString err;auto toc=Fb3::Parser().parse(t,&err).toObject();parts_=toc["Parts"].toArray();if(s!=200||!err.isEmpty()||parts_.isEmpty()){error("Литрес не предоставил данные книги");return;}
                    for(int i=0;i<parts_.size();++i){auto p=parts_[i].toObject();if(!QRegularExpression("^[a-zA-Z0-9_.-]+\\.js$").match(p["url"].toString()).hasMatch()||p["url"].toString().contains("..")||p["xps"].toArray().isEmpty()){error("Неизвестный формат частей книги");return;}if(i+1<parts_.size())p["e"]=parts_[i+1].toObject()["s"].toInt()-1;parts_[i]=p;}
                    total_=qMax(1,parts_.last().toObject()["e"].toInt());remote([this](bool ok,const QJsonObject &r){if(!ok){error("Не удалось прочитать закладку Литрес");return;}reconcile(r);ready_=true;showPointer(progress_.local["xpointer"].toString());});
                });
            });
        });
    }
    int partFor(const QString &xp)const{auto a=Fb3::pointerPath(xp);if(a.isEmpty()||(!parts_.isEmpty()&&Fb3::compare(a,parts_.first().toObject()["xps"].toArray())<0))return 0;for(int i=0;i<parts_.size();++i){auto p=parts_[i].toObject();if(Fb3::compare(a,p["xps"].toArray())>=0&&Fb3::compare(a,p["xpe"].toArray())<=0)return i;}return -1;}
    void showPointer(const QString &xp){int p=partFor(xp);if(p<0){error("Сохранённое место не найдено в этой версии книги");return;}loadPart(p,xp,false);}
    void fetchPart(int p,const std::function<void(bool)> &done){if(cache_.contains(p)){done(true);return;}if(fetching_.contains(p)){QTimer::singleShot(100,this,[this,p,done](){fetchPart(p,done);});return;}fetching_.insert(p);
        get(base_+"json/"+parts_[p].toObject()["url"].toString(),[this,p,done](int s,const QString &t){fetching_.remove(p);QString err;auto data=Fb3::Parser().parse(t,&err);bool ok=s==200&&err.isEmpty()&&data.isArray()&&!data.toArray().isEmpty();if(ok)cache_[p]=data.toArray();done(ok);});
    }
    void loadPart(int p,const QString &xp,bool last,bool moved=false){if(p<0||p>=parts_.size()||loading_)return;loading_=true;status("Загрузка части книги…");const int e=++epoch_;
        fetchPart(p,[this,p,xp,last,moved,e](bool ok){if(e!=epoch_)return;loading_=false;if(!ok){error("Не удалось загрузить часть · повторите через меню");return;}
            imageShown_.clear();layout_.load(cache_[p],parts_[p].toObject()["s"].toInt());layout_.paginate(width()-44,font_,textHeight());int pos=xp.isEmpty()?0:layout_.position(xp);if(!xp.isEmpty()&&pos<0){error("Точное место не найдено · откройте веб-версию");return;}part_=p;failed_=false;line_=last?qMax(0,layout_.lines.size()-1):layout_.lineFor(pos);if(last)line_=layout_.previous(line_,textHeight());
            status(progress_.pending?"Место сохранено на книге":"Синхронизировано");if(diagnostic)diagnostic(QString("layout ready part=%1 lines=%2 firstLine=%3").arg(part_).arg(layout_.lines.size()).arg(line_));if(moved)changed();
            if(isVisible()&&!ReaderHistory::remember("/mnt/ext1/litres-client",userId_,art_,title_)&&diagnostic)diagnostic("last book save failed");
            visibleImages();
            for(auto i=cache_.begin();i!=cache_.end();)if(qAbs(i.key()-p)>1)i=cache_.erase(i);else ++i;
            if(p+1<parts_.size()&&!fetching_.contains(p+1))QTimer::singleShot(150,this,[this,p](){if(part_==p)fetchPart(p+1,[](bool){});});
        });
    }
    void changed(){if(part_<0||layout_.lines.isEmpty())return;int pos=layout_.lines[line_].start;progress_.change(layout_.pointer(pos),layout_.percent(pos,total_));if(persist())status("Место сохранено на книге");save_.start(1800);visibleImages();}
    void resized(){if(part_<0||layout_.lines.isEmpty())return;const QString xp=layout_.pointer(layout_.lines[line_].start);layout_.paginate(width()-44,font_,textHeight());line_=layout_.lineFor(qMax(0,layout_.position(xp)));visibleImages();update();}
public:
    std::function<void(const QString&)> diagnostic;
    std::function<void(bool)> leave; // true explicitly opens the web reader.
    explicit NativeReader(QWebEngineProfile *profile,const QUrl &url,QWidget *parent=nullptr):QWidget(parent),net_(new ReaderTransport(profile,this)),readerUrl_(url){QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);font_=qBound(22,prefs.value("reader/nativeFont",30).toInt(),48);setFocusPolicy(Qt::StrongFocus);setAutoFillBackground(true);setPalette(QPalette(Qt::white));save_.setSingleShot(true);connect(&save_,&QTimer::timeout,this,[this](){sync();});retry_.setInterval(60000);connect(&retry_,&QTimer::timeout,this,[this](){if(!progress_.conflict)sync();});retry_.start();QTimer::singleShot(0,this,[this](){initialize();});}
    ~NativeReader()override{closed_=true;persist();}
    QJsonObject diagnostics()const{QJsonObject d;d["ready"]=ready_;d["failed"]=failed_;d["part"]=part_;d["line"]=line_;d["lines"]=layout_.lines.size();d["pending"]=progress_.pending;d["images"]=layout_.images.size();d["imagesLoaded"]=imageShown_.size();d["status"]=status_;return d;}
    bool pending()const{return progress_.pending;}
    void sync(){if(!ready_||syncing_||!progress_.pending||progress_.conflict)return;syncing_=true;remote([this](bool ok,const QJsonObject &r){if(!ok){syncing_=false;status("Место на книге · синхронизация позже");return;}if(!reconcile(r)||!progress_.pending){syncing_=false;return;}auto body=progress_.begin();QPointer<NativeReader> self(this);net_->request(QUrl(api("users/me/arts/"+art_+"/fb3/autobookmark")),"POST",body,[self](int s,const QString &t){if(!self||self->closed_)return;self->syncing_=false;if(self->diagnostic)self->diagnostic(QString("bookmark response HTTP=%1").arg(s));bool ok=false;auto r=payload(s,t,&ok);if(ok&&!r["xpointer"].toString().isEmpty()){self->progress_.ack(r);self->persist();self->status(self->progress_.pending?"Место сохранено на книге":"Синхронизировано");if(self->progress_.pending)self->save_.start(1800);}else if(s==409){self->progress_.conflict=true;self->status("Конфликт закладки · OK: меню");}else self->status("Место на книге · синхронизация позже");});});}
    void control(const QString &action){if(!ready_||loading_||failed_||layout_.lines.isEmpty())return;int target=line_;if(action=="pageDown")target=layout_.next(line_,textHeight());else if(action=="pageUp"){if(line_==0&&part_>0){loadPart(part_-1,{},true,true);return;}target=layout_.previous(line_,textHeight());}else if(action=="up")target=qMax(0,line_-1);else if(action=="down")target=line_+1;else return;
        if(target>=layout_.lines.size()){if(part_+1<parts_.size())loadPart(part_+1,{},false,true);return;}if(target!=line_){line_=target;changed();}}
    void menu(){QMenu m(this);auto *back=m.addAction("Вернуться к библиотеке");auto *web=m.addAction("Открыть веб-версию");auto *larger=m.addAction("Увеличить шрифт");auto *smaller=m.addAction("Уменьшить шрифт");auto *retry=m.addAction("Повторить загрузку / синхронизацию");auto *a=m.exec(mapToGlobal(QPoint(24,70)));if(a==back||a==web){save_.stop();persist();sync();if(leave)leave(a==web);}else if(a==larger||a==smaller){font_=qBound(22,font_+(a==larger?2:-2),48);QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);prefs.setValue("reader/nativeFont",font_);resized();}else if(a==retry){imageFailed_.clear();visibleImages();if(failed_){failed_=false;if(ready_)showPointer(progress_.local["xpointer"].toString());else initialize();}else{progress_.conflict=false;sync();}}}
    void pause(){save_.stop();persist();sync();}
protected:
    void resizeEvent(QResizeEvent *e)override{QWidget::resizeEvent(e);resized();}
    void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),Qt::white);QFont f=font();f.setPixelSize(20);p.setFont(f);p.setPen(Qt::black);p.drawText(QRect(22,10,width()-44,30),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(title_.isEmpty()?"Литрес · Чтение":title_,Qt::ElideRight,width()-44));p.drawLine(22,48,width()-22,48);if(!failed_)layout_.draw(p,line_,textHeight(),QPoint(22,62));else p.drawText(QRect(24,100,width()-48,height()-180),Qt::TextWordWrap|Qt::AlignTop,"Не удалось открыть лёгкую читалку.\n\nНажмите OK, чтобы повторить загрузку или открыть веб-версию.");f.setPixelSize(17);p.setFont(f);QString pct=progress_.local.contains("percent")?QString::number(progress_.local["percent"].toDouble(),'f',1)+"% · ":"";p.drawText(QRect(22,height()-57,width()-44,25),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(pct+status_,Qt::ElideRight,width()-44));p.drawText(QRect(22,height()-29,width()-44,24),Qt::AlignLeft|Qt::AlignVCenter,"↑/↓ строка · Листать экран · OK меню");}
};
