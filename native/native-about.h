#ifndef GEEK_NATIVE_ABOUT_H
#define GEEK_NATIVE_ABOUT_H

/* Install only from CDlgAboutGeek's completed OnInitDialog.  The existing
   modal dialog and both standard BUTTON HWNDs retain their IDs, styles,
   window procedures, notifications and keyboard/default-button handling. */
typedef int (WINAPI *GEEK_ABOUT_TEXT)(HWND,LPWSTR,int);
typedef int (WINAPI *GEEK_ABOUT_CLASS)(HWND,LPWSTR,int);
typedef HWND (WINAPI *GEEK_ABOUT_PARENT)(HWND);

typedef struct {
    HWND window;
    HWND homepage;
    HWND close;
    HWND version;
    HWND author;
    HWND icon;
    HBRUSH background;
    COLORREF background_color;
    int region_width;
    int region_height;
    int region_dpi;
    BOOL hot;
    BOOL pressed;
    BOOL tracking;
} GEEK_ABOUT_STATE;

typedef struct {
    RECT window;
    RECT client;
    RECT caption;
    RECT close;
    int width;
    int height;
    int dpi;
} GEEK_ABOUT_LAYOUT;

static GEEK_ABOUT_STATE geek_about_state;

static COLORREF geek_about_background_color(void) {
    return dark()?RGB(20,18,24):RGB(246,244,249);
}

static HBRUSH geek_about_background_brush(void) {
    COLORREF color=geek_about_background_color();
    if(!geek_about_state.background || geek_about_state.background_color!=color) {
        HBRUSH brush=Brush(color);
        if(brush) {
            HBRUSH previous=geek_about_state.background;
            geek_about_state.background=brush;
            geek_about_state.background_color=color;
            if(previous)Delete(previous);
        }
    }
    return geek_about_state.background;
}

/* Draw a complete RGB surface into a temporary top-down DIB, then publish
   it with alpha255. Nothing is rasterized into target on the success path. */
static BOOL geek_about_publish(HDC target,const RECT *rect,BOOL nonclient,
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
    return content(target,data);
}

static BOOL geek_about_layout(HWND h,GEEK_ABOUT_LAYOUT *g) {
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
    int top=g->client.top-caption;
    if(top<0)top=0;
    if(top>=g->client.top)top=0;
    int right=g->width-g->client.left;
    if(right<=g->client.left)return FALSE;
    g->caption=(RECT){g->client.left,top,right,g->client.top};
    g->close=(RECT){right-px(40,g->dpi),top,right,g->client.top};
    if(g->close.left<g->caption.left)g->close.left=g->caption.left;
    return TRUE;
}

static BOOL geek_about_close_at(HWND h,POINT point) {
    GEEK_ABOUT_LAYOUT g;
    if(!(GetLong(h,GWL_STYLE)&WS_SYSMENU) || !geek_about_layout(h,&g))return FALSE;
    point.x-=g.window.left;point.y-=g.window.top;
    return geek_chrome_inside(&g.close,point);
}

