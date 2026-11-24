#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commctrl.h>

/* These are Geek's existing import slots. No DLL, runtime, loader or frontend
   is added: every hook executes inside the original native process. */
#define API(name, address) ((__typeof__(&name))(*(void **)(address##ULL)))
#define Send API(SendMessageW,0x140256F28)
#define Client API(GetClientRect,0x140257200)
#define Fill API(FillRect,0x140257268)
#define Brush API(CreateSolidBrush,0x1402563D0)
#define Pen API(CreatePen,0x140256378)
#define Select API(SelectObject,0x1402563A8)
#define Delete API(DeleteObject,0x140256380)
#define Round API(RoundRect,0x1402563C8)
#define Text API(DrawTextW,0x1402571C0)
#define TextColor API(SetTextColor,0x140256390)
#define BkMode API(SetBkMode,0x140256398)
#define Font API(CreateFontIndirectW,0x140256358)
#define Save API(SaveDC,0x1402561E0)
#define Restore API(RestoreDC,0x1402561D0)
#define Proc API(GetProcAddress,0x140256A78)
#define Module API(GetModuleHandleW,0x140256AC0)
#define Cursor API(GetCursorPos,0x140257210)
#define ToScreen API(ClientToScreen,0x1402570C8)
#define Invalidate API(InvalidateRect,0x1402572A0)
#define Redraw API(RedrawWindow,0x140257260)
#define DrawIconNative API(DrawIconEx,0x1402572D8)
#define GetLong API(GetWindowLongW,0x140256FA8)
#define SetLong API(SetWindowLongW,0x140256FA0)
#define MapPoints API(MapWindowPoints,0x140257118)

typedef BOOL (WINAPI *SET_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR,DWORD_PTR);
typedef LRESULT (WINAPI *DEF_SUBCLASS)(HWND,UINT,WPARAM,LPARAM);
typedef HRGN (WINAPI *ROUND_REGION)(int,int,int,int,int,int);
typedef UINT (WINAPI *DPI_WINDOW)(HWND);
typedef BOOL (WINAPI *TRACK_MOUSE)(LPTRACKMOUSEEVENT);

static int dpi_of(HWND h) {
    DPI_WINDOW fn=(DPI_WINDOW)Proc(Module(L"user32.dll"),"GetDpiForWindow");
    UINT dpi=fn?fn(h):96;
    return dpi>=96 && dpi<=768?(int)dpi:96;
}
static int px(int v,int dpi) {return (v*dpi+48)/96;}
static BOOL dark(void) {return ((BOOL (*)(void))0x14000C49CULL)() & 255;}
static void solid(HDC dc,RECT *r,COLORREF c) {
    HBRUSH b=Brush(c); Fill(dc,r,b); Delete(b);
}
static void rounded(HDC dc,RECT r,int rad,COLORREF c,COLORREF edge) {
    HBRUSH b=Brush(c); HPEN p=Pen(PS_SOLID,1,edge);
    HGDIOBJ ob=Select(dc,b), op=Select(dc,p);
    Round(dc,r.left,r.top,r.right,r.bottom,rad,rad);
    Select(dc,op);Select(dc,ob);Delete(p);Delete(b);
}
static HFONT font_at(int dpi,int height,int weight) {
    LOGFONTW f={0}; f.lfHeight=-px(height,dpi); f.lfWeight=weight;
    f.lfCharSet=DEFAULT_CHARSET; f.lfQuality=CLEARTYPE_QUALITY;
    const WCHAR *s=L"Segoe UI";
    for(int i=0;s[i];++i)f.lfFaceName[i]=s[i];
    return Font(&f);
}
static void get_text(HWND h,int index,int column,WCHAR *buf,int length) {
    LVITEMW item={0}; item.iSubItem=column;item.pszText=buf;
    item.cchTextMax=length;buf[0]=0;
    Send(h,LVM_GETITEMTEXTW,(WPARAM)index,(LPARAM)&item);
    buf[length-1]=0;
}
static void model_text(void *model,HWND h,int index,int column,WCHAR *out,int cap) {
    if(!model){get_text(h,index,column,out,cap);return;}
    WCHAR *s=0;
    ((void*(*)(void*,WCHAR**,int))(*(void***)model)[1])(model,&s,column);
    int n=0;if(s){while(s[n] && n<cap-1){out[n]=s[n];++n;}}out[n]=0;
    /* This is the original row getter's CStringW return contract. Release the
       temporary through its own ATL manager, exactly as native GetDispInfo. */
    if(s){char *data=(char*)s-0x18;
        if(InterlockedDecrement((volatile LONG*)(data+0x10))<=0){
            void *mgr=*(void**)data;
            ((void(*)(void*,void*))(*(void***)mgr)[1])(mgr,data);
        }
    }
}
static int append(WCHAR *out,int pos,int cap,const WCHAR *s) {
    if(!s)return pos;
    while(*s && pos<cap-1)out[pos++]=*s++;
    out[pos]=0;return pos;
}

