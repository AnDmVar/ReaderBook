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
