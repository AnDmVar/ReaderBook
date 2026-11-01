#pragma once
// Test-only desktop stubs. Never included by the ARM application build.
#define NET_CONNECTED 1
static inline int QueryNetwork(){return NET_CONNECTED;}
static inline int NetConnect(const char*){return 0;}
static inline int PanelHeight(){return 74;}
static inline void PostponeTimedPoweroff(){}
#define BROWSER_FOR_AUTH "/ebrmain/bin/browser.app"
static inline int OpenBook(const char*,const char*,int){return 1;}

static inline int &TestOrientation(){static int value=0;return value;}
static inline void SetOrientation(int n){TestOrientation()=n;}
static inline int GetOrientation(){return TestOrientation();}
static inline int ScreenWidth(){return TestOrientation()==1||TestOrientation()==2?1024:758;}
static inline int ScreenHeight(){return TestOrientation()==1||TestOrientation()==2?758:1024;}

// Recording display stubs for native landscape output, never used on device.
typedef struct ibitmap_s {unsigned short width,height,depth,scanline;unsigned char data[];} ibitmap;
struct icanvas {int width=1024,height=758;};
static inline icanvas*&TestCanvas(){static icanvas *c=nullptr;return c;}
static inline icanvas&TestDefaultCanvas(){static icanvas c;return c;}
static inline icanvas*GetCanvas(){return TestCanvas()?TestCanvas():&TestDefaultCanvas();}
static inline void SetCanvas(icanvas*c){TestCanvas()=c;}
struct TestDisplayState {int clip[4]={0,0,758,1024};int draws=0,updates=0,x=0,y=0,w=0,h=0,last=-1;bool defaultCanvas=false;};
static inline TestDisplayState &TestDisplay(){static TestDisplayState value;return value;}
static inline void GetClip(int*x,int*y,int*w,int*h){auto &s=TestDisplay();*x=s.clip[0];*y=s.clip[1];*w=s.clip[2];*h=s.clip[3];}
static inline void SetClip(int x,int y,int w,int h){auto&s=TestDisplay();s.clip[0]=x;s.clip[1]=y;s.clip[2]=w;s.clip[3]=h;}
static inline void DrawBitmap(int x,int y,const ibitmap*b){auto&s=TestDisplay();++s.draws;s.defaultCanvas=GetCanvas()==&TestDefaultCanvas();s.x=x;s.y=y;s.w=b->width;s.h=b->height;s.last=b->data[(b->height-1)*b->scanline+b->width-1];}
static inline void PartialUpdate(int,int,int,int){++TestDisplay().updates;}
