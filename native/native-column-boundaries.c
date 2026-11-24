#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#define API(name,address) ((__typeof__(&name))(*(void **)(address##ULL)))
#define Send API(SendMessageW,0x140256F28)
#define Client API(GetClientRect,0x140257200)
#define Proc API(GetProcAddress,0x140256A78)
#define Module API(GetModuleHandleW,0x140256AC0)
#define AddSubclass API(SetWindowSubclass,0x1402560A8)
#define SUBCLASS_ID ((UINT_PTR)0x434F4C4C)
#define COLUMN_LIMIT_CAPACITY 8

typedef LRESULT (WINAPI *DEF_SUBCLASS)(HWND,UINT,WPARAM,LPARAM);
typedef BOOL (WINAPI *REMOVE_SUBCLASS)(HWND,SUBCLASSPROC,UINT_PTR);
typedef UINT (WINAPI *DPI_WINDOW)(HWND);

typedef struct {
    void *self;
    HWND list,header;
    int width[3],index[3],start_width[3];
    int start_x,client_width,divider;
    BOOL installed,busy,dragging,normalized;
} COLUMN_STATE;
_Static_assert(sizeof(COLUMN_STATE)==88,"unexpected column state size");
#define states (*(COLUMN_STATE (*)[COLUMN_LIMIT_CAPACITY])0x140412858ULL)

static COLUMN_STATE *get_state(HWND list,BOOL create) {
    COLUMN_STATE *free_slot=0;
    for(int i=0;i<COLUMN_LIMIT_CAPACITY;++i) {
        if(states[i].list==list && list)return &states[i];
        if(!states[i].list && !free_slot)free_slot=&states[i];
    }
    if(create && free_slot) {*free_slot=(COLUMN_STATE){0};free_slot->list=list;return free_slot;}
    return 0;
}
static int dpi_of(HWND h) {
    DPI_WINDOW fn=(DPI_WINDOW)Proc(Module(L"user32.dll"),"GetDpiForWindow");
    UINT dpi=fn?fn(h):96;return dpi>=96&&dpi<=768?(int)dpi:96;
}
static int pixels(int n,int dpi) {return (n*dpi+48)/96;}
static int clamp(int n,int low,int high) {return n<low?low:(n>high?high:n);}
static DEF_SUBCLASS native_subclass(void) {return (DEF_SUBCLASS)(*(void**)0x1402560B8ULL);}

static BOOL read_columns(COLUMN_STATE *s,int *width) {
    if(!s || !s->header || (int)Send(s->header,HDM_GETITEMCOUNT,0,0)!=3)return FALSE;
    for(int logical=0;logical<3;++logical) {
        int n=(int)Send(s->list,LVM_GETCOLUMNWIDTH,(WPARAM)s->index[logical],0);
        if(n<=0)return FALSE;
        width[logical]=n;
    }
    return TRUE;
}
static int viewport(COLUMN_STATE *s) {
    RECT r={0};return Client(s->list,&r)?r.right-r.left:0;
}
static void minimums(COLUMN_STATE *s,int total,int *minimum) {
    int dpi=dpi_of(s->list);
    minimum[0]=pixels(150,dpi);minimum[1]=pixels(60,dpi);minimum[2]=pixels(100,dpi);
    if(total<minimum[0]+minimum[1]+minimum[2]) {
        minimum[0]=total*45/100;minimum[1]=total*20/100;minimum[2]=total*25/100;
    }
}

/* Internal dividers transfer width between neighbors. The endpoints always
   stay at the viewport edges. Shrink first to avoid transient hscroll. */
