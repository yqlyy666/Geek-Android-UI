#ifndef GEEK_NATIVE_CHROME_H
#define GEEK_NATIVE_CHROME_H

/* Included after native-ui.c's native import macros and drawing helpers.
   Keep the original caption/frame styles and WM_NCCALCSIZE contract.  Only
   the existing non-client pixels and caption-button input are replaced. */

typedef struct {
    HWND window;
    int hot;
    int pressed;
    BOOL active;
} GEEK_CHROME_STATE;

typedef struct {
    RECT window;
    RECT client;
    RECT caption;
    RECT minimize;
    RECT maximize;
    RECT close;
    int width;
    int height;
    int dpi;
    LONG style;
    BOOL maximized;
} GEEK_CHROME_LAYOUT;

typedef HRESULT (WINAPI *GEEK_DWM_ATTRIBUTE)(HWND,DWORD,LPCVOID,DWORD);
typedef HRESULT (WINAPI *GEEK_DWM_EXTEND)(HWND,const void*);
typedef HANDLE (WINAPI *GEEK_BEGIN_PAINT)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *GEEK_ALPHA_PAINT)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_END_PAINT)(HANDLE,BOOL);
typedef int (WINAPI *GEEK_METRIC_DPI)(int,UINT);
typedef BOOL (WINAPI *GEEK_REMOVE_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR);
typedef HWND (WINAPI *GEEK_CHILD_WINDOW)(HWND,UINT);
typedef BOOL (WINAPI *GEEK_VISIBLE_WINDOW)(HWND);
typedef HMENU (WINAPI *GEEK_WINDOW_MENU)(HWND);
typedef struct {
    DWORD size;
    DWORD flags;
    const RECT *exclude;
    const BLENDFUNCTION *blend;
} GEEK_CHROME_PAINT_PARAMS;

static GEEK_CHROME_STATE geek_chrome_state;

/* The toolkit's wrapper has six arguments, unlike the two-argument DWM API.
   Keep its loader, return value and ABI while removing its glass margins. */
__declspec(dllexport) HRESULT ui_opaque_frame(void *native,HWND h,int left,int top,int right,int bottom) {
    return ((HRESULT(*)(void*,HWND,int,int,int,int))0x14011EAF0ULL)(native,h,0,0,0,0);
}

static BOOL geek_chrome_inside(const RECT *r,POINT p) {
    return p.x>=r->left && p.x<r->right && p.y>=r->top && p.y<r->bottom;
}

static int geek_chrome_metric(int index,int dpi) {
    GEEK_METRIC_DPI fn=(GEEK_METRIC_DPI)Proc(Module(L"user32.dll"),"GetSystemMetricsForDpi");
    if(fn)return fn(index,(UINT)dpi);
    return API(GetSystemMetrics,0x1402571B8)(index);
}

static BOOL geek_chrome_layout(HWND h,GEEK_CHROME_LAYOUT *g) {
    if(!API(GetWindowRect,0x140257208)(h,&g->window))return FALSE;
    RECT client;
    if(!Client(h,&client))return FALSE;
    POINT start={client.left,client.top},end={client.right,client.bottom};
    if(!ToScreen(h,&start) || !ToScreen(h,&end))return FALSE;
    g->client.left=start.x-g->window.left;
    g->client.top=start.y-g->window.top;
    g->client.right=end.x-g->window.left;
    g->client.bottom=end.y-g->window.top;
    g->width=g->window.right-g->window.left;
    g->height=g->window.bottom-g->window.top;
    g->dpi=dpi_of(h);
    g->style=GetLong(h,GWL_STYLE);
    g->maximized=API(IsZoomed,0x140256F50)(h);
    int top=geek_chrome_metric(SM_CYFRAME,g->dpi)+geek_chrome_metric(SM_CXPADDEDBORDER,g->dpi);
    int caption=geek_chrome_metric(SM_CYCAPTION,g->dpi);
    if(top<0)top=0;
    if(caption<px(18,g->dpi))caption=px(23,g->dpi);
    int bottom=top+caption;
    /* A real HMENU, if present, remains below this caption and continues to
       receive its original non-client painting and hit testing. */
    if(bottom>g->client.top)bottom=g->client.top;
    if(bottom<=top || g->width<=0 || g->height<=0)return FALSE;
    int side=g->client.left;
    if(side<1)side=1;
    int right=g->width-side;
    int bw=px(44,g->dpi);
    g->caption=(RECT){side,top,right,bottom};
    g->close=(RECT){right-bw,top,right,bottom};
    g->maximize=(RECT){right-2*bw,top,right-bw,bottom};
    g->minimize=(RECT){right-3*bw,top,right-2*bw,bottom};
    return TRUE;
}

