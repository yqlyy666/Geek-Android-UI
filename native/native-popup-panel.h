#ifndef GEEK_NATIVE_POPUP_PANEL_H
#define GEEK_NATIVE_POPUP_PANEL_H

#include <uxtheme.h>

/* Include after native-ui.c's import aliases and DPI/drawing helpers.
   The original CXTPPopupBar HWND, controls, WndProc and input stay native. */
#define GEEK_POPUP_PANEL_SLOTS 32
#define GEEK_POPUP_PANEL_ID 0x4750504CUL
#define GEEK_POPUP_PANEL_DIAMETER 36

typedef struct {
    HWND window;
    int width;
    int height;
    int dpi;
    BOOL busy;
} GEEK_POPUP_PANEL_SLOT;

typedef BOOL (WINAPI *GEEK_POPUP_PANEL_REMOVE)(HWND,SUBCLASSPROC,UINT_PTR);
typedef HANDLE (WINAPI *GEEK_POPUP_PANEL_BEGIN)(HDC,const RECT*,int,const BP_PAINTPARAMS*,HDC*);
typedef HRESULT (WINAPI *GEEK_POPUP_PANEL_ALPHA)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_POPUP_PANEL_END)(HANDLE,BOOL);
typedef HRGN (WINAPI *GEEK_POPUP_PANEL_RECT_RGN)(int,int,int,int);
typedef int (WINAPI *GEEK_POPUP_PANEL_GET_RGN)(HWND,HRGN);
typedef BOOL (WINAPI *GEEK_POPUP_PANEL_EQUAL_RGN)(HRGN,HRGN);

static GEEK_POPUP_PANEL_SLOT geek_popup_panel_slots[GEEK_POPUP_PANEL_SLOTS];

static void geek_popup_panel_shape(HWND h,GEEK_POPUP_PANEL_SLOT *slot,BOOL force) {
    if(slot && slot->busy)return;
    RECT window;
    if(!h || !API(GetWindowRect,0x140257208)(h,&window))return;
    int width=window.right-window.left,height=window.bottom-window.top,dpi=dpi_of(h);
    if(width<=0 || height<=0)return;
    HMODULE gdi=Module(L"gdi32.dll");
    ROUND_REGION create=(ROUND_REGION)Proc(gdi,"CreateRoundRectRgn");
    if(!create)return;
    int diameter=px(GEEK_POPUP_PANEL_DIAMETER,dpi);
    if(diameter>width)diameter=width;
    if(diameter>height)diameter=height;
    HRGN region=create(0,0,width,height,diameter,diameter);
    if(!region)return;
    if(slot && !force && slot->width==width && slot->height==height && slot->dpi==dpi) {
        /* Native popup animation/shadow code can clear or replace a region
           without changing HWND geometry. A size cache alone cannot prove
           the requested rounded shape is still installed. */
        GEEK_POPUP_PANEL_RECT_RGN make=(GEEK_POPUP_PANEL_RECT_RGN)Proc(gdi,"CreateRectRgn");
        GEEK_POPUP_PANEL_GET_RGN get=(GEEK_POPUP_PANEL_GET_RGN)Proc(Module(L"user32.dll"),"GetWindowRgn");
        GEEK_POPUP_PANEL_EQUAL_RGN equal=(GEEK_POPUP_PANEL_EQUAL_RGN)Proc(gdi,"EqualRgn");
        if(make && get && equal) {
            HRGN actual=make(0,0,0,0);
            if(actual) {
                BOOL matches=get(h,actual)!=ERROR && equal(region,actual);
                Delete(actual);
                if(matches) {Delete(region);return;}
            }
        }
    }
    if(slot) {
        /* SetWindowRgn can synchronously reenter WINDOWPOSCHANGED. Store both
           the pending geometry and guard before handing the region to USER. */
        slot->width=width;slot->height=height;slot->dpi=dpi;slot->busy=TRUE;
    }
    BOOL applied=API(SetWindowRgn,0x140257158)(h,region,FALSE);
    if(!applied)Delete(region);
    if(slot) {
        if(!applied)slot->width=slot->height=slot->dpi=0;
        slot->busy=FALSE;
    }
}