static BOOL geek_about_caption_content(HDC dc,void *data) {
    GEEK_ABOUT_LAYOUT *g=(GEEK_ABOUT_LAYOUT*)data;
    GEEK_ABOUT_TEXT text=(GEEK_ABOUT_TEXT)Proc(Module(L"user32.dll"),"GetWindowTextW");
    if(!text)return FALSE;
    int saved=Save(dc);if(!saved)return FALSE;
    BOOL isdark=dark();COLORREF back=geek_about_background_color();
    RECT top={0,0,g->width,g->client.top};solid(dc,&top,back);
    HFONT font=font_at(g->dpi,12,FW_MEDIUM);
    if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,isdark?RGB(235,229,242):RGB(65,57,78));
    WCHAR title[256];title[0]=0;
    text(geek_about_state.window,title,256);
    RECT line={g->caption.left+px(12,g->dpi),g->caption.top,
               g->close.left-px(8,g->dpi),g->caption.bottom};
    if(line.right>line.left)Text(dc,title,-1,&line,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    COLORREF ink=isdark?RGB(232,226,239):RGB(62,56,73);
    if(geek_about_state.hot && (GetLong(geek_about_state.window,GWL_STYLE)&WS_SYSMENU)) {
        COLORREF hover=geek_about_state.pressed?RGB(186,42,67):RGB(211,54,79);
        RECT fill={g->close.left+px(3,g->dpi),g->close.top+px(2,g->dpi),
                   g->close.right-px(3,g->dpi),g->close.bottom-px(2,g->dpi)};
        rounded(dc,fill,px(18,g->dpi),hover,hover);ink=RGB(255,255,255);
    }
    if(GetLong(geek_about_state.window,GWL_STYLE)&WS_SYSMENU) {
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

static void geek_about_paint_caption(HWND h) {
    GEEK_ABOUT_LAYOUT g;if(!geek_about_layout(h,&g))return;
    HDC dc=API(GetWindowDC,0x1402573F8)(h);if(!dc)return;
    RECT top={0,0,g.width,g.client.top};
    geek_about_publish(dc,&top,TRUE,geek_about_caption_content,&g);
    COLORREF back=geek_about_background_color();
    RECT left={0,g.client.top,g.client.left,g.height};
    RECT right={g.client.right,g.client.top,g.width,g.height};
    RECT bottom={g.client.left,g.client.bottom,g.client.right,g.height};
    if(!geek_surface_buffer(dc,&left,back,TRUE,TRUE,0))solid(dc,&left,back);
    if(!geek_surface_buffer(dc,&right,back,TRUE,TRUE,0))solid(dc,&right,back);
    if(!geek_surface_buffer(dc,&bottom,back,TRUE,TRUE,0))solid(dc,&bottom,back);
    API(ReleaseDC,0x1402571D0)(h,dc);
}

static BOOL geek_about_background(HWND h,HDC supplied) {
    RECT client;if(!Client(h,&client))return FALSE;
    GEEK_CHILD_WINDOW child=(GEEK_CHILD_WINDOW)Proc(Module(L"user32.dll"),"GetWindow");
    GEEK_VISIBLE_WINDOW visible=(GEEK_VISIBLE_WINDOW)Proc(Module(L"user32.dll"),"IsWindowVisible");
    if(!child || !visible)return FALSE;
    HDC dc=supplied?supplied:API(GetDC,0x1402571C8)(h);if(!dc)return FALSE;
    BOOL painted=FALSE;
    int saved=Save(dc);
    if(saved) {
        HWND current=child(h,GW_CHILD);
        for(int n=0;current && n<64;++n) {
            RECT r;
            if(visible(current) && API(GetWindowRect,0x140257208)(current,&r)) {
                MapPoints(0,h,(POINT*)&r,2);
                API(ExcludeClipRect,0x1402561F0)(dc,r.left,r.top,r.right,r.bottom);
            }
            current=child(current,GW_HWNDNEXT);
        }
        COLORREF back=geek_about_background_color();
        if(!geek_surface_buffer(dc,&client,back,TRUE,FALSE,0))solid(dc,&client,back);
        Restore(dc,saved);
        painted=TRUE;
    }
    if(!supplied)API(ReleaseDC,0x1402571D0)(h,dc);
    return painted;
}

static void geek_about_region(HWND h) {
    RECT r;if(!API(GetWindowRect,0x140257208)(h,&r))return;
    int width=r.right-r.left,height=r.bottom-r.top,dpi=dpi_of(h);
    if(width<=0 || height<=0)return;
    if(width==geek_about_state.region_width && height==geek_about_state.region_height && dpi==geek_about_state.region_dpi)return;
    /* SetWindowRgn can synchronously dispatch WINDOWPOSCHANGED. */
    geek_about_state.region_width=width;geek_about_state.region_height=height;geek_about_state.region_dpi=dpi;
    ROUND_REGION make=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
    BOOL installed=FALSE;
    if(make) {
        HRGN region=make(0,0,width+1,height+1,px(28,dpi),px(28,dpi));
        if(region) {
            installed=API(SetWindowRgn,0x140257158)(h,region,FALSE)!=0;
            if(!installed)Delete(region);
        }
    }
    if(!installed) {
        geek_about_state.region_width=0;geek_about_state.region_height=0;geek_about_state.region_dpi=0;
    }
}

static void geek_about_dwm(HWND h) {
    HMODULE dwm=Module(L"dwmapi.dll");if(!dwm)return;
    GEEK_DWM_EXTEND extend=(GEEK_DWM_EXTEND)Proc(dwm,"DwmExtendFrameIntoClientArea");
    if(extend){int margins[4]={0,0,0,0};extend(h,margins);}
    GEEK_DWM_ATTRIBUTE set=(GEEK_DWM_ATTRIBUTE)Proc(dwm,"DwmSetWindowAttribute");
    if(!set)return;
    int policy=1,corner=2;BOOL allow=TRUE,isdark=dark();
    COLORREF back=geek_about_background_color(),ink=isdark?RGB(235,229,242):RGB(65,57,78);
    set(h,2,&policy,sizeof(policy));set(h,4,&allow,sizeof(allow));
    set(h,20,&isdark,sizeof(isdark));set(h,33,&corner,sizeof(corner));
    set(h,34,&back,sizeof(back));set(h,35,&back,sizeof(back));set(h,36,&ink,sizeof(ink));
}

typedef struct {HWND window;NMCUSTOMDRAW *drawing;} GEEK_ABOUT_BUTTON_DRAW;

static BOOL geek_about_button_content(HDC dc,void *data) {
    GEEK_ABOUT_BUTTON_DRAW *button=(GEEK_ABOUT_BUTTON_DRAW*)data;
    HWND h=button->window;NMCUSTOMDRAW *drawing=button->drawing;RECT r=drawing->rc;
    GEEK_ABOUT_TEXT get=(GEEK_ABOUT_TEXT)Proc(Module(L"user32.dll"),"GetWindowTextW");
    if(!get)return FALSE;
    int saved=Save(dc);if(!saved)return FALSE;
    BOOL isdark=dark();int dpi=dpi_of(h);UINT state=drawing->uItemState;
    BOOL disabled=(state&(CDIS_DISABLED|CDIS_GRAYED))!=0;
    BOOL pressed=(state&CDIS_SELECTED)!=0,hot=(state&CDIS_HOT)!=0;
    BOOL primary=h==geek_about_state.close;
    UINT cues=(UINT)Send(h,WM_QUERYUISTATE,0,0);
    COLORREF fill=isdark?RGB(33,31,38):RGB(255,253,255);
    COLORREF edge=isdark?RGB(128,105,162):RGB(185,166,214);
    COLORREF ink=isdark?RGB(236,231,240):RGB(68,46,94);
    if(primary) {fill=isdark?RGB(183,151,220):RGB(116,80,163);edge=fill;ink=isdark?RGB(35,23,48):RGB(255,255,255);}
    if(hot) {
        fill=primary?(isdark?RGB(197,166,231):RGB(128,91,175)):(isdark?RGB(48,40,60):RGB(241,233,251));
        if(primary)edge=fill;
    }
    if(pressed) {
        fill=primary?(isdark?RGB(160,128,197):RGB(95,62,139)):(isdark?RGB(62,49,78):RGB(228,216,244));
        if(primary)edge=fill;
    }
    if(disabled) {fill=isdark?RGB(43,39,48):RGB(232,228,236);edge=fill;ink=isdark?RGB(126,118,136):RGB(159,151,168);}
    solid(dc,&r,geek_about_background_color());
    RECT card={r.left+1,r.top+1,r.right-1,r.bottom-1};
    rounded(dc,card,px(20,dpi),fill,edge);
    if(!disabled && pressed) {
        int inset=px(3,dpi);if(inset<2)inset=2;
        RECT ring={card.left+inset,card.top+inset,card.right-inset,card.bottom-inset};
        if(ring.right>ring.left && ring.bottom>ring.top)
            rounded(dc,ring,px(16,dpi),fill,primary?ink:edge);
    }
    WCHAR text[256];text[0]=0;
    get(h,text,256);
    HFONT font=font_at(dpi,13,FW_MEDIUM);if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,ink);
    UINT flags=DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS;
    if(cues&UISF_HIDEACCEL)flags|=DT_HIDEPREFIX;
    RECT line=card;line.left+=px(5,dpi);line.right-=px(5,dpi);
    if(line.right>line.left)Text(dc,text,-1,&line,flags);
    Restore(dc,saved);if(font)Delete(font);return TRUE;
}

typedef struct {HWND window;RECT client;} GEEK_ABOUT_LABEL_DRAW;

/* The native themed STATIC can leave a transparent glyph rectangle in the
   composited dialog. Paint only the two existing text labels as complete
   opaque surfaces, keeping their original strings and borrowed HFONTs. */
static BOOL geek_about_label_content(HDC dc,void *data) {
    GEEK_ABOUT_LABEL_DRAW *label=(GEEK_ABOUT_LABEL_DRAW*)data;
    HWND h=label->window;
    HFONT font=(HFONT)Send(h,WM_GETFONT,0,0);
    if(!font)return FALSE;
    WCHAR text[256];text[0]=0;
    API(GetWindowTextW,0x1402571F0)(h,text,256);
    int saved=Save(dc);if(!saved)return FALSE;
    solid(dc,&label->client,geek_about_background_color());
    Select(dc,font);BkMode(dc,TRANSPARENT);
    TextColor(dc,dark()?RGB(236,231,240):RGB(31,28,35));
    UINT flags=DT_LEFT|DT_WORDBREAK|DT_EXPANDTABS;
    if(GetLong(h,GWL_STYLE)&SS_NOPREFIX)flags|=DT_NOPREFIX;
    if((UINT)Send(h,WM_QUERYUISTATE,0,0)&UISF_HIDEACCEL)flags|=DT_HIDEPREFIX;
    RECT line=label->client;
    Text(dc,text,-1,&line,flags);
    Restore(dc,saved);return TRUE;
}

static LRESULT CALLBACK geek_about_label_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_about_label_subclass,id);
        if(h==geek_about_state.version)geek_about_state.version=0;
        if(h==geek_about_state.author)geek_about_state.author=0;
        return def(h,message,w,l);
    }
    if(h!=geek_about_state.version && h!=geek_about_state.author)return def(h,message,w,l);
    RECT client;
    if(message==WM_PAINT && Client(h,&client) && client.right>0 && client.bottom>0) {
        PAINTSTRUCT paint;
        HDC dc=API(BeginPaint,0x1402571D8)(h,&paint);
        if(!dc)return def(h,message,w,l);
        GEEK_ABOUT_LABEL_DRAW label={h,client};
        if(!geek_about_publish(dc,&client,FALSE,geek_about_label_content,&label))
            def(h,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);
        API(EndPaint,0x1402571E0)(h,&paint);
        return 0;
    }
    if(message==WM_PRINTCLIENT && w && Client(h,&client)) {
        GEEK_ABOUT_LABEL_DRAW label={h,client};
        if(geek_about_publish((HDC)w,&client,FALSE,geek_about_label_content,&label))return 0;
    }
    if(message==WM_ERASEBKGND && w && Client(h,&client) &&
       geek_surface_buffer((HDC)w,&client,geek_about_background_color(),TRUE,FALSE,0))return 1;
    if(message==WM_SETTEXT || message==WM_SETFONT) {
        LRESULT result=def(h,message,w,l);Invalidate(h,0,FALSE);return result;
    }
    return def(h,message,w,l);
}