static BOOL geek_chrome_button_enabled(const GEEK_CHROME_LAYOUT *g,int hit) {
    if(hit==HTCLOSE)return (g->style&WS_SYSMENU)!=0;
    if(hit==HTMINBUTTON)return (g->style&WS_MINIMIZEBOX)!=0;
    if(hit==HTMAXBUTTON)return (g->style&WS_MAXIMIZEBOX)!=0;
    return FALSE;
}

static int geek_chrome_button_at(HWND h,POINT screen) {
    GEEK_CHROME_LAYOUT g;
    if(!geek_chrome_layout(h,&g))return 0;
    screen.x-=g.window.left;screen.y-=g.window.top;
    int hit=geek_chrome_inside(&g.close,screen)?HTCLOSE:
            geek_chrome_inside(&g.maximize,screen)?HTMAXBUTTON:
            geek_chrome_inside(&g.minimize,screen)?HTMINBUTTON:0;
    return geek_chrome_button_enabled(&g,hit)?hit:0;
}

static void geek_chrome_line(HDC dc,int x1,int y1,int x2,int y2) {
    API(MoveToEx,0x1402561A8)(dc,x1,y1,0);
    API(LineTo,0x140256168)(dc,x2,y2);
}

static void geek_chrome_button(HDC dc,const GEEK_CHROME_LAYOUT *g,RECT r,int hit,BOOL isdark) {
    BOOL enabled=geek_chrome_button_enabled(g,hit);
    BOOL hot=enabled && geek_chrome_state.hot==hit;
    BOOL pressed=hot && geek_chrome_state.pressed==hit;
    COLORREF ink=isdark?RGB(232,226,239):RGB(62,56,73);
    if(!enabled)ink=isdark?RGB(100,94,108):RGB(180,174,188);
    if(hot) {
        COLORREF hover=hit==HTCLOSE?(pressed?RGB(186,42,67):RGB(211,54,79)):
            (isdark?(pressed?RGB(77,65,94):RGB(60,51,72)):
                    (pressed?RGB(221,210,237):RGB(232,223,245)));
        RECT fill={r.left+px(3,g->dpi),r.top+px(2,g->dpi),r.right-px(3,g->dpi),r.bottom-px(2,g->dpi)};
        rounded(dc,fill,px(9,g->dpi),hover,hover);
        if(hit==HTCLOSE)ink=RGB(255,255,255);
    }
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    int a=px(5,g->dpi),stroke=px(1,g->dpi);
    if(stroke<1)stroke=1;
    HPEN pen=Pen(PS_SOLID,stroke,ink);
    HGDIOBJ old=Select(dc,pen);
    if(hit==HTMINBUTTON)geek_chrome_line(dc,cx-a,cy+px(3,g->dpi),cx+a+1,cy+px(3,g->dpi));
    else if(hit==HTCLOSE) {
        geek_chrome_line(dc,cx-a,cy-a,cx+a+1,cy+a+1);
        geek_chrome_line(dc,cx+a,cy-a,cx-a-1,cy+a+1);
    } else if(g->maximized) {
        int shift=px(2,g->dpi);
        geek_chrome_line(dc,cx-a+shift,cy-a,cx+a,cy-a);
        geek_chrome_line(dc,cx+a,cy-a,cx+a,cy+a-shift);
        geek_chrome_line(dc,cx+a,cy+a-shift,cx+a-shift,cy+a-shift);
        geek_chrome_line(dc,cx-a,cy-a+shift,cx+a-shift,cy-a+shift);
        geek_chrome_line(dc,cx+a-shift,cy-a+shift,cx+a-shift,cy+a);
        geek_chrome_line(dc,cx+a-shift,cy+a,cx-a,cy+a);
        geek_chrome_line(dc,cx-a,cy+a,cx-a,cy-a+shift);
    } else {
        geek_chrome_line(dc,cx-a,cy-a,cx+a,cy-a);
        geek_chrome_line(dc,cx+a,cy-a,cx+a,cy+a);
        geek_chrome_line(dc,cx+a,cy+a,cx-a,cy+a);
        geek_chrome_line(dc,cx-a,cy+a,cx-a,cy-a);
    }
    Select(dc,old);Delete(pen);
}

