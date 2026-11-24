#ifndef GEEK_NATIVE_SURFACE_H
#define GEEK_NATIVE_SURFACE_H

#include <uxtheme.h>

/* The native view, list and EDIT keep their HWNDs and message handlers.  GDI
   produces RGB pixels but can leave their alpha byte clear on the toolkit's
   composited surfaces.  Copy completed RGB drawing into a 32-bit buffer and
   publish it with alpha 255; never redraw text, selection, caret or IME here. */
typedef HANDLE (WINAPI *GEEK_SURFACE_BEGIN)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *GEEK_SURFACE_ALPHA)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_SURFACE_END)(HANDLE,BOOL);
typedef BOOL (WINAPI *GEEK_SURFACE_REMOVE)(HWND,SUBCLASSPROC,UINT_PTR);
typedef HRGN (WINAPI *GEEK_SURFACE_RECT_REGION)(int,int,int,int);
typedef int (WINAPI *GEEK_SURFACE_WINDOW_REGION)(HWND,HRGN);
typedef int (WINAPI *GEEK_SURFACE_OFFSET_REGION)(HRGN,int,int);
typedef int (WINAPI *GEEK_SURFACE_CLIP_REGION)(HDC,HRGN,int);
typedef int (WINAPI *GEEK_SURFACE_COMBINE_REGION)(HRGN,HRGN,HRGN,int);
typedef HWND (WINAPI *GEEK_SURFACE_GET_WINDOW)(HWND,UINT);
typedef BOOL (WINAPI *GEEK_SURFACE_IS_VISIBLE)(HWND);

typedef struct {
    HWND view;
    HWND list;
    HWND edit;
    HBRUSH edit_brush[2];
    int palette;
    BOOL synchronizing;
    BOOL view_installed;
    BOOL list_installed;
} GEEK_SURFACE_STATE;

static GEEK_SURFACE_STATE geek_surface_state;

static COLORREF geek_surface_background(void) {
    return dark()?RGB(20,18,24):RGB(246,244,249);
}
static COLORREF geek_surface_edit_background(void) {
    return dark()?RGB(33,31,38):RGB(255,253,255);
}

static BOOL geek_surface_buffer(HDC target,const RECT *r,COLORREF color,BOOL fill,
                                BOOL nonclient,const RECT *exclude) {
    GEEK_SURFACE_BEGIN begin=(GEEK_SURFACE_BEGIN)(*(void**)0x140257488ULL);
    GEEK_SURFACE_ALPHA alpha=(GEEK_SURFACE_ALPHA)(*(void**)0x140257498ULL);
    GEEK_SURFACE_END end=(GEEK_SURFACE_END)(*(void**)0x140257490ULL);
    if(!target || !r || r->right<=r->left || r->bottom<=r->top || !begin || !alpha || !end)return FALSE;
    BP_PAINTPARAMS parameters={sizeof(parameters),nonclient?BPPF_NONCLIENT:0,(RECT*)exclude,0};
    HDC dc=0;HANDLE buffer=begin(target,r,2,&parameters,&dc);
    if(!buffer || !dc) {
        if(buffer)end(buffer,FALSE);
        return FALSE;
    }
    if(fill)solid(dc,(RECT*)r,color);
    else if(!API(BitBlt,0x140256328)(dc,r->left,r->top,r->right-r->left,r->bottom-r->top,
                                   target,r->left,r->top,SRCCOPY)) {
        end(buffer,FALSE);return FALSE;
    }
    if(FAILED(alpha(buffer,0,255))) {end(buffer,FALSE);return FALSE;}
    return SUCCEEDED(end(buffer,TRUE));
}

/* Target-DC clipping is retained by BeginBufferedPaint.  In particular the
   search frame's excluded text client must stay excluded on commit. */
static void geek_surface_make_opaque(HDC dc,const RECT *r) {
    geek_surface_buffer(dc,r,0,FALSE,FALSE,0);
}

static void geek_surface_make_frame_opaque(HDC dc,const RECT *r,const RECT *exclude) {
    geek_surface_buffer(dc,r,0,FALSE,TRUE,exclude);
}

