#pragma once
#include "Fb3Data.h"
#include "ReaderImages.h"
#include <QTextImageFormat>
#include <QMap>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QSet>
namespace Fb3 {
struct Ref { QString xp; int offset=0; Ref(QString p=QString(),int o=0):xp(p),offset(o){} };
struct Run {int start=0,length=0,index=0;QVector<Ref> refs;};
struct ImageSlot {QString name,resource;int position=0;QSize original;};
struct Line {qreal top=0,bottom=0;int start=0,length=0;};
class Layout {
    QVector<Ref> ancestors_;int index_=0;bool first_=true;QTextCursor cursor_;bool afterImage_=false;QTextBlockFormat beforeImage_;QMap<QString,QImage> loadedImages_;
    static QImage placeholder(){QImage i(120,80,QImage::Format_RGB32);i.fill(Qt::white);QPainter p(&i);p.setPen(Qt::gray);p.drawRect(1,1,117,77);p.drawText(i.rect(),Qt::AlignCenter,"…");return i;}
    void text(QString s){s.remove(QChar(0xad));if(s.isEmpty())return;if(afterImage_){cursor_.insertBlock(beforeImage_);afterImage_=false;}Run r;r.start=cursor_.position();r.length=s.size();r.index=index_;r.refs=ancestors_;runs.append(r);cursor_.insertText(s);for(auto &a:ancestors_)a.offset+=s.size();}
    void node(const QJsonObject &o,int depth=0){if(depth>64)return;const QString t=o["t"].toString(),xp=path(o["xp"].toArray());bool anchor=!xp.isEmpty();if(anchor)ancestors_.append(Ref(xp));
        QTextCharFormat old=cursor_.charFormat(),fmt=old;
        if(t=="em"||t=="i")fmt.setFontItalic(true);if(t=="strong"||t=="b"||t=="title")fmt.setFontWeight(QFont::Bold);if(t=="sup")fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);cursor_.setCharFormat(fmt);
        if(t=="img"){
            const QString name=o["s"].toString();
            if(!imageName(name)){cursor_.insertText("[Неизвестный формат иллюстрации]");}
            else{
                if(!afterImage_)beforeImage_=cursor_.blockFormat();
                if(!cursor_.atBlockStart())cursor_.insertBlock();
                QTextBlockFormat bf;bf.setAlignment(Qt::AlignCenter);bf.setTopMargin(8);bf.setBottomMargin(8);cursor_.setBlockFormat(bf);
                ImageSlot slot;slot.name=name;slot.position=cursor_.position();slot.resource="fb3-image:/"+name;slot.original=QSize(o["w"].toInt(),o["h"].toInt());images.append(slot);
                doc.addResource(QTextDocument::ImageResource,QUrl(slot.resource),placeholder());QTextImageFormat image;image.setName(slot.resource);image.setWidth(100);image.setHeight(100);cursor_.insertImage(image);
                if(!xp.isEmpty()){Run r;r.start=slot.position;r.length=1;r.index=index_;r.refs.append(Ref(xp));runs.append(r);}afterImage_=true;
            }
        }
        else if(t=="br"){cursor_.insertText(QString(QChar::LineSeparator));}
        else{bool priorBlock=false;for(const auto &v:o["c"].toArray()){if(v.isString())text(v.toString());else if(v.isObject()){QString child=v.toObject()["t"].toString();bool block=child=="p"||child=="v"||child=="subtitle";if(block&&priorBlock)cursor_.insertText(QString(QChar::LineSeparator));node(v.toObject(),depth+1);priorBlock=block;}}}
        cursor_.setCharFormat(old);if(anchor)ancestors_.removeLast();
    }
    void blocks(const QJsonArray &a,int depth=0){if(depth>64)return;for(const auto &v:a){if(!v.isObject())continue;const auto o=v.toObject();if(wrapper(o)){const QString xp=path(o["xp"].toArray());if(!xp.isEmpty())ancestors_.append(Ref(xp));blocks(o["c"].toArray(),depth+1);if(!xp.isEmpty())ancestors_.removeLast();continue;}
        // Title/poem wrappers contain paragraphs: preserve each paragraph as a line block.
        const QString t=o["t"].toString();if((t=="title"||t=="poem"||t=="stanza"||t=="epigraph"||t=="cite")&&!o["c"].toArray().isEmpty()){
            if(!first_)cursor_.insertBlock();first_=false;afterImage_=false;QTextBlockFormat b;b.setTopMargin(12);b.setBottomMargin(12);b.setAlignment(t=="title"?Qt::AlignCenter:Qt::AlignLeft);cursor_.setBlockFormat(b);node(o);++index_;continue;}
        if(!first_)cursor_.insertBlock();first_=false;afterImage_=false;QTextBlockFormat b;b.setTopMargin(0);b.setBottomMargin(8);b.setTextIndent(t=="p"?30:0);b.setAlignment(o["fl"].toString()=="center"?Qt::AlignCenter:o["fl"].toString()=="right"?Qt::AlignRight:t=="p"?Qt::AlignJustify:Qt::AlignLeft);cursor_.setBlockFormat(b);node(o);++index_;
    }}
public:
    QTextDocument doc;QVector<Run> runs;QVector<Line> lines;QVector<ImageSlot> images;
    Layout(){doc.setUndoRedoEnabled(false);doc.setUseDesignMetrics(true);cursor_=QTextCursor(&doc);}
    void load(const QJsonArray &a,int start){for(const auto &i:images)doc.addResource(QTextDocument::ImageResource,QUrl(i.resource),QImage());loadedImages_.clear();images.clear();afterImage_=false;doc.clear();runs.clear();lines.clear();ancestors_.clear();index_=start;first_=true;cursor_=QTextCursor(&doc);blocks(a);}
    void setImage(const QString &name,const QImage &image){if(image.isNull())loadedImages_.remove(name);else loadedImages_[name]=image;for(auto &i:images)if(i.name==name){if(!image.isNull())i.original=image.size();doc.addResource(QTextDocument::ImageResource,QUrl(i.resource),image.isNull()?placeholder():image);}doc.markContentsDirty(0,doc.characterCount());}
    void paginate(int width,int fontPixels=30,int pageHeight=900){
        for(const auto &i:images){QSize natural=i.original.isValid()?i.original:QSize(300,400);QSize display=natural.scaled(QSize(qMax(80,width),qMax(60,pageHeight-48)),Qt::KeepAspectRatio);if(display.width()>natural.width())display=natural;
            QTextCursor c(&doc);c.setPosition(i.position);c.setPosition(i.position+1,QTextCursor::KeepAnchor);QTextImageFormat format=c.charFormat().toImageFormat();format.setWidth(display.width());format.setHeight(display.height());c.setCharFormat(format);}
        QFont f("Noto Sans");f.setPixelSize(fontPixels);doc.setDefaultFont(f);QTextOption option=doc.defaultTextOption();option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);doc.setDefaultTextOption(option);doc.setDocumentMargin(0);doc.setTextWidth(qMax(80,width));doc.documentLayout()->documentSize();lines.clear();for(auto b=doc.begin();b.isValid();b=b.next()){auto *l=b.layout();QPointF pos=l->position();for(int i=0;i<l->lineCount();++i){auto x=l->lineAt(i);Line y;y.top=pos.y()+x.y();y.bottom=y.top+x.height();y.start=b.position()+x.textStart();y.length=x.textLength();lines.append(y);}}}
    int endLine(int first,int height)const{if(lines.isEmpty())return 0;int e=qBound(0,first,lines.size()-1)+1;while(e<lines.size()&&lines[e].bottom-lines[first].top<=height)++e;return e;}
    int next(int first,int height)const{int e=endLine(first,height);return e;}
    int previous(int first,int height)const{if(lines.isEmpty())return 0;int last=qBound(0,first-1,lines.size()-1),i=last;while(i>0&&lines[last].bottom-lines[i-1].top<=height)--i;return i;}
    QString pointer(int pos)const{for(const auto &r:runs){if(pos<r.start+r.length&&!r.refs.isEmpty()){const auto &ref=r.refs.last();return ref.xp+"/1."+QString::number(ref.offset+qBound(0,pos-r.start,r.length));}}return {};}
    int position(const QString &pointer)const{QString xp=path(pointerPath(pointer));int offset=pointerOffset(pointer);for(const auto &r:runs)for(const auto &ref:r.refs)if(ref.xp==xp&&offset>=ref.offset&&offset<ref.offset+r.length)return r.start+offset-ref.offset;return -1;}
    int lineFor(int pos)const{for(int i=0;i<lines.size();++i)if(pos<lines[i].start+qMax(1,lines[i].length))return i;return qMax(0,lines.size()-1);}
    double percent(int pos,int total)const{for(const auto &r:runs)if(pos<r.start+r.length)return qBound(0.,r.index*100.0/qMax(1,total),100.);return 0.;}
    void draw(QPainter &p,int first,int height,const QPoint &origin){if(lines.isEmpty())return;first=qBound(0,first,lines.size()-1);const int end=endLine(first,height);qreal top=lines[first].top,bottom=lines[end-1].bottom;p.save();p.translate(origin);p.setClipRect(QRectF(0,0,doc.textWidth(),qMin<qreal>(height,bottom-top)));p.translate(0,-top);QAbstractTextDocumentLayout::PaintContext c;c.clip=QRectF(0,top,doc.textWidth(),bottom-top);c.palette.setColor(QPalette::Text,Qt::black);doc.documentLayout()->draw(&p,c);p.restore();}
};
}