/* The side and bottom frame have the same per-pixel alpha contract as the
   caption. Plain GDI FillRect can leave these surfaces transparent in dark
   mode. Buffer each non-client rectangle without touching any client pixel. */
static void geek_chrome_fill_opaque(HDC target,const RECT *r,COLORREF color,BOOL nonclient) {
    if(r->right<=r->left || r->bottom<=r->top)return;
    HDC dc=0;
    GEEK_CHROME_PAINT_PARAMS params={sizeof(params),nonclient?4:0,0,0};
    HANDLE buffer=((GEEK_BEGIN_PAINT)(*(void**)0x140257488ULL))(target,r,2,&params,&dc);
    if(buffer && dc) {
        RECT fill=*r;
        solid(dc,&fill,color);
        ((GEEK_ALPHA_PAINT)(*(void**)0x140257498ULL))(buffer,0,255);
        ((GEEK_END_PAINT)(*(void**)0x140257490ULL))(buffer,TRUE);
    } else {
        if(buffer)((GEEK_END_PAINT)(*(void**)0x140257490ULL))(buffer,FALSE);
        RECT fill=*r;
        solid(target,&fill,color);
    }
}

/* Docking bars and the central view do not cover every main-client pixel.
   The main frame owns these gaps. Explicitly exclude every visible direct
   child so none of its controls or text can be painted over here. */
static void geek_chrome_background(HWND h,HDC supplied) {
    GEEK_CHILD_WINDOW child=(GEEK_CHILD_WINDOW)Proc(Module(L"user32.dll"),"GetWindow");
    GEEK_VISIBLE_WINDOW visible=(GEEK_VISIBLE_WINDOW)Proc(Module(L"user32.dll"),"IsWindowVisible");
    if(!child || !visible)return;
    RECT client;
    if(!Client(h,&client))return;
    HDC dc=supplied?supplied:API(GetDC,0x1402571C8)(h);
    if(!dc)return;
    int saved=Save(dc);
    if(saved) {
        HWND current=child(h,GW_CHILD);
        for(int count=0;current && count<256;++count) {
            RECT r;
            if(visible(current) && API(GetWindowRect,0x140257208)(current,&r)) {
                MapPoints(0,h,(POINT*)&r,2);
                API(ExcludeClipRect,0x1402561F0)(dc,r.left,r.top,r.right,r.bottom);
            }
            current=child(current,GW_HWNDNEXT);
        }
        geek_chrome_fill_opaque(dc,&client,dark()?RGB(20,18,24):RGB(246,244,249),FALSE);
        Restore(dc,saved);
    }
    if(!supplied)API(ReleaseDC,0x1402571D0)(h,dc);
}

