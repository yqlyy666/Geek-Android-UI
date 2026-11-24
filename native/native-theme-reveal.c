#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define API(name,address) ((__typeof__(&name))(*(void **)(address##ULL)))
#define Module API(GetModuleHandleW,0x140256AC0)
#define Proc API(GetProcAddress,0x140256A78)
#define Delete API(DeleteObject,0x140256380)
#define Select API(SelectObject,0x1402563A8)
#define THEME_TIMER ((UINT_PTR)0x47545256)
#define SUBCLASS_ID ((UINT_PTR)0x47545256)
#define BEGIN_REVEAL (WM_APP+0x57D)
#define NEXT_FRAME (WM_APP+0x57E)
#define ANIMATION_MS 440

typedef LRESULT (CALLBACK *SUBCLASS)(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
typedef BOOL (WINAPI *ADD_SUBCLASS)(HWND,SUBCLASS,UINT_PTR,DWORD_PTR);
typedef BOOL (WINAPI *REMOVE_SUBCLASS)(HWND,SUBCLASS,UINT_PTR);
typedef LRESULT (WINAPI *DEF_SUBCLASS)(HWND,UINT,WPARAM,LPARAM);
typedef BOOL (WINAPI *LAYER_UPDATE)(HWND,HDC,POINT*,SIZE*,HDC,POINT*,COLORREF,BLENDFUNCTION*,DWORD);
typedef HRGN (WINAPI *ROUND_REGION)(int,int,int,int,int,int);
typedef UINT (WINAPI *WINDOW_DPI)(HWND);
typedef BOOL (WINAPI *GDI_FLUSH)(void);
typedef BOOL (WINAPI *QUEUE_CREATE)(PHANDLE,HANDLE,WAITORTIMERCALLBACK,PVOID,DWORD,DWORD,ULONG);
typedef BOOL (WINAPI *QUEUE_DELETE)(HANDLE,HANDLE,HANDLE);
typedef HRESULT (WINAPI *COMPOSITION_FLUSH)(void);

typedef struct {
    HWND owner,overlay;
    HDC source_dc,frame_dc;
    HBITMAP source_bitmap,frame_bitmap;
    HGDIOBJ source_old,frame_old;
    DWORD *source_pixels,*frame_pixels;
    LAYER_UPDATE update;
    RECT bounds;
    POINT center;
    int width,height,maximum_radius,radius;
    ULONGLONG start;
    UINT_PTR timer;
    DWORD frames,generation;
    BOOL preparing,switching,registered;
    HANDLE queue;
    HWND frame_target;
    DWORD frame_generation;
    volatile LONG frame_pending;
    COMPOSITION_FLUSH composition_flush;
    DWORD shield_presented,new_theme_ready,last_frame_presented;
} THEME_REVEAL;
_Static_assert(sizeof(THEME_REVEAL)<=256,"theme state exceeds reserved storage");
#define reveal (*(THEME_REVEAL *)0x140412B18ULL)

static BOOL dark(void) {return ((BOOL (*)(void))0x14000C49CULL)()&255;}
static DEF_SUBCLASS next_proc(void) {return (DEF_SUBCLASS)(*(void**)0x1402560B8ULL);}
static BOOL present(void) {
    GDI_FLUSH flush=(GDI_FLUSH)Proc(Module(L"gdi32.dll"),"GdiFlush");
    return flush && flush() && reveal.composition_flush && SUCCEEDED(reveal.composition_flush());
}

static void finish(void) {
    HWND overlay=reveal.overlay;reveal.overlay=0;
    if(reveal.queue) {
        QUEUE_DELETE stop=(QUEUE_DELETE)Proc(Module(L"kernel32.dll"),"DeleteTimerQueueTimer");
        if(stop)stop(0,reveal.queue,INVALID_HANDLE_VALUE);
        reveal.queue=0;
    }
    reveal.frame_pending=0;
    if(reveal.timer && overlay)API(KillTimer,0x1402570D8)(overlay,reveal.timer);
    reveal.timer=0;
    if(overlay)API(DestroyWindow,0x140257188)(overlay);
    if(reveal.source_dc && reveal.source_old)Select(reveal.source_dc,reveal.source_old);
    if(reveal.frame_dc && reveal.frame_old)Select(reveal.frame_dc,reveal.frame_old);
    if(reveal.source_bitmap)Delete(reveal.source_bitmap);
    if(reveal.frame_bitmap)Delete(reveal.frame_bitmap);
    if(reveal.source_dc)API(DeleteDC,0x140256330)(reveal.source_dc);
    if(reveal.frame_dc)API(DeleteDC,0x140256330)(reveal.frame_dc);
    reveal.source_dc=0;reveal.frame_dc=0;reveal.source_bitmap=0;reveal.frame_bitmap=0;
    reveal.source_old=0;reveal.frame_old=0;reveal.source_pixels=0;reveal.frame_pixels=0;
    reveal.preparing=FALSE;
}
static void frame(HWND window);
static LRESULT CALLBACK overlay_proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==NEXT_FRAME) {
        if(h==reveal.overlay && (DWORD)w==reveal.frame_generation) {
            InterlockedExchange(&reveal.frame_pending,0);
            frame(h);
        }
        return 0;
    }
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND)return 1;
    return API(DefWindowProcW,0x1402573A0)(h,msg,w,l);
}
static BOOL upload(void) {
    POINT at={reveal.bounds.left,reveal.bounds.top},source={0,0};
    SIZE size={reveal.width,reveal.height};
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    return reveal.update && reveal.overlay && reveal.update(reveal.overlay,0,&at,&size,
        reveal.frame_dc,&source,0,&blend,ULW_ALPHA);
}

