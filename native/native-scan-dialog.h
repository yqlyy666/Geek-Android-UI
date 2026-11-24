#ifndef GEEK_NATIVE_SCAN_DIALOG_H
#define GEEK_NATIVE_SCAN_DIALOG_H

/* The scan wizard keeps its MFC dialog, controls and command flow.  This
   layer only owns the pixels: an opaque themed surface, rounded child
   regions, a compact progress track and native tree/button text. */

typedef struct {
    HWND window;
    HWND tree;
    HWND progress;
    HWND finish;
    HWND cancel;
    HBRUSH brush[2];
    int palette;
    int region_width;
    int region_height;
    int region_dpi;
    BOOL close_hot;
    BOOL close_pressed;
    BOOL tracking;
} GEEK_SCAN_STATE;

typedef int (WINAPI *GEEK_SCAN_CLASS_NAME)(HWND,LPWSTR,int);
typedef HWND (WINAPI *GEEK_SCAN_PARENT)(HWND);
typedef int (WINAPI *GEEK_SCAN_TEXT)(HWND,LPWSTR,int);

typedef struct {
    RECT window;
    RECT client;
    RECT caption;
    RECT close;
    int width;
    int height;
    int dpi;
} GEEK_SCAN_LAYOUT;

static GEEK_SCAN_STATE geek_scan_state;

static COLORREF geek_scan_background(void) {
    return dark()?RGB(20,18,24):RGB(246,244,249);
}
static COLORREF geek_scan_panel(void) {
    return dark()?RGB(33,31,38):RGB(255,253,255);
}
static COLORREF geek_scan_text(void) {
    return dark()?RGB(236,231,240):RGB(31,28,35);
}
static COLORREF geek_scan_muted(void) {
    return dark()?RGB(185,177,194):RGB(108,102,115);
}
static HBRUSH geek_scan_brush(void) {
    int index=dark()?1:0;
    HBRUSH brush=geek_scan_state.brush[index];
    if(!brush)geek_scan_state.brush[index]=brush=Brush(geek_scan_background());
    return brush;
}

static BOOL geek_scan_class(HWND h,HWND parent,const WCHAR *expected,int length) {
    if(!h || !parent)return FALSE;
    GEEK_SCAN_CLASS_NAME name=(GEEK_SCAN_CLASS_NAME)Proc(Module(L"user32.dll"),"GetClassNameW");
    GEEK_SCAN_PARENT getparent=(GEEK_SCAN_PARENT)Proc(Module(L"user32.dll"),"GetParent");
    if(!name || !getparent || getparent(h)!=parent)return FALSE;
    WCHAR value[32]={0};
    if(name(h,value,32)!=length)return FALSE;
    for(int i=0;i<length;++i) {
        WCHAR c=value[i];if(c>=L'A' && c<=L'Z')c+=L'a'-L'A';
        if(c!=expected[i])return FALSE;
    }
    return TRUE;
}

static void geek_scan_region(HWND h) {
    RECT r;if(!h || !API(GetWindowRect,0x140257208)(h,&r))return;
    int width=r.right-r.left,height=r.bottom-r.top,dpi=dpi_of(h);
    if(width==geek_scan_state.region_width && height==geek_scan_state.region_height &&
       dpi==geek_scan_state.region_dpi)return;
    ROUND_REGION make=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
    if(!make || width<=0 || height<=0)return;
    HRGN region=make(0,0,width+1,height+1,px(22,dpi),px(22,dpi));
    if(region) {
        /* SetWindowRgn may synchronously re-enter WM_WINDOWPOSCHANGED. Publish
           the dimensions before the call so that re-entrant sizing does not
           apply a second region with stale cache values. */
        geek_scan_state.region_width=width;
        geek_scan_state.region_height=height;
        geek_scan_state.region_dpi=dpi;
        if(!API(SetWindowRgn,0x140257158)(h,region,FALSE)) {
            geek_scan_state.region_width=0;
            geek_scan_state.region_height=0;
            geek_scan_state.region_dpi=0;
            Delete(region);
        }
    }
}

