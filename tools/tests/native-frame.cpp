#include <QApplication>
#include "stubs/inkview.h"
#include "../../src/NativeFrame.h"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){QApplication app(argc,argv);SetOrientation(1);icanvas selected;SetCanvas(&selected);QImage page(1024,684,QImage::Format_RGB32);page.fill(Qt::white);page.setPixel(1023,683,qRgb(0,0,0));assert(NativeFrame::present(page,QPoint(0,0)));auto&s=TestDisplay();assert(GetCanvas()==&selected&&s.defaultCanvas);assert(s.draws==1&&s.updates==1&&s.w==1024&&s.h==684&&s.y==0&&s.last==0);assert(s.clip[2]==758&&s.clip[3]==1024);assert(!NativeFrame::present(page,QPoint(1,75)));assert(s.draws==1);std::cout<<"PASS full landscape bitmap including rightmost pixel, SDK canvas origin without duplicate panel offset, restored clip, rejected out-of-bounds frame\n";}
