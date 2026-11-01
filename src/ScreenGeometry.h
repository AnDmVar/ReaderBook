#pragma once
#include <QGuiApplication>
#include <QScreen>
#include <QLibrary>
#include <QRect>
#include <QEventLoop>
#include <QThread>
// This app is pinned to the firmware Qt 5.12 ABI. InkView rotates its screen,
// but pocketbook2 does not notify Qt's cached QScreen geometry. Notify through
// the exported QPA event API; resizing only the window leaves the old clip.
namespace ScreenGeometry {
inline bool synchronize(const QSize &size,int orientation){
    QScreen *screen=QGuiApplication::primaryScreen();if(!QGuiApplication::instance()||QThread::currentThread()!=QGuiApplication::instance()->thread()||!screen||size.width()<200||size.height()<200||size.width()>4000||size.height()>4000)return false;
    using Geometry=void(*)(QScreen*,const QRect&,const QRect&);
    using Orientation=void(*)(QScreen*,Qt::ScreenOrientation);
    using Flush=bool(*)(QEventLoop::ProcessEventsFlags);
    static Geometry geometry=reinterpret_cast<Geometry>(QLibrary::resolve("Qt5Gui",5,"_ZN22QWindowSystemInterface26handleScreenGeometryChangeEP7QScreenRK5QRectS4_"));
    static Orientation rotate=reinterpret_cast<Orientation>(QLibrary::resolve("Qt5Gui",5,"_ZN22QWindowSystemInterface29handleScreenOrientationChangeEP7QScreenN2Qt17ScreenOrientationE"));
    static Flush flush=reinterpret_cast<Flush>(QLibrary::resolve("Qt5Gui",5,"_ZN22QWindowSystemInterface23flushWindowSystemEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE"));
    if(!geometry||!flush)return false;QRect bounds(QPoint(),size);if(screen->geometry()!=bounds||screen->availableGeometry()!=bounds)geometry(screen,bounds,bounds);
    if(rotate){const Qt::ScreenOrientation orientations[]={Qt::PortraitOrientation,Qt::LandscapeOrientation,Qt::InvertedLandscapeOrientation,Qt::InvertedPortraitOrientation};if(orientation>=0&&orientation<4)rotate(screen,orientations[orientation]);}
    // pocketbook2's event dispatcher does not necessarily drain queued QPA
    // notifications. Apply before resizing/repainting; do not pump user input.
    flush(QEventLoop::ExcludeUserInputEvents);
    return screen->geometry()==bounds&&screen->availableGeometry()==bounds;
}
}
