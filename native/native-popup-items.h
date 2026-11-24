#ifndef GEEK_NATIVE_POPUP_ITEMS_H
#define GEEK_NATIVE_POPUP_ITEMS_H

/* Render the existing CXTP controls in their own measured/hit-test rectangles.
   Their command IDs, enabled state, navigation and submenu objects stay native. */
static void geek_popup_release(WCHAR *s) {
    if(!s)return;
    char *data=(char*)s-0x18;
    if(InterlockedDecrement((volatile LONG*)(data+0x10))<=0) {
        void *manager=*(void**)data;
        ((void(*)(void*,void*))(*(void***)manager)[1])(manager,data);
    }
}

static int geek_popup_length(const WCHAR *s) {
    int n=0;if(s)while(s[n] && s[n]!='\t' && n<4096)++n;
    return n;
}

static void geek_popup_line(HDC dc,int x1,int y1,int x2,int y2) {
    API(MoveToEx,0x1402561A8)(dc,x1,y1,0);
    API(LineTo,0x140256168)(dc,x2,y2);
}

/* Defined separately so icon handling can use the original image manager. */
static void geek_popup_icon(void *paint,void *cdc,void *control,
                            int x,int y,int size,BOOL enabled,BOOL selected) {
    void *icon=((void*(*)(void*,int))(*(void***)control)[0x150/8])(control,size);
    if(!icon)return;
    UI_SIZE requested={size,size},actual=requested;
    ((UI_SIZE*(*)(void*,UI_SIZE*,void*,UI_SIZE))0x140109370ULL)(paint,&actual,icon,requested);
    POINT position={x+(size-actual.cx)/2,y+(size-actual.cy)/2};
    ((void(*)(void*,void*,POINT,UI_SIZE,void*,BOOL,BOOL,BOOL,int,BOOL,BOOL))
        (*(void***)paint)[0x190/8])(paint,cdc,position,actual,icon,selected,FALSE,enabled,0,FALSE,FALSE);
}

