#ifndef GEEK_NATIVE_SEARCH_STYLE_H
#define GEEK_NATIVE_SEARCH_STYLE_H

#include "native-surface.h"

/* Keep the existing EDIT HWND, ID, notifications, native text and caret.
   Only its surrounding non-client pixels and the original view layout move. */
typedef struct {
    HWND window;
    int region_width;
    int region_height;
    int region_dpi;
} GEEK_SEARCH_STATE;

typedef BOOL (WINAPI *GEEK_SEARCH_REMOVE_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR);
static GEEK_SEARCH_STATE geek_search_state;

static BOOL geek_search_window_rect(HWND h,RECT *rect) {
    return API(GetWindowRect,0x140257208)(h,rect);
}

static int geek_search_line_height(HWND h,int dpi) {
    int height=px(14,dpi);
    HDC dc=API(GetDC,0x1402571C8)(h);
    if(dc) {
        HFONT font=(HFONT)Send(h,WM_GETFONT,0,0);
        HGDIOBJ old=font?Select(dc,font):0;
        TEXTMETRICW metric;
        if(API(GetTextMetricsW,0x1402562A8)(dc,&metric) && metric.tmHeight>0)
            height=metric.tmHeight;
        if(old)Select(dc,old);
        API(ReleaseDC,0x1402571D0)(h,dc);
    }
    return height;
}

static void geek_search_region(HWND h) {
    RECT r;
    if(!geek_search_window_rect(h,&r))return;
    int width=r.right-r.left,height=r.bottom-r.top,dpi=dpi_of(h);
    if(width<=0 || height<=0)return;
    if(geek_search_state.region_width==width && geek_search_state.region_height==height &&
       geek_search_state.region_dpi==dpi)return;
    /* SetWindowRgn can send WINDOWPOSCHANGED synchronously. Cache first. */
    geek_search_state.region_width=width;
    geek_search_state.region_height=height;
    geek_search_state.region_dpi=dpi;
    ROUND_REGION make=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
    if(make) {
        int diameter=px(20,dpi);
        HRGN region=make(0,0,width+1,height+1,diameter,diameter);
        /* Windows owns a region only after SetWindowRgn succeeds. */
        if(region) {
            if(API(SetWindowRgn,0x140257158)(h,region,FALSE)) {
                /* The first parent paint can precede region installation.
                   Repaint its newly exposed corners once the shape exists. */
                if(geek_surface_state.view)Invalidate(geek_surface_state.view,0,TRUE);
            } else Delete(region);
        }
    }
}

static void geek_search_paint_frame(HWND h) {
    RECT window,client;
    if(!geek_search_window_rect(h,&window) || !Client(h,&client))return;
    POINT start={client.left,client.top},end={client.right,client.bottom};
    if(!ToScreen(h,&start) || !ToScreen(h,&end))return;
    HDC dc=API(GetWindowDC,0x1402573F8)(h);
    if(!dc)return;
    int saved=Save(dc),dpi=dpi_of(h);
    /* Even repainting on focus or WM_PAINT must not touch text or caret. */
    RECT textclient={start.x-window.left,start.y-window.top,end.x-window.left,end.y-window.top};
    API(ExcludeClipRect,0x1402561F0)(dc,textclient.left,textclient.top,textclient.right,textclient.bottom);
    BOOL isdark=dark(),focused=API(GetFocus,0x1402571B0)()==h;
    COLORREF back=geek_surface_edit_background();
    COLORREF edge=focused?(isdark?RGB(194,161,237):RGB(126,85,178)):
        (isdark?RGB(102,89,121):RGB(188,175,208));
    RECT r={0,0,window.right-window.left,window.bottom-window.top};
    solid(dc,&r,isdark?RGB(20,18,24):RGB(246,244,249));
    int stroke=px(focused?2:1,dpi);if(stroke<1)stroke=1;
    HBRUSH brush=Brush(back);HPEN pen=Pen(PS_SOLID,stroke,edge);
    HGDIOBJ oldbrush=Select(dc,brush),oldpen=Select(dc,pen);
    int inset=stroke/2,diameter=px(20,dpi);
    Round(dc,inset,inset,r.right-inset,r.bottom-inset,diameter,diameter);
    Select(dc,oldpen);Select(dc,oldbrush);Delete(pen);Delete(brush);
    geek_surface_make_frame_opaque(dc,&r,&textclient);
    Restore(dc,saved);API(ReleaseDC,0x1402571D0)(h,dc);
}

