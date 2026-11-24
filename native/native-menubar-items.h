#ifndef GEEK_NATIVE_MENUBAR_ITEMS_H
#define GEEK_NATIVE_MENUBAR_ITEMS_H

/* Paint the existing top-level menu controls inside their native hit boxes. */
static void geek_menubar_release(WCHAR *s) {
    if(!s)return;
    char *data=(char*)s-0x18;
    if(InterlockedDecrement((volatile LONG*)(data+0x10))<=0) {
        void *manager=*(void**)data;
        ((void(*)(void*,void*))(*(void***)manager)[1])(manager,data);
    }
}

__declspec(dllexport) UI_SIZE *ui_menubar_item(void *paint,UI_SIZE *out,void *cdc,void *control,BOOL draw) {
    typedef UI_SIZE *(*ORIGINAL)(void*,UI_SIZE*,void*,void*,BOOL);
    ORIGINAL original=(ORIGINAL)(*(void***)paint)[0xA8/8];
    if(!out || !cdc || !control)return original(paint,out,cdc,control,draw);
    void *parent=*(void**)((char*)control+0x128);
    if(!parent || *(ULONG_PTR*)parent!=0x14028C560ULL)
        return original(paint,out,cdc,control,draw);
    HWND h=*(HWND*)((char*)parent+0x40);
    int dpi=dpi_of(h);
    if(!draw) {
        UI_SIZE *result=original(paint,out,cdc,control,FALSE);
        out->cx+=px(12,dpi);
        if(out->cy<px(34,dpi))out->cy=px(34,dpi);
        return result;
    }
    HDC dc=*(HDC*)((char*)cdc+8);
    if(!dc)return original(paint,out,cdc,control,draw);
    int saved=Save(dc);
    if(!saved)return original(paint,out,cdc,control,draw);
    WCHAR *caption=0;
    ((WCHAR**(*)(void*,WCHAR**))(*(void***)control)[0xA0/8])(control,&caption);
    int length=0;
    if(caption)while(caption[length] && caption[length]!='\t' && length<4096)++length;
    int type=*(int*)((char*)control+0x120);
    int enabledState=((int(*)(void*))0x1400F85E0ULL)(control);
    BOOL enabled=enabledState!=0 && !(type==4 && enabledState==5);
    BOOL selected=((int(*)(void*))(*(void***)control)[0xD8/8])(control)!=0;
    BOOL opened=(type>=2 && type<=4) &&
        ((int(*)(void*))(*(void***)control)[0x168/8])(control)!=0;
    BOOL isdark=dark(),active=enabled && (selected || opened);
    COLORREF base=isdark?RGB(20,18,24):RGB(246,244,249);
    COLORREF fill=isdark?RGB(33,31,38):RGB(255,251,254);
    COLORREF edge=isdark?RGB(54,47,62):RGB(234,226,241);
    COLORREF text=isdark?RGB(236,231,240):RGB(31,28,35);
    if(active) {
        fill=isdark?RGB(76,61,93):RGB(232,221,248);
        edge=fill;
        text=isdark?RGB(236,224,255):RGB(62,42,95);
    }
    if(!enabled)text=isdark?RGB(122,116,132):RGB(167,159,175);
    RECT row=*(RECT*)((char*)control+0xC0),pill=row;
    solid(dc,&row,base);
    int inset=px(3,dpi);
    pill.left+=inset;pill.right-=inset;pill.top+=inset;pill.bottom-=inset;
    if(pill.right>pill.left && pill.bottom>pill.top)
        rounded(dc,pill,px(24,dpi),fill,edge);
    HFONT font=font_at(dpi,13,FW_NORMAL);
    if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);TextColor(dc,text);
    RECT label=pill;
    label.left+=px(5,dpi);label.right-=px(5,dpi);
    if(caption && length && label.right>label.left)
        Text(dc,caption,length,&label,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS);
    Restore(dc,saved);
    if(font)Delete(font);
    geek_menubar_release(caption);
    out->cx=0;out->cy=0;
    return out;
}

#endif