__declspec(dllexport) void ui_list_measure(void *self,MEASUREITEMSTRUCT *m) {
    if(!m)return;
    int height=px(42,dpi_of(*(HWND*)((char*)self+0x40)));
    *(int*)((char*)self+0x240)=height;
    m->itemHeight=height;
}

typedef struct {RECT cell[3];UINT visible;} UI_COLUMNS;

/* Use the original header's live rectangles for every field. This follows
   native resizing, reordering, hidden columns and horizontal scrolling. */
__declspec(dllexport) void ui_card_columns(void *self,HWND h,const RECT *row,UI_COLUMNS *out) {
    for(int i=0;i<3;++i)out->cell[i]=*row;
    out->visible=0;
    HWND header=(HWND)Send(h,LVM_GETHEADER,0,0);
    int count=header?(int)Send(header,HDM_GETITEMCOUNT,0,0):0;
    if(count>32)count=32;
    for(int i=0;i<count;++i){
        int logical=((int(*)(void*,int))0x140019158ULL)(self,i);
        if(logical<0 || logical>2)continue;
        RECT cell;
        if(!Send(header,HDM_GETITEMRECT,(WPARAM)i,(LPARAM)&cell))continue;
        MapPoints(header,h,(POINT*)&cell,2);
        if(cell.right<=cell.left)continue;
        cell.top=row->top;cell.bottom=row->bottom;
        out->cell[logical]=cell;out->visible|=1U<<logical;
    }
    if(count<=0){
        int dpi=dpi_of(h),size=px(92,dpi),date=px(132,dpi);
        int split=row->right-size-date;
        if(split<row->left)split=row->left;
        out->cell[0].right=split;
        out->cell[1].left=split;out->cell[1].right=row->right-date;
        out->cell[2].left=row->right-date;out->visible=7;
    }
}

