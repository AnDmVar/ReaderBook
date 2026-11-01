#pragma once
#include <QImage>
#include <QPoint>
#include <QByteArray>
#include <cstring>
// InkView is initialized and owned by pocketbook2; never initialize it again.
namespace NativeFrame {
inline bool present(const QImage &frame,const QPoint &origin,QSize *canvasSize=nullptr){
 const int sw=ScreenWidth(),sh=ScreenHeight();if(frame.isNull()||frame.width()>sw||frame.height()>sh||origin.x()<0||origin.y()<0||origin.x()+frame.width()>sw||origin.y()+frame.height()>sh)return false;
 QImage gray=frame.convertToFormat(QImage::Format_Grayscale8);int stride=gray.bytesPerLine();QByteArray bytes(int(sizeof(ibitmap))+stride*gray.height(),'\0');auto *bitmap=reinterpret_cast<ibitmap *>(bytes.data());bitmap->width=gray.width();bitmap->height=gray.height();bitmap->depth=8;bitmap->scanline=stride;std::memcpy(bitmap->data,gray.constBits(),size_t(stride*gray.height()));
 icanvas *previous=GetCanvas();SetCanvas(nullptr);icanvas *screen=GetCanvas();
 if(canvasSize)*canvasSize=screen?QSize(screen->width,screen->height):QSize();
 if(!screen||origin.x()+frame.width()>screen->width||origin.y()+frame.height()>screen->height){SetCanvas(previous);return false;}
 int x,y,w,h;GetClip(&x,&y,&w,&h);SetClip(0,0,screen->width,screen->height);DrawBitmap(origin.x(),origin.y(),bitmap);PartialUpdate(origin.x(),origin.y(),frame.width(),frame.height());SetClip(x,y,w,h);SetCanvas(previous);return true;
}
}
