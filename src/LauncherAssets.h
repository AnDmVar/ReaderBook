#pragma once
#include "LauncherIcon.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
namespace LauncherAssets {
inline bool atomic(const QString &name,const QByteArray &data){QDir().mkpath(QFileInfo(name).absolutePath());if(QFileInfo(name).isSymLink())return false;QFile old(name);if(old.open(QIODevice::ReadOnly)&&old.readAll()==data)return true;old.close();QSaveFile f(name);if(!f.open(QIODevice::WriteOnly))return false;return f.write(data)==data.size()&&f.commit();}
inline bool removeMacDuplicate(const QString &application){QFileInfo i(QFileInfo(application).dir().filePath("._"+QFileInfo(application).fileName()));if(!i.exists())return true;if(i.isSymLink()||!i.isFile()||i.size()<26||i.size()>1024*1024)return false;QFile f(i.filePath());if(!f.open(QIODevice::ReadOnly))return false;auto magic=f.read(8);f.close();return magic==QByteArray::fromHex("0005160700020000")&&QFile::remove(i.filePath());}
inline bool install(const QString &root,const QString &application){if(QFileInfo(application).absoluteFilePath()!=QDir(root).filePath("applications/litres.app"))return false;
 const bool cleaned=removeMacDuplicate(application);
 const QString bitmap=root+"/applications/icons/readerbook.bmp",path=root+"/system/config/desktop/view.json";QJsonObject config;QFile f(path);QByteArray original;if(f.exists()){if(QFileInfo(path).isSymLink()||!f.open(QIODevice::ReadOnly)||f.size()>1024*1024)return false;original=f.readAll();QJsonParseError error;auto d=QJsonDocument::fromJson(original,&error);if(error.error!=QJsonParseError::NoError||!d.isObject())return false;config=d.object();f.close();}else config["version"]="5";
 if(!atomic(bitmap,QByteArray(reinterpret_cast<const char *>(pbLauncherIcon),sizeof(pbLauncherIcon))))return false;
 if(config.contains("applications")&&!config["applications"].isObject())return false;auto apps=config["applications"].toObject();auto own=apps["U_litres"].toObject();own["path"]=application;own["title"]="Литрес";own["icon"]=bitmap;own["focused_icon"]=bitmap;apps["U_litres"]=own;config["applications"]=apps;
 auto bytes=QJsonDocument(config).toJson(QJsonDocument::Indented);if(!original.isEmpty()&&!QFileInfo::exists(path+".readerbook-backup")&&!atomic(path+".readerbook-backup",original))return false;if(bytes!=original&&!atomic(path,bytes))return false;return cleaned;
}
}
