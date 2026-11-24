#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stddef.h>
#include <limits.h>

#define API(name,address) ((__typeof__(&name))(*(void **)(address##ULL)))
#define Send API(SendMessageW,0x140256F28)
#define Module API(GetModuleHandleW,0x140256AC0)
#define Proc API(GetProcAddress,0x140256A78)
#define Client API(GetClientRect,0x140257200)
#define Delete API(DeleteObject,0x140256380)
#define Select API(SelectObject,0x1402563A8)
#define SUBCLASS_ID ((UINT_PTR)0x47535343)
#define FRAME_MESSAGE (WM_APP+0x57F)
#define DURATION_MS 170
#define MAX_CACHE_ROWS 128
#define HEADER_REGION_SHIELD 1

typedef LRESULT (WINAPI *DEF_SUBCLASS)(HWND,UINT,WPARAM,LPARAM);
typedef BOOL (WINAPI *ADD_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR,DWORD_PTR);
typedef BOOL (WINAPI *REMOVE_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR);
typedef BOOL (WINAPI *QUEUE_CREATE)(PHANDLE,HANDLE,WAITORTIMERCALLBACK,PVOID,DWORD,DWORD,ULONG);
typedef BOOL (WINAPI *QUEUE_DELETE)(HANDLE,HANDLE,HANDLE);
typedef BOOL (WINAPI *SCROLL_INFO)(HWND,int,LPSCROLLINFO);
typedef BOOL (WINAPI *GDI_FLUSH)(void);
typedef HANDLE (WINAPI *BUFFER_BEGIN)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *BUFFER_ALPHA)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *BUFFER_END)(HANDLE,BOOL);
typedef UINT (WINAPI *TIME_PERIOD)(UINT);
typedef BOOL (WINAPI *DISPLAY_MODE)(LPCWSTR,DWORD,DEVMODEW*);
typedef BOOL (WINAPI *COUNTER_FREQUENCY)(LARGE_INTEGER*);

#define performance_frequency (*(LONGLONG *)0x140412CE0ULL)
static ULONGLONG precise_now(void) {
    if(!performance_frequency) {
        COUNTER_FREQUENCY frequency=(COUNTER_FREQUENCY)Proc(Module(L"kernel32.dll"),"QueryPerformanceFrequency");
        LARGE_INTEGER value;
        if(frequency&&frequency(&value)&&value.QuadPart>0)performance_frequency=value.QuadPart;
    }
    LARGE_INTEGER tick;
    if(performance_frequency&&API(QueryPerformanceCounter,0x1402564E8)(&tick))
        return (ULONGLONG)(tick.QuadPart*1000/performance_frequency);
    return API(GetTickCount64,0x140256A80)();
}
#define NOW precise_now

typedef struct SCROLL_STATE {
    void *self;
    HWND list;
    struct SCROLL_STATE *next;
    HANDLE queue;
    volatile LONG pending;
    DWORD generation;
    int row_height,body_top,body_height,width,count,capacity;
    double visual,target,from;
    ULONGLONG start;
    BOOL active,busy,internal,ready;
    int native_top,frames;
    HDC cache;
    HBITMAP bitmap;
    HGDIOBJ previous;
    DWORD *pixels;
    int cache_id[MAX_CACHE_ROWS];
    int remainder;
    int last_direction;
    BOOL precise;
    int interval;
    TIME_PERIOD end_period;
    double presented;
    ULONGLONG presented_at;
    DWORD suppressed_native_rows;
    DWORD native_commits;
    UINT last_stop_message;
    BOOL external_redraw_disabled;
    HWND header;
    DWORD shielded_commits,suppressed_header_redraw;
} SCROLL_STATE;
_Static_assert(offsetof(SCROLL_STATE,visual)==64,"scroll diagnostic layout changed");
_Static_assert(offsetof(SCROLL_STATE,frames)==116,"scroll diagnostic frame offset changed");
#define root (*(SCROLL_STATE **)0x140412C18ULL)
static LRESULT CALLBACK scroll_header_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