static void geek_card_content(void *self,DRAWITEMSTRUCT *item) {
    if(!item || item->itemID==(UINT)-1)return;
    HWND h=*(HWND*)((char*)self+0x40);HDC dc=item->hDC;
    int dpi=dpi_of(h), saved=Save(dc);BOOL isdark=dark();
    COLORREF back=isdark?RGB(20,18,24):RGB(246,244,249);
    COLORREF card=isdark?RGB(33,31,38):RGB(255,253,255);
    COLORREF edge=isdark?RGB(59,56,66):RGB(228,224,234);
    COLORREF text=isdark?RGB(236,231,240):RGB(31,28,35);
    COLORREF muted=isdark?RGB(184,177,192):RGB(108,102,115);
    BOOL selected=(item->itemState&ODS_SELECTED)!=0;
    RECT cr;Client(h,&cr);RECT row=item->rcItem;row.left=0;row.right=cr.right;
    solid(dc,&row,back);
    RECT r={px(12,dpi),row.top+px(2,dpi),cr.right-px(12,dpi),row.bottom-px(2,dpi)};
    POINT cursor={0};Cursor(&cursor);POINT origin={0};ToScreen(h,&origin);
    cursor.x-=origin.x;cursor.y-=origin.y;
    BOOL hot=cursor.y>=row.top && cursor.y<row.bottom && cursor.x>=r.left && cursor.x<r.right;
    if(selected){card=isdark?RGB(53,43,68):RGB(237,230,249);edge=isdark?RGB(128,105,162):RGB(185,166,214);}
    else if(hot){card=isdark?RGB(42,38,49):RGB(249,245,255);edge=isdark?RGB(79,70,93):RGB(211,199,230);}
    rounded(dc,r,px(12,dpi),card,edge);
    UI_COLUMNS columns;ui_card_columns(self,h,&row,&columns);
    void **begin=*(void***)((char*)self+0x218), **end=*(void***)((char*)self+0x220);
    void *model=0;
    if(begin && item->itemID<(UINT)(end-begin))model=begin[item->itemID];
    RECT namecol=columns.cell[0];
    if(namecol.left<r.left)namecol.left=r.left;
    if(namecol.right>r.right)namecol.right=r.right;
    int iconx=namecol.left+px(12,dpi), icony=row.top+px(9,dpi);
    if(model && (columns.visible&1) && namecol.right-namecol.left>px(44,dpi)) {
        HICON icon=((HICON(*)(void*,BOOL))(*(void***)model)[2])(model,TRUE);
        if(icon)DrawIconNative(dc,iconx,icony,icon,px(24,dpi),px(24,dpi),0,0,DI_NORMAL);
    }
    WCHAR name[384],size[64],date[64],detail[512];
    model_text(model,h,item->itemID,0,name,384);model_text(model,h,item->itemID,1,size,64);
    model_text(model,h,item->itemID,2,date,64);detail[0]=0;int len=0;
    if(model && *(ULONG_PTR*)model==0x140261BC8ULL) {
        const WCHAR *version=*(const WCHAR**)((char*)model+0x80);
        const WCHAR *publisher=*(const WCHAR**)((char*)model+0x88);
        if(version && *version)len=append(detail,len,512,version);
        if(publisher && *publisher){if(len)len=append(detail,len,512,L"   |   ");len=append(detail,len,512,publisher);}
    }
    if(!detail[0])append(detail,0,512,L"已安装的应用");
    BkMode(dc,TRANSPARENT);
    HFONT mainfont=font_at(dpi,14,FW_MEDIUM), subfont=font_at(dpi,11,FW_NORMAL);
    Select(dc,mainfont);TextColor(dc,text);
    RECT title={namecol.left+px(48,dpi),row.top+px(3,dpi),namecol.right-px(8,dpi),row.top+px(21,dpi)};
    if((columns.visible&1) && title.right>title.left)
        Text(dc,name,-1,&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    Select(dc,subfont);TextColor(dc,muted);
    RECT subtitle={title.left,row.top+px(21,dpi),title.right,row.top+px(38,dpi)};
    if((columns.visible&1) && subtitle.right>subtitle.left)
        Text(dc,detail,-1,&subtitle,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    for(int logical=1;logical<=2;++logical){
        if(!(columns.visible&(1U<<logical)))continue;
        RECT cell=columns.cell[logical];
        cell.left+=px(8,dpi);cell.right-=px(8,dpi);
        if(cell.left<r.left+px(6,dpi))cell.left=r.left+px(6,dpi);
        if(cell.right>r.right-px(6,dpi))cell.right=r.right-px(6,dpi);
        cell.top=r.top;cell.bottom=r.bottom;
        if(cell.right<=cell.left)continue;
        Text(dc,logical==1?size:date,-1,&cell,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(logical==1?DT_RIGHT:DT_CENTER));
    }
    Restore(dc,saved);Delete(mainfont);Delete(subfont);
}

/* Owner-draw notifications are not restricted to the outer WM_PAINT path.
   Publish the entire row, including the left/right card gutters, only after
   its GDI RGB drawing has an opaque alpha channel.  Drawing into the live
   list DC and correcting it after WM_PAINT can expose transparent gutters
   during rapid hover/scroll updates. Keep the native notification untouched. */
typedef HANDLE (WINAPI *GEEK_CARD_BEGIN)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *GEEK_CARD_ALPHA)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_CARD_END)(HANDLE,BOOL);