static void apply(COLUMN_STATE *s,const int *width,int total) {
    if(s->busy)return;
    s->busy=TRUE;
    int previous[3]={0};read_columns(s,previous);
    for(int pass=0;pass<2;++pass)for(int i=0;i<3;++i) {
        BOOL shrinking=width[i]<previous[i];
        if(width[i]!=previous[i] && shrinking==(pass==0))
            Send(s->list,LVM_SETCOLUMNWIDTH,(WPARAM)s->index[i],width[i]);
    }
    int offset=API(GetScrollPos,0x1402573B0)(s->list,SB_HORZ);
    if(offset)Send(s->list,LVM_SCROLL,(WPARAM)(INT_PTR)-offset,0);
    for(int i=0;i<3;++i)s->width[i]=width[i];
    s->client_width=total;s->normalized=TRUE;s->busy=FALSE;
    API(InvalidateRect,0x1402572A0)(s->list,0,FALSE);
    API(InvalidateRect,0x1402572A0)(s->header,0,FALSE);
}
static void normalize(COLUMN_STATE *s,BOOL use_current) {
    if(!s || s->busy || s->dragging || !s->installed)return;
    int total=viewport(s),current[3]={0};
    if(total<=0 || !read_columns(s,current))return;
    int minimum[3];minimums(s,total,minimum);
    int date=use_current||!s->normalized?current[2]:s->width[2];
    int size=use_current||!s->normalized?current[1]:s->width[1];
    date=clamp(date,minimum[2],total-minimum[0]-minimum[1]);
    size=clamp(size,minimum[1],total-date-minimum[0]);
    int widths[3]={total-date-size,size,date};
    if(current[0]!=widths[0] || current[1]!=size || current[2]!=date)
        apply(s,widths,total);
    else {
        for(int i=0;i<3;++i)s->width[i]=widths[i];
        s->client_width=total;s->normalized=TRUE;
    }
}
static int divider_at(COLUMN_STATE *s,int x) {
    int width[3];if(!read_columns(s,width))return -1;
    int t=pixels(5,dpi_of(s->header));
    if(x>=width[0]-t && x<=width[0]+t)return 0;
    if(x>=width[0]+width[1]-t && x<=width[0]+width[1]+t)return 1;
    if(x>=s->client_width-t)return 2;
    return -1;
}
static void end_drag(COLUMN_STATE *s) {
    if(!s->dragging)return;
    s->dragging=FALSE;
    if(API(GetCapture,0x140256EB8)()==s->header)API(ReleaseCapture,0x140256EA8)();
}
static void drag_to(COLUMN_STATE *s,int x) {
    int minimum[3],widths[3],total=s->client_width;
    minimums(s,total,minimum);
    for(int i=0;i<3;++i)widths[i]=s->start_width[i];
    int i=s->divider,j=i+1;
    int delta=clamp(x-s->start_x,minimum[i]-widths[i],widths[j]-minimum[j]);
    widths[i]+=delta;widths[j]-=delta;
    apply(s,widths,total);
}
static void remove_hook(HWND h,SUBCLASSPROC callback,UINT_PTR id) {
    REMOVE_SUBCLASS remove=(REMOVE_SUBCLASS)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
    if(remove)remove(h,callback,id);
}

