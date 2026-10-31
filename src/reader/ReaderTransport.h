#pragma once
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QJsonObject>
#include <QPointer>
#include <QQueue>
#include <functional>
// Chromium owns TLS/cookies. A blank HTTPS transport page has no reader UI,
// animations or book layout. Rendering and page keys never run JavaScript.
class ReaderTransport:public QObject {
    struct Request {QUrl url;QByteArray method;QJsonObject body;std::function<void(int,const QString&)> done;};
    QWebEnginePage *page_;QTimer poll_,timeout_;QQueue<Request> queue_;Request current_;bool failed_=false,ready_=false,busy_=false,polling_=false;int serial_=0;
    static QString js(const QString &s){QJsonArray a;a.append(s);QString x=QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));return x.mid(1,x.size()-2);}
    void complete(int status,const QString &text){timeout_.stop();poll_.stop();busy_=false;auto done=current_.done;current_.done=nullptr;if(done)done(status,text);QTimer::singleShot(0,this,[this](){next();});}
    void next(){if(!ready_||busy_||queue_.isEmpty())return;busy_=true;current_=queue_.dequeue();const int id=++serial_;
        QString code="(function(){var h={'Accept':'application/json'};var u="+js(current_.url.toString())+";var method="+js(QString::fromLatin1(current_.method))+";if(u.indexOf('https://api.litres.ru/')===0){h['app-id']='115';h['client-host']='www.litres.ru';h['ui-language-code']='ru';h['ui-currency']='RUB';var m=document.cookie.match(/(?:^|;\\s*)SID=([^;]*)/);if(m)h['session-id']=m[1];}if(method==='POST')h['Content-Type']='application/json';window.__pbReaderResult=null;window.__pbReaderActive="+QString::number(id)+";window.__pbReaderAbort=new AbortController();fetch(u,{method:method,headers:h,credentials:'include',cache:'no-store',signal:window.__pbReaderAbort.signal";
        if(current_.method=="POST")code+=",body:"+js(QString::fromUtf8(QJsonDocument(current_.body).toJson(QJsonDocument::Compact)));
        code+="}).then(function(r){return r.text().then(function(t){if(window.__pbReaderActive==="+QString::number(id)+")window.__pbReaderResult={id:"+QString::number(id)+",status:r.status,text:t.length<=4194304?t:''};});}).catch(function(){if(window.__pbReaderActive==="+QString::number(id)+")window.__pbReaderResult={id:"+QString::number(id)+",status:0,text:''};});return true;})()";
        timeout_.start(30000);QPointer<ReaderTransport> self(this);page_->runJavaScript(code,[self,id](const QVariant &v){if(!self||!self->busy_||self->serial_!=id)return;if(!v.toBool())self->complete(0,{});else self->poll_.start();});
    }
public:
    explicit ReaderTransport(QWebEngineProfile *profile,QObject *parent=nullptr):QObject(parent),page_(new QWebEnginePage(profile,this)){
        page_->settings()->setAttribute(QWebEngineSettings::AutoLoadImages,false);poll_.setInterval(100);timeout_.setSingleShot(true);
        connect(page_,&QWebEnginePage::loadFinished,this,[this](bool ok){if(ready_)return;timeout_.stop();if(!ok||page_->url()!=QUrl("https://www.litres.ru/robots.txt")){failed_=true;while(!queue_.isEmpty()){auto r=queue_.dequeue();r.done(0,{});}return;}ready_=true;next();});
        connect(&poll_,&QTimer::timeout,this,[this](){if(polling_)return;polling_=true;const int id=serial_;QPointer<ReaderTransport> self(this);page_->runJavaScript("window.__pbReaderResult || null",[self,id](const QVariant &v){if(!self)return;self->polling_=false;auto m=v.toMap();if(self->busy_&&id==self->serial_&&m["id"].toInt()==id)self->complete(m["status"].toInt(),m["text"].toString());});});
        connect(&timeout_,&QTimer::timeout,this,[this](){page_->runJavaScript("window.__pbReaderAbort && window.__pbReaderAbort.abort()");if(busy_)complete(0,{});else{failed_=true;while(!queue_.isEmpty()){auto r=queue_.dequeue();r.done(0,{});}}});
        timeout_.start(30000);page_->load(QUrl("https://www.litres.ru/robots.txt"));
    }
    void request(const QUrl &url,const QByteArray &method,const QJsonObject &body,const std::function<void(int,const QString&)> &done){
        bool allowed=url.scheme()=="https"&&url.userInfo().isEmpty()&&(url.port()==-1||url.port()==443)&&((url.host()=="api.litres.ru"&&url.path().startsWith("/foundation/api/"))||(url.host()=="www.litres.ru"&&(url.path().startsWith("/download_book/")||url.path().startsWith("/download_book_subscr/")||url.path().startsWith("/pub/t/"))));
        if(!allowed||(method!="GET"&&method!="POST")){done(0,{});return;}Request r;r.url=url;r.method=method;r.body=body;r.done=done;queue_.enqueue(r);if(failed_){failed_=false;timeout_.start(30000);page_->load(QUrl("https://www.litres.ru/robots.txt"));}next();
    }
};
