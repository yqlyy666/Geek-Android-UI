#ifndef GEEK_NATIVE_TOOLTIP_H
#define GEEK_NATIVE_TOOLTIP_H

/* Replace the original list's truncated-tooltip text helper, not its
   TTN_GETDISPINFO notification, hit testing or tooltip HWND. Native ABI:
     CStringW *GetTruncatedText(list, CStringW *result, int row, int column).
   This path borrows one window DC and restores its font. It never creates
   an MFC CDC wrapper or a compatible DC during mouse-hover notifications. */
typedef struct {
    UINT text_flags;
    COLORREF background;
    HFONT font;
} GEEK_TOOLTIP_STYLE;

static void geek_tooltip_release(WCHAR *text) {
    if(!text)return;
    char *data=(char*)text-0x18;
    if(InterlockedDecrement((volatile LONG*)(data+0x10))<=0) {
        void *manager=*(void**)data;
        ((void(*)(void*,void*))(*(void***)manager)[1])(manager,data);
    }
}

__declspec(dllexport) WCHAR **ui_tooltip_text(void *self,WCHAR **out,int row,int column) {
    if(!out)return out;
    void *manager=((void*(*)(void))0x1401BBC5CULL)();
    if(!manager) {
        ((void(*)(LONG))0x1400048ECULL)((LONG)0x80004005UL);
        return out;
    }
    char *empty=((char*(*)(void*))(*(void***)manager)[3])(manager);
    *out=(WCHAR*)(empty+0x18);
    if(!self || row<0 || column<0)return out;
    void **begin=*(void***)((char*)self+0x218);
    void **end=*(void***)((char*)self+0x220);
    if(!begin || !end || end<begin || (SIZE_T)row>=(SIZE_T)(end-begin))return out;
    void *model=begin[row];
    if(!model)return out;

    WCHAR *text=0;
    ((WCHAR**(*)(void*,WCHAR**,int,int))0x1401CDBC8ULL)(self,&text,row,column);
    RECT cell={0};
    ((void(*)(void*,RECT*,void*,int,int))0x1400186D4ULL)(self,&cell,model,row,column);
    GEEK_TOOLTIP_STYLE style={0,(COLORREF)-1,0};
    int logical=((int(*)(void*,int))0x140019158ULL)(self,column);
    unsigned char custom=((unsigned char(*)(void*,int,BOOL,GEEK_TOOLTIP_STYLE*,void*))
        (*(void***)self)[0x2F0/8])(self,logical,FALSE,&style,model);
    HFONT font=custom?style.font:*(HFONT*)((char*)self+0x288);
    HWND h=*(HWND*)((char*)self+0x40);
    HDC dc=h?API(GetDC,0x1402571C8)(h):0;
    if(dc) {
        HGDIOBJ old=font?Select(dc,font):0;
        SIZE extent={0};
        int length=text?*(int*)((char*)text-0x10):0;
        BOOL measured=text && length>=0 && API(GetTextExtentPoint32W,0x140256350)(dc,text,length,&extent);
        if(old && old!=HGDI_ERROR)Select(dc,old);
        API(ReleaseDC,0x1402571D0)(h,dc);
        if(measured && extent.cx>cell.right-cell.left+(column?0:8))
            ((WCHAR**(*)(WCHAR**,WCHAR**))0x140005CD8ULL)(out,&text);
    }
    geek_tooltip_release(text);
    return out;
}

#endif