__declspec(dllexport) void ui_draw_card(void *self,DRAWITEMSTRUCT *item) {
    if(!item || item->itemID==(UINT)-1)return;
    GEEK_CARD_BEGIN begin=(GEEK_CARD_BEGIN)(*(void**)0x140257488ULL);
    GEEK_CARD_ALPHA alpha=(GEEK_CARD_ALPHA)(*(void**)0x140257498ULL);
    GEEK_CARD_END end=(GEEK_CARD_END)(*(void**)0x140257490ULL);
    HWND h=*(HWND*)((char*)self+0x40);RECT client;
    if(item->hDC && begin && alpha && end && Client(h,&client)) {
        RECT row={0,item->rcItem.top,client.right,item->rcItem.bottom};
        if(row.right>row.left && row.bottom>row.top) {
            HDC dc=0;HANDLE buffer=begin(item->hDC,&row,2,0,&dc);
            if(buffer && dc) {
                DRAWITEMSTRUCT drawing=*item;drawing.hDC=dc;
                geek_card_content(self,&drawing);
                if(SUCCEEDED(alpha(buffer,0,255))) {
                    if(SUCCEEDED(end(buffer,TRUE)))return;
                } else end(buffer,FALSE);
            } else if(buffer)end(buffer,FALSE);
        }
    }
    geek_card_content(self,item);
}

/* Defined by native-surface.h below. Either list subclass can be installed
   last during native initialization, so both erase paths must publish an
   opaque fill instead of depending on the other subclass's WM_PAINT tail. */
static BOOL geek_surface_buffer(HDC target,const RECT *r,COLORREF color,BOOL fill,
                                BOOL nonclient,const RECT *exclude);

typedef struct {
    HWND window;
    int item;
    BOOL hot;
    BOOL tracking;
} GEEK_LIST_HOVER_STATE;

static GEEK_LIST_HOVER_STATE geek_list_hover_state;

/* Geek clears its virtual list while sorting. Starting that native rebuild
   at a nonzero top index leaves the control with a negative scroll origin. */
