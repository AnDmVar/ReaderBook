#include "reader/Fb3Layout.h"
#include "reader/ReaderHistory.h"
#include <QApplication>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
QJsonObject paragraph(int n,const QString &text){QJsonObject o;o["t"]="p";o["xp"]=QJsonArray{1,n};o["c"]=QJsonArray{text};return o;}
int main(int argc,char **argv){QApplication app(argc,argv);QTemporaryDir temp;assert(temp.isValid());assert(ReaderHistory::read(temp.path(),"42").isEmpty());assert(ReaderHistory::remember(temp.path(),"42","123","Книга «Название»"));assert(ReaderHistory::read(temp.path(),"42")["art"]=="123");assert(ReaderHistory::read(temp.path(),"43").isEmpty());assert(!ReaderHistory::remember(temp.path(),"../bad","123","Книга"));assert(!ReaderHistory::remember(temp.path(),"42","456",""));assert(ReaderHistory::read(temp.path(),"42")["art"]=="123");assert(ReaderHistory::remember(temp.path(),"42","456","Другая книга"));assert(ReaderHistory::read(temp.path(),"42")["art"]=="456");
 for(QString n:{"i_001.jpg","chapter/image.png","a-b.JPEG"})assert(Fb3::imageName(n));for(QString n:{"../secret.jpg","/img.jpg","http://host/a.jpg","x/%2e%2e/a.png","x/./a.jpg","x.svg","x.jpg?sid=foo"})assert(!Fb3::imageName(n));assert(Fb3::decodeImage("not an image").isNull());
 for(QSize shape:{QSize(200,1200),QSize(1200,200),QSize(300,300)}){
  QImage source(shape,QImage::Format_RGB32);source.fill(Qt::red);QByteArray bytes;QBuffer b(&bytes);b.open(QIODevice::WriteOnly);assert(source.save(&b,"PNG"));QImage decoded=Fb3::decodeImage(bytes);assert(!decoded.isNull());assert(decoded.width()<=758&&decoded.height()<=1024);assert(qAbs(double(decoded.width())/decoded.height()-double(shape.width())/shape.height())<.04);
  QJsonObject image;image["t"]="img";image["s"]="i_001.jpg";image["xp"]=QJsonArray{1,3}; // no dimensions: decoder must supply them
  QJsonArray data{paragraph(2,"До иллюстрации"),image,paragraph(4,QString::fromUtf8("После иллюстрации без потерь. ").repeated(20))};
  for(int width:{240,714})for(int height:{300,850})for(int font:{22,48}){
   Fb3::Layout l;l.load(data,0);l.setImage("i_001.jpg",decoded);l.paginate(width,font,height);assert(l.images.size()==1);int imagePos=l.images[0].position;assert(l.position("/1/3/1.0")==imagePos);assert(l.pointer(imagePos)=="/1/3/1.0");assert(l.position("/1/4/1.0")>imagePos);int first=l.lineFor(imagePos);assert(l.lines[first].bottom-l.lines[first].top<=height);
   QImage output(width,height,QImage::Format_RGB32);output.fill(Qt::white);QPainter p(&output);l.draw(p,first,height,QPoint());p.end();int x0=width,y0=height,x1=-1,y1=-1;
   for(int y=0;y<height;++y)for(int x=0;x<width;++x){QColor c=output.pixelColor(x,y);if(c.red()>200&&c.green()<50&&c.blue()<50){x0=qMin(x0,x);x1=qMax(x1,x);y0=qMin(y0,y);y1=qMax(y1,y);}}
   assert(x1>x0&&y1>y0);double ratio=double(x1-x0+1)/(y1-y0+1);if(qMin(qAbs((y1-y0+1)-(x1-x0+1)*double(shape.height())/shape.width()),qAbs((x1-x0+1)-(y1-y0+1)*double(shape.width())/shape.height()))>2){output.save("/tmp/failed-image.png");std::cerr<<"ratio "<<shape.width()<<"x"<<shape.height()<<" viewport "<<width<<"x"<<height<<" font "<<font<<" red bounds "<<x0<<","<<y0<<" "<<x1<<","<<y1<<" first "<<first<<" lineheight "<<(l.lines[first].bottom-l.lines[first].top)<<"\n";assert(false);}assert(y1<height); // entire illustration, not a clipped viewport slice
   QSet<int> seen;for(int line=0;line<l.lines.size();){int end=l.endLine(line,height);for(int i=line;i<end;++i)seen.insert(i);int next=l.next(line,height);assert(next>line);line=next;}assert(seen.size()==l.lines.size());
   l.setImage("i_001.jpg",{});l.paginate(width,font,height);assert(l.position("/1/4/1.0")>imagePos);
   if(width==714&&height==850&&font==22)output.save("/tmp/native-image-"+QString::number(shape.width())+".png");
  }
 }
 QDir(temp.path()+"/native-reader").removeRecursively();assert(ReaderHistory::read(temp.path(),"42").isEmpty());std::cout<<"PASS per-account durable last-book history; safe image paths/decode; portrait/landscape/square images retain proportions and fit pages in 24 layouts; image/text anchors and complete line coverage\n";
}
