#pragma once
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
namespace Fb3 {
inline bool imageName(const QString &s){if(s.size()>200||!QRegularExpression("^[A-Za-z0-9_./-]+\\.(?:jpg|jpeg|png|gif|webp)$",QRegularExpression::CaseInsensitiveOption).match(s).hasMatch())return false;for(const auto &p:s.split('/'))if(p.isEmpty()||p=="."||p=="..")return false;return true;}
inline QImage decodeImage(const QByteArray &bytes){if(bytes.isEmpty()||bytes.size()>8*1024*1024)return {};QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader r(&buffer);r.setAutoTransform(true);QSize size=r.size();if(!size.isValid()||qint64(size.width())*size.height()>24000000)return {};if(size.width()>758||size.height()>1024)r.setScaledSize(size.scaled(QSize(758,1024),Qt::KeepAspectRatio));return r.read();}
}