static BOOL geek_scan_layout(HWND h,GEEK_SCAN_LAYOUT *g) {
    RECT client;
    if(!API(GetWindowRect,0x140257208)(h,&g->window) || !Client(h,&client))return FALSE;
    POINT start={client.left,client.top},finish={client.right,client.bottom};
    if(!ToScreen(h,&start) || !ToScreen(h,&finish))return FALSE;
    g->client=(RECT){start.x-g->window.left,start.y-g->window.top,
                     finish.x-g->window.left,finish.y-g->window.top};
    g->width=g->window.right-g->window.left;g->height=g->window.bottom-g->window.top;
    g->dpi=dpi_of(h);
    if(g->width<=0 || g->height<=0 || g->client.top<=0)return FALSE;
    int caption=geek_chrome_metric((GetLong(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)?SM_CYSMCAPTION:SM_CYCAPTION,g->dpi);
    int top=g->client.top-caption;if(top<0)top=0;
    int right=g->width-g->client.left;
    if(right<=g->client.left)return FALSE;
    g->caption=(RECT){g->client.left,top,right,g->client.top};
    g->close=(RECT){right-px(40,g->dpi),top,right,g->client.top};
    if(g->close.left<g->caption.left)g->close.left=g->caption.left;
    return TRUE;
}

static BOOL geek_scan_close_at(HWND h,POINT point) {
    GEEK_SCAN_LAYOUT g;
    if(!(GetLong(h,GWL_STYLE)&WS_SYSMENU) || !geek_scan_layout(h,&g))return FALSE;
    point.x-=g.window.left;point.y-=g.window.top;
    return geek_chrome_inside(&g.close,point);
}

static BOOL geek_scan_caption_content(HDC dc,void *data) {
    GEEK_SCAN_LAYOUT *g=(GEEK_SCAN_LAYOUT*)data;
    GEEK_SCAN_TEXT get=(GEEK_SCAN_TEXT)Proc(Module(L"user32.dll"),"GetWindowTextW");
    if(!get)return FALSE;
    int saved=Save(dc);if(!saved)return FALSE;
    BOOL isdark=dark();COLORREF back=geek_scan_background();
    RECT top={0,0,g->width,g->client.top};solid(dc,&top,back);
    HFONT font=font_at(g->dpi,12,FW_MEDIUM);if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,geek_scan_text());
    WCHAR title[256];title[0]=0;get(geek_scan_state.window,title,256);
    RECT line={g->caption.left+px(12,g->dpi),g->caption.top,
               g->close.left-px(8,g->dpi),g->caption.bottom};
    if(line.right>line.left)Text(dc,title,-1,&line,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    COLORREF ink=geek_scan_text();
    if(geek_scan_state.close_hot && (GetLong(geek_scan_state.window,GWL_STYLE)&WS_SYSMENU)) {
        COLORREF hover=geek_scan_state.close_pressed?RGB(186,42,67):RGB(211,54,79);
        RECT fill={g->close.left+px(3,g->dpi),g->close.top+px(2,g->dpi),
                   g->close.right-px(3,g->dpi),g->close.bottom-px(2,g->dpi)};
        rounded(dc,fill,px(18,g->dpi),hover,hover);ink=RGB(255,255,255);
    }
    if(GetLong(geek_scan_state.window,GWL_STYLE)&WS_SYSMENU) {
        int cx=(g->close.left+g->close.right)/2,cy=(g->close.top+g->close.bottom)/2;
        int a=px(5,g->dpi),stroke=px(1,g->dpi);if(stroke<1)stroke=1;
        HPEN pen=Pen(PS_SOLID,stroke,ink);HGDIOBJ previous=pen?Select(dc,pen):0;
        geek_chrome_line(dc,cx-a,cy-a,cx+a+1,cy+a+1);
        geek_chrome_line(dc,cx+a,cy-a,cx-a-1,cy+a+1);
        if(previous && previous!=HGDI_ERROR)Select(dc,previous);
        if(pen)Delete(pen);
    }
    Restore(dc,saved);if(font)Delete(font);return TRUE;
}

static BOOL geek_scan_publish_buffered(HDC target,const RECT *rect,BOOL nonclient,
                                       BOOL (*content)(HDC,void*),void *data) {
    if(!target || !rect || rect->right<=rect->left || rect->bottom<=rect->top)return FALSE;
    GEEK_BEGIN_PAINT begin=(GEEK_BEGIN_PAINT)(*(void**)0x140257488ULL);
    GEEK_ALPHA_PAINT alpha=(GEEK_ALPHA_PAINT)(*(void**)0x140257498ULL);
    GEEK_END_PAINT end=(GEEK_END_PAINT)(*(void**)0x140257490ULL);
    if(begin && alpha && end) {
        GEEK_CHROME_PAINT_PARAMS params={sizeof(params),nonclient?4:0,0,0};
        HDC dc=0;HANDLE buffer=begin(target,rect,2,&params,&dc);
        if(buffer && dc) {
            if(content(dc,data) && SUCCEEDED(alpha(buffer,0,255))) {
                if(SUCCEEDED(end(buffer,TRUE)))return TRUE;
            } else end(buffer,FALSE);
        } else if(buffer)end(buffer,FALSE);
    }
    return FALSE;
}

static BOOL geek_scan_publish(HDC target,const RECT *rect,BOOL nonclient,
                              BOOL (*content)(HDC,void*),void *data) {
    if(geek_scan_publish_buffered(target,rect,nonclient,content,data))return TRUE;
    return content(target,data);
}

static void geek_scan_paint_frame(HWND h) {
    GEEK_SCAN_LAYOUT g;if(!geek_scan_layout(h,&g))return;
    HDC dc=API(GetWindowDC,0x1402573F8)(h);if(!dc)return;
    RECT top={0,0,g.width,g.client.top};
    geek_scan_publish(dc,&top,TRUE,geek_scan_caption_content,&g);
    COLORREF back=geek_scan_background();
    RECT left={0,g.client.top,g.client.left,g.height};
    RECT right={g.client.right,g.client.top,g.width,g.height};
    RECT bottom={g.client.left,g.client.bottom,g.client.right,g.height};
    if(!geek_surface_buffer(dc,&left,back,TRUE,TRUE,0))solid(dc,&left,back);
    if(!geek_surface_buffer(dc,&right,back,TRUE,TRUE,0))solid(dc,&right,back);
    if(!geek_surface_buffer(dc,&bottom,back,TRUE,TRUE,0))solid(dc,&bottom,back);
    API(ReleaseDC,0x1402571D0)(h,dc);
}

/* The wizard's client background is also rendered by Geek's composited paint
   manager. A plain FillRect can leave the RGB color in a transparent surface,
   which exposes the light native background around the tree and buttons.
   Commit an opaque buffered fill while clipping every visible child HWND. */
static BOOL geek_scan_background_fill(HWND h,HDC supplied) {
    RECT client;if(!Client(h,&client))return FALSE;
    GEEK_CHILD_WINDOW child=(GEEK_CHILD_WINDOW)Proc(Module(L"user32.dll"),"GetWindow");
    GEEK_VISIBLE_WINDOW visible=(GEEK_VISIBLE_WINDOW)Proc(Module(L"user32.dll"),"IsWindowVisible");
    if(!child || !visible)return FALSE;
    HDC dc=supplied?supplied:API(GetDC,0x1402571C8)(h);if(!dc)return FALSE;
    BOOL painted=FALSE;int saved=Save(dc);
    if(saved) {
        HWND current=child(h,GW_CHILD);
        for(int count=0;current && count<64;++count) {
            if(visible(current))geek_surface_exclude_child(h,dc,current,current==geek_scan_state.tree);
            current=child(current,GW_HWNDNEXT);
        }
        COLORREF back=geek_scan_background();
        if(!geek_surface_buffer(dc,&client,back,TRUE,FALSE,0))solid(dc,&client,back);
        Restore(dc,saved);painted=TRUE;
    }
    if(!supplied)API(ReleaseDC,0x1402571D0)(h,dc);
    return painted;
}

static POINT geek_scan_message_point(HWND h,UINT message,LPARAM value) {
    POINT p={(short)LOWORD(value),(short)HIWORD(value)};
    if(message==WM_MOUSEMOVE || message==WM_LBUTTONUP)ToScreen(h,&p);
    return p;
}

static void geek_scan_child_region(HWND h,int radius) {
    RECT r;if(!h || !API(GetWindowRect,0x140257208)(h,&r))return;
    int width=r.right-r.left,height=r.bottom-r.top,dpi=dpi_of(h);
    ROUND_REGION make=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
    if(!make || width<=0 || height<=0)return;
    HRGN region=make(0,0,width+1,height+1,px(radius,dpi),px(radius,dpi));
    if(region && !API(SetWindowRgn,0x140257158)(h,region,FALSE))Delete(region);
}

static void geek_scan_theme(HWND h) {
    if(!h)return;
    API(SetWindowTheme,0x140257480)(h,L"",L"");
    Send(h,TVM_SETBKCOLOR,0,geek_scan_panel());
    Send(h,TVM_SETTEXTCOLOR,0,geek_scan_text());
    Send(h,TVM_SETLINECOLOR,0,dark()?RGB(66,61,74):RGB(226,221,232));
    SetLong(h,GWL_EXSTYLE,GetLong(h,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);
    LONG style=GetLong(h,GWL_STYLE);
    style&=~(TVS_HASLINES|TVS_LINESATROOT);
    SetLong(h,GWL_STYLE,style);
    API(SetWindowPos,0x1402572F0)(h,0,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    geek_scan_child_region(h,12);
}

static void geek_scan_flatten_progress(HWND h) {
    if(!h)return;
    LONG extended=GetLong(h,GWL_EXSTYLE);
    LONG style=GetLong(h,GWL_STYLE);
    LONG next_extended=extended&~WS_EX_CLIENTEDGE;
    LONG next_style=style&~WS_BORDER;
    if(next_extended==extended && next_style==style)return;
    SetLong(h,GWL_EXSTYLE,next_extended);
    SetLong(h,GWL_STYLE,next_style);
    API(SetWindowPos,0x1402572F0)(h,0,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}

static void geek_scan_dwm(HWND h) {
    HMODULE dwm=Module(L"dwmapi.dll");if(!dwm)return;
    GEEK_DWM_EXTEND extend=(GEEK_DWM_EXTEND)Proc(dwm,"DwmExtendFrameIntoClientArea");
    if(extend){int margins[4]={0,0,0,0};extend(h,margins);}
    GEEK_DWM_ATTRIBUTE set=(GEEK_DWM_ATTRIBUTE)Proc(dwm,"DwmSetWindowAttribute");
    if(!set)return;
    int policy=1,corner=2;BOOL isdark=dark(),allow=TRUE;
    COLORREF back=geek_scan_background();
    COLORREF ink=isdark?RGB(236,231,240):RGB(65,57,78);
    set(h,2,&policy,sizeof(policy));set(h,4,&allow,sizeof(allow));
    set(h,20,&isdark,sizeof(isdark));set(h,33,&corner,sizeof(corner));
    set(h,34,&back,sizeof(back));set(h,35,&back,sizeof(back));set(h,36,&ink,sizeof(ink));
}

static void geek_scan_draw_button(HWND h,HDC dc) {
    RECT r;if(!Client(h,&r))return;
    int dpi=dpi_of(h),saved=Save(dc);BOOL isdark=dark();
    COLORREF fill=geek_scan_panel(),edge=isdark?RGB(90,80,105):RGB(213,203,225);
    COLORREF ink=geek_scan_text();
    BOOL primary=h==geek_scan_state.finish;
    BOOL (WINAPI *enabled_fn)(HWND)=(BOOL(WINAPI *)(HWND))Proc(Module(L"user32.dll"),"IsWindowEnabled");
    BOOL enabled=enabled_fn?enabled_fn(h):TRUE;
    UINT state=(UINT)Send(h,BM_GETSTATE,0,0);
    if(primary){fill=isdark?RGB(135,103,165):RGB(111,76,155);edge=fill;ink=RGB(255,255,255);}
    POINT cursor={0},origin={0};Cursor(&cursor);ToScreen(h,&origin);
    cursor.x-=origin.x;cursor.y-=origin.y;
    if(enabled && cursor.x>=0 && cursor.x<r.right && cursor.y>=0 && cursor.y<r.bottom) {
        if(primary)fill=isdark?RGB(153,121,184):RGB(126,89,169);
        else fill=isdark?RGB(52,45,62):RGB(240,234,247);
    }
    if(enabled && (state&BST_PUSHED)) {
        if(primary)fill=isdark?RGB(116,83,143):RGB(94,62,133);
        else fill=isdark?RGB(61,51,73):RGB(228,219,239);
    }
    if(!enabled) {fill=isdark?RGB(43,40,48):RGB(235,232,239);edge=fill;ink=geek_scan_muted();}
    solid(dc,&r,geek_scan_background());
    RECT card={r.left+1,r.top+1,r.right-1,r.bottom-1};
    rounded(dc,card,px(14,dpi),fill,edge);
    HWND (WINAPI *focus_fn)(void)=(HWND(WINAPI *)(void))Proc(Module(L"user32.dll"),"GetFocus");
    if(enabled && focus_fn && focus_fn()==h) {
        int inset=px(3,dpi);RECT ring={card.left+inset,card.top+inset,card.right-inset,card.bottom-inset};
        if(ring.right>ring.left && ring.bottom>ring.top)
            rounded(dc,ring,px(11,dpi),fill,isdark?RGB(211,191,231):RGB(89,61,120));
    }
    WCHAR text[96]={0};
    GEEK_SCAN_TEXT get=(GEEK_SCAN_TEXT)Proc(Module(L"user32.dll"),"GetWindowTextW");
    if(get)get(h,text,96);
    HFONT font=font_at(dpi,12,FW_MEDIUM);if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,ink);
    RECT line=card;line.left+=px(6,dpi);line.right-=px(6,dpi);
    Text(dc,text,-1,&line,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    Restore(dc,saved);if(font)Delete(font);
}

static LRESULT CALLBACK geek_scan_button_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=API(BeginPaint,0x1402571D8)(h,&paint);
        if(dc){geek_scan_draw_button(h,dc);API(EndPaint,0x1402571E0)(h,&paint);return 0;}
    }
    if(message==WM_MOUSEMOVE || message==WM_MOUSELEAVE) {Invalidate(h,0,FALSE);}
    if(message==WM_MOUSEMOVE) {
        TRACK_MOUSE track=(TRACK_MOUSE)Proc(Module(L"user32.dll"),"TrackMouseEvent");
        TRACKMOUSEEVENT event={sizeof(event),TME_LEAVE,h,0};if(track)track(&event);
    }
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_scan_button_subclass,id);
    }
    return def(h,message,w,l);
}