__declspec(dllexport) void ui_sort_from_top(void *self,NMHDR *notify,LRESULT *result) {
    HWND h=*(HWND*)((char*)self+0x40);
    if(h && (int)Send(h,LVM_GETTOPINDEX,0,0)!=0) {
        Send(h,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
        if((int)Send(h,LVM_GETTOPINDEX,0,0)!=0)
            Send(h,LVM_ENSUREVISIBLE,0,FALSE);
    }
    ((void(*)(void*,NMHDR*,LRESULT*))0x140019970ULL)(self,notify,result);
}

static BOOL geek_list_hot_item(HWND h,int *item) {
    RECT client;POINT point={0},origin={0};
    if(!Client(h,&client) || !Cursor(&point) || !ToScreen(h,&origin))return FALSE;
    point.x-=origin.x;point.y-=origin.y;
    int inset=px(12,dpi_of(h));
    if(point.x<inset || point.x>=client.right-inset || point.y<0 || point.y>=client.bottom)return FALSE;
    int count=(int)Send(h,LVM_GETITEMCOUNT,0,0);
    if(count<=0)return FALSE;
    int top=(int)Send(h,LVM_GETTOPINDEX,0,0);
    if(top<0 || top>=count)return FALSE;
    RECT first={LVIR_BOUNDS,0,0,0};
    if(!Send(h,LVM_GETITEMRECT,(WPARAM)top,(LPARAM)&first))return FALSE;
    int height=first.bottom-first.top;
    if(height<=0 || point.y<first.top)return FALSE;
    int current=top+(point.y-first.top)/height;
    if(current<top || current>=count)return FALSE;
    RECT row={LVIR_BOUNDS,0,0,0};
    if(current==top)row=first;
    else if(!Send(h,LVM_GETITEMRECT,(WPARAM)current,(LPARAM)&row))return FALSE;
    if(point.y<row.top || point.y>=row.bottom)return FALSE;
    *item=current;return TRUE;
}

static void geek_list_invalidate_item(HWND h,int item) {
    RECT client,row={LVIR_BOUNDS,0,0,0};
    if(item<0 || !Client(h,&client) || !Send(h,LVM_GETITEMRECT,(WPARAM)item,(LPARAM)&row))return;
    row.left=0;row.right=client.right;
    if(row.top<0)row.top=0;
    if(row.bottom>client.bottom)row.bottom=client.bottom;
    if(row.right>0 && row.bottom>row.top)Invalidate(h,&row,FALSE);
}

static void geek_list_update_hover(HWND h) {
    if(geek_list_hover_state.window!=h) {
        geek_list_hover_state=(GEEK_LIST_HOVER_STATE){0};
        geek_list_hover_state.window=h;
    }
    int current=0;BOOL hot=geek_list_hot_item(h,&current);
    BOOL oldhot=geek_list_hover_state.hot;int previous=geek_list_hover_state.item;
    if(oldhot==hot && (!hot || previous==current))return;
    geek_list_hover_state.hot=hot;geek_list_hover_state.item=current;
    if(oldhot)geek_list_invalidate_item(h,previous);
    if(hot)geek_list_invalidate_item(h,current);
}


static LRESULT CALLBACK list_subclass(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    if(msg==WM_MOUSEMOVE) {
        LRESULT result=def(h,msg,w,l);
        /* Native hover notifications can repeat while the pointer stays in
           one row. Repainting every row for each event makes foreground
           wheel scrolling much more expensive than inactive scrolling. */
        geek_list_update_hover(h);
        if(!geek_list_hover_state.tracking) {
            TRACK_MOUSE fn=(TRACK_MOUSE)Proc(Module(L"user32.dll"),"TrackMouseEvent");
            TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};
            if(fn && fn(&track))geek_list_hover_state.tracking=TRUE;
        }
        return result;
    }
    if(msg==WM_MOUSELEAVE) {
        LRESULT result=def(h,msg,w,l);
        /* A single leave repaint also clears any native copied-scroll hot
           pixels from before the first cached mouse position. */
        if(geek_list_hover_state.window==h) {
            geek_list_hover_state.hot=FALSE;geek_list_hover_state.tracking=FALSE;
        }
        Invalidate(h,0,FALSE);return result;
    }
    if(msg==WM_MOUSEWHEEL || msg==WM_MOUSEHWHEEL || msg==WM_VSCROLL || msg==WM_HSCROLL ||
       msg==LVM_SCROLL || msg==LVM_ENSUREVISIBLE || msg==WM_KEYDOWN || msg==WM_PAINT) {
        LRESULT result=def(h,msg,w,l);
        /* Rows can move beneath a stationary pointer during scrolling,
           keyboard navigation or filtering. Changed state is cached before
           invalidation, so a follow-up WM_PAINT does not form a paint loop. */
        geek_list_update_hover(h);return result;
    }
    if(msg==WM_ERASEBKGND) {
        RECT r;Client(h,&r);COLORREF back=dark()?RGB(20,18,24):RGB(246,244,249);
        if(!geek_surface_buffer((HDC)w,&r,back,TRUE,FALSE,0))solid((HDC)w,&r,back);
        return 1;
    }
    if(msg==WM_NCDESTROY) {
        if(geek_list_hover_state.window==h)
            geek_list_hover_state=(GEEK_LIST_HOVER_STATE){0};
    }
    return def(h,msg,w,l);
}

__declspec(dllexport) void ui_pre_subclass(void *self) {
    ((void(*)(void*))0x14001A4C4ULL)(self);
    HWND h=*(HWND*)((char*)self+0x40);
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    add(h,list_subclass,0x4D415455,0);
    /* The original header is kept as the working sort controls. Card rendering
       no longer depends on its column widths. */
    Send(h,LVM_SETBKCOLOR,0,dark()?RGB(20,18,24):RGB(246,244,249));
    Send(h,LVM_SETTEXTBKCOLOR,0,CLR_NONE);
}

#include "native-tooltip.h"

__declspec(dllexport) void ui_round_popup(void *paint,void *popup) {
    HWND h=*(HWND*)((char*)popup+0x40);RECT r;
    if(!h || !Client(h,&r))return;
    int radius=px(22,dpi_of(h));
    ROUND_REGION make=(ROUND_REGION)Proc(Module(L"gdi32.dll"),"CreateRoundRectRgn");
    if(make){HRGN region=make(0,0,r.right+1,r.bottom+1,radius,radius);
        if(region && !API(SetWindowRgn,0x140257158)(h,region,FALSE))Delete(region);}
}