typedef struct {RECT client;HDC target;HICON icon;} GEEK_ABOUT_ICON_DRAW;

static BOOL geek_about_plain_icon(HWND h) {
    LONG style=GetLong(h,GWL_STYLE);
    /* Preserve native rendering for special static layouts and disabled
       presentation. This dialog's existing icon is plain 0x50000003. */
    return (style&0xFFFF)==SS_ICON && !(style&WS_DISABLED);
}

static BOOL geek_about_icon_content(HDC dc,void *data) {
    GEEK_ABOUT_ICON_DRAW *drawing=(GEEK_ABOUT_ICON_DRAW*)data;
    /* about_publish normally permits direct RGB fallback. An icon is only
       repaired after an opaque buffered commit, never by naked GDI paint. */
    if(dc==drawing->target || !drawing->icon)return FALSE;
    int saved=Save(dc);if(!saved)return FALSE;
    HBRUSH background=Brush(geek_about_background_color());
    if(!background){Restore(dc,saved);return FALSE;}
    BOOL filled=Fill(dc,&drawing->client,background)!=0;
    Delete(background);
    /* Borrow the control's current original HICON. Zero dimensions without
       DI_DEFAULTSIZE retain its loaded resource size; no image is replaced,
       resized, cached or destroyed by this subclass. */
    BOOL drawn=filled && DrawIconNative(dc,0,0,drawing->icon,0,0,0,0,DI_NORMAL);
    Restore(dc,saved);return drawn;
}