/* Fade only a two-pixel circular rim. All other pixels are either the
   exact old view or a transparent hole showing the live new-theme UI. */
static BOOL draw_radius(int radius) {
    long long outer=(long long)radius*radius;
    int inside=radius>2?radius-2:0;
    long long inner=(long long)inside*inside;
    long long span=outer-inner;
    DWORD *out=reveal.frame_pixels,*old=reveal.source_pixels;
    if(!out || !old)return FALSE;
    for(int y=0;y<reveal.height;++y) {
        long long dy=y-reveal.center.y,y2=dy*dy;
        for(int x=0;x<reveal.width;++x) {
            long long dx=x-reveal.center.x,distance=dx*dx+y2;
            DWORD pixel=*old++;
            if(radius<=0 || distance>=outer)*out++=pixel|0xFF000000UL;
            else if(distance<=inner)*out++=0;
            else {
                DWORD alpha=span?(DWORD)((distance-inner)*255/span):0;
                DWORD r=((pixel>>16)&255)*alpha/255;
                DWORD g=((pixel>>8)&255)*alpha/255;
                DWORD b=(pixel&255)*alpha/255;
                *out++=(alpha<<24)|(r<<16)|(g<<8)|b;
            }
        }
    }
    if(!upload())return FALSE;
    reveal.radius=radius;++reveal.frames;return TRUE;
}
static void CALLBACK post_frame(void *context,BOOLEAN fired) {
    (void)context;(void)fired;
    if(InterlockedExchange(&reveal.frame_pending,1)==0) {
        if(!API(PostMessageW,0x140257288)(reveal.frame_target,NEXT_FRAME,reveal.frame_generation,0))
            InterlockedExchange(&reveal.frame_pending,0);
    }
}
static void frame(HWND window) {
    if(window!=reveal.overlay || !reveal.overlay)return;
    HWND owner=reveal.owner;
    RECT current;
    if(API(IsIconic,0x140256DA0)(owner) || !API(IsWindowVisible,0x1402570F8)(owner) ||
       API(GetForegroundWindow,0x140257358)()!=owner ||
       !API(GetWindowRect,0x140257208)(owner,&current) ||
       current.left!=reveal.bounds.left || current.top!=reveal.bounds.top ||
       current.right!=reveal.bounds.right || current.bottom!=reveal.bounds.bottom) {finish();return;}
    ULONGLONG elapsed=API(GetTickCount64,0x140256A80)()-reveal.start;
    if(elapsed>=ANIMATION_MS) {
        if(draw_radius(reveal.maximum_radius) && present())reveal.last_frame_presented=1;
        finish();return;
    }
    long long left=ANIMATION_MS-(long long)elapsed;
    long long total=(long long)ANIMATION_MS*ANIMATION_MS*ANIMATION_MS;
    int radius=(int)((reveal.maximum_radius*(total-left*left*left))/total);
    if(radius!=reveal.radius && !draw_radius(radius))finish();
}
static BOOL bitmap(HDC target,int width,int height,HDC *dc,HBITMAP *image,
                   HGDIOBJ *previous,DWORD **pixels) {
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    *dc=API(CreateCompatibleDC,0x140256360)(target);
    if(!*dc)return FALSE;
    *image=API(CreateDIBSection,0x1402562B8)(target,&info,DIB_RGB_COLORS,(void**)pixels,0,0);
    if(!*image || !*pixels)return FALSE;
    *previous=Select(*dc,*image);
    return *previous && *previous!=HGDI_ERROR;
}
static BOOL prepare(HWND owner) {
    BOOL effects=TRUE;
    if(!API(SystemParametersInfoW,0x140257278)(SPI_GETCLIENTAREAANIMATION,0,&effects,0) ||
       !effects || API(IsIconic,0x140256DA0)(owner) ||
       API(GetForegroundWindow,0x140257358)()!=owner ||
       !API(GetWindowRect,0x140257208)(owner,&reveal.bounds))return FALSE;
    reveal.width=reveal.bounds.right-reveal.bounds.left;
    reveal.height=reveal.bounds.bottom-reveal.bounds.top;
    if(reveal.width<=0 || reveal.height<=0 || reveal.width>12000 || reveal.height>12000 ||
       (long long)reveal.width*reveal.height>16000000)return FALSE;
    reveal.preparing=TRUE;
    reveal.shield_presented=0;reveal.new_theme_ready=0;reveal.last_frame_presented=0;
    reveal.update=(LAYER_UPDATE)Proc(Module(L"user32.dll"),"UpdateLayeredWindow");
    if(!reveal.update)return FALSE;
    HMODULE dwm=Module(L"dwmapi.dll");
    if(!dwm)dwm=API(LoadLibraryW,0x140256A58)(L"dwmapi.dll");
    reveal.composition_flush=dwm?(COMPOSITION_FLUSH)Proc(dwm,"DwmFlush"):0;
    QUEUE_CREATE create=(QUEUE_CREATE)Proc(Module(L"kernel32.dll"),"CreateTimerQueueTimer");
    QUEUE_DELETE stop=(QUEUE_DELETE)Proc(Module(L"kernel32.dll"),"DeleteTimerQueueTimer");
    if(!reveal.composition_flush || !create || !stop)return FALSE;
    API(GetCursorPos,0x140257210)(&reveal.center);
    reveal.center.x-=reveal.bounds.left;reveal.center.y-=reveal.bounds.top;
    /* Popup items can extend outside the owner. Preserve the click position
       there too, so the circle truly starts at the selected menu item. */
    int dx=reveal.center.x>reveal.width-reveal.center.x?reveal.center.x:reveal.width-reveal.center.x;
    int dy=reveal.center.y>reveal.height-reveal.center.y?reveal.center.y:reveal.height-reveal.center.y;
    long long squared=(long long)dx*dx+(long long)dy*dy;
    int radius=1;while((long long)radius*radius<squared)++radius;
    reveal.maximum_radius=radius+3;reveal.radius=0;reveal.frames=0;++reveal.generation;
    /* Preserve the already visible old view. Repainting it before the shield
       is shown exposes an unnecessary whole-window refresh. */
    HDC target=API(GetWindowDC,0x1402573F8)(owner);
    if(!target)return FALSE;
    BOOL captured=bitmap(target,reveal.width,reveal.height,&reveal.source_dc,
                       &reveal.source_bitmap,&reveal.source_old,&reveal.source_pixels) &&
                  bitmap(target,reveal.width,reveal.height,&reveal.frame_dc,
                       &reveal.frame_bitmap,&reveal.frame_old,&reveal.frame_pixels) &&
                  API(BitBlt,0x140256328)(reveal.source_dc,0,0,reveal.width,reveal.height,
                                        target,0,0,SRCCOPY);
    API(ReleaseDC,0x1402571D0)(owner,target);
    if(!captured)return FALSE;
    GDI_FLUSH flush=(GDI_FLUSH)Proc(Module(L"gdi32.dll"),"GdiFlush");
    if(!flush || !flush())return FALSE;
    HINSTANCE instance=(HINSTANCE)Module(0);
    if(!reveal.registered) {
        WNDCLASSW cls={0};cls.lpfnWndProc=overlay_proc;cls.hInstance=instance;
        cls.lpszClassName=L"GeekThemeReveal";
        if(!API(RegisterClassW,0x140257388)(&cls))return FALSE;
        reveal.registered=TRUE;
    }
    reveal.overlay=API(CreateWindowExW,0x140256FE8)(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
        L"GeekThemeReveal",L"",WS_POPUP,reveal.bounds.left,reveal.bounds.top,
        reveal.width,reveal.height,owner,0,instance,0);
    if(!reveal.overlay)return FALSE;
    HRGN shape=API(CreateRectRgn,0x1402562A0)(0,0,0,0);
    if(shape && API(GetWindowRgn,0x140257138)(owner,shape)==ERROR) {
        Delete(shape);shape=0;
        ROUND_REGION rounded=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
        WINDOW_DPI getdpi=(WINDOW_DPI)Proc(Module(L"user32.dll"),"GetDpiForWindow");
        UINT dpi=getdpi?getdpi(owner):96;if(dpi<96||dpi>768)dpi=96;
        int diameter=(16*(int)dpi+48)/96;
        if(rounded && !API(IsZoomed,0x140256F50)(owner))
            shape=rounded(0,0,reveal.width,reveal.height,diameter,diameter);
    }
    if(shape && !API(SetWindowRgn,0x140257158)(reveal.overlay,shape,FALSE))Delete(shape);
    if(!draw_radius(0))return FALSE;
    if(!API(SetWindowPos,0x1402572F0)(reveal.overlay,HWND_TOP,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW))return FALSE;
    if(!present())return FALSE;
    reveal.shield_presented=1;
    return TRUE;
}
static LRESULT CALLBACK owner_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    (void)ref;DEF_SUBCLASS def=next_proc();
    if(h!=reveal.owner)return def(h,msg,w,l);
    if(msg==BEGIN_REVEAL) {
        if(reveal.overlay && reveal.preparing && (DWORD)w==reveal.generation) {
            RECT current;
            if(!API(GetWindowRect,0x140257208)(h,&current) ||
               current.left!=reveal.bounds.left || current.top!=reveal.bounds.top ||
               current.right!=reveal.bounds.right || current.bottom!=reveal.bounds.bottom) {
                finish();return 0;
            }
            API(RedrawWindow,0x140257260)(h,0,0,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW);
            if(!present()){finish();return 0;}
            reveal.new_theme_ready=1;
            reveal.start=API(GetTickCount64,0x140256A80)();reveal.preparing=FALSE;
            QUEUE_CREATE create=(QUEUE_CREATE)Proc(Module(L"kernel32.dll"),"CreateTimerQueueTimer");
            reveal.frame_target=reveal.overlay;reveal.frame_generation=reveal.generation;
            InterlockedExchange(&reveal.frame_pending,0);
            if(!create || !create(&reveal.queue,0,post_frame,0,1,16,WT_EXECUTEDEFAULT))finish();
        }
        return 0;
    }
    if(reveal.overlay && !reveal.switching && !reveal.preparing) {
        if(msg==WM_MOVE || msg==WM_SIZE || msg==WM_DPICHANGED || msg==WM_SYSCOMMAND ||
           (msg==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) || msg==WM_CANCELMODE ||
           msg==WM_LBUTTONDOWN || msg==WM_NCLBUTTONDOWN || msg==WM_KEYDOWN)finish();
    }
    if(msg==WM_NCDESTROY) {
        finish();REMOVE_SUBCLASS remove=(REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,owner_proc,id);
        reveal.owner=0;
    }
    return def(h,msg,w,l);
}
static void switch_theme(void *self,UINT command,void (*original)(void *)) {
    HWND h=*(HWND *)((char *)self+0x40);
    if(!h || reveal.switching || reveal.owner!=h) {original(self);return;}
    finish();BOOL before=dark();BOOL setup=FALSE;reveal.switching=TRUE;
    if(!((command==0x8099 && !before)||(command==0x809A && before)))setup=prepare(h);
    if(!setup)finish();
    original(self);
    if(setup && dark()!=before && reveal.overlay) {
        /* Start after Codejock dismisses its menus and the existing chrome
           command wrapper has restored the original window dimensions. */
        if(!API(PostMessageW,0x140257288)(h,BEGIN_REVEAL,reveal.generation,0))finish();
    } else finish();
    reveal.switching=FALSE;
}
__declspec(dllexport) void ui_theme_light(void *self) {
    switch_theme(self,0x8099,(void (*)(void *))0x1400289E4ULL);
}
__declspec(dllexport) void ui_theme_dark(void *self) {
    switch_theme(self,0x809A,(void (*)(void *))0x1400289ACULL);
}
__declspec(dllexport) void ui_theme_auto(void *self) {
    switch_theme(self,0x809B,(void (*)(void *))0x140028978ULL);
}
__declspec(dllexport) void ui_restore_main(HWND h,RECT *position,int maximized,long long size) {
    ((void (*)(HWND,RECT*,int,long long))0x1404065A0ULL)(h,position,maximized,size);
    if(!h || reveal.owner==h)return;
    ADD_SUBCLASS add=(ADD_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(add(h,owner_proc,SUBCLASS_ID,0))reveal.owner=h;
}
__declspec(dllexport) void native_probe(void) {}