static void geek_popup_panel_border(HWND h) {
    RECT window,client;
    if(!h || !API(GetWindowRect,0x140257208)(h,&window) || !Client(h,&client))return;
    POINT start={client.left,client.top},end={client.right,client.bottom};
    if(!ToScreen(h,&start) || !ToScreen(h,&end))return;
    int width=window.right-window.left,height=window.bottom-window.top;
    if(width<=0 || height<=0)return;
    RECT inner={start.x-window.left,start.y-window.top,end.x-window.left,end.y-window.top};
    if(inner.left<=0 && inner.top<=0 && inner.right>=width && inner.bottom>=height)return;
    HDC target=API(GetWindowDC,0x1402573F8)(h);
    if(!target)return;
    int saved=Save(target);
    if(saved) {
        API(ExcludeClipRect,0x1402561F0)(target,inner.left,inner.top,inner.right,inner.bottom);
        RECT outer={0,0,width,height};
        HDC dc=target,bufferDC=0;
        GEEK_POPUP_PANEL_BEGIN begin=(GEEK_POPUP_PANEL_BEGIN)(*(void**)0x140257488ULL);
        GEEK_POPUP_PANEL_ALPHA alpha=(GEEK_POPUP_PANEL_ALPHA)(*(void**)0x140257498ULL);
        GEEK_POPUP_PANEL_END endpaint=(GEEK_POPUP_PANEL_END)(*(void**)0x140257490ULL);
        HANDLE buffer=0;
        if(begin && alpha && endpaint) {
            BP_PAINTPARAMS params={sizeof(params),BPPF_NONCLIENT,&inner,0};
            buffer=begin(target,&outer,2,&params,&bufferDC);
            if(buffer && !bufferDC) {endpaint(buffer,FALSE);buffer=0;}
            if(buffer && bufferDC)dc=bufferDC;
        }
        BOOL isdark=dark();
        COLORREF back=isdark?RGB(33,31,38):RGB(255,251,254);
        COLORREF edge=isdark?RGB(77,70,86):RGB(225,216,232);
        solid(dc,&outer,back);
        rounded(dc,outer,px(GEEK_POPUP_PANEL_DIAMETER,dpi_of(h)),back,edge);
        if(buffer) {
            HRESULT result=alpha(buffer,0,255);
            endpaint(buffer,SUCCEEDED(result));
        }
        Restore(target,saved);
    }
    API(ReleaseDC,0x1402571D0)(h,target);
}

static LRESULT CALLBACK geek_popup_panel_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    GEEK_POPUP_PANEL_SLOT *slot=(GEEK_POPUP_PANEL_SLOT*)ref;
    if(!slot || slot->window!=h)return def(h,message,w,l);
    if(message==WM_NCDESTROY) {
        GEEK_POPUP_PANEL_REMOVE remove=(GEEK_POPUP_PANEL_REMOVE)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_popup_panel_subclass,id);
        *slot=(GEEK_POPUP_PANEL_SLOT){0};
        return def(h,message,w,l);
    }
    LRESULT result=def(h,message,w,l);
    if(slot->busy)return result;
    if(message==WM_WINDOWPOSCHANGED || message==WM_SIZE || message==WM_DPICHANGED ||
       message==WM_THEMECHANGED || message==WM_SETTINGCHANGE || (message==WM_SHOWWINDOW && w)) {
        geek_popup_panel_shape(h,slot,message==WM_DPICHANGED || message==WM_THEMECHANGED || message==WM_SETTINGCHANGE);
        geek_popup_panel_border(h);
    } else if(message==WM_NCPAINT || message==WM_PAINT) {
        geek_popup_panel_shape(h,slot,FALSE);
        geek_popup_panel_border(h);
    }
    return result;
}

static void geek_popup_panel_ensure(void *paint,void *popup,BOOL force) {
    if(!popup || *(int*)((char*)popup+0x180)!=2)return;
    /* CXTPPopupBar's own shadow flag (+2E0, initialized to 1) gates both
       native shadow creation sites. The panel region does not clip those
       separate right/bottom shadow HWNDs. Disable only this popup and let
       its existing manager remove any shadows already associated with it.
       Original ABI: RemoveShadow(manager=paint+618, popup CWnd object). */
    DWORD *shadow=(DWORD*)((char*)popup+0x2E0);
    if(*shadow) {
        *shadow=0;
        void *manager=paint?*(void**)((char*)paint+0x618):0;
        if(manager)((void(*)(void*,void*))0x140165BE0ULL)(manager,popup);
    }
    HWND h=*(HWND*)((char*)popup+0x40);
    if(!h)return;
    GEEK_POPUP_PANEL_SLOT *slot=0,*free_slot=0;
    for(int i=0;i<GEEK_POPUP_PANEL_SLOTS;++i) {
        if(geek_popup_panel_slots[i].window==h) {slot=&geek_popup_panel_slots[i];break;}
        if(!geek_popup_panel_slots[i].window && !free_slot)free_slot=&geek_popup_panel_slots[i];
    }
    if(!slot && free_slot) {
        free_slot->window=h;
        SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
        if(add(h,geek_popup_panel_subclass,GEEK_POPUP_PANEL_ID,(DWORD_PTR)free_slot))slot=free_slot;
        else *free_slot=(GEEK_POPUP_PANEL_SLOT){0};
    }
    if(slot && slot->busy)return;
    geek_popup_panel_shape(h,slot,force);
    geek_popup_panel_border(h);
}

__declspec(dllexport) void ui_popup_panel_round(void *paint,void *popup) {
    /* Relayout can clear an unchanged region: force the explicit native
       positioning hooks, while the first-draw entry checks the live shape. */
    geek_popup_panel_ensure(paint,popup,TRUE);
}

__declspec(dllexport) void ui_popup_panel_ensure(void *paint,void *popup) {
    /* Initial show may bypass all three relayout hooks. Called before native
       popup drawing to install once and disable independent square shadows.
       Repeated paints verify the actual region without resetting a good one. */
    geek_popup_panel_ensure(paint,popup,FALSE);
}

__declspec(dllexport) RECT *ui_popup_panel_borders(void *paint,RECT *out,void *bar) {
    if(!out || !bar)return out;
    if(*(int*)((char*)bar+0x180)!=2) {
        ((void(*)(void*,RECT*,void*))0x140108C00ULL)(paint,out,bar);
        return out;
    }
    int margin=px(10,dpi_of(*(HWND*)((char*)bar+0x40)));
    *out=(RECT){margin,margin,margin,margin};
    return out;
}

#endif