static LRESULT CALLBACK geek_about_icon_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_about_icon_subclass,id);
        if(h==geek_about_state.icon)geek_about_state.icon=0;
        return def(h,message,w,l);
    }
    if(h!=geek_about_state.icon || !geek_about_plain_icon(h))return def(h,message,w,l);
    RECT client;
    if((message==WM_PAINT || (message==WM_PRINTCLIENT && w)) &&
       Client(h,&client) && client.right>0 && client.bottom>0) {
        HICON icon=(HICON)Send(h,STM_GETICON,0,0);
        if(!icon || !DrawIconNative)return def(h,message,w,l);
        if(message==WM_PRINTCLIENT) {
            GEEK_ABOUT_ICON_DRAW drawing={client,(HDC)w,icon};
            if(geek_about_publish((HDC)w,&client,FALSE,geek_about_icon_content,&drawing))return 0;
            return def(h,message,w,l);
        }
        PAINTSTRUCT paint;
        HDC dc=API(BeginPaint,0x1402571D8)(h,&paint);
        if(!dc)return def(h,message,w,l);
        GEEK_ABOUT_ICON_DRAW drawing={client,dc,icon};
        BOOL painted=geek_about_publish(dc,&client,FALSE,geek_about_icon_content,&drawing);
        API(EndPaint,0x1402571E0)(h,&paint);
        if(painted)return 0;
        /* Restore the original WM_PAINT contract after our paint has ended.
           Invalidate first so the native STATIC receives a real update area. */
        Invalidate(h,0,FALSE);
        return def(h,message,w,l);
    }
    if(message==WM_ERASEBKGND && w && Client(h,&client) &&
       geek_surface_buffer((HDC)w,&client,geek_about_background_color(),TRUE,FALSE,0))return 1;
    if(message==STM_SETICON || message==STM_SETIMAGE) {
        LRESULT result=def(h,message,w,l);Invalidate(h,0,FALSE);return result;
    }
    return def(h,message,w,l);
}