static LRESULT CALLBACK header_hook(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    COLUMN_STATE *s=(COLUMN_STATE*)(ULONG_PTR)ref;
    DEF_SUBCLASS def=native_subclass();
    if(!s || s->header!=h)return def(h,msg,w,l);
    if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK) {
        normalize(s,TRUE);
        int x=(int)(short)LOWORD(l),divider=divider_at(s,x);
        if(divider>=0) {
            if(divider==2)return 0;
            s->divider=divider;s->start_x=x;
            if(!read_columns(s,s->start_width))return 0;
            s->dragging=TRUE;API(SetCapture,0x140256EB0)(h);
            return 0;
        }
    }
    if(msg==WM_MOUSEMOVE && s->dragging) {drag_to(s,(int)(short)LOWORD(l));return 0;}
    if(msg==WM_LBUTTONUP && s->dragging) {
        drag_to(s,(int)(short)LOWORD(l));end_drag(s);return 0;
    }
    if(msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE)end_drag(s);
    if(msg==WM_SETCURSOR) {
        POINT p={0};API(GetCursorPos,0x140257210)(&p);
        API(ScreenToClient,0x1402572B0)(h,&p);
        int divider=divider_at(s,p.x);
        if(divider>=0) {
            API(SetCursor,0x1402572A8)(API(LoadCursorW,0x1402572C0)(0,divider==2?IDC_ARROW:IDC_SIZEWE));
            return TRUE;
        }
    }
    LRESULT result=def(h,msg,w,l);
    if(msg==WM_NCDESTROY) {
        end_drag(s);remove_hook(h,header_hook,id);s->header=0;s->installed=FALSE;
        if(!s->list)*s=(COLUMN_STATE){0};
    }
    return result;
}
static LRESULT CALLBACK list_hook(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    COLUMN_STATE *s=(COLUMN_STATE*)(ULONG_PTR)ref;
    DEF_SUBCLASS def=native_subclass();
    if(!s || s->list!=h)return def(h,msg,w,l);
    if(msg==WM_NOTIFY && l) {
        NMHDR *notify=(NMHDR*)l;
        if(notify->hwndFrom==s->header && (int)notify->code==(int)HDN_BEGINDRAG)return TRUE;
    }
    if(msg==WM_HSCROLL || msg==WM_MOUSEHWHEEL)return 0;
    if(msg==WM_NCDESTROY) {
        end_drag(s);remove_hook(h,list_hook,id);s->list=0;
        if(!s->header)*s=(COLUMN_STATE){0};
        return def(h,msg,w,l);
    }
    LRESULT result=def(h,msg,w,l);
    if(!s->busy && (msg==WM_SIZE || msg==WM_DPICHANGED || msg==LVM_SETCOLUMNWIDTH ||
                   msg==LVM_INSERTCOLUMNW || msg==LVM_DELETECOLUMN))
        normalize(s,msg==LVM_SETCOLUMNWIDTH);
    return result;
}
static COLUMN_STATE *ensure(void *self,HWND list) {
    COLUMN_STATE *s=get_state(list,TRUE);
    HWND header=(HWND)Send(list,LVM_GETHEADER,0,0);
    if(!s || !header || Send(header,HDM_GETITEMCOUNT,0,0)!=3)return 0;
    s->self=self;
    if(!s->installed || s->header!=header) {
        s->header=header;
        for(int i=0;i<3;++i)s->index[i]=i;
        for(int i=0;i<3;++i) {
            int logical=((int(*)(void*,int))0x140019158ULL)(self,i);
            if(logical>=0 && logical<3)s->index[logical]=i;
        }
        if(!AddSubclass(header,header_hook,SUBCLASS_ID,(DWORD_PTR)s) ||
           !AddSubclass(list,list_hook,SUBCLASS_ID,(DWORD_PTR)s))return 0;
        s->installed=TRUE;
        LONG style=API(GetWindowLongW,0x140256FA8)(header,GWL_STYLE);
        API(SetWindowLongW,0x140256FA0)(header,GWL_STYLE,style&~HDS_DRAGDROP);
        int order[3]={s->index[0],s->index[1],s->index[2]};
        Send(list,LVM_SETCOLUMNORDERARRAY,3,(LPARAM)order);
        normalize(s,TRUE);
    }
    return s;
}

__declspec(dllexport) void ui_card_columns(void *self,HWND list,const RECT *row,void *out) {
    COLUMN_STATE *s=ensure(self,list);
    if(s && !s->busy && !s->dragging && viewport(s)!=s->client_width)normalize(s,FALSE);
    ((void (*)(void *,HWND,const RECT *,void *))0x140401090ULL)(self,list,row,out);
}
__declspec(dllexport) void ui_pre_subclass(void *self) {
    ((void (*)(void *))0x140402320ULL)(self);
    ensure(self,*(HWND *)((char *)self+0x40));
}
__declspec(dllexport) void native_probe(void) {}
