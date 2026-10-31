#pragma once
#include "Fb3Data.h"
#include "ReaderImages.h"
#include "ReaderHistory.h"
#include <QCryptographicHash>
namespace Fb3 {
class OfflineBook {
    QString dir_;QJsonObject manifest_;
    static bool write(const QString &name,const QByteArray &bytes){QSaveFile f(name);if(!f.open(QIODevice::WriteOnly))return false;f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);return f.write(bytes)==bytes.size()&&f.commit();}
    static QJsonObject object(const QString &p,int limit){QFile f(p);if(!f.open(QIODevice::ReadOnly)||f.size()>limit)return {};return QJsonDocument::fromJson(f.readAll()).object();}
    QString partPath(int n)const{return dir_+"/part-"+QString::number(n)+".json";}
    QString imagePath(const QString &n)const{return dir_+"/image-"+QString::fromLatin1(QCryptographicHash::hash(n.toUtf8(),QCryptographicHash::Sha256).toHex())+".png";}
public:
    bool open(const QString &root,const QString &user,const QString &art){if(!ReaderHistory::numeric(user)||!ReaderHistory::numeric(art))return false;dir_=root+"/native-reader/"+user+"/books/"+art;manifest_=object(dir_+"/book.json",262144);return manifest_["user"]==user&&manifest_["art"]==art&&!manifest_["parts"].toArray().isEmpty()&&manifest_["allowed"].toBool();}
    bool prepare(const QString &root,const QString &user,const QString &art,const QString &title,const QString &base,const QJsonArray &parts){if(!ReaderHistory::numeric(user)||!ReaderHistory::numeric(art)||parts.isEmpty()||parts.size()>10000)return false;QString d=root+"/native-reader/"+user+"/books/"+art;auto old=object(d+"/book.json",262144);
        QString revision=QString::fromLatin1(QCryptographicHash::hash((base+QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact))).toUtf8(),QCryptographicHash::Sha256).toHex());dir_=d;
        // A different edition gets an empty cache: never mix fragments/anchors.
        if(!old.isEmpty()&&old["revision"]!=revision){QString archive=dir_+"-old-"+old["revision"].toString().left(12)+"-"+QString::number(QDateTime::currentMSecsSinceEpoch());if(!QDir().rename(dir_,archive))return false;}
        if(!QDir().mkpath(dir_))return false;manifest_=QJsonObject{{"user",user},{"art",art},{"title",title},{"base",base},{"parts",parts},{"revision",revision},{"allowed",true},{"complete",old["revision"]==revision&&old["complete"].toBool()},{"saved_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};return write(dir_+"/book.json",QJsonDocument(manifest_).toJson(QJsonDocument::Compact));}
    void revoke(){if(dir_.isEmpty())return;manifest_["allowed"]=false;write(dir_+"/book.json",QJsonDocument(manifest_).toJson(QJsonDocument::Compact));}
    QJsonObject metadata()const{return manifest_;}
    bool valid()const{return !dir_.isEmpty()&&manifest_["allowed"].toBool();}
    QJsonArray part(int n)const{if(!valid()||n<0||n>=manifest_["parts"].toArray().size())return {};QFile f(partPath(n));if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)return {};return QJsonDocument::fromJson(f.readAll()).array();}
    bool savePart(int n,const QJsonArray &data){return valid()&&n>=0&&n<manifest_["parts"].toArray().size()&&!data.isEmpty()&&write(partPath(n),QJsonDocument(data).toJson(QJsonDocument::Compact));}
    QImage image(const QString &name)const{if(!valid()||!imageName(name))return {};QFile f(imagePath(name));if(!f.open(QIODevice::ReadOnly)||f.size()>8*1024*1024)return {};return decodeImage(f.readAll());}
    bool saveImage(const QString &name,const QByteArray &png){return valid()&&imageName(name)&&!decodeImage(png).isNull()&&write(imagePath(name),png);}
    bool finish(){if(!valid()||savedParts()!=manifest_["parts"].toArray().size())return false;manifest_["complete"]=true;return write(dir_+"/book.json",QJsonDocument(manifest_).toJson(QJsonDocument::Compact));}
    int savedParts()const{int n=0;for(int i=0;i<manifest_["parts"].toArray().size();++i)if(QFileInfo(partPath(i)).size()>2)++n;return n;}
    bool hasImage(const QString &name)const{return valid()&&imageName(name)&&QFileInfo(imagePath(name)).size()>0;}
    static QSet<QString> images(const QJsonArray &nodes){QSet<QString> out;for(const auto &v:nodes)if(v.isObject()){auto o=v.toObject();if(o["t"]=="img"&&imageName(o["s"].toString()))out.insert(o["s"].toString());out.unite(images(o["c"].toArray()));}return out;}
};
}