__declspec(dllexport) void ui_menu_background(void *paint,void *cdc,void *bar) {
    int type=*(int*)((char*)bar+0x180);
    BOOL menubar=*(ULONG_PTR*)bar==0x14028C560ULL;
    if(!menubar && type!=2) {
        ((void(*)(void*,void*,void*))(*(void***)paint)[0xD8/8])(paint,cdc,bar);
        return;
    }
    HWND h=*(HWND*)((char*)bar+0x40);HDC dc=*(HDC*)((char*)cdc+8);RECT r;
    if(!h || !dc || !Client(h,&r))return;
    int saved=Save(dc),dpi=dpi_of(h);
    BOOL isdark=dark();
    COLORREF back=menubar?(isdark?RGB(20,18,24):RGB(246,244,249)):
                              (isdark?RGB(33,31,38):RGB(255,251,254));
    solid(dc,&r,back);
    if(!menubar)rounded(dc,r,px(36,dpi),back,isdark?RGB(77,70,86):RGB(225,216,232));
    Restore(dc,saved);
}

typedef struct {int cx,cy;} UI_SIZE;
#include "native-popup-items.h"
#include "native-menubar-items.h"
#include "native-popup-panel.h"
#include "native-popup-separator.h"
__declspec(dllexport) UI_SIZE *ui_menu_measure(void *paint,UI_SIZE *out,void *dc,void *control,BOOL draw) {
    int type=*(int*)((char*)control+0x120);
    if(type>=1 && type<=4)return ui_popup_item(paint,out,dc,control,draw);
    typedef UI_SIZE *(*ORIGINAL)(void*,UI_SIZE*,void*,void*,BOOL);
    ORIGINAL original=(ORIGINAL)(*(void***)paint)[0xA0/8];
    UI_SIZE *result=original(paint,out,dc,control,draw);
    if(!draw){
        void *parent=*(void**)((char*)control+0x128);
        HWND h=parent?*(HWND*)((char*)parent+0x40):0;
        int dpi=dpi_of(h),minimum=px(36,dpi);
        out->cy+=px(10,dpi);if(out->cy<minimum)out->cy=minimum;
        out->cx+=px(20,dpi);
    }
    return result;
}

__declspec(dllexport) void ui_menu_highlight(void *theme,HDC dc,int part,int state,const RECT *rect,const RECT *clip) {
    if(!dc || !rect)return;
    RECT r=*rect;int dpi=96;
    /* Menu font height follows the application's existing per-monitor scale. */
    int height=r.bottom-r.top;
    if(height>=60)dpi=192;else if(height>=45)dpi=144;
    r.left+=px(5,dpi);r.right-=px(5,dpi);r.top+=px(2,dpi);r.bottom-=px(2,dpi);
    COLORREF c=dark()?RGB(76,61,93):RGB(232,221,248);
    rounded(dc,r,px(12,dpi),c,c);
}

__declspec(dllexport) void ui_menu_highlight_fallback(void *paint,void *cdc,const RECT *r,int color,int border) {
    if(cdc)ui_menu_highlight(0,*(HDC*)((char*)cdc+8),14,2,r,0);
}

#include "native-search-style-sort-from-top.h"
#include "native-status-layout.h"
#include "native-menu-opaque.h"
#include "native-chrome.h"
#include "native-about.h"
#include "native-scan-dialog.h"

/* Linker entry for the extracted helper image. */
__declspec(dllexport) void native_probe(void) {}

/* CDlgAboutGeek alone owns this OnInitDialog slot. Let its native code
   localize the existing controls and populate version information first. */
__declspec(dllexport) BOOL ui_about_init(void *about) {
    BOOL result=((BOOL(*)(void*))0x140004E08ULL)(about);
    geek_about_install(*(HWND*)((char*)about+0x40));
    return result;
}

/* CWizardDlg owns this exact scan-dialog initialization slot. Keep the
   original MFC initialization and all scan commands intact. */
__declspec(dllexport) BOOL ui_scan_init(void *scan) {
    BOOL result=((BOOL(*)(void*))0x14005A80CULL)(scan);
    if(result)geek_scan_install(*(HWND*)((char*)scan+0x40));
    return result;
}