__declspec(dllexport) UI_SIZE *ui_popup_item(void *paint,UI_SIZE *out,void *cdc,void *control,BOOL draw) {
    typedef UI_SIZE *(*ORIGINAL)(void*,UI_SIZE*,void*,void*,BOOL);
    ORIGINAL original=(ORIGINAL)(*(void***)paint)[0xA0/8];
    if(!out || !cdc || !control)return original(paint,out,cdc,control,draw);
    void *parent=*(void**)((char*)control+0x128);
    HWND h=parent?*(HWND*)((char*)parent+0x40):0;
    HDC dc=*(HDC*)((char*)cdc+8);
    if(!dc)return original(paint,out,cdc,control,draw);
    int dpi=dpi_of(h), saved=Save(dc);
    if(!saved)return original(paint,out,cdc,control,draw);
    UI_SIZE *result=out;
    if(!draw)result=original(paint,out,cdc,control,FALSE);
    WCHAR *caption=0,*shortcut=0;
    ((WCHAR**(*)(void*,WCHAR**))(*(void***)control)[0xA0/8])(control,&caption);
    ((WCHAR**(*)(void*,WCHAR**))0x1400F8830ULL)(control,&shortcut);
    int captionlen=geek_popup_length(caption);
    const WCHAR *keys=shortcut;
    if((!keys || !*keys) && caption && caption[captionlen]=='\t')keys=caption+captionlen+1;
    HFONT font=font_at(dpi,13,FW_NORMAL);
    if(font)Select(dc,font);
    BkMode(dc,TRANSPARENT);
    RECT captionbox={0},keybox={0};
    if(caption && captionlen)Text(dc,caption,captionlen,&captionbox,DT_SINGLELINE|DT_CALCRECT);
    if(keys && *keys)Text(dc,keys,-1,&keybox,DT_SINGLELINE|DT_CALCRECT|DT_NOPREFIX);
    if(!draw) {
        int width=captionbox.right+px(68,dpi);
        if(keybox.right)width+=keybox.right+px(24,dpi);
        int oldwidth=out->cx+px(16,dpi);
        if(width<oldwidth)width=oldwidth;
        if(width<px(172,dpi))width=px(172,dpi);
        int height=out->cy+px(8,dpi),minimum=px(36,dpi);
        int textheight=captionbox.bottom+px(16,dpi);
        if(height<minimum)height=minimum;
        if(height<textheight)height=textheight;
        out->cx=width;out->cy=height;
    } else {
        BOOL isdark=dark();
        int type=*(int*)((char*)control+0x120);
        int enabledState=((int(*)(void*))0x1400F85E0ULL)(control);
        BOOL enabled=enabledState!=0 && !(type==4 && enabledState==5);
        BOOL selected=((int(*)(void*))(*(void***)control)[0xD8/8])(control)!=0;
        BOOL opened=(type>=2 && type<=4) &&
            ((int(*)(void*))(*(void***)control)[0x168/8])(control)!=0;
        int checked=*(int*)((char*)control+0xA0);
        void *action=*(void**)((char*)control+0x1D0);
        if(checked==-1 && action)checked=*(int*)((char*)action+0x60);
        if(checked<0)checked=0;
        RECT row=*(RECT*)((char*)control+0xC0),r=row;
        COLORREF base=isdark?RGB(33,31,38):RGB(255,251,254);
        COLORREF text=isdark?RGB(236,231,240):RGB(31,28,35);
        COLORREF muted=isdark?RGB(178,170,188):RGB(113,104,122);
        COLORREF accent=isdark?RGB(208,188,255):RGB(103,80,164);
        solid(dc,&row,base);
        r.left+=px(4,dpi);r.right-=px(4,dpi);
        r.top+=px(2,dpi);r.bottom-=px(2,dpi);
        if(enabled && (selected || opened)) {
            COLORREF hover=isdark?RGB(67,54,84):RGB(234,223,249);
            rounded(dc,r,px(24,dpi),hover,hover);
        }
        if(!enabled)text=muted=isdark?RGB(122,116,132):RGB(167,159,175);
        int cy=(row.top+row.bottom)/2;
        if(checked) {
            RECT mark={row.left+px(10,dpi),cy-px(8,dpi),row.left+px(26,dpi),cy+px(8,dpi)};
            COLORREF fill=enabled?accent:muted;
            rounded(dc,mark,px(6,dpi),fill,fill);
            HPEN pen=Pen(PS_SOLID,px(2,dpi),isdark?RGB(38,24,57):RGB(255,255,255));
            HGDIOBJ old=Select(dc,pen);
            geek_popup_line(dc,mark.left+px(3,dpi),cy,mark.left+px(6,dpi),cy+px(3,dpi));
            geek_popup_line(dc,mark.left+px(6,dpi),cy+px(3,dpi),mark.left+px(13,dpi),cy-px(4,dpi));
            Select(dc,old);Delete(pen);
        } else {
            geek_popup_icon(paint,cdc,control,row.left+px(10,dpi),cy-px(8,dpi),px(16,dpi),enabled,selected);
        }
        RECT label={row.left+px(38,dpi),row.top,row.right-px(24,dpi),row.bottom};
        if(keys && *keys) {
            RECT keyrect=label;
            keyrect.left=keyrect.right-keybox.right;
            TextColor(dc,muted);
            Text(dc,keys,-1,&keyrect,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_NOPREFIX);
            label.right=keyrect.left-px(24,dpi);
        }
        TextColor(dc,text);
        if(caption && label.right>label.left)
            Text(dc,caption,captionlen,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if(type>=2 && type<=4) {
            int x=row.right-px(12,dpi),a=px(3,dpi);
            HPEN pen=Pen(PS_SOLID,px(2,dpi),text);HGDIOBJ old=Select(dc,pen);
            geek_popup_line(dc,x-a,cy-a,x,cy);
            geek_popup_line(dc,x,cy,x-a,cy+a);
            Select(dc,old);Delete(pen);
        }
        out->cx=0;out->cy=0;
    }
    Restore(dc,saved);if(font)Delete(font);
    geek_popup_release(shortcut);geek_popup_release(caption);
    return result;
}

#endif
