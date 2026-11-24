#ifndef GEEK_NATIVE_STATUS_STYLE_H
#define GEEK_NATIVE_STATUS_STYLE_H

/* One native CXTPStatusBar remains attached to the main frame.  Its live
   statistics pane (0x22B) remains the source of the count/size text.  The
   promotional pane (0x22D) is hidden through the toolkit's own API. */
typedef HANDLE (WINAPI *GEEK_STATUS_BEGIN)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *GEEK_STATUS_ALPHA)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_STATUS_END)(HANDLE,BOOL);
typedef BOOL (WINAPI *GEEK_STATUS_REMOVE)(HWND,SUBCLASSPROC,UINT_PTR);

static HWND geek_status_styled_window;

static BOOL geek_status_word(const WCHAR *s,const WCHAR *word) {
    while(*word) {if(*s++!=*word++)return FALSE;}
    return TRUE;
}

static BOOL geek_status_name_start(WCHAR c) {
    return (c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z') || c==L'_';
}
static BOOL geek_status_name_char(WCHAR c) {
    return geek_status_name_start(c) || (c>=L'0' && c<=L'9') || c==L':' || c==L'.' || c==L'-';
}
static BOOL geek_status_space(WCHAR c) {return c==L' ' || c==L'\t' || c==L'\r' || c==L'\n';}

/* Return only after a complete XML element tag. Attribute values must have
   matched quotes, so '>' inside a quoted value does not end the tag. */
static const WCHAR *geek_status_after_tag(const WCHAR *s) {
    const WCHAR *p=s+1;BOOL closing=*p==L'/';if(closing)++p;
    if(!geek_status_name_start(*p))return 0;
    do{++p;}while(geek_status_name_char(*p) && p-s<1024);
    if(closing) {
        while(geek_status_space(*p) && p-s<1024)++p;
        return *p==L'>'?p+1:0;
    }
    while(*p && p-s<1024) {
        if(*p==L'>')return p+1;
        if(*p==L'/' && p[1]==L'>')return p+2;
        if(!geek_status_space(*p))return 0;
        do{++p;}while(geek_status_space(*p) && p-s<1024);
        if(*p==L'>' || (*p==L'/' && p[1]==L'>'))continue;
        if(!geek_status_name_start(*p))return 0;
        do{++p;}while(geek_status_name_char(*p) && p-s<1024);
        while(geek_status_space(*p) && p-s<1024)++p;
        if(*p++!=L'=')return 0;
        while(geek_status_space(*p) && p-s<1024)++p;
        WCHAR quote=*p++;if(quote!=L'\'' && quote!=L'"')return 0;
        while(*p && *p!=quote && *p!=L'<' && p-s<1024)++p;
        if(*p!=quote)return 0;
        ++p;
    }
    return 0;
}

/* XTP stores StackPanel/Image/TextBlock/Run trees in the pane CString. Keep
   the native inner text, discard complete XML element tags, and decode XML
   entities. Unknown entities and ordinary '< 10' comparisons stay literal. */
static int geek_status_text(const WCHAR *s,WCHAR *out,int cap) {
    int n=0;
    if(cap<1)return 0;
    out[0]=0;if(!s)return 0;
    while(*s && n<cap-1) {
        if(*s==L'<') {
            const WCHAR *name=s+1;
            if(*name==L'/')++name;
            const WCHAR *after=geek_status_after_tag(s);
            if(after) {
                if(geek_status_word(name,L"LineBreak") && !geek_status_name_char(name[9]) && n && out[n-1]!=L' ')out[n++]=L' ';
                s=after;continue;
            }
        }
        if(*s==L'&') {
            const WCHAR *p=s+1;unsigned value=0;int used=0;
            if(geek_status_word(p,L"amp;")){value=L'&';used=4;}
            else if(geek_status_word(p,L"lt;")){value=L'<';used=3;}
            else if(geek_status_word(p,L"gt;")){value=L'>';used=3;}
            else if(geek_status_word(p,L"quot;")){value=L'"';used=5;}
            else if(geek_status_word(p,L"apos;")){value=L'\'';used=5;}
            else if(geek_status_word(p,L"nbsp;")){value=0xA0;used=5;}
            else if(*p==L'#') {
                ++p;unsigned radix=10;if(*p==L'x' || *p==L'X'){radix=16;++p;}
                int digits=0;
                while(digits<8) {
                    unsigned digit=99;
                    if(*p>=L'0' && *p<=L'9')digit=*p-L'0';
                    else if(*p>=L'a' && *p<=L'f')digit=*p-L'a'+10;
                    else if(*p>=L'A' && *p<=L'F')digit=*p-L'A'+10;
                    if(digit>=radix || value>(0x10FFFF-digit)/radix)break;
                    value=value*radix+digit;++p;++digits;
                }
                if(digits && *p==L';' && value && value<=0x10FFFF && !(value>=0xD800 && value<=0xDFFF))used=(int)(p-s);
            }
            if(used) {
                if(value>0xFFFF) {
                    if(n>=cap-2)break;
                    value-=0x10000;out[n++]=(WCHAR)(0xD800+(value>>10));out[n++]=(WCHAR)(0xDC00+(value&1023));
                } else out[n++]=(WCHAR)value;
                s+=used+1;continue;
            }
        }
        WCHAR c=*s++;out[n++]=(c==L'\r' || c==L'\n' || c==L'\t')?L' ':c;
    }
    out[n]=0;return n;
}

static int geek_status_minimum(HWND h) {
    int dpi=dpi_of(h),minimum=px(32,dpi);
    HDC dc=API(GetDC,0x1402571C8)(h);
    HFONT font=font_at(dpi,11,FW_NORMAL);
    if(dc && font) {
        HGDIOBJ previous=Select(dc,font);TEXTMETRICW metric={0};
        if(API(GetTextMetricsW,0x1402562A8)(dc,&metric) && metric.tmHeight>0 && metric.tmHeight<px(128,dpi)) {
            int measured=metric.tmHeight+px(12,dpi);
            if(minimum<measured)minimum=measured;
        }
        if(previous)Select(dc,previous);
    }
    if(font)Delete(font);
    if(dc)API(ReleaseDC,0x1402571D0)(h,dc);
    return minimum;
}

static void geek_status_paint_content(void *status,HDC dc,const RECT *client) {
    BOOL isdark=dark();
    solid(dc,(RECT*)client,isdark?RGB(20,18,24):RGB(246,244,249));
    int saved=Save(dc);if(!saved)return;
    HWND h=*(HWND*)((char*)status+0x40);int dpi=dpi_of(h);
    HFONT font=font_at(dpi,11,FW_NORMAL);
    if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,isdark?RGB(184,177,192):RGB(108,102,115));
    void *pane=((void *(*)(void*,int))0x1400FF3D0ULL)(status,0x22B);
    const WCHAR *native=pane?*(const WCHAR**)((char*)pane+0x50):0;
    WCHAR text[768];int length=geek_status_text(native,text,768);
    RECT line=*client;line.left+=px(12,dpi);line.right-=px(12,dpi);
    line.top+=px(4,dpi);line.bottom-=px(4,dpi);
    if(length && line.right>line.left && line.bottom>line.top)
        Text(dc,text,length,&line,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    Restore(dc,saved);if(font)Delete(font);
}

/* Patch the native OnPaint call at file 0xFFF34 to this two-argument export.
   Its existing CPaintDC and CXTPBufferDC lifetime remain exactly native. */
__declspec(dllexport) void ui_status_draw(void *status,void *cdc) {
    if(!status || !cdc)return;
    if(*(ULONG_PTR*)status!=0x140278730ULL) {
        ((void(*)(void*,void*))0x140100190ULL)(status,cdc);return;
    }
    HWND h=*(HWND*)((char*)status+0x40);HDC target=*(HDC*)((char*)cdc+8);RECT client;
    if(!h || !target || !Client(h,&client) || client.right<=client.left || client.bottom<=client.top)return;
    GEEK_STATUS_BEGIN begin=(GEEK_STATUS_BEGIN)(*(void**)0x140257488ULL);
    GEEK_STATUS_ALPHA alpha=(GEEK_STATUS_ALPHA)(*(void**)0x140257498ULL);
    GEEK_STATUS_END end=(GEEK_STATUS_END)(*(void**)0x140257490ULL);
    HDC dc=0;HANDLE buffer=0;
    if(begin && alpha && end)buffer=begin(target,&client,2,0,&dc);
    if(buffer && dc) {
        geek_status_paint_content(status,dc,&client);
        if(SUCCEEDED(alpha(buffer,0,255))) {
            if(SUCCEEDED(end(buffer,TRUE)))return;
        } else end(buffer,FALSE);
    } else if(buffer)end(buffer,FALSE);
    geek_status_paint_content(status,target,&client);
}

static LRESULT CALLBACK geek_status_subclass(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref) {
    DEF_SUBCLASS def=(DEF_SUBCLASS)(*(void**)0x1402560B8ULL);
    /* A flat status surface uses its full child-window rectangle.  This also
       removes the toolkit's old -2 top compensation and classic NC border. */
    if(message==WM_NCCALCSIZE || message==WM_NCPAINT)return 0;
    if(message==WM_NCHITTEST)return HTCLIENT;
    if(message==WM_ERASEBKGND) {
        RECT r;if(w && Client(h,&r))solid((HDC)w,&r,dark()?RGB(20,18,24):RGB(246,244,249));
        return 1;
    }
    if(message==WM_NCDESTROY) {
        GEEK_STATUS_REMOVE remove=(GEEK_STATUS_REMOVE)Proc(Module(L"comctl32.dll"),"RemoveWindowSubclass");
        if(remove)remove(h,geek_status_subclass,id);
        if(geek_status_styled_window==h)geek_status_styled_window=0;
    }
    return def(h,message,w,l);
}

static BOOL geek_status_apply(HWND h,void *status) {
    BOOL changed=FALSE;
    void *promo=((void *(*)(void*,int))0x1400FF3D0ULL)(status,0x22D);
    if(promo && !(*(int*)((char*)promo+0x58)&1)) {
        /* CXTPStatusBarPane::SetVisible(FALSE,FALSE), not RemovePane: native
           update-by-ID code keeps its original owned pane object. */
        ((void(*)(void*,BOOL,BOOL))0x140101FC0ULL)(promo,FALSE,FALSE);changed=TRUE;
    }
    if(*(int*)((char*)status+0x19C)) {
        /* Native m_bSizeGripper gates painting, hit testing and right inset. */
        *(int*)((char*)status+0x19C)=0;changed=TRUE;
    }
    if(geek_status_styled_window!=h) {
        SET_SUBCLASS add=(SET_SUBCLASS)(*(void**)0x1402560A8ULL);
        if(add(h,geek_status_subclass,0x47535453,(DWORD_PTR)status)) {
            geek_status_styled_window=h;changed=TRUE;
            API(SetWindowPos,0x1402572F0)(h,0,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
    }
    if(changed)((void(*)(void*))0x1401013E0ULL)(status);
    return changed;
}

#endif