static void geek_chrome_paint(HWND h) {
    GEEK_CHROME_LAYOUT g;
    if(!geek_chrome_layout(h,&g))return;
    HDC dc=API(GetWindowDC,0x1402573F8)(h);
    if(!dc)return;
    HDC target=dc;
    RECT top={0,0,g.width,g.caption.bottom};
    HDC buffered=0;
    GEEK_CHROME_PAINT_PARAMS params={sizeof(params),4,0,0}; /* BPPF_NONCLIENT */
    HANDLE buffer=((GEEK_BEGIN_PAINT)(*(void**)0x140257488ULL))(target,&top,2,&params,&buffered);
    if(buffer && !buffered) {
        ((GEEK_END_PAINT)(*(void**)0x140257490ULL))(buffer,FALSE);
        buffer=0;
    }
    if(buffer && buffered)dc=buffered;
    int saved=Save(dc);BOOL isdark=dark();
    COLORREF back=isdark?RGB(20,18,24):RGB(246,244,249);
    solid(dc,&top,back);
    RECT left={0,g.caption.bottom,g.client.left,g.height};
    RECT right={g.client.right,g.caption.bottom,g.width,g.height};
    RECT bottom={g.client.left,g.client.bottom,g.client.right,g.height};
    RECT gap={g.client.left,g.caption.bottom,g.client.right,g.client.top};
    /* A standard HMENU owns its NC pixels. Geek uses the original XTP child
       menu instead, so any extra NC band is frame background. */
    if(gap.bottom>gap.top) {
        GEEK_WINDOW_MENU menu=(GEEK_WINDOW_MENU)Proc(Module(L"user32.dll"),"GetMenu");
        if(menu && !menu(h))geek_chrome_fill_opaque(target,&gap,back,TRUE);
    }
    geek_chrome_fill_opaque(target,&left,back,TRUE);
    geek_chrome_fill_opaque(target,&right,back,TRUE);
    geek_chrome_fill_opaque(target,&bottom,back,TRUE);
    HFONT font=font_at(g.dpi,12,FW_MEDIUM);
    Select(dc,font);BkMode(dc,TRANSPARENT);
    TextColor(dc,isdark?RGB(235,229,242):RGB(65,57,78));
    RECT title={g.caption.left+px(15,g.dpi),g.caption.top,g.minimize.left-px(10,g.dpi),g.caption.bottom};
    Text(dc,L"Geek Uninstaller",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    geek_chrome_button(dc,&g,g.minimize,HTMINBUTTON,isdark);
    geek_chrome_button(dc,&g,g.maximize,HTMAXBUTTON,isdark);
    geek_chrome_button(dc,&g,g.close,HTCLOSE,isdark);
    Restore(dc,saved);Delete(font);
    if(buffer) {
        ((GEEK_ALPHA_PAINT)(*(void**)0x140257498ULL))(buffer,0,255);
        ((GEEK_END_PAINT)(*(void**)0x140257490ULL))(buffer,TRUE);
    }
    API(ReleaseDC,0x1402571D0)(h,target);
}

static void geek_chrome_dwm(HWND h) {
    HMODULE dwm=Module(L"dwmapi.dll");
    if(!dwm)dwm=API(LoadLibraryW,0x140256A58)(L"dwmapi.dll");
    GEEK_DWM_EXTEND extend=dwm?(GEEK_DWM_EXTEND)Proc(dwm,"DwmExtendFrameIntoClientArea"):0;
    if(extend) {
        int margins[4]={0,0,0,0};
        extend(h,margins);
    }
    GEEK_DWM_ATTRIBUTE set=dwm?(GEEK_DWM_ATTRIBUTE)Proc(dwm,"DwmSetWindowAttribute"):0;
    if(!set)return;
    int policy=1; /* DWMWA_NCRENDERING_POLICY / DWMNCRP_DISABLED */
    set(h,2,&policy,sizeof(policy));
    BOOL allow=TRUE;
    set(h,4,&allow,sizeof(allow)); /* DWMWA_ALLOW_NCPAINT */
    int corner=2; /* DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_ROUND */
    set(h,33,&corner,sizeof(corner));
    COLORREF color=dark()?RGB(20,18,24):RGB(246,244,249);
    set(h,34,&color,sizeof(color)); /* DWMWA_BORDER_COLOR */
    set(h,35,&color,sizeof(color)); /* DWMWA_CAPTION_COLOR (fallback) */
}

static POINT geek_chrome_message_point(HWND h,UINT message,LPARAM l) {
    POINT p={(short)LOWORD(l),(short)HIWORD(l)};
    if(message==WM_MOUSEMOVE || message==WM_LBUTTONUP)ToScreen(h,&p);
    return p;
}

static LRESULT CALLBACK geek_chrome_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    /* The native ColorScheme command can lose a physical pixel while its
       themed frame is recalculated. Keep the user's normal outer size only
       around those three menu commands; all native theme work still runs. */
    if(h==geek_chrome_state.window && message==WM_COMMAND && !l && !HIWORD(w) &&
       LOWORD(w)>=0x8099 && LOWORD(w)<=0x809B) {
        RECT before,after;
        LONG style=GetLong(h,GWL_STYLE);
        BOOL retain=!(style&(WS_MINIMIZE|WS_MAXIMIZE)) &&
            API(GetWindowRect,0x140257208)(h,&before) &&
            before.right>before.left && before.bottom>before.top;
        LRESULT result=def(h,message,w,l);
        if(retain && !(GetLong(h,GWL_STYLE)&(WS_MINIMIZE|WS_MAXIMIZE)) &&
           API(GetWindowRect,0x140257208)(h,&after) &&
           (after.right-after.left!=before.right-before.left ||
            after.bottom-after.top!=before.bottom-before.top)) {
            API(SetWindowPos,0x1402572F0)(h,0,0,0,before.right-before.left,before.bottom-before.top,
                SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        return result;
    }
    /* DefWindowProc's private caption-only paint requests must not restore
       native glyphs over the caption that this subclass owns. */
    if(message==0x00AE || message==0x00AF)return 0;
    if(message==WM_ERASEBKGND) {
        geek_chrome_background(h,(HDC)w);return 1;
    }
    if(message==WM_PAINT) {
        LRESULT result=def(h,message,w,l);
        geek_chrome_background(h,0);return result;
    }
    if(message==WM_NCHITTEST) {
        LRESULT native=def(h,message,w,l);
        /* Never replace the native resize edges/corners or client-area hit
           testing.  Native HTCAPTION keeps drag, double click and system menu. */
        if(native>=HTLEFT && native<=HTBOTTOMRIGHT)return native;
        GEEK_CHROME_LAYOUT g;
        POINT screen=geek_chrome_message_point(h,message,l);
        if(geek_chrome_layout(h,&g)) {
            POINT p={screen.x-g.window.left,screen.y-g.window.top};
            if(geek_chrome_inside(&g.caption,p)) {
                int button=geek_chrome_button_at(h,screen);
                return button?button:HTCAPTION;
            }
        }
        return native;
    }
    if(message==WM_NCLBUTTONDOWN) {
        int button=geek_chrome_button_at(h,geek_chrome_message_point(h,message,l));
        if(button) {
            geek_chrome_state.pressed=button;geek_chrome_state.hot=button;
            API(SetCapture,0x140256EB0)(h);
            if(API(GetCapture,0x140256EB8)()!=h)geek_chrome_state.pressed=0;
            geek_chrome_paint(h);return 0;
        }
    }
    if((message==WM_LBUTTONUP || message==WM_NCLBUTTONUP) && geek_chrome_state.pressed) {
        int down=geek_chrome_state.pressed;
        int up=geek_chrome_button_at(h,geek_chrome_message_point(h,message,l));
        geek_chrome_state.pressed=0;geek_chrome_state.hot=up;
        if(API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
        geek_chrome_paint(h);
        if(down==up) {
            UINT command=down==HTCLOSE?SC_CLOSE:down==HTMINBUTTON?SC_MINIMIZE:
                (API(IsZoomed,0x140256F50)(h)?SC_RESTORE:SC_MAXIMIZE);
            Send(h,WM_SYSCOMMAND,command,0);
        }
        return 0;
    }
    if(message==WM_NCMOUSEMOVE || (message==WM_MOUSEMOVE && geek_chrome_state.pressed)) {
        int hot=geek_chrome_button_at(h,geek_chrome_message_point(h,message,l));
        if(hot!=geek_chrome_state.hot){geek_chrome_state.hot=hot;geek_chrome_paint(h);}
        if(message==WM_NCMOUSEMOVE) {
            TRACK_MOUSE track=(TRACK_MOUSE)Proc(Module(L"user32.dll"),"TrackMouseEvent");
            TRACKMOUSEEVENT event={sizeof(event),TME_LEAVE|TME_NONCLIENT,h,0};
            if(track)track(&event);
        }
        /* Calling the standard NC mouse handler for our buttons could paint
           native colored glyphs over them. Other NC targets retain defaults. */
        if(hot || geek_chrome_state.pressed)return 0;
    }
    if(message==WM_NCMOUSELEAVE) {
        if(geek_chrome_state.hot){geek_chrome_state.hot=0;geek_chrome_paint(h);}
        return 0;
    }
    if(message==WM_CANCELMODE || message==WM_CAPTURECHANGED) {
        if(geek_chrome_state.pressed || geek_chrome_state.hot) {
            geek_chrome_state.pressed=0;geek_chrome_state.hot=0;
            if(message==WM_CANCELMODE && API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
            geek_chrome_paint(h);
        }
    }
    if(message==WM_SETTEXT) {
        LRESULT result=def(h,message,w,(LPARAM)L"Geek Uninstaller");
        geek_chrome_paint(h);return result;
    }
    if(message==WM_NCPAINT) {
        /* Preserve any real menu/scrollbar NC drawing, then replace only the
           caption and outside frame. No client pixels are painted here. */
        def(h,message,w,l);geek_chrome_paint(h);return 0;
    }
    if(message==WM_NCACTIVATE) {
        geek_chrome_state.active=w!=0;
        LRESULT result=def(h,message,w,-1);
        geek_chrome_paint(h);return result;
    }
    /* Geek broadcasts its own allocated message when ColorScheme changes.
       It does not send WM_THEMECHANGED, so synchronize the DWM border only
       after the original theme handler has applied the new palette. */
    UINT native_theme_message=*(const UINT*)0x14033796CULL;
    if(message==WM_DWMCOMPOSITIONCHANGED || message==WM_THEMECHANGED || message==WM_SETTINGCHANGE ||
       (native_theme_message>=WM_USER && message==native_theme_message)) {
        LRESULT result=def(h,message,w,l);
        geek_chrome_dwm(h);geek_chrome_paint(h);return result;
    }
    if(message==WM_SIZE || message==WM_WINDOWPOSCHANGED || message==WM_DPICHANGED) {
        LRESULT result=def(h,message,w,l);
        if(message==WM_DPICHANGED)geek_status_layout(h);
        if(message!=WM_SIZE || w!=SIZE_MINIMIZED)geek_chrome_paint(h);
        return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_chrome_subclass,id);
        geek_chrome_state.window=0;geek_chrome_state.pressed=0;geek_chrome_state.hot=0;
        return def(h,message,w,l);
    }
    return def(h,message,w,l);
}

__declspec(dllexport) void ui_restore_main(HWND h,RECT *position,int maximized,long long default_size) {
    /* Preserve the previously verified width clamp/default centering helper.
       Original ABI: HWND, RECT*, BOOL, packed CSize (width low32). */
    ((void(*)(HWND,RECT*,int,long long))0x140255988ULL)(h,position,maximized,default_size);
    if(!h || geek_chrome_state.window==h)return;
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(!add(h,geek_chrome_subclass,0x47434852,0))return;
    geek_chrome_state.window=h;geek_chrome_state.active=TRUE;
    geek_chrome_state.hot=0;geek_chrome_state.pressed=0;
    geek_chrome_dwm(h);
    geek_status_layout(h);
    API(SetWindowTextW,0x140257098)(h,L"Geek Uninstaller");
    API(RedrawWindow,0x140257260)(h,0,0,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);
    geek_chrome_paint(h);
}

#endif
