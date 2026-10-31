#pragma once
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>
namespace Fb3 {
// State machine is independent of transport: a server change blocks writes,
// including a same-version/different-pointer response. No blind 409 retries.
class Progress {
public:
    QJsonObject base,local,attempt;bool pending=false,conflict=false;int generation=0,attemptGeneration=0;
    void change(const QString &xp,double percent){if(xp.isEmpty())return;local["xpointer"]=xp;local["percent"]=percent;pending=base["xpointer"]!=local["xpointer"]||base["percent"]!=local["percent"];++generation;}
    bool reconcile(const QJsonObject &remote){
        if(!attempt.isEmpty()&&remote["xpointer"]==attempt["xpointer"]&&remote["percent"]==attempt["percent"]){ack(remote);return true;}
        if(pending&&(remote["version"].toInt(-1)!=base["version"].toInt(-1)||remote["xpointer"]!=base["xpointer"])){conflict=true;return false;}
        base=remote;if(!pending)local=remote;conflict=false;return true;
    }
    QJsonObject begin(){attempt=local;attemptGeneration=generation;attempt["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);attempt["created_at"]=QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss");attempt["version"]=base["version"].toInt(-1)+1;attempt["is_epub"]=false;attempt["device_name"]="web";return attempt;}
    void ack(const QJsonObject &remote){base=remote;if(attemptGeneration==generation){local=remote;pending=false;}conflict=false;attempt=QJsonObject();}
    void chooseRemote(const QJsonObject &remote){base=local=remote;attempt=QJsonObject();pending=conflict=false;++generation;}
    void chooseLocal(const QJsonObject &remote){base=remote;attempt=QJsonObject();conflict=false;pending=true;}
    QJsonObject json()const{QJsonObject o;o["base"]=base;o["local"]=local;o["pending"]=pending;return o;}
    void restore(const QJsonObject &o){base=o["base"].toObject();local=o["local"].toObject();pending=o["pending"].toBool();conflict=false;attempt=QJsonObject();++generation;}
};
}
