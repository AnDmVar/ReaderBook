#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDateTime>
namespace ReaderHistory {
inline bool numeric(const QString &s){return QRegularExpression("^[0-9]{1,20}$").match(s).hasMatch()&&s!="0";}
inline QString path(const QString &root,const QString &user){return root+"/native-reader/"+user+"/last-book.json";}
inline QJsonObject read(const QString &root,const QString &user){if(!numeric(user))return {};QFile f(path(root,user));if(!f.open(QIODevice::ReadOnly)||f.size()>8192)return {};auto o=QJsonDocument::fromJson(f.readAll()).object();if(o["user"].toString()!=user||!numeric(o["art"].toString())||o["title"].toString().trimmed().isEmpty()||o["title"].toString().size()>400)return {};return o;}
inline bool remember(const QString &root,const QString &user,const QString &art,const QString &title){if(!numeric(user)||!numeric(art)||title.trimmed().isEmpty()||title.size()>400)return false;QDir().mkpath(QFileInfo(path(root,user)).absolutePath());QJsonObject o;o["user"]=user;o["art"]=art;o["title"]=title;o["opened_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);QSaveFile f(path(root,user));if(!f.open(QIODevice::WriteOnly))return false;f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);auto bytes=QJsonDocument(o).toJson(QJsonDocument::Compact);return f.write(bytes)==bytes.size()&&f.commit();}
}
