#pragma once
#include "LauncherIcon.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QImage>
#include <QtEndian>
namespace LauncherAssets {
inline QByteArray bitmap(){QImage image=QImage::fromData(pbLauncherIcon,sizeof(pbLauncherIcon),"PNG").convertToFormat(QImage::Format_RGB32);if(image.isNull())return {};int w=image.width(),h=image.height(),stride=(w*3+3)&~3;QByteArray out(54+stride*h,'\0');out[0]='B';out[1]='M';auto u16=[&out](int p,quint16 v){qToLittleEndian<quint16>(v,reinterpret_cast<uchar *>(out.data()+p));};auto u32=[&out](int p,quint32 v){qToLittleEndian<quint32>(v,reinterpret_cast<uchar *>(out.data()+p));};u32(2,out.size());u32(10,54);u32(14,40);u32(18,w);u32(22,h);u16(26,1);u16(28,24);u32(34,stride*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x){QRgb c=image.pixel(x,h-1-y);int pos=54+y*stride+x*3;out[pos]=char(qBlue(c));out[pos+1]=char(qGreen(c));out[pos+2]=char(qRed(c));}return out;}

inline bool atomic(const QString &name,const QByteArray &data){QDir().mkpath(QFileInfo(name).absolutePath());if(QFileInfo(name).isSymLink())return false;QFile old(name);if(old.open(QIODevice::ReadOnly)&&old.readAll()==data)return true;old.close();QSaveFile f(name);if(!f.open(QIODevice::WriteOnly))return false;return f.write(data)==data.size()&&f.commit();}
inline bool removeMacDuplicate(const QString &application){QFileInfo i(QFileInfo(application).dir().filePath("._"+QFileInfo(application).fileName()));if(!i.exists())return true;if(i.isSymLink()||!i.isFile()||i.size()<26||i.size()>1024*1024)return false;QFile f(i.filePath());if(!f.open(QIODevice::ReadOnly))return false;auto magic=f.read(8);f.close();return magic==QByteArray::fromHex("0005160700020000")&&QFile::remove(i.filePath());}
inline bool install(const QString &root,const QString &application){if(QFileInfo(application).absoluteFilePath()!=QDir(root).filePath("applications/litres.app"))return false;
 const bool cleaned=removeMacDuplicate(application);
 const QString iconPath=root+"/applications/icons/readerbook.bmp",path=root+"/system/config/desktop/view.json";QJsonObject config;QFile f(path);QByteArray original;if(f.exists()){if(QFileInfo(path).isSymLink()||!f.open(QIODevice::ReadOnly)||f.size()>1024*1024)return false;original=f.readAll();QJsonParseError error;auto d=QJsonDocument::fromJson(original,&error);if(error.error!=QJsonParseError::NoError||!d.isObject())return false;config=d.object();f.close();}else config["version"]="5";
 auto data=bitmap();if(data.isEmpty()||!atomic(iconPath,data))return false;
 if(config.contains("applications")&&!config["applications"].isObject())return false;auto apps=config["applications"].toObject();auto own=apps["U_litres"].toObject();own["path"]=QDir(root).relativeFilePath(application);own["title"]="Литрес";own["icon"]=QJsonObject{{"path","applications/icons/readerbook.bmp"}};own["focused_icon"]=QJsonObject{{"path","applications/icons/readerbook.bmp"}};apps["U_litres"]=own;config["applications"]=apps;
 auto bytes=QJsonDocument(config).toJson(QJsonDocument::Indented);if(!original.isEmpty()&&!QFileInfo::exists(path+".readerbook-backup")&&!atomic(path+".readerbook-backup",original))return false;if(bytes!=original&&!atomic(path,bytes))return false;return cleaned;
}
}