typedef struct {
    HWND window;
    RECT rect;
} GEEK_SCAN_PROGRESS_DRAW;

static BOOL geek_scan_progress_content(HDC dc,void *data) {
    GEEK_SCAN_PROGRESS_DRAW *drawing=(GEEK_SCAN_PROGRESS_DRAW*)data;
    HWND h=drawing->window;RECT r=drawing->rect;
    int dpi=dpi_of(h);BOOL isdark=dark();
    solid(dc,&r,geek_scan_background());
    RECT track={r.left+1,r.top+1,r.right-1,r.bottom-1};
    rounded(dc,track,px(8,dpi),isdark?RGB(47,43,53):RGB(232,228,236),
            isdark?RGB(73,66,81):RGB(217,211,224));
    PBRANGE range={0,100};LRESULT pos=Send(h,PBM_GETRANGE,0,(LPARAM)&range);
    if(range.iHigh<=range.iLow)range.iHigh=100;
    pos=Send(h,PBM_GETPOS,0,0);if(pos<range.iLow)pos=range.iLow;if(pos>range.iHigh)pos=range.iHigh;
    int width=track.right-track.left;
    int fillwidth=(int)((long long)(pos-range.iLow)*width/(range.iHigh-range.iLow));
    if(fillwidth>px(3,dpi)) {
        RECT fill=track;fill.right=fill.left+fillwidth;
        rounded(dc,fill,px(8,dpi),isdark?RGB(160,121,198):RGB(112,75,158),
                isdark?RGB(160,121,198):RGB(112,75,158));
    }
    return TRUE;
}

