#include "../../src/ScreenGeometry.h"
#include "../../src/LauncherAssets.h"
#include "../../src/reader/OfflineLibrary.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QTest>
#include <QScrollBar>
#include <cassert>
#include <iostream>
int main(int argc,char **argv){QApplication app(argc,argv);auto *s=QGuiApplication::primaryScreen();QRect original=s->geometry(),available=s->availableGeometry();assert(ScreenGeometry::synchronize(QSize(758,1024),0));QTest::qWait(50);assert(s->geometry().size()==QSize(758,1024));assert(ScreenGeometry::synchronize(QSize(1024,758),1));QTest::qWait(50);assert(s->geometry().size()==QSize(1024,758));assert(s->availableGeometry().size()==QSize(1024,758));assert(ScreenGeometry::synchronize(original.size(),0));QTest::qWait(50);
 auto bmp=LauncherAssets::bitmap();assert(bmp.left(2)=="BM");assert(qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bmp.constData()+28))==24);assert(QImage::fromData(bmp,"BMP").size()==QSize(120,120));QTemporaryDir temp;QString root=temp.path(),exe=root+"/applications/litres.app";assert(LauncherAssets::atomic(exe,"fixture"));QJsonObject config{{"version","4"},{"view",QJsonObject{{"groups",QJsonArray{"keep"}}}}};assert(LauncherAssets::atomic(root+"/system/config/desktop/view.json",QJsonDocument(config).toJson()));assert(LauncherAssets::install(root,exe));QFile f(root+"/system/config/desktop/view.json");assert(f.open(QIODevice::ReadOnly));auto out=QJsonDocument::fromJson(f.readAll()).object();assert(out["view"]==config["view"]);auto own=out["applications"].toObject()["U_litres"].toObject();assert(own["icon"].isObject());assert(own["icon"].toObject()["path"]=="applications/icons/readerbook.bmp");assert(own["path"]=="applications/litres.app");
 Fb3::OfflineBook book;QJsonArray parts{QJsonObject{{"s",0},{"e",1},{"url","000.js"}}};assert(book.prepare(root,"42","123","Книга","https://www.litres.ru/download_book/123/456/",parts));assert(book.savePart(0,QJsonArray{QJsonObject{{"t","p"},{"c",QJsonArray{"text"}}}}));
 for(int mode=0;mode<3;++mode){OfflineLibrary dialog(root,"42");dialog.show();dialog.activateWindow();dialog.raise();QTest::qWait(60);auto *list=dialog.findChild<QListWidget *>();assert(list&&list->count()==1);list->setFocus();if(mode<2){QTest::keyClick(list,Qt::Key_Down);assert(dialog.focusWidget()!=list);QTest::keyClick(dialog.focusWidget(),Qt::Key_Down);if(mode==1)QTest::keyClick(dialog.focusWidget(),Qt::Key_Down);QTest::keyClick(dialog.focusWidget(),Qt::Key_Enter);assert(dialog.result()==(mode==0?OfflineLibrary::Website:OfflineLibrary::Home));}else{QTest::keyClick(list,Qt::Key_PageUp);assert(dialog.result()==OfflineLibrary::Website);}}
 std::cout<<"PASS QScreen geometry/orientation events update stale portrait bounds; native menu uses file-path RGB24 icon; saved-book buttons work with arrows and physical Enter, Back and Home\n";
}
