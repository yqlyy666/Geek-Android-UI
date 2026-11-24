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
typedef BOOL (WINAPI *REMOVE_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR);
typedef HRGN (WINAPI *ROUND_REGION)(int,int,int,int,int,int);
typedef UINT (WINAPI *DPI_WINDOW)(HWND);
typedef BOOL (WINAPI *TRACK_MOUSE)(LPTRACKMOUSEEVENT);
typedef UINT_PTR (WINAPI *GEEK_SET_TIMER)(HWND,UINT_PTR,UINT,TIMERPROC);
typedef BOOL (WINAPI *GEEK_KILL_TIMER)(HWND,UINT_PTR);

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

#define GEEK_COLUMN_LIMIT_CAPACITY 16
typedef struct {
    HWND list,header;
    int size_header_index,date_header_index;
    int fixed_size_width,fixed_date_width;
} GEEK_COLUMN_LIMIT_STATE;
static GEEK_COLUMN_LIMIT_STATE geek_column_limits[GEEK_COLUMN_LIMIT_CAPACITY];

static GEEK_COLUMN_LIMIT_STATE *geek_column_limit_state(HWND h,BOOL create) {
    GEEK_COLUMN_LIMIT_STATE *free_slot=0;
    for(int i=0;i<GEEK_COLUMN_LIMIT_CAPACITY;++i) {
        GEEK_COLUMN_LIMIT_STATE *state=&geek_column_limits[i];
        if(state->list==h && h)return state;
        if(!state->list && !free_slot)free_slot=state;
    }
    if(create && free_slot){*free_slot=(GEEK_COLUMN_LIMIT_STATE){0};free_slot->list=h;return free_slot;}
    return 0;
}

static void geek_list_capture_column_limits(void *self,HWND h,GEEK_COLUMN_LIMIT_STATE *state) {
    if(!h || !state || state->header)return;
    HWND header=(HWND)Send(h,LVM_GETHEADER,0,0);
    if(!header)return;
    int count=(int)Send(header,HDM_GETITEMCOUNT,0,0);
    int size_index=-1,date_index=-1;
    for(int i=0;i<count && i<32;++i) {
        int logical=self?((int(*)(void*,int))0x140019158ULL)(self,i):i;
        if(logical==1)size_index=i;
        if(logical==2)date_index=i;
    }
    if(size_index<0)size_index=1;
    if(date_index<0)date_index=2;
    RECT size_rect={0},date_rect={0};
    if(!Send(header,HDM_GETITEMRECT,(WPARAM)size_index,(LPARAM)&size_rect) ||
       !Send(header,HDM_GETITEMRECT,(WPARAM)date_index,(LPARAM)&date_rect))return;
    if(date_rect.right<=date_rect.left || size_rect.right<=size_rect.left)return;
    state->header=header;
    state->size_header_index=size_index;
    state->date_header_index=date_index;
    state->fixed_size_width=size_rect.right-size_rect.left;
    state->fixed_date_width=date_rect.right-date_rect.left;
}

static void geek_list_limit_header_width(HWND h,HWND header,NMHEADERW *notify) {
    GEEK_COLUMN_LIMIT_STATE *state=geek_column_limit_state(h,FALSE);
    if(!state || state->header!=header || !notify || !notify->pitem)return;
    int code=(int)notify->hdr.code;
    BOOL changing=code==(int)HDN_ITEMCHANGINGA || code==(int)HDN_ITEMCHANGINGW ||
                  code==(int)HDN_TRACKA || code==(int)HDN_TRACKW;
    if(changing && notify->iItem==state->size_header_index &&
       (notify->pitem->mask&HDI_WIDTH))
        notify->pitem->cxy=state->fixed_size_width;
}

/* Use the original header's live rectangles for every field. This follows
   native resizing, reordering, hidden columns and horizontal scrolling. */