static void geek_surface_exclude_child(HWND parent,HDC dc,HWND child,BOOL rounded_child) {
    RECT r;
    if(!child || !API(GetWindowRect,0x140257208)(child,&r))return;
    MapPoints(0,parent,(POINT*)&r,2);
    if(rounded_child) {
        /* A rounded window region leaves the four corners to its
           parent. Excluding the bounding RECT leaves their startup pixels
           untouched, so a dark first paint can retain white corner wedges. */
        HMODULE gdi=Module(L"gdi32.dll");
        GEEK_SURFACE_RECT_REGION create=(GEEK_SURFACE_RECT_REGION)Proc(gdi,"CreateRectRgn");
        GEEK_SURFACE_WINDOW_REGION get=(GEEK_SURFACE_WINDOW_REGION)Proc(Module(L"user32.dll"),"GetWindowRgn");
        GEEK_SURFACE_OFFSET_REGION offset=(GEEK_SURFACE_OFFSET_REGION)Proc(gdi,"OffsetRgn");
        GEEK_SURFACE_CLIP_REGION select=(GEEK_SURFACE_CLIP_REGION)Proc(gdi,"ExtSelectClipRgn");
        GEEK_SURFACE_COMBINE_REGION combine=(GEEK_SURFACE_COMBINE_REGION)Proc(gdi,"CombineRgn");
        if(create && get && offset && select && combine) {
            HRGN region=create(0,0,0,0);
            if(region) {
                BOOL excluded=FALSE;
                if(get(child,region)!=ERROR) {
                    /* SetWindowRgn's stored region may extend one pixel past
                       the HWND. Only its intersection with actual bounds can
                       hide parent pixels. */
                    HRGN bounds=create(0,0,r.right-r.left,r.bottom-r.top);
                    if(bounds) {
                        excluded=combine(region,region,bounds,RGN_AND)!=ERROR &&
                                 offset(region,r.left,r.top)!=ERROR && select(dc,region,RGN_DIFF)!=ERROR;
                        Delete(bounds);
                    }
                }
                Delete(region);
                if(excluded)return;
            }
        }
    }
    API(ExcludeClipRect,0x1402561F0)(dc,r.left,r.top,r.right,r.bottom);
}

static void geek_surface_exclude_children(HWND h,HDC dc) {
    if(h!=geek_surface_state.view)return;
    /* ui_search_layout can run for a hidden ListView before the visible
       sibling.  The old singleton pointers then let the view's background
       paint cover the real list after sorting.  Exclude every visible child
       currently owned by this view, so a stale binding cannot paint over a
       child surface. */
    HMODULE user=Module(L"user32.dll");
    GEEK_SURFACE_GET_WINDOW get_window=(GEEK_SURFACE_GET_WINDOW)Proc(user,"GetWindow");
    GEEK_SURFACE_IS_VISIBLE is_visible=(GEEK_SURFACE_IS_VISIBLE)Proc(user,"IsWindowVisible");
    if(get_window && is_visible) {
        HWND child=get_window(h,GW_CHILD);
        while(child) {
            HWND next=get_window(child,GW_HWNDNEXT);
            if(is_visible(child))geek_surface_exclude_child(h,dc,child,FALSE);
            child=next;
        }
        return;
    }
    geek_surface_exclude_child(h,dc,geek_surface_state.edit,TRUE);
    geek_surface_exclude_child(h,dc,geek_surface_state.list,FALSE);
}

static BOOL geek_surface_paint_background(HWND h,HDC dc) {
    RECT r;
    if(!dc || !Client(h,&r))return FALSE;
    int saved=Save(dc);if(!saved)return FALSE;
    geek_surface_exclude_children(h,dc);
    COLORREF color=geek_surface_background();
    if(!geek_surface_buffer(dc,&r,color,TRUE,FALSE,0))solid(dc,&r,color);
    Restore(dc,saved);
    return TRUE;
}

static void geek_surface_finish(HWND h) {
    RECT r;
    if(!Client(h,&r))return;
    HDC dc=API(GetDC,0x1402571C8)(h);
    if(!dc)return;
    if(h==geek_surface_state.view) {
        /* The view owns only the gaps around its existing child controls.
           Repaint these even when native WM_PAINT skipped WM_ERASEBKGND. */
        geek_surface_paint_background(h,dc);
    } else {
        int saved=Save(dc);
        if(saved) {
            geek_surface_make_opaque(dc,&r);
            Restore(dc,saved);
        }
    }
    API(ReleaseDC,0x1402571D0)(h,dc);
}

