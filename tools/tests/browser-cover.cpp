#include "browser/SitePresentation.h"
#include <QApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QBuffer>
#include <QImage>
#include <QtTest/QTest>
#include <cassert>
#include <iostream>
QVariant eval(QWebEnginePage *p,const QString &s){bool done=false;QVariant result;p->runJavaScript(s,[&](const QVariant &v){result=v;done=true;});for(int i=0;i<500&&!done;++i)QTest::qWait(10);assert(done);return result;}
int main(int argc,char**argv){QApplication app(argc,argv);QWebEngineView view;view.resize(379,650);view.show();QImage im(100,150,QImage::Format_RGB32);im.fill(Qt::black);QByteArray bytes;QBuffer b(&bytes);b.open(QIODevice::WriteOnly);im.save(&b,"PNG");QString html="<style>body{margin:20px}.card{display:flex;gap:12px;width:335px}.uik-container-oVrQr5{width:96px;flex-shrink:0;height:300px;padding-top:160px}.cover{position:absolute;left:0;bottom:0;height:300px;width:96px}#info{min-width:0}</style><div class='card'><div class='uik-container-oVrQr5'><img id='cover' class='cover' data-testid='adaptiveCover__img' src='data:image/png;base64,"+QString::fromLatin1(bytes.toBase64())+"'></div><div id='info'>Название книги<br>Автор<br>Читать</div></div>";bool loaded=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool ok){assert(ok);loaded=true;});view.setHtml(html,QUrl("https://www.litres.ru/my-books/read-and-listen/"));for(int i=0;i<500&&!loaded;++i)QTest::qWait(10);assert(loaded);assert(eval(view.page(),"Math.abs(cover.getBoundingClientRect().top-info.getBoundingClientRect().top)>100").toBool());eval(view.page(),SitePresentation::injection());QTest::qWait(100);assert(eval(view.page(),"(function(){var c=cover.getBoundingClientRect(),f=cover.parentElement.getBoundingClientRect(),t=info.getBoundingClientRect();return Math.abs(c.top-t.top)<1&&Math.abs(c.width-96)<1&&Math.abs(c.height-144)<1&&Math.abs(f.height-c.height)<1&&c.right<=t.left;})()").toBool());view.grab().save("/tmp/cover-alignment.png");std::cout<<"PASS cover remains inside its aspect-correct frame beside the book text instead of being positioned against the page\n";}
