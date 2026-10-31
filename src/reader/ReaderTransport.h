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
    struct Request {bool binary=false;QUrl url;QByteArray method;QJsonObject body;std::function<void(int,const QString&)> done;};
    QWebEnginePage *page_;QTimer poll_,timeout_;QQueue<Request> queue_;Request current_;bool failed_=false,ready_=false,busy_=false,polling_=false;int serial_=0;
    static QString js(const QString &s){QJsonArray a;a.append(s);QString x=QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));return x.mid(1,x.size()-2);}
    void complete(int status,const QString &text){timeout_.stop();poll_.stop();busy_=false;auto done=current_.done;current_.done=nullptr;QPointer<ReaderTransport> self(this);if(done)done(status,text);if(self)QTimer::singleShot(0,self,[self](){if(self)self->next();});}
    void next(){if(!ready_||busy_||queue_.isEmpty())return;busy_=true;current_=queue_.dequeue();const int id=++serial_;
        QString code="(function(){var h={'Accept':'application/json'};var u="+js(current_.url.toString())+";var method="+js(QString::fromLatin1(current_.method))+";if(u.indexOf('https://api.litres.ru/')===0){h['app-id']='115';h['client-host']='www.litres.ru';h['ui-language-code']='ru';h['ui-currency']='RUB';var m=document.cookie.match(/(?:^|;\\s*)SID=([^;]*)/);if(m)h['session-id']=m[1];}if(method==='POST')h['Content-Type']='application/json';window.__pbReaderResult=null;window.__pbReaderActive="+QString::number(id)+";window.__pbReaderAbort=new AbortController();fetch(u,{method:method,headers:h,credentials:'include',cache:'no-store',signal:window.__pbReaderAbort.signal";
        if(current_.method=="POST")code+=",body:"+js(QString::fromUtf8(QJsonDocument(current_.body).toJson(QJsonDocument::Compact)));
        const QString idString=QString::number(id);
        if(current_.binary){
            // Normalize through Chromium's raster codecs: firmware Qt may lack a JPEG plugin.
            code+="}).then(function(r){if(r.status!==200||Number(r.headers.get('content-length'))>8388608)throw Error('image response');return r.arrayBuffer().then(function(buf){if(buf.byteLength>8388608)throw Error('image size');var a=new Uint8Array(buf);if(!((a[0]===255&&a[1]===216)||(a[0]===137&&a[1]===80&&a[2]===78)||(a[0]===71&&a[1]===73&&a[2]===70)||(a[0]===82&&a[1]===73&&a[2]===70&&a[3]===70)))throw Error('raster format');return new Promise(function(resolve,reject){var image=new Image(),url=URL.createObjectURL(new Blob([buf]));image.onload=function(){URL.revokeObjectURL(url);try{var w=image.naturalWidth,h=image.naturalHeight;if(!w||!h||w*h>24000000)throw Error('image dimensions');var scale=Math.min(1,758/w,1024/h),canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(w*scale));canvas.height=Math.max(1,Math.round(h*scale));var ctx=canvas.getContext('2d');ctx.fillStyle='white';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.drawImage(image,0,0,canvas.width,canvas.height);var data=canvas.toDataURL('image/png').split(',')[1];if(window.__pbReaderActive==="+idString+")window.__pbReaderResult={id:"+idString+",status:200,text:data};resolve();}catch(e){reject(e);}};image.onerror=function(){URL.revokeObjectURL(url);reject(Error('image decode'));};image.src=url;});});})";
        }else code+="}).then(function(r){return r.text().then(function(t){if(window.__pbReaderActive==="+idString+")window.__pbReaderResult={id:"+idString+",status:r.status,text:t.length<=4194304?t:''};});})";
        code+=".catch(function(){if(window.__pbReaderActive==="+idString+")window.__pbReaderResult={id:"+idString+",status:0,text:''};});return true;})()";
        timeout_.start(30000);QPointer<ReaderTransport> self(this);page_->runJavaScript(code,[self,id](const QVariant &v){if(!self||!self->busy_||self->serial_!=id)return;if(!v.toBool())self->complete(0,{});else self->poll_.start();});
    }
public:
    explicit ReaderTransport(QWebEngineProfile *profile,QObject *parent=nullptr):QObject(parent),page_(new QWebEnginePage(profile,this)){
        page_->settings()->setAttribute(QWebEngineSettings::AutoLoadImages,true);poll_.setInterval(100);timeout_.setSingleShot(true);
        connect(page_,&QWebEnginePage::loadFinished,this,[this](bool ok){if(ready_)return;timeout_.stop();if(!ok||page_->url()!=QUrl("https://www.litres.ru/robots.txt")){failed_=true;while(!queue_.isEmpty()){auto r=queue_.dequeue();r.done(0,{});}return;}ready_=true;next();});
        connect(&poll_,&QTimer::timeout,this,[this](){if(polling_)return;polling_=true;const int id=serial_;QPointer<ReaderTransport> self(this);page_->runJavaScript("window.__pbReaderResult || null",[self,id](const QVariant &v){if(!self)return;self->polling_=false;auto m=v.toMap();if(self->busy_&&id==self->serial_&&m["id"].toInt()==id)self->complete(m["status"].toInt(),m["text"].toString());});});
        connect(&timeout_,&QTimer::timeout,this,[this](){page_->runJavaScript("window.__pbReaderAbort && window.__pbReaderAbort.abort()");if(busy_)complete(0,{});else{failed_=true;while(!queue_.isEmpty()){auto r=queue_.dequeue();r.done(0,{});}}});
        timeout_.start(30000);page_->load(QUrl("https://www.litres.ru/robots.txt"));
    }
    void request(const QUrl &url,const QByteArray &method,const QJsonObject &body,const std::function<void(int,const QString&)> &done,bool binary=false){
        bool allowed=url.scheme()=="https"&&url.userInfo().isEmpty()&&(url.port()==-1||url.port()==443)&&((url.host()=="api.litres.ru"&&url.path().startsWith("/foundation/api/"))||(url.host()=="www.litres.ru"&&(url.path().startsWith("/download_book/")||url.path().startsWith("/download_book_subscr/")||url.path().startsWith("/pub/t/"))));
        if(!allowed||(method!="GET"&&method!="POST")){done(0,{});return;}Request r;r.binary=binary;r.url=url;r.method=method;r.body=body;r.done=done;queue_.enqueue(r);if(failed_){failed_=false;timeout_.start(30000);page_->load(QUrl("https://www.litres.ru/robots.txt"));}next();
    }
    void image(const QUrl &url,const std::function<void(int,const QByteArray&)> &done){request(url,"GET",{},[done](int s,const QString &b64){if(b64.size()>12*1024*1024){done(0,{});return;}done(s,QByteArray::fromBase64(b64.toLatin1()));},true);}
};
