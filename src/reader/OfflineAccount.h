#pragma once
#include "ReaderHistory.h"
namespace OfflineAccount {
inline QString active(const QString &root){QFile f(root+"/native-reader/active-account.json");if(!f.open(QIODevice::ReadOnly)||f.size()>256)return {};auto id=QJsonDocument::fromJson(f.readAll()).object()["user"].toString();return ReaderHistory::numeric(id)?id:QString();}
inline bool remember(const QString &root,const QString &user){if(!ReaderHistory::numeric(user))return false;QDir().mkpath(root+"/native-reader");QSaveFile f(root+"/native-reader/active-account.json");if(!f.open(QIODevice::WriteOnly))return false;f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);auto bytes=QJsonDocument(QJsonObject{{"user",user}}).toJson(QJsonDocument::Compact);return f.write(bytes)==bytes.size()&&f.commit();}
}