static void geek_search_frame_changed(HWND h) {
    API(SetWindowPos,0x1402572F0)(h,0,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}

static LRESULT CALLBACK geek_search_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_NCCALCSIZE) {
        def(h,message,w,l);
        RECT *r=w?&((NCCALCSIZE_PARAMS*)l)->rgrc[0]:(RECT*)l;
        if(r) {
            int dpi=dpi_of(h),height=r->bottom-r->top;
            /* The original cue starts one physical pixel below client.top.
               Keep that native inset and room for the caret/descenders. */
            int side=px(12,dpi),line=geek_search_line_height(h,dpi)+2;
            if(height<=0)return 0;
            if(side*2>=r->right-r->left)side=(r->right-r->left-1)/2;
            if(side<0)side=0;
            if(line>height)line=height;
            if(line<1)line=1;
            int top=(height-line)/2;if(top<0)top=0;
            r->left+=side;r->right-=side;
            r->top+=top;r->bottom=r->top+line;
        }
        return 0;
    }
    if(message==WM_NCHITTEST) {
        /* The padding remains clickable as part of the native edit. */
        RECT r;POINT p={(short)LOWORD(l),(short)HIWORD(l)};
        if(geek_search_window_rect(h,&r) && p.x>=r.left && p.x<r.right && p.y>=r.top && p.y<r.bottom)
            return HTCLIENT;
    }
    if(message==WM_NCPAINT) {
        def(h,message,w,l);geek_search_paint_frame(h);return 0;
    }
    if(message==WM_SETFONT || message==WM_THEMECHANGED || message==WM_SETTINGCHANGE) {
        LRESULT result=def(h,message,w,l);
        geek_search_state.region_dpi=0;
        geek_search_frame_changed(h);geek_search_region(h);geek_search_paint_frame(h);
        return result;
    }
    if(message==WM_SIZE || message==WM_WINDOWPOSCHANGED || message==WM_DPICHANGED) {
        LRESULT result=def(h,message,w,l);
        geek_search_region(h);geek_search_paint_frame(h);return result;
    }
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==WM_PAINT) {
        LRESULT result=def(h,message,w,l);
        if(message==WM_PAINT)geek_surface_finish(h);
        geek_search_paint_frame(h);return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_SEARCH_REMOVE_SUBCLASS remove=(GEEK_SEARCH_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_search_subclass,id);
        if(geek_search_state.window==h)geek_search_state=(GEEK_SEARCH_STATE){0};
        return def(h,message,w,l);
    }
    return def(h,message,w,l);
}

static void geek_search_install(HWND h) {
    if(!h || geek_search_state.window==h)return;
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(!add(h,geek_search_subclass,0x47534544,0))return;
    geek_search_state=(GEEK_SEARCH_STATE){0};geek_search_state.window=h;
    LONG style=GetLong(h,GWL_STYLE),extended=GetLong(h,GWL_EXSTYLE);
    if(style&WS_BORDER)SetLong(h,GWL_STYLE,style&~WS_BORDER);
    if(extended&WS_EX_CLIENTEDGE)SetLong(h,GWL_EXSTYLE,extended&~WS_EX_CLIENTEDGE);
    geek_search_frame_changed(h);
}

__declspec(dllexport) void ui_search_layout(void *view) {
    if(!view)return;
    ((void(*)(void*))0x14003BCB0ULL)(view);
    HWND parent=*(HWND*)((char*)view+0x40);
    HWND list=*(HWND*)((char*)view+0x370);
    HWND edit=*(HWND*)((char*)view+0xA90);
    HWND overlay=*(HWND*)((char*)view+0x740);
    RECT client;
    if(!parent || !list || !edit || !Client(parent,&client))return;
    int dpi=dpi_of(parent),width=client.right-client.left,height=client.bottom-client.top;
    int band=px(54,dpi),inset=px(12,dpi),search_height=px(38,dpi),top=px(8,dpi);
    int search_width=width-2*inset;if(search_width<1)search_width=1;
    int list_height=height-band;if(list_height<0)list_height=0;
    geek_surface_install(parent,list,edit);
    geek_search_install(edit);
    API(SetWindowPos,0x1402572F0)(edit,0,client.left+inset,client.top+top,search_width,search_height,
        SWP_NOZORDER|SWP_NOACTIVATE);
    API(SetWindowPos,0x1402572F0)(list,0,client.left,client.top+band,width,list_height,
        SWP_NOZORDER|SWP_NOACTIVATE);
    /* The original empty/status overlay is a child of the list. Recenter it
       after the list has the new height, retaining its native 200px height. */
    RECT overlay_rect,list_client;
    if(overlay && geek_search_window_rect(overlay,&overlay_rect) && Client(list,&list_client)) {
        int overlay_height=overlay_rect.bottom-overlay_rect.top;
        API(SetWindowPos,0x1402572F0)(overlay,0,0,(list_client.bottom-list_client.top-overlay_height)/2,
            list_client.right-list_client.left,overlay_height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    geek_search_region(edit);geek_search_paint_frame(edit);
}

#endif