static void geek_about_buttons_layout(HWND h) {
    RECT client;if(!Client(h,&client))return;
    int dpi=dpi_of(h),height=px(36,dpi),bottom=client.bottom-px(16,dpi);
    HWND buttons[2]={geek_about_state.homepage,geek_about_state.close};
    for(int i=0;i<2;++i) {
        RECT r;if(!buttons[i] || !API(GetWindowRect,0x140257208)(buttons[i],&r))continue;
        MapPoints(0,h,(POINT*)&r,2);
        int width=r.right-r.left,top=bottom-height;if(top<0)top=0;
        if(width>0 && bottom>top)API(SetWindowPos,0x1402572F0)(buttons[i],0,r.left,top,width,bottom-top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
}

static void geek_about_refresh(HWND h) {
    geek_about_background_brush();geek_about_dwm(h);geek_about_region(h);
    Invalidate(h,0,FALSE);
    if(geek_about_state.homepage)Invalidate(geek_about_state.homepage,0,FALSE);
    if(geek_about_state.close)Invalidate(geek_about_state.close,0,FALSE);
    if(geek_about_state.version)Invalidate(geek_about_state.version,0,FALSE);
    if(geek_about_state.author)Invalidate(geek_about_state.author,0,FALSE);
    if(geek_about_state.icon)Invalidate(geek_about_state.icon,0,FALSE);
    geek_about_paint_caption(h);
}

static LRESULT CALLBACK geek_about_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(h!=geek_about_state.window)return def(h,message,w,l);
    if(message==WM_NOTIFY && l) {
        NMHDR *header=(NMHDR*)l;
        if(header->code==NM_CUSTOMDRAW &&
           (header->hwndFrom==geek_about_state.homepage || header->hwndFrom==geek_about_state.close)) {
            NMCUSTOMDRAW *drawing=(NMCUSTOMDRAW*)l;
            if(drawing->dwDrawStage==CDDS_PREPAINT) {
                GEEK_ABOUT_BUTTON_DRAW button={header->hwndFrom,drawing};
                if(geek_about_publish(drawing->hdc,&drawing->rc,FALSE,geek_about_button_content,&button))return CDRF_SKIPDEFAULT;
            }
        }
    }
    BOOL known_static=l && ((HWND)l==geek_about_state.version ||
                           (HWND)l==geek_about_state.author || (HWND)l==geek_about_state.icon);
    if(message==WM_CTLCOLORDLG || (message==WM_CTLCOLORSTATIC && known_static)) {
        HBRUSH brush=geek_about_background_brush();
        if(w && brush) {
            BkMode((HDC)w,TRANSPARENT);TextColor((HDC)w,dark()?RGB(236,231,240):RGB(31,28,35));
            API(SetBkColor,0x1402563A0)((HDC)w,geek_about_background_color());
            return (LRESULT)brush;
        }
    }
    if(message==WM_ERASEBKGND) {
        if(geek_about_background(h,(HDC)w))return 1;
        return def(h,message,w,l);
    }
    if(message==WM_PAINT) {LRESULT result=def(h,message,w,l);geek_about_background(h,0);return result;}
    if(message==0x00AE || message==0x00AF)return 0;
    if(message==WM_NCPAINT) {def(h,message,w,l);geek_about_paint_caption(h);return 0;}
    if(message==WM_NCACTIVATE) {LRESULT result=def(h,message,w,-1);geek_about_paint_caption(h);return result;}
    if(message==WM_SETTEXT) {LRESULT result=def(h,message,w,l);geek_about_paint_caption(h);return result;}
    if(message==WM_NCHITTEST) {
        LRESULT result=def(h,message,w,l);
        if(result>=HTLEFT && result<=HTBOTTOMRIGHT)return result;
        GEEK_ABOUT_LAYOUT g;POINT screen=geek_chrome_message_point(h,message,l);
        if(geek_about_layout(h,&g)) {
            POINT point={screen.x-g.window.left,screen.y-g.window.top};
            if(geek_chrome_inside(&g.caption,point))return geek_about_close_at(h,screen)?HTCLOSE:HTCAPTION;
        }
        return result;
    }
    if(message==WM_NCLBUTTONDOWN && geek_about_close_at(h,geek_chrome_message_point(h,message,l))) {
        geek_about_state.pressed=TRUE;geek_about_state.hot=TRUE;
        API(SetCapture,0x140256EB0)(h);
        if(API(GetCapture,0x140256EB8)()!=h)geek_about_state.pressed=FALSE;
        geek_about_paint_caption(h);return 0;
    }
    if((message==WM_LBUTTONUP || message==WM_NCLBUTTONUP) && geek_about_state.pressed) {
        BOOL activate=geek_about_close_at(h,geek_chrome_message_point(h,message,l));
        geek_about_state.pressed=FALSE;geek_about_state.hot=activate;
        if(API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
        geek_about_paint_caption(h);
        if(activate)Send(h,WM_SYSCOMMAND,SC_CLOSE,0);
        return 0;
    }
    if(message==WM_NCMOUSEMOVE || (message==WM_MOUSEMOVE && geek_about_state.pressed)) {
        BOOL hot=geek_about_close_at(h,geek_chrome_message_point(h,message,l));
        if(hot!=geek_about_state.hot){geek_about_state.hot=hot;geek_about_paint_caption(h);}
        if(message==WM_NCMOUSEMOVE && !geek_about_state.tracking) {
            TRACK_MOUSE track=(TRACK_MOUSE)Proc(Module(L"user32.dll"),"TrackMouseEvent");
            TRACKMOUSEEVENT event={sizeof(event),TME_LEAVE|TME_NONCLIENT,h,0};
            if(track && track(&event))geek_about_state.tracking=TRUE;
        }
        if(hot || geek_about_state.pressed)return 0;
    }
    if(message==WM_NCMOUSELEAVE) {
        BOOL repaint=geek_about_state.hot;geek_about_state.hot=FALSE;geek_about_state.tracking=FALSE;
        if(repaint)geek_about_paint_caption(h);return 0;
    }
    if(message==WM_CANCELMODE || message==WM_CAPTURECHANGED) {
        BOOL repaint=geek_about_state.pressed || geek_about_state.hot;
        geek_about_state.pressed=FALSE;geek_about_state.hot=FALSE;
        if(message==WM_CANCELMODE && API(GetCapture,0x140256EB8)()==h)API(ReleaseCapture,0x140256EA8)();
        if(repaint)geek_about_paint_caption(h);
    }
    UINT native_theme_message=*(const UINT*)0x14033796CULL;
    if(message==WM_THEMECHANGED || message==WM_SETTINGCHANGE || message==WM_DWMCOMPOSITIONCHANGED ||
       (native_theme_message>=WM_USER && message==native_theme_message)) {
        LRESULT result=def(h,message,w,l);geek_about_refresh(h);return result;
    }
    if(message==WM_SIZE || message==WM_WINDOWPOSCHANGED || message==WM_DPICHANGED) {
        LRESULT result=def(h,message,w,l);
        if(message!=WM_SIZE || w!=SIZE_MINIMIZED) {
            geek_about_region(h);
            if(message==WM_SIZE || message==WM_DPICHANGED)geek_about_buttons_layout(h);
            geek_about_paint_caption(h);
        }
        return result;
    }
    if(message==WM_NCDESTROY) {
        GEEK_REMOVE_SUBCLASS remove=(GEEK_REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_about_subclass,id);
        HBRUSH brush=geek_about_state.background;geek_about_state=(GEEK_ABOUT_STATE){0};
        LRESULT result=def(h,message,w,l);if(brush)Delete(brush);return result;
    }
    return def(h,message,w,l);
}

static BOOL geek_about_is_class(HWND h,HWND parent,const WCHAR *expected,int length) {
    if(!h)return FALSE;
    HMODULE user=Module(L"user32.dll");
    GEEK_ABOUT_CLASS name=(GEEK_ABOUT_CLASS)Proc(user,"GetClassNameW");
    GEEK_ABOUT_PARENT getparent=(GEEK_ABOUT_PARENT)Proc(user,"GetParent");
    WCHAR text[16]={0};
    if(!name || !getparent || getparent(h)!=parent || name(h,text,16)!=length)return FALSE;
    for(int i=0;i<length;++i){WCHAR c=text[i];if(c>=L'A' && c<=L'Z')c+=L'a'-L'A';if(c!=expected[i])return FALSE;}
    return TRUE;
}

static BOOL geek_about_is_button(HWND h,HWND parent) {
    if(!geek_about_is_class(h,parent,L"button",6))return FALSE;
    LONG type=GetLong(h,GWL_STYLE)&BS_TYPEMASK;
    return type==BS_PUSHBUTTON || type==BS_DEFPUSHBUTTON;
}

static void geek_about_author_credit(HWND h) {
    HWND author=geek_about_state.author;
    if(!author || (GetLong(author,GWL_STYLE)&SS_TYPEMASK)!=SS_LEFT)return;
    RECT r,client;
    if(!API(GetWindowRect,0x140257208)(author,&r) || !Client(h,&client))return;
    MapPoints(0,h,(POINT*)&r,2);
    int dpi=dpi_of(h),height=r.bottom-r.top;
    int minimum=px(32,dpi);
    if(height<minimum)height=minimum;
    /* Keep the two-line attribution clear of the original button row. */
    int limit=client.bottom-px(16+36+8,dpi)-r.top;
    if(height>limit || r.right<=r.left)return;
    WCHAR text[256];text[0]=0;
    int length=API(GetWindowTextW,0x1402571F0)(author,text,256);
    const WCHAR credit[]=L"\nUI美化by 一千凌一夜";
    int count=(int)(sizeof(credit)/sizeof(credit[0]));
    if(length<=0 || length+count>256)return;
    for(int i=0;i<count;++i)text[length+i]=credit[i];
    if(!API(SetWindowPos,0x1402572F0)(author,0,r.left,r.top,r.right-r.left,height,SWP_NOZORDER|SWP_NOACTIVATE))return;
    API(SetWindowTextW,0x140257098)(author,text);
}

static void geek_about_install(HWND h) {
    if(!h || geek_about_state.window)return;
    GEEK_CHILD_WINDOW getwindow=(GEEK_CHILD_WINDOW)Proc(Module(L"user32.dll"),"GetWindow");
    if(geek_chrome_state.window && (!getwindow || getwindow(h,GW_OWNER)!=geek_chrome_state.window))return;
    HWND homepage=API(GetDlgItem,0x1402570A0)(h,500),close=API(GetDlgItem,0x1402570A0)(h,IDOK);
    if(!geek_about_is_button(homepage,h) || !geek_about_is_button(close,h))return;
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    if(!add || !add(h,geek_about_subclass,0x47414254,0))return;
    geek_about_state=(GEEK_ABOUT_STATE){0};geek_about_state.window=h;
    geek_about_state.homepage=homepage;geek_about_state.close=close;
    HWND version=API(GetDlgItem,0x1402570A0)(h,501);
    if(geek_about_is_class(version,h,L"static",6))geek_about_state.version=version;
    if(getwindow) {
        HWND current=getwindow(h,GW_CHILD);
        for(int n=0;current && n<32;++n) {
            LONG control_id=GetLong(current,GWL_ID);
            if((control_id==-1 || control_id==0xFFFF) && geek_about_is_class(current,h,L"static",6)) {
                if((GetLong(current,GWL_STYLE)&SS_TYPEMASK)==SS_ICON)geek_about_state.icon=current;
                else geek_about_state.author=current;
            }
            current=getwindow(current,GW_HWNDNEXT);
        }
    }
    geek_about_author_credit(h);
    HWND labels[2]={geek_about_state.version,geek_about_state.author};
    for(int i=0;i<2;++i)
        if(labels[i] && (GetLong(labels[i],GWL_STYLE)&SS_TYPEMASK)==SS_LEFT)
            add(labels[i],geek_about_label_subclass,0x4741424C,0);
    if(geek_about_state.icon && geek_about_plain_icon(geek_about_state.icon))
        add(geek_about_state.icon,geek_about_icon_subclass,0x47414249,0);
    geek_about_buttons_layout(h);geek_about_refresh(h);
    API(RedrawWindow,0x140257260)(h,0,0,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW);
}

#endif