static DEF_SUBCLASS next_proc(void) {return (DEF_SUBCLASS)(*(void**)0x1402560B8ULL);}
static SCROLL_STATE *find(HWND list) {
    for(SCROLL_STATE *s=root;s;s=s->next)if(s->list==list)return s;
    return 0;
}
static void invalidate_cache(SCROLL_STATE *s) {
    for(int i=0;i<MAX_CACHE_ROWS;++i)s->cache_id[i]=-1;
}
static void free_cache(SCROLL_STATE *s) {
    if(s->cache&&s->previous)Select(s->cache,s->previous);
    if(s->bitmap)Delete(s->bitmap);
    if(s->cache)API(DeleteDC,0x140256330)(s->cache);
    s->cache=0;s->bitmap=0;s->previous=0;s->pixels=0;s->ready=FALSE;
    invalidate_cache(s);
}
static int maximum(SCROLL_STATE *s) {
    SCROLLINFO info={sizeof(info),SIF_RANGE|SIF_PAGE};
    SCROLL_INFO get=(SCROLL_INFO)Proc(Module(L"user32.dll"),"GetScrollInfo");
    if(get&&get(s->list,SB_VERT,&info)) {
        int last=info.nMax-(int)info.nPage+1;
        return last>0?last*s->row_height:0;
    }
    int page=s->body_height/s->row_height;
    return s->count>page?(s->count-page)*s->row_height:0;
}
static int actual(SCROLL_STATE *s) {
    int top=(int)Send(s->list,LVM_GETTOPINDEX,0,0);
    if(top<0)top=0;s->native_top=top;
    return top*s->row_height;
}
/* Suppress only native row pixel copies. Unlike hiding the whole ListView,
   the temporary region retains the header and the scrollbars throughout. */