static LRESULT CALLBACK geek_scan_progress_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=API(BeginPaint,0x1402571D8)(h,&paint);
        if(dc) {
            RECT r;if(!Client(h,&r))r=(RECT){0,0,0,0};
            GEEK_SCAN_PROGRESS_DRAW drawing={h,r};
            if(!geek_scan_publish_buffered(dc,&r,FALSE,geek_scan_progress_content,&drawing)) {
                geek_scan_progress_content(dc,&drawing);
                geek_surface_buffer(dc,&r,0,FALSE,FALSE,0);
            }
            API(EndPaint,0x1402571E0)(h,&paint);return 0;
        }
    }
    if(message==PBM_SETPOS || message==PBM_DELTAPOS || message==PBM_SETSTEP || message==PBM_STEPIT ||
       message==WM_THEMECHANGED || message==WM_SETTINGCHANGE) {
        LRESULT result=def(h,message,w,l);Invalidate(h,0,FALSE);return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_scan_progress_subclass,id);
    }
    return def(h,message,w,l);
}

static LRESULT CALLBACK geek_scan_tree_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_ERASEBKGND) {if(w){RECT r;Client(h,&r);solid((HDC)w,&r,geek_scan_panel());}return 1;}
    if(message==WM_THEMECHANGED || message==WM_SETTINGCHANGE) {geek_scan_theme(h);Invalidate(h,0,TRUE);}
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_scan_tree_subclass,id);
    }
    return def(h,message,w,l);
}