static void geek_surface_sync_palette(BOOL force) {
    int palette=dark()?2:1;
    if((geek_surface_state.palette==palette && !force) || geek_surface_state.synchronizing)return;
    geek_surface_state.palette=palette;
    geek_surface_state.synchronizing=TRUE;
    HWND list=geek_surface_state.list;
    if(list) {
        /* Reuse Geek's own capability-checked dark-mode permission helper.
           The native alternate theme path skips Explorer, leaving a light
           scrollbar after dark mode changes.  Keep the same native controls. */
        ((void(*)(HWND))0x14000C2CCULL)(list);
        API(SetWindowTheme,0x140257480)(list,palette==2?L"DarkMode_Explorer":L"Explorer",0);
        HWND header=(HWND)Send(list,LVM_GETHEADER,0,0);
        if(header) {
            ((void(*)(HWND))0x14000C2CCULL)(header);
            HRESULT result=API(SetWindowTheme,0x140257480)(header,palette==2?L"DarkMode_ItemsView":L"ItemsView",0);
            if(FAILED(result) && palette==2)API(SetWindowTheme,0x140257480)(header,L"ItemsView",0);
        }
        /* Geek restores a one-pixel static edge only in light mode. Keep the
           same borderless native list in both palettes, including its native
           scrollbar. The guard above covers synchronous frame messages. */
        LONG extended=GetLong(list,GWL_EXSTYLE);
        if(extended&WS_EX_STATICEDGE) {
            SetLong(list,GWL_EXSTYLE,extended&~WS_EX_STATICEDGE);
            API(SetWindowPos,0x1402572F0)(list,0,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
        /* Theme handlers can restore the original list colors. Apply the
           selected palette after those synchronous handlers complete. */
        Send(list,LVM_SETBKCOLOR,0,geek_surface_background());
        Send(list,LVM_SETTEXTBKCOLOR,0,CLR_NONE);
    }
    if(geek_surface_state.view)Invalidate(geek_surface_state.view,0,TRUE);
    if(list)Invalidate(list,0,TRUE);
    if(geek_surface_state.edit)Invalidate(geek_surface_state.edit,0,TRUE);
    geek_surface_state.synchronizing=FALSE;
}

static LRESULT CALLBACK geek_surface_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if((message==WM_CTLCOLOREDIT || message==WM_CTLCOLORSTATIC) &&
       h==geek_surface_state.view && (HWND)l==geek_surface_state.edit && w) {
        int index=dark()?1:0;
        COLORREF back=geek_surface_edit_background();
        HBRUSH brush=geek_surface_state.edit_brush[index];
        if(!brush)geek_surface_state.edit_brush[index]=brush=Brush(back);
        if(brush) {
            TextColor((HDC)w,index?RGB(236,231,240):RGB(31,28,35));
            API(SetBkColor,0x1402563A0)((HDC)w,back);BkMode((HDC)w,OPAQUE);
            return (LRESULT)brush;
        }
    }
    if(message==WM_ERASEBKGND) {
        geek_surface_sync_palette(FALSE);
        if(geek_surface_paint_background(h,(HDC)w))return 1;
        return def(h,message,w,l);
    }
    UINT native_theme_message=*(const UINT*)0x14033796CULL;
    BOOL theme=message==WM_THEMECHANGED || message==WM_SETTINGCHANGE || message==WM_SYSCOLORCHANGE ||
        (native_theme_message>=WM_USER && message==native_theme_message);
    if(message==WM_PAINT || theme || message==WM_SIZE) {
        if(!theme)geek_surface_sync_palette(FALSE);
        LRESULT result=def(h,message,w,l);
        /* Native/theme handlers can change palette state synchronously. */
        geek_surface_sync_palette(theme);
        /* The list draws each owner-drawn row into an opaque buffer and
           erases its own client. A second full-client GetDC copy here can
           overwrite a freshly repainted list after scrolling or sorting. */
        /* Diagnostic build: let the native view finish WM_PAINT without a
           second GetDC pass that can overwrite freshly painted list rows. */
        return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_SURFACE_REMOVE remove=(GEEK_SURFACE_REMOVE)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_surface_subclass,id);
        if(h==geek_surface_state.view) {
            if(geek_surface_state.edit_brush[0])Delete(geek_surface_state.edit_brush[0]);
            if(geek_surface_state.edit_brush[1])Delete(geek_surface_state.edit_brush[1]);
            geek_surface_state=(GEEK_SURFACE_STATE){0};
        } else if(h==geek_surface_state.list) {
            geek_surface_state.list=0;geek_surface_state.list_installed=FALSE;
        }
    }
    return def(h,message,w,l);
}

static void geek_surface_install(HWND view,HWND list,HWND edit) {
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    HMODULE user=Module(L"user32.dll");
    GEEK_SURFACE_IS_VISIBLE is_visible=(GEEK_SURFACE_IS_VISIBLE)Proc(user,"IsWindowVisible");
    BOOL current_visible=geek_surface_state.list && (!is_visible || is_visible(geek_surface_state.list));
    BOOL new_visible=list && (!is_visible || is_visible(list));
    /* Keep a visible list bound when a hidden sibling calls the layout hook.
       If the previous binding is hidden, prefer the newly visible list. */
    if(geek_surface_state.list && current_visible && !new_visible)list=geek_surface_state.list;
    if(list!=geek_surface_state.list)geek_surface_state.palette=0;
    geek_surface_state.view=view;geek_surface_state.list=list;geek_surface_state.edit=edit;
    if(view && !geek_surface_state.view_installed)
        geek_surface_state.view_installed=add(view,geek_surface_subclass,0x47535657,0);
    if(list && !geek_surface_state.list_installed)
        geek_surface_state.list_installed=add(list,geek_surface_subclass,0x47534C56,0);
    geek_surface_sync_palette(FALSE);
}

#endif