static BOOL restrict_body(SCROLL_STATE *s,HRGN *previous) {
    RECT window,client;POINT origin={0};
    if(!API(GetWindowRect,0x140257208)(s->list,&window)||!Client(s->list,&client)||
       !API(ClientToScreen,0x1402570C8)(s->list,&origin))return FALSE;
    HRGN original=API(CreateRectRgn,0x1402562A0)(0,0,0,0);
    if(!original)return FALSE;
    int kind=API(GetWindowRgn,0x140257138)(s->list,original);
    HRGN restricted=API(CreateRectRgn,0x1402562A0)(0,0,window.right-window.left,window.bottom-window.top);
    HRGN body=API(CreateRectRgn,0x1402562A0)(origin.x-window.left,origin.y-window.top+s->body_top,
        origin.x-window.left+client.right,origin.y-window.top+client.bottom);
    if(!restricted||!body){if(restricted)Delete(restricted);if(body)Delete(body);Delete(original);return FALSE;}
    if(kind!=ERROR)API(CombineRgn,0x140256290)(restricted,original,0,RGN_COPY);
    BOOL okay=API(CombineRgn,0x140256290)(restricted,restricted,body,RGN_DIFF)!=ERROR &&
              API(SetWindowRgn,0x140257158)(s->list,restricted,FALSE);
    Delete(body);
    if(!okay){Delete(restricted);Delete(original);return FALSE;}
    if(kind==ERROR){Delete(original);original=0;}
    *previous=original;return TRUE;
}
static void sync_native(SCROLL_STATE *s,int y) {
    int top=s->row_height>0?y/s->row_height:0;
    int current=(int)Send(s->list,LVM_GETTOPINDEX,0,0);
    if(current<0)current=0;
    if(top!=current) {
        s->internal=TRUE;
        /* The HWND still contains our fractional frame. Native LVM_SCROLL
           would otherwise scroll-copy that frame a second time before the
           next custom paint, visibly moving text forward then backward. */
        BOOL redraw=!s->external_redraw_disabled;
        HRGN previous=0;BOOL restricted=FALSE;
        if(redraw) {
            if(HEADER_REGION_SHIELD)restricted=restrict_body(s,&previous);
            /* Codejock consumes the notification without the standard
               WS_VISIBLE clipping change. Apply that part explicitly so
               native SmoothScrollWindow cannot copy any live pixels. */
            if(!restricted) {
                Send(s->list,WM_SETREDRAW,FALSE,0);
                API(DefWindowProcW,0x1402573A0)(s->list,WM_SETREDRAW,FALSE,0);
            }
        }
        Send(s->list,LVM_SCROLL,0,(LPARAM)((top-current)*s->row_height));
        if(redraw) {
            if(restricted) {
                if(!API(SetWindowRgn,0x140257158)(s->list,previous,FALSE)&&previous)Delete(previous);
                ++s->shielded_commits;
            } else {
                API(DefWindowProcW,0x1402573A0)(s->list,WM_SETREDRAW,TRUE,0);
                Send(s->list,WM_SETREDRAW,TRUE,0);
            }
        }
        s->internal=FALSE;
        ++s->native_commits;
    }
    s->native_top=(int)Send(s->list,LVM_GETTOPINDEX,0,0);
}
static void stop(SCROLL_STATE *s,BOOL settle) {
    if(s->queue) {
        QUEUE_DELETE remove=(QUEUE_DELETE)Proc(Module(L"kernel32.dll"),"DeleteTimerQueueTimer");
        if(remove)remove(0,s->queue,INVALID_HANDLE_VALUE);
        s->queue=0;
    }
    if(s->precise&&s->end_period)s->end_period(1);
    s->precise=FALSE;
    InterlockedExchange(&s->pending,0);++s->generation;
    BOOL animated=s->active;
    if(animated&&settle&&s->row_height>0) {
        int y=((int)(s->visual+s->row_height/2)/s->row_height)*s->row_height;
        sync_native(s,y);s->visual=actual(s);s->target=s->visual;
    }
    s->active=FALSE;
    if(animated)API(InvalidateRect,0x1402572A0)(s->list,0,FALSE);
}
static BOOL measure(SCROLL_STATE *s) {
    RECT client={0},first={LVIR_BOUNDS,0,0,0},header={0};
    HWND head=(HWND)Send(s->list,LVM_GETHEADER,0,0);
    int top=(int)Send(s->list,LVM_GETTOPINDEX,0,0);
    int count=(int)Send(s->list,LVM_GETITEMCOUNT,0,0);
    if(top<0)top=0;
    if(!Client(s->list,&client)||count<=0||!head||
       !API(GetWindowRect,0x140257208)(head,&header)||
       !Send(s->list,LVM_GETITEMRECT,top,(LPARAM)&first))return FALSE;
    API(MapWindowPoints,0x140257118)(0,s->list,(POINT*)&header,2);
    int height=first.bottom-first.top,body=client.bottom-header.bottom;
    if(height<=0||body<=0||client.right<=0)return FALSE;
    int capacity=body/height+9;
    if(capacity>MAX_CACHE_ROWS||capacity<2||
       (long long)capacity*height*client.right>18000000)return FALSE;
    if(s->width!=client.right || s->row_height!=height ||
       s->capacity!=capacity || s->body_top!=header.bottom || s->count!=count)free_cache(s);
    s->width=client.right;s->body_top=header.bottom;s->body_height=body;
    s->row_height=height;s->capacity=capacity;s->count=count;
    return TRUE;
}
static BOOL cache_create(SCROLL_STATE *s) {
    if(s->ready)return TRUE;
    HDC target=API(GetDC,0x1402571C8)(s->list);
    if(!target)return FALSE;
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=s->width;info.bmiHeader.biHeight=-s->capacity*s->row_height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    s->cache=API(CreateCompatibleDC,0x140256360)(target);
    if(s->cache)s->bitmap=API(CreateDIBSection,0x1402562B8)(target,&info,DIB_RGB_COLORS,(void**)&s->pixels,0,0);
    API(ReleaseDC,0x1402571D0)(s->list,target);
    if(s->cache&&s->bitmap&&s->pixels)s->previous=Select(s->cache,s->bitmap);
    if(!s->previous||s->previous==HGDI_ERROR){free_cache(s);return FALSE;}
    invalidate_cache(s);s->ready=TRUE;return TRUE;
}
static BOOL cache_row(SCROLL_STATE *s,int index) {
    int slot=index%s->capacity;
    if(s->cache_id[slot]==index)return TRUE;
    int saved=API(SaveDC,0x1402561E0)(s->cache);
    if(!saved)return FALSE;
    int y=slot*s->row_height;
    API(IntersectClipRect,0x140256160)(s->cache,0,y,s->width,y+s->row_height);
    /* Negative logical row coordinates disable stale hover highlighting in
       cached cards; viewport origin maps the exact existing painter to its
       cache slot. Selection still comes from the live native control. */
    const int logical_top=-0x100000;
    API(SetViewportOrgEx,0x140256220)(s->cache,0,y-logical_top,0);
    DRAWITEMSTRUCT item={0};
    item.CtlType=ODT_LISTVIEW;item.itemID=index;item.itemAction=ODA_DRAWENTIRE;
    if(Send(s->list,LVM_GETITEMSTATE,index,LVIS_SELECTED)&LVIS_SELECTED)item.itemState=ODS_SELECTED;
    item.hwndItem=s->list;item.hDC=s->cache;
    item.rcItem=(RECT){0,logical_top,s->width,logical_top+s->row_height};
    ((void (*)(void*,DRAWITEMSTRUCT*))0x140401440ULL)(s->self,&item);
    API(RestoreDC,0x1402561D0)(s->cache,saved);
    GDI_FLUSH flush=(GDI_FLUSH)Proc(Module(L"gdi32.dll"),"GdiFlush");
    if(!flush||!flush())return FALSE;
    DWORD *pixel=s->pixels+(SIZE_T)y*s->width;
    SIZE_T n=(SIZE_T)s->row_height*s->width;
    for(SIZE_T i=0;i<n;++i)pixel[i]|=0xFF000000UL;
    s->cache_id[slot]=index;return TRUE;
}
static BOOL paint(SCROLL_STATE *s) {
    if(!cache_create(s))return FALSE;
    int position=(int)(s->visual+0.5),first=position/s->row_height;
    int remainder=position%s->row_height;
    int rows=(s->body_height+remainder+s->row_height-1)/s->row_height;
    for(int n=0;n<rows;++n)if(first+n<s->count&&!cache_row(s,first+n))return FALSE;
    PAINTSTRUCT ps;HDC target=API(BeginPaint,0x1402571D8)(s->list,&ps);
    if(!target)return FALSE;
    RECT body={0,s->body_top,s->width,s->body_top+s->body_height};
    BUFFER_BEGIN begin=(BUFFER_BEGIN)(*(void**)0x140257488ULL);
    BUFFER_ALPHA alpha=(BUFFER_ALPHA)(*(void**)0x140257498ULL);
    BUFFER_END end=(BUFFER_END)(*(void**)0x140257490ULL);
    HDC dc=target,buffer_dc=0;HANDLE buffer=0;
    if(begin&&alpha&&end) {
        buffer=begin(target,&body,2,0,&buffer_dc);
        if(buffer&&buffer_dc)dc=buffer_dc;
        else if(buffer){end(buffer,FALSE);buffer=0;}
    }
    HBRUSH back=API(CreateSolidBrush,0x1402563D0)((COLORREF)Send(s->list,LVM_GETBKCOLOR,0,0));
    if(back){API(FillRect,0x140257268)(dc,&body,back);Delete(back);}
    BOOL okay=TRUE;int destination=s->body_top-remainder;
    for(int n=0;n<rows;++n) {
        int index=first+n;
        if(index>=s->count)break;
        int skip=destination<body.top?body.top-destination:0;
        int amount=s->row_height-skip;
        if(destination+skip+amount>body.bottom)amount=body.bottom-destination-skip;
        if(amount>0 && !API(BitBlt,0x140256328)(dc,0,destination+skip,s->width,amount,
             s->cache,0,(index%s->capacity)*s->row_height+skip,SRCCOPY))okay=FALSE;
        destination+=s->row_height;
    }
    if(buffer) {
        HRESULT result=alpha(buffer,0,255);
        if(FAILED(result)){end(buffer,FALSE);okay=FALSE;}
        else if(FAILED(end(buffer,TRUE)))okay=FALSE;
    }
    API(EndPaint,0x1402571E0)(s->list,&ps);
    if(okay){++s->frames;s->presented=s->visual;s->presented_at=NOW();}return okay;
}
static int frame_interval(SCROLL_STATE *s) {
    MONITORINFOEXW monitor={0};monitor.cbSize=sizeof(monitor);
    HMONITOR display=API(MonitorFromWindow,0x1402573D8)(s->list,MONITOR_DEFAULTTONEAREST);
    DISPLAY_MODE mode=(DISPLAY_MODE)Proc(Module(L"user32.dll"),"EnumDisplaySettingsW");
    DEVMODEW settings={0};settings.dmSize=sizeof(settings);
    if(display && mode && API(GetMonitorInfoW,0x1402573E0)(display,(MONITORINFO*)&monitor) &&
       mode(monitor.szDevice,ENUM_CURRENT_SETTINGS,&settings)) {
        int hz=(int)settings.dmDisplayFrequency;
        if(hz>=140)return 6;
        if(hz>=90)return 8;
    }
    return 15;
}
static double position_at(SCROLL_STATE *s,ULONGLONG now) {
    ULONGLONG elapsed=now-s->start;
    if(elapsed>=DURATION_MS)return s->target;
    double t=(double)(DURATION_MS-(int)elapsed)/DURATION_MS;
    return s->target+(s->from-s->target)*t*t*t;
}
static void CALLBACK post_frame(void *context,BOOLEAN fired) {
    (void)fired;SCROLL_STATE *s=(SCROLL_STATE*)context;
    if(InterlockedExchange(&s->pending,1)==0)
        if(!API(PostMessageW,0x140257288)(s->list,FRAME_MESSAGE,s->generation,0))
            InterlockedExchange(&s->pending,0);
}
static void frame(SCROLL_STATE *s) {
    if(!s->active)return;
    if((int)Send(s->list,LVM_GETITEMCOUNT,0,0)!=s->count ||
       !API(IsWindowVisible,0x1402570F8)(s->list)){stop(s,FALSE);return;}
    s->visual=position_at(s,NOW());
    BOOL done=NOW()-s->start>=DURATION_MS;
    if(done) {
        s->visual=s->target;sync_native(s,(int)s->target);
    }
    RECT body={0,s->body_top,s->width,s->body_top+s->body_height};
    API(InvalidateRect,0x1402572A0)(s->list,&body,FALSE);
    API(UpdateWindow,0x140256DE0)(s->list);
    if(done)stop(s,FALSE);
}
static BOOL wheel(SCROLL_STATE *s,WPARAM w) {
    if(LOWORD(w)&(MK_CONTROL|MK_SHIFT))return FALSE;
    BOOL effects=TRUE;UINT lines=3;
    API(SystemParametersInfoW,0x140257278)(SPI_GETCLIENTAREAANIMATION,0,&effects,0);
    if(!effects || !measure(s))return FALSE;
    API(SystemParametersInfoW,0x140257278)(SPI_GETWHEELSCROLLLINES,0,&lines,0);
    if(!lines)return TRUE;
    int delta=(short)HIWORD(w);
    if(!delta)return TRUE;
    s->remainder+=delta;int notches=s->remainder/WHEEL_DELTA;
    s->remainder-=notches*WHEEL_DELTA;
    if(!notches)return TRUE;
    if(lines==WHEEL_PAGESCROLL)lines=s->body_height/s->row_height;
    if(lines>100)lines=100;
    if(!s->active) {
        s->visual=s->target=actual(s);invalidate_cache(s);
        if(!cache_create(s))return FALSE;
    } else s->visual=position_at(s,NOW());
    double amount=-(double)notches*lines*s->row_height;
    int direction=amount>0?1:-1;
    if(s->active && direction!=s->last_direction)
        s->target=((int)(s->visual+s->row_height/2)/s->row_height)*s->row_height;
    double target=s->target+amount;
    int limit=maximum(s);
    if(target<0)target=0;if(target>limit)target=limit;
    s->target=target;s->from=s->visual;s->start=NOW();s->last_direction=direction;
    if(s->target==s->visual)return TRUE;
    s->active=TRUE;
    if(!s->queue) {
        QUEUE_CREATE create=(QUEUE_CREATE)Proc(Module(L"kernel32.dll"),"CreateTimerQueueTimer");
        QUEUE_DELETE remove=(QUEUE_DELETE)Proc(Module(L"kernel32.dll"),"DeleteTimerQueueTimer");
        HMODULE winmm=Module(L"winmm.dll");
        if(!winmm)winmm=API(LoadLibraryW,0x140256A58)(L"winmm.dll");
        TIME_PERIOD begin=winmm?(TIME_PERIOD)Proc(winmm,"timeBeginPeriod"):0;
        s->end_period=winmm?(TIME_PERIOD)Proc(winmm,"timeEndPeriod"):0;
        if(begin&&s->end_period&&begin(1)==0)s->precise=TRUE;
        s->interval=frame_interval(s);
        if(!create||!remove||!create(&s->queue,0,post_frame,s,1,s->interval,WT_EXECUTEDEFAULT)) {
            if(s->precise&&s->end_period)s->end_period(1);
            s->precise=FALSE;
            s->active=FALSE;return FALSE;
        }
    }
    return TRUE;
}
static LRESULT CALLBACK scroll_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    SCROLL_STATE *s=(SCROLL_STATE*)(ULONG_PTR)ref;DEF_SUBCLASS def=next_proc();
    if(!s||s->list!=h)return def(h,msg,w,l);
    if(msg==FRAME_MESSAGE) {
        if((DWORD)w==s->generation){InterlockedExchange(&s->pending,0);frame(s);}return 0;
    }
    if(msg==WM_MOUSEWHEEL) {
        if(wheel(s,w))return 0;
        stop(s,TRUE);invalidate_cache(s);
    }
    if(msg==WM_PAINT && s->active && !s->busy) {
        s->busy=TRUE;BOOL okay=paint(s);s->busy=FALSE;
        if(okay)return 0;stop(s,TRUE);API(InvalidateRect,0x1402572A0)(h,0,FALSE);
    }
    if(msg==WM_ERASEBKGND && s->active)return 1;
    if(msg==WM_MOUSEMOVE && s->active && !(w&MK_LBUTTON))return 0;
    BOOL interrupted=msg==WM_VSCROLL||msg==WM_HSCROLL||msg==WM_MOUSEHWHEEL||msg==WM_KEYDOWN||
        msg==WM_LBUTTONDOWN||msg==WM_RBUTTONDOWN||msg==WM_MBUTTONDOWN||
        msg==WM_SIZE||msg==WM_DPICHANGED||msg==WM_THEMECHANGED||msg==WM_SETTINGCHANGE||
        msg==LVM_SETITEMCOUNT||msg==LVM_DELETEALLITEMS||msg==LVM_SORTITEMS||msg==LVM_SORTITEMSEX||
        msg==LVM_SETCOLUMNWIDTH||msg==LVM_ENSUREVISIBLE||msg==LVM_SETITEMSTATE;
    if(msg==LVM_SCROLL&&!s->internal)interrupted=TRUE;
    if(msg==WM_NOTIFY && l) {
        NMHDR *notify=(NMHDR*)l;
        HWND header=(HWND)Send(h,LVM_GETHEADER,0,0);
        if(notify->hwndFrom==header) {
            int code=(int)notify->code;
            if(code==(int)HDN_ITEMCLICKA||code==(int)HDN_ITEMCLICKW||
               code==(int)HDN_BEGINTRACKA||code==(int)HDN_BEGINTRACKW||
               code==(int)HDN_TRACKA||code==(int)HDN_TRACKW||
               code==(int)HDN_ENDTRACKA||code==(int)HDN_ENDTRACKW||
               code==(int)HDN_BEGINDRAG||code==(int)HDN_ENDDRAG)interrupted=TRUE;
        }
    }
    if(msg==WM_SETREDRAW && !s->internal) {
        if(!w && s->active){s->last_stop_message=msg;stop(s,TRUE);invalidate_cache(s);}
        s->external_redraw_disabled=!w;
    }
    UINT theme=*(const UINT*)0x14033796CULL;
    if(theme>=WM_USER&&msg==theme)interrupted=TRUE;
    if(interrupted&&!s->internal&&!s->busy){s->last_stop_message=msg;stop(s,TRUE);invalidate_cache(s);}
    if(msg==WM_NCDESTROY) {
        stop(s,FALSE);free_cache(s);
        REMOVE_SUBCLASS remove=(REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,scroll_proc,id);
        if(remove&&s->header)remove(s->header,scroll_header_proc,SUBCLASS_ID);
        s->header=0;
        SCROLL_STATE **link=&root;
        while(*link&&*link!=s)link=&(*link)->next;
        if(*link==s)*link=s->next;
        LRESULT result=def(h,msg,w,l);
        API(HeapFree,0x140256A10)(API(GetProcessHeap,0x140256A08)(),0,s);return result;
    }
    return def(h,msg,w,l);
}
static LRESULT CALLBACK scroll_header_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    SCROLL_STATE *s=(SCROLL_STATE*)(ULONG_PTR)ref;DEF_SUBCLASS def=next_proc();
    if(!s||s->header!=h)return def(h,msg,w,l);
    if(msg==WM_SETREDRAW&&s->internal){++s->suppressed_header_redraw;return 0;}
    if(msg==WM_NCDESTROY) {
        REMOVE_SUBCLASS remove=(REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,scroll_header_proc,id);s->header=0;
    }
    return def(h,msg,w,l);
}
__declspec(dllexport) void ui_pre_subclass(void *self) {
    ((void (*)(void *))0x140457660ULL)(self);
    HWND list=*(HWND *)((char*)self+0x40);if(!list||find(list))return;
    SCROLL_STATE *s=API(HeapAlloc,0x140256A18)(API(GetProcessHeap,0x140256A08)(),HEAP_ZERO_MEMORY,sizeof(SCROLL_STATE));
    if(!s)return;s->self=self;s->list=list;s->generation=1;invalidate_cache(s);
    ADD_SUBCLASS add=(ADD_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(!add(list,scroll_proc,SUBCLASS_ID,(DWORD_PTR)s)) {
        API(HeapFree,0x140256A10)(API(GetProcessHeap,0x140256A08)(),0,s);return;
    }
    s->header=(HWND)Send(list,LVM_GETHEADER,0,0);
    if(s->header&&!add(s->header,scroll_header_proc,SUBCLASS_ID,(DWORD_PTR)s))s->header=0;
    s->next=root;root=s;
}

/* Hover and native scroll handlers may ask the parent to draw an item
   directly, outside WM_PAINT. During easing there must be one presenter. */
__declspec(dllexport) void ui_draw_card(void *self,DRAWITEMSTRUCT *item) {
    HWND list=*(HWND *)((char*)self+0x40);
    SCROLL_STATE *s=find(list);
    if(s&&s->active) {
        ++s->suppressed_native_rows;
        return;
    }
    ((void (*)(void*,DRAWITEMSTRUCT*))0x1404012B0ULL)(self,item);
}

#include "native-scroll-render.h"
__declspec(dllexport) void native_probe(void) {}