__declspec(dllexport) void ui_card_columns(void *self,HWND h,const RECT *row,UI_COLUMNS *out) {
    GEEK_COLUMN_LIMIT_STATE *state=geek_column_limit_state(h,TRUE);
    geek_list_capture_column_limits(self,h,state);
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
    if(state && state->fixed_date_width>0 && (out->visible&(1U<<2))) {
        int date=state->fixed_date_width;
        int left=row->right-date;
        out->cell[2]=(RECT){left,row->top,row->right,row->bottom};
        if(out->visible&(1U<<1)) {
            if(out->cell[1].right>left)out->cell[1].right=left;
            if(out->cell[1].left>out->cell[1].right)out->cell[1].left=out->cell[1].right;
        }
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

#define GEEK_LIST_SORT_RECOVER (WM_APP+0x46A)
#define GEEK_LIST_SORT_STATE_CAPACITY 16
#define GEEK_LIST_SORT_TIMER_ID ((UINT_PTR)0x474B534F)
typedef BOOL (WINAPI *GEEK_POST_MESSAGE)(HWND,UINT,WPARAM,LPARAM);
static void geek_list_redraw_after_reorder(HWND h,int previous_top);
static BOOL geek_list_handle_sort_timer(HWND h,UINT_PTR timer);

typedef struct {
    HWND list;
    HWND header;
    int previous_top;
    RECT previous_rect;
    UINT_PTR recovery_timer;
    BOOL pressed;
    BOOL previous_rect_valid;
    BOOL recovery_pending;
} GEEK_LIST_SORT_STATE;

/* The main dialog may create a visible and a hidden ListView at once. Keep
   sorting state with each HWND so the hidden instance cannot steal recovery
   messages from the list whose header the user actually clicked. */
static GEEK_LIST_SORT_STATE geek_list_sort_states[GEEK_LIST_SORT_STATE_CAPACITY];

static GEEK_LIST_SORT_STATE *geek_list_sort_state(HWND list,BOOL create) {
    GEEK_LIST_SORT_STATE *free_slot=0;
    for(int i=0;i<GEEK_LIST_SORT_STATE_CAPACITY;++i) {
        GEEK_LIST_SORT_STATE *state=&geek_list_sort_states[i];
        if(state->list==list && list)return state;
        if(!state->list && !free_slot)free_slot=state;
    }
    if(create && free_slot) {
        *free_slot=(GEEK_LIST_SORT_STATE){0};
        free_slot->list=list;
        return free_slot;
    }
    return 0;
}

static void geek_list_clear_sort_recovery(HWND h) {
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,FALSE);
    if(state) {
        if(state->recovery_timer) {
            GEEK_KILL_TIMER kill=(GEEK_KILL_TIMER)Proc(Module(L"user32.dll"),"KillTimer");
            if(kill)kill(h,state->recovery_timer);
            state->recovery_timer=0;
        }
        state->recovery_pending=FALSE;
    }
}

static BOOL geek_list_get_item_rect(HWND h,int item,RECT *rect) {
    if(!rect || item<0)return FALSE;
    *rect=(RECT){LVIR_BOUNDS,0,0,0};
    return Send(h,LVM_GETITEMRECT,(WPARAM)item,(LPARAM)rect)!=0;
}

static int geek_list_capture_sort_anchor(HWND h) {
    int top=(int)Send(h,LVM_GETTOPINDEX,0,0);
    if(top<0)top=0;
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,FALSE);
    if(state) {
        state->previous_top=top;
        state->previous_rect_valid=geek_list_get_item_rect(h,top,&state->previous_rect);
    }
    return top;
}