static void geek_scan_refresh(HWND h) {
    if(!h)return;
    int palette=dark()?2:1;geek_scan_state.palette=palette;
    HBRUSH brush=geek_scan_brush();
    if(brush)Invalidate(h,0,FALSE);
    geek_scan_region(h);geek_scan_dwm(h);
    geek_scan_flatten_progress(geek_scan_state.progress);
    if(geek_scan_state.tree) {geek_scan_theme(geek_scan_state.tree);Invalidate(geek_scan_state.tree,0,TRUE);}
    if(geek_scan_state.progress)Invalidate(geek_scan_state.progress,0,FALSE);
    if(geek_scan_state.finish)Invalidate(geek_scan_state.finish,0,FALSE);
    if(geek_scan_state.cancel)Invalidate(geek_scan_state.cancel,0,FALSE);
    geek_scan_paint_frame(h);
}

static LRESULT CALLBACK geek_scan_dialog_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(h!=geek_scan_state.window)return def(h,message,w,l);
    if(message==WM_ERASEBKGND) {
        if(w && geek_scan_background_fill(h,(HDC)w))return 1;
    }
    if(message==WM_PAINT) {
        LRESULT result=def(h,message,w,l);
        geek_scan_background_fill(h,0);
        return result;
    }
    if(message==WM_NOTIFY && l) {
        NMHDR *header=(NMHDR*)l;
        if(header->hwndFrom==geek_scan_state.tree && header->code==NM_CUSTOMDRAW) {
            NMTVCUSTOMDRAW *drawing=(NMTVCUSTOMDRAW*)l;
            if(drawing->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(drawing->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                BOOL selected=(drawing->nmcd.uItemState&CDIS_SELECTED)!=0;
                drawing->clrText=selected?(dark()?RGB(248,244,252):RGB(65,45,83)):geek_scan_text();
                drawing->clrTextBk=selected?(dark()?RGB(76,60,94):RGB(232,222,244)):geek_scan_panel();
                return CDRF_NEWFONT;
            }
        }
    }
    if(message==WM_CTLCOLORDLG) {
        HBRUSH brush=geek_scan_brush();
        if(w && brush){BkMode((HDC)w,OPAQUE);TextColor((HDC)w,geek_scan_text());
            API(SetBkColor,0x1402563A0)((HDC)w,geek_scan_background());return (LRESULT)brush;}
    }
    if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN) {
        HWND child=(HWND)l;
        if(child==geek_scan_state.tree || child==geek_scan_state.progress ||
           child==geek_scan_state.finish || child==geek_scan_state.cancel ||
           geek_scan_class(child,h,L"static",6)) {
            HBRUSH brush=geek_scan_brush();if(w && brush){BkMode((HDC)w,TRANSPARENT);TextColor((HDC)w,geek_scan_text());return (LRESULT)brush;}
        }
    }
    if(message==0x00AE || message==0x00AF)return 0;
    if(message==WM_NCPAINT) {def(h,message,w,l);geek_scan_paint_frame(h);return 0;}
    if(message==WM_NCACTIVATE) {LRESULT result=def(h,message,w,-1);geek_scan_paint_frame(h);return result;}
    if(message==WM_SETTEXT) {LRESULT result=def(h,message,w,l);geek_scan_paint_frame(h);return result;}
    if(message==WM_NCHITTEST) {
        LRESULT result=def(h,message,w,l);
        if(result>=HTLEFT && result<=HTBOTTOMRIGHT)return result;
        GEEK_SCAN_LAYOUT g;POINT screen={(short)LOWORD(l),(short)HIWORD(l)};
        if(geek_scan_layout(h,&g)) {
            POINT point={screen.x-g.window.left,screen.y-g.window.top};
            if(geek_chrome_inside(&g.caption,point))return geek_scan_close_at(h,screen)?HTCLOSE:HTCAPTION;
        }
        return result;
    }
    if(message==WM_NCLBUTTONDOWN && geek_scan_close_at(h,geek_scan_message_point(h,message,l))) {
        geek_scan_state.close_pressed=TRUE;geek_scan_state.close_hot=TRUE;
        API(SetCapture,0x140256EB0)(h);
        if(API(GetCapture,0x140256EB8)()!=h)geek_scan_state.close_pressed=FALSE;
        geek_scan_paint_frame(h);return 0;
    }
    if((message==WM_LBUTTONUP || message==WM_NCLBUTTONUP) && geek_scan_state.close_pressed) {
        BOOL activate=geek_scan_close_at(h,geek_scan_message_point(h,message,l));
        geek_scan_state.close_pressed=FALSE;geek_scan_state.close_hot=activate;
        if(API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
        geek_scan_paint_frame(h);
        if(activate)Send(h,WM_SYSCOMMAND,SC_CLOSE,0);
        return 0;
    }
    if(message==WM_NCMOUSEMOVE || (message==WM_MOUSEMOVE && geek_scan_state.close_pressed)) {
        BOOL hot=geek_scan_close_at(h,geek_scan_message_point(h,message,l));
        if(hot!=geek_scan_state.close_hot){geek_scan_state.close_hot=hot;geek_scan_paint_frame(h);}
        if(message==WM_NCMOUSEMOVE && !geek_scan_state.tracking) {
            TRACK_MOUSE track=(TRACK_MOUSE)Proc(Module(L"user32.dll"),"TrackMouseEvent");
            TRACKMOUSEEVENT event={sizeof(event),TME_LEAVE|TME_NONCLIENT,h,0};
            if(track && track(&event))geek_scan_state.tracking=TRUE;
        }
        if(hot || geek_scan_state.close_pressed)return 0;
    }
    if(message==WM_NCMOUSELEAVE) {
        BOOL repaint=geek_scan_state.close_hot;geek_scan_state.close_hot=FALSE;geek_scan_state.tracking=FALSE;
        if(repaint)geek_scan_paint_frame(h);return 0;
    }
    if(message==WM_CANCELMODE || message==WM_CAPTURECHANGED) {
        BOOL repaint=geek_scan_state.close_pressed || geek_scan_state.close_hot;
        geek_scan_state.close_pressed=FALSE;geek_scan_state.close_hot=FALSE;
        if(message==WM_CANCELMODE && API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
        if(repaint)geek_scan_paint_frame(h);
    }
    if(message==WM_THEMECHANGED || message==WM_SETTINGCHANGE || message==WM_SYSCOLORCHANGE || message==WM_DWMCOMPOSITIONCHANGED) {
        LRESULT result=def(h,message,w,l);geek_scan_refresh(h);return result;
    }
    if(message==WM_SIZE || message==WM_DPICHANGED) {
        LRESULT result=def(h,message,w,l);geek_scan_region(h);geek_scan_paint_frame(h);return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_scan_dialog_subclass,id);
        HBRUSH first=geek_scan_state.brush[0],second=geek_scan_state.brush[1];
        geek_scan_state=(GEEK_SCAN_STATE){0};
        LRESULT result=def(h,message,w,l);if(first)Delete(first);if(second)Delete(second);return result;
    }
    return def(h,message,w,l);
}

