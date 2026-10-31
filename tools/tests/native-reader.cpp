#include "reader/Fb3Data.h"
#include "reader/Fb3Layout.h"
#include "reader/ReaderProgress.h"
#include "reader/NativeReader.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <cassert>
#include <iostream>
static QJsonObject bookmark(int version,const QString &xp,double percent){QJsonObject o;o["version"]=version;o["xpointer"]=xp;o["percent"]=percent;return o;}
int main(int argc,char**argv){QApplication app(argc,argv);QString error;Fb3::Parser parser;
 auto a=parser.parse("[{t:'p', xp:[1,2,3], c:['Первое ',{t:'em',xp:[1,2,3,2],c:['слово']},' и хвост\\u0021',],},]",&error);assert(error.isEmpty()&&a.isArray());
 for(QString s:{"(()=>42)()","{x:process.exit()}","[undefined]","{x:NaN}","[1] trailing","{a:1,a:2}","['\\z']","{a:1; b:2}","[","{a:'unfinished}"}){parser.parse(s,&error);assert(!error.isEmpty());}
 Fb3::Layout layout;layout.load(a.toArray(),1);layout.paginate(300,30);assert(layout.doc.begin().blockFormat().alignment()==Qt::AlignJustify);assert(layout.doc.toPlainText()==QString::fromUtf8("Первое слово и хвост!"));
 for(int i=0;i<layout.doc.toPlainText().size();++i){QString xp=layout.pointer(i);assert(!xp.isEmpty());assert(layout.position(xp)==i);}
 assert(layout.position("/1/2/3/1.9")==9); // parent and inline anchors both restore
 QJsonArray paras;for(int i=0;i<100;++i){QJsonObject p;p["t"]="p";p["xp"]=QJsonArray{1,3,i};p["c"]=QJsonArray{QString::fromUtf8("Строка без потерь и пропусков. ").repeated(12)};paras.append(p);}layout.load(paras,0);
 for(int width:{240,500,714})for(int font:{22,30,48}){layout.paginate(width,font);QSet<int> visited;int line=0,turns=0;
  while(line<layout.lines.size()){int end=layout.endLine(line,650);assert(end>line);for(int j=line;j<end;++j){assert(!visited.contains(j));visited.insert(j);}int next=layout.next(line,650);assert(next>line);if(next<layout.lines.size()){assert(next==end);assert(layout.previous(next,650)<=line);}line=next;assert(++turns<10000);}
  assert(visited.size()==layout.lines.size());
  for(const auto &l:layout.lines){QString xp=layout.pointer(l.start);assert(layout.position(xp)==l.start);}
 }
 Fb3::Progress p;auto remote=bookmark(6,"/1/7/271/1.0",15.1);assert(p.reconcile(remote));assert(!p.pending);p.change("/1/7/272/1.3",15.2);assert(p.pending);assert(p.reconcile(remote));auto body=p.begin();assert(body["version"].toInt()==7);assert(body["is_epub"]==false);assert(!body["id"].toString().isEmpty());
 p.change("/1/7/273/1.0",15.3);p.ack(bookmark(7,body["xpointer"].toString(),15.2));assert(p.pending&&p.local["xpointer"]=="/1/7/273/1.0");
 assert(!p.reconcile(bookmark(8,"/1/9/1/1.0",24.)));assert(p.conflict);auto saved=p.json();Fb3::Progress q;q.restore(saved);assert(q.pending);assert(!q.reconcile(bookmark(8,"/1/9/1/1.0",24.)));q.chooseRemote(bookmark(8,"/1/9/1/1.0",24.));assert(!q.pending);q.change("/1/9/2/1.0",24.1);auto b=q.begin(); // ambiguous network response acknowledged by next GET
 assert(q.reconcile(bookmark(9,b["xpointer"].toString(),24.1)));assert(!q.pending);
 p.chooseLocal(bookmark(9,"/1/10/1/1.0",30.));assert(p.pending&&!p.conflict);assert(p.begin()["version"].toInt()==10);
 std::cout<<"PASS strict data-only parser; inline/parent XPointer round trips; 9 page/font layouts with every line covered; versioned sync, edits during save, offline state, conflict and ambiguous-response handling\n";
 if(argc>1){QFile f(argv[1]);assert(f.open(QIODevice::ReadOnly));auto data=parser.parse(QString::fromUtf8(f.readAll()),&error);assert(error.isEmpty()&&data.isArray());QElapsedTimer time;time.start();layout.load(data.toArray(),0);layout.paginate(714,30);qint64 ms=time.elapsed();assert(!layout.lines.isEmpty());for(const auto &l:layout.lines){bool text=false;for(const auto &r:layout.runs)if(l.start>=r.start&&l.start<r.start+r.length)text=true;if(text)assert(layout.position(layout.pointer(l.start))==l.start);}for(const auto &r:layout.runs)for(int offset:{0,r.length/2,r.length-1})assert(layout.position(layout.pointer(r.start+offset))==r.start+offset);QImage im(758,1024,QImage::Format_RGB32);im.fill(Qt::white);QPainter paint(&im);layout.draw(paint,0,900,QPoint(22,50));paint.end();im.save("/tmp/native-reader-preview.png");time.restart();int n=0;for(int l=0;l<layout.lines.size();l=layout.next(l,900))++n;
  std::cout<<"PASS provided private chunk: "<<layout.runs.size()<<" runs, "<<layout.lines.size()<<" lines, "<<n<<" pages; layout "<<ms<<" ms, page traversal "<<time.elapsed()<<" ms on host Qt (not device speed)\n";}
}
