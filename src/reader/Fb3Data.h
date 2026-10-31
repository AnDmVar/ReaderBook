#pragma once
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QRegularExpression>
#include <cmath>

// Data-only subset of JSON5 used by FB3. Never evaluates JavaScript.
// Explicit limits prevent malformed/hostile content from exhausting the reader.
namespace Fb3 {
class Parser {
    QString s_; int p_=0, count_=0; QString error_;
    void fail(){if(error_.isEmpty())error_=QString("Некорректные данные книги (%1)").arg(p_);}
    void ws(){for(;;){while(p_<s_.size()&&s_[p_].isSpace())++p_;if(s_.mid(p_,2)=="//"){while(p_<s_.size()&&s_[p_]!='\n')++p_;}else if(s_.mid(p_,2)=="/*"){int q=s_.indexOf("*/",p_+2);if(q<0){fail();return;}p_=q+2;}else break;}}
    bool take(QChar c){ws();if(p_<s_.size()&&s_[p_]==c){++p_;return true;}return false;}
    QString str(){QString out;const QChar quote=s_[p_++];bool closed=false;while(p_<s_.size()){
        QChar c=s_[p_++];if(c==quote){closed=true;break;}if(c=='\n'||c=='\r'){fail();break;}
        if(c!='\\'){out+=c;continue;}if(p_>=s_.size()){fail();break;}c=s_[p_++];
        if(c=='n')out+='\n';else if(c=='r')out+='\r';else if(c=='t')out+='\t';else if(c=='b')out+=QChar(8);else if(c=='f')out+=QChar(12);else if(c=='v')out+=QChar(11);
        else if(c=='\n'){}else if(c=='\r'){if(p_<s_.size()&&s_[p_]=='\n')++p_;}
        else if(c=='u'||c=='x'){int n=c=='u'?4:2;bool ok=false;ushort v=s_.mid(p_,n).toUShort(&ok,16);if(!ok||p_+n>s_.size()){fail();break;}out+=QChar(v);p_+=n;}
        else if(c=='0'){if(p_<s_.size()&&s_[p_].isDigit()){fail();break;}out+=QChar(0);}
        else if(c=='\\'||c=='"'||c=='\'')out+=c;else{fail();break;}
    }if(!closed)fail();return out;}
    QJsonValue value(int depth){ws();if(!error_.isEmpty()||depth>64||++count_>250000||p_>=s_.size()){fail();return {};}
        QChar c=s_[p_];if(c=='"'||c=='\'')return str();
        if(c=='['){++p_;QJsonArray a;if(take(']'))return a;do{a.append(value(depth+1));if(take(']'))return a;if(!take(',')){fail();return {};}if(take(']'))return a;}while(error_.isEmpty());return {};}
        if(c=='{'){++p_;QJsonObject o;if(take('}'))return o;do{ws();QString k;if(p_<s_.size()&&(s_[p_]=='"'||s_[p_]=='\''))k=str();else{int start=p_;while(p_<s_.size()&&(s_[p_].isLetterOrNumber()||s_[p_]=='_'||s_[p_]=='$'||s_[p_]=='-'))++p_;k=s_.mid(start,p_-start);}if(k.isEmpty()||!take(':')||o.contains(k)){fail();return {};}o[k]=value(depth+1);if(take('}'))return o;if(!take(',')){fail();return {};}if(take('}'))return o;}while(error_.isEmpty());return {};}
        int start=p_;while(p_<s_.size()&&!s_[p_].isSpace()&&s_[p_]!=','&&s_[p_]!=']'&&s_[p_]!='}')++p_;QString token=s_.mid(start,p_-start);
        if(token=="null")return QJsonValue(QJsonValue::Null);if(token=="true")return true;if(token=="false")return false;
        static const QRegularExpression number("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$");bool ok=false;double n=token.toDouble(&ok);if(!number.match(token).hasMatch()||!ok||!std::isfinite(n)){fail();return {};}return n;
    }
public:
    QJsonValue parse(const QString &s,QString *error){s_=s;p_=0;count_=0;error_.clear();if(s.size()>4*1024*1024){if(error)*error="Слишком большая часть книги";return {};}
        auto v=value(0);ws();if(p_!=s_.size())fail();if(error)*error=error_;return error_.isEmpty()?v:QJsonValue();}
};
inline QString path(const QJsonArray &xp){QString out;for(const auto &v:xp){if(!v.isDouble()||v.toInt()<0||v.toDouble()!=v.toInt())return {};out+="/"+QString::number(v.toInt());}return out;}
inline QJsonArray pointerPath(QString xp){int i=xp.lastIndexOf("/1.");if(i<0)return {};xp=xp.left(i);QJsonArray a;for(const auto &n:xp.split('/',QString::SkipEmptyParts)){bool ok=false;int v=n.toInt(&ok);if(!ok||v<0)return {};a.append(v);}return a;}
inline int pointerOffset(const QString &xp){bool ok=false;int n=xp.mid(xp.lastIndexOf('.')+1).toInt(&ok);return ok?qMax(0,n):0;}
inline int compare(const QJsonArray &a,const QJsonArray &b){for(int i=0;i<qMax(a.size(),b.size());++i){int x=i<a.size()?a[i].toInt():0,y=i<b.size()?b[i].toInt():0;if(x!=y)return x<y?-1:1;}return 0;}
inline bool wrapper(const QJsonObject &o){QString t=o["t"].toString();return t=="div"?!o.contains("fl")&&!o.contains("op"):QStringList({"section","article","aside","header","footer","nav","main","figure","ul","ol","dl","table","thead","tbody","tfoot","tr"}).contains(t);}
}