static void geek_scan_install(HWND h) {
    if(!h || geek_scan_state.window)return;
    HWND tree=API(GetDlgItem,0x1402570A0)(h,0x472),progress=API(GetDlgItem,0x1402570A0)(h,0x476);
    HWND finish=API(GetDlgItem,0x1402570A0)(h,3),cancel=API(GetDlgItem,0x1402570A0)(h,0x4C7);
    if(!geek_scan_class(tree,h,L"systreeview32",13) ||
       !geek_scan_class(progress,h,L"msctls_progress32",17) ||
       !geek_scan_class(finish,h,L"button",6) || !geek_scan_class(cancel,h,L"button",6))return;
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(!add || !add(h,geek_scan_dialog_subclass,0x5343414E,0))return;
    geek_scan_state=(GEEK_SCAN_STATE){0};geek_scan_state.window=h;geek_scan_state.tree=tree;
    geek_scan_state.progress=progress;geek_scan_state.finish=finish;geek_scan_state.cancel=cancel;
    add(tree,geek_scan_tree_subclass,0x53434154,0);
    add(progress,geek_scan_progress_subclass,0x53434150,0);
    add(finish,geek_scan_button_subclass,0x53434146,0);
    add(cancel,geek_scan_button_subclass,0x53434143,0);
    geek_scan_refresh(h);
    API(RedrawWindow,0x140257260)(h,0,0,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW);
}

#endif