static void geek_list_post_sort_recovery(HWND h,int previous_top) {
    /* Header notification, the list's sort message and header mouse-up can all
       observe one physical click. Keep one queued recovery per list so those
       paths cannot repaint the same owner-draw surface concurrently. */
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,FALSE);
    if(state && state->recovery_pending)return;
    if(state && (state->previous_top!=previous_top || !state->previous_rect_valid))
        geek_list_capture_sort_anchor(h);
    if(state) {
        GEEK_SET_TIMER set=(GEEK_SET_TIMER)Proc(Module(L"user32.dll"),"SetTimer");
        UINT_PTR timer=set?set(h,GEEK_LIST_SORT_TIMER_ID,10,0):0;
        if(timer) {
            state->recovery_timer=timer;
            state->recovery_pending=TRUE;
            return;
        }
    }
    GEEK_POST_MESSAGE post=(GEEK_POST_MESSAGE)Proc(Module(L"user32.dll"),"PostMessageW");
    if(post && post(h,GEEK_LIST_SORT_RECOVER,(WPARAM)previous_top,0)) {
        if(state)state->recovery_pending=TRUE;
        return;
    }
    geek_list_redraw_after_reorder(h,previous_top);
}

static LRESULT CALLBACK header_subclass(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    GEEK_LIST_SORT_STATE *state=(GEEK_LIST_SORT_STATE*)(ULONG_PTR)ref;
    HWND list=state?state->list:0;
    (void)w;(void)l;
    if(msg==WM_LBUTTONDOWN && state && state->header==h && list) {
        state->previous_top=geek_list_capture_sort_anchor(list);
        state->pressed=TRUE;
    }
    LRESULT result=def(h,msg,w,l);
    if(msg==WM_LBUTTONUP && state && state->header==h && list && state->pressed) {
        int previous_top=state->previous_top;
        state->pressed=FALSE;
        /* The Xtreme header performs Geek's sort synchronously while its
           native mouse-up handler runs. Defer recovery until that handler has
           returned so the rebuilt model and its item rectangles are current. */
        geek_list_post_sort_recovery(list,previous_top);
    }
    if(msg==WM_CANCELMODE && state)state->pressed=FALSE;
    if(msg==WM_NCDESTROY) {
        if(state && state->header==h) {
            state->header=0;state->pressed=FALSE;
            if(!state->list)*state=(GEEK_LIST_SORT_STATE){0};
        }
        REMOVE_SUBCLASS remove=(REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,header_subclass,id);
    }
    return result;
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

static void geek_list_redraw_after_reorder(HWND h,int previous_top) {
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,FALSE);
    BOOL anchor_valid=state && state->previous_top==previous_top && state->previous_rect_valid;
    int previous_top_y=anchor_valid?state->previous_rect.top:0;
    if(geek_list_hover_state.window==h) {
        geek_list_hover_state.hot=FALSE;
        geek_list_hover_state.tracking=FALSE;
    }
    /* A sorted report ListView can retain a stale pixel scroll surface while
       its model is rebuilt.  The scrollbar and accessibility tree still
       report valid rows, but the owner-draw cards remain blank until the
       native control receives a real scroll invalidation.  Re-enable drawing,
       nudge the control by one pixel in both directions, then restore the
       saved item near the same viewport before publishing the rows. */
    Send(h,WM_SETREDRAW,TRUE,0);
    int count=(int)Send(h,LVM_GETITEMCOUNT,0,0);
    if(previous_top<0)previous_top=0;
    if(count>0 && previous_top>=count)previous_top=count-1;
    if(count>0)Send(h,LVM_ENSUREVISIBLE,(WPARAM)previous_top,FALSE);
    Send(h,LVM_SCROLL,0,1);
    Send(h,LVM_SCROLL,0,-1);
    /* Calibrate the sorted row's actual viewport position against the saved
       first-row position. LVM_SCROLL direction is measured per control. */
    if(count>0 && anchor_valid) {
        RECT current={0};
        if(geek_list_get_item_rect(h,previous_top,&current)) {
            int direction=0;
            int before=current.top;
            Send(h,LVM_SCROLL,0,1);
            RECT probe={0};
            if(geek_list_get_item_rect(h,previous_top,&probe) && probe.top!=before) {
                direction=probe.top>before?1:-1;
                Send(h,LVM_SCROLL,0,-1);
            } else {
                Send(h,LVM_SCROLL,0,-1);
                if(geek_list_get_item_rect(h,previous_top,&probe) && probe.top!=before) {
                    direction=probe.top<before?1:-1;
                    Send(h,LVM_SCROLL,0,1);
                }
            }
            if(direction && geek_list_get_item_rect(h,previous_top,&current)) {
                for(int attempt=0;attempt<3;++attempt) {
                    int offset=previous_top_y-current.top;
                    if(offset< -4096)offset=-4096;
                    if(offset>4096)offset=4096;
                    if(offset==0)break;
                    Send(h,LVM_SCROLL,0,(LPARAM)(offset*direction));
                    RECT corrected={0};
                    if(!geek_list_get_item_rect(h,previous_top,&corrected))break;
                    if(corrected.top==current.top)break;
                    current=corrected;
                }
            }
        }
    }
    int top=(int)Send(h,LVM_GETTOPINDEX,0,0);
    int page=(int)Send(h,LVM_GETCOUNTPERPAGE,0,0);
    if(top<0)top=0;
    if(page<1)page=1;
    if(count>0) {
        int last=top+page-1;
        if(last>=count)last=count-1;
        for(int item=top;item<=last;++item)Send(h,LVM_UPDATE,(WPARAM)item,0);
        Send(h,LVM_REDRAWITEMS,(WPARAM)top,(LPARAM)last);
    }
    Redraw(h,0,0,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    /* The parent view may repaint after the list's deferred recovery and can
       otherwise leave the owner-draw child covered by a freshly filled
       background. Invalidate both surfaces after the list has its rows. */
    typedef HWND (WINAPI *GET_PARENT)(HWND);
    typedef BOOL (WINAPI *UPDATE_WINDOW)(HWND);
    GET_PARENT get_parent=(GET_PARENT)Proc(Module(L"user32.dll"),"GetParent");
    UPDATE_WINDOW update_window=(UPDATE_WINDOW)Proc(Module(L"user32.dll"),"UpdateWindow");
    HWND parent=get_parent?get_parent(h):0;
    if(parent && parent!=h) {
        Invalidate(parent,0,TRUE);
        Redraw(parent,0,0,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        if(update_window)update_window(parent);
    }
    Invalidate(h,0,TRUE);
    if(update_window)update_window(h);
    if(state)state->previous_rect_valid=FALSE;
}

static BOOL geek_list_handle_sort_timer(HWND h,UINT_PTR timer) {
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,FALSE);
    if(!state || !state->recovery_pending || !timer || state->recovery_timer!=timer)return FALSE;
    GEEK_KILL_TIMER kill=(GEEK_KILL_TIMER)Proc(Module(L"user32.dll"),"KillTimer");
    if(kill)kill(h,timer);
    state->recovery_timer=0;
    state->recovery_pending=FALSE;
    geek_list_redraw_after_reorder(h,state->previous_top);
    return TRUE;
}

static void geek_list_queue_sort_recovery(HWND h,int previous_top) {
    geek_list_post_sort_recovery(h,previous_top);
}

static LRESULT CALLBACK list_subclass(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    GEEK_LIST_SORT_STATE *state=(GEEK_LIST_SORT_STATE*)(ULONG_PTR)ref;
    if(msg==WM_NOTIFY && l) {
        /* The header sends its click notification to the ListView first.
           Geek's XTP handler can consume that notification without ever
           issuing LVM_SORTITEMS or forwarding LVN_COLUMNCLICK to the outer
           view.  Catch both ANSI and Unicode header click codes here, after
           the native list handler returns, so a prior pixel scroll offset
           cannot hide the newly sorted owner-draw rows. */
        NMHDR *notify=(NMHDR*)l;
        HWND header=(HWND)Send(h,LVM_GETHEADER,0,0);
        int code=(int)notify->code;
        if(header && notify->hwndFrom==header)
            geek_list_limit_header_width(h,header,(NMHEADERW*)notify);
        BOOL header_click=header && notify->hwndFrom==header &&
            (code==(int)HDN_ITEMCLICKA || code==(int)HDN_ITEMCLICKW);
        int previous_top=header_click?geek_list_capture_sort_anchor(h):0;
        LRESULT result=def(h,msg,w,l);
        if(header_click)geek_list_queue_sort_recovery(h,previous_top);
        return result;
    }
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
    if(msg==WM_SETREDRAW) {
        /* The parent can toggle redraw from inside its LVN_COLUMNCLICK
           handler, while its model is still being reordered. Do not force a
           synchronous owner-draw paint from this nested message; the parent
           notification hook repaints after the native sort handler returns. */
        return def(h,msg,w,l);
    }
    if(msg==GEEK_LIST_SORT_RECOVER) {
        geek_list_clear_sort_recovery(h);
        geek_list_redraw_after_reorder(h,(int)w);
        return 0;
    }
    if(msg==WM_TIMER && state && geek_list_handle_sort_timer(h,(UINT_PTR)w))return 0;
    if(msg==LVM_SORTITEMS || msg==LVM_SORTITEMSEX) {
        int previous_top=geek_list_capture_sort_anchor(h);
        LRESULT result=def(h,msg,w,l);
        geek_list_queue_sort_recovery(h,previous_top);
        return result;
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
        geek_list_clear_sort_recovery(h);
        if(state && state->list==h)*state=(GEEK_LIST_SORT_STATE){0};
    }
    return def(h,msg,w,l);
}

__declspec(dllexport) void ui_pre_subclass(void *self) {
    ((void(*)(void*))0x14001A4C4ULL)(self);
    HWND h=*(HWND*)((char*)self+0x40);
    SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
    GEEK_LIST_SORT_STATE *state=geek_list_sort_state(h,TRUE);
    GEEK_COLUMN_LIMIT_STATE *columns=geek_column_limit_state(h,TRUE);
    geek_list_capture_column_limits(self,h,columns);
    add(h,list_subclass,0x4D415455,(DWORD_PTR)state);
    HWND header=(HWND)Send(h,LVM_GETHEADER,0,0);
    GEEK_COLUMN_LIMIT_STATE *columns=geek_column_limit_state(h,TRUE);
    geek_list_capture_column_limits(self,h,columns);
    if(header && state && state->header!=header) {
        state->header=header;state->pressed=FALSE;
        add(header,header_subclass,0x48445253,(DWORD_PTR)state);
    }
    /* The original header is kept as the working sort controls. Card rendering
       no longer depends on its column widths. */
    Send(h,LVM_SETBKCOLOR,0,dark()?RGB(20,18,24):RGB(246,244,249));
    Send(h,LVM_SETTEXTBKCOLOR,0,CLR_NONE);
}

/* Freeze the Size separator at its original width so Install Time keeps its
   right-hand column boundary while the list is resized or reordered. */
__declspec(dllexport) void ui_sort_from_top(void *self,NMHDR *notify,LRESULT *result) {
    HWND h=*(HWND*)((char*)self+0x40);
    if(h && (int)Send(h,LVM_GETTOPINDEX,0,0)!=0) {
        Send(h,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
        if((int)Send(h,LVM_GETTOPINDEX,0,0)!=0)
            Send(h,LVM_ENSUREVISIBLE,0,FALSE);
    }
    ((void(*)(void*,NMHDR*,LRESULT*))0x140019970ULL)(self,notify,result);
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

#include "native-search-style.h"
#include "native-status-layout.h"
#include "native-menu-opaque.h"
#include "native-chrome.h"
#include "native-about.h"
#include "native-scan-dialog.h"

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
