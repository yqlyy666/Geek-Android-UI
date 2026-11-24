#ifndef GEEK_NATIVE_POPUP_SEPARATOR_H
#define GEEK_NATIVE_POPUP_SEPARATOR_H

/* Bind the call site at original file 0x10FDC9, not paintmanager's vtable.
   Native CXTPCommandBar::Draw invokes +0x108 when the next visible control
   has BeginGroup (+0x98), after skipping the first visible control.

   Original bytes: FF 90 08 01 00 00. Replace with E8 rel32 90.
   ABI: CSize*(paint, CSize* out, CDC*, commandbar, nextControl, BOOL draw).
   NativeXP +0x108 is 0x14016CDF0; its unthemed/base implementation is
   0x140169BB0. Calling the live virtual preserves the selected theme.
   Both native implementations return out, measure a DPI-scaled 8x8 popup
   separator, and set {0,0} on the drawing path. */
typedef SIZE *(*GEEK_POPUP_SEPARATOR_NATIVE)(void*,SIZE*,void*,void*,void*,BOOL);

__declspec(dllexport) SIZE *ui_popup_separator(void *paint,SIZE *out,void *cdc,
                                               void *bar,void *control,BOOL draw) {
    if(!paint)return out;
    GEEK_POPUP_SEPARATOR_NATIVE original=
        (GEEK_POPUP_SEPARATOR_NATIVE)(*(void***)paint)[0x108/8];
    if(!out || !bar || !control || !draw ||
       *(int*)((char*)bar+0x180)!=2 || !cdc)
        return original(paint,out,cdc,bar,control,draw);

    HDC dc=*(HDC*)((char*)cdc+8);
    HWND window=*(HWND*)((char*)bar+0x40);
    RECT client;
    if(!dc || !window || !Client(window,&client))
        return original(paint,out,cdc,bar,control,draw);

    /* Ask the unchanged native virtual for its gap rather than introducing
       a second layout rule. The existing layout/hit-test geometry is kept. */
    SIZE measured={0};
    original(paint,&measured,cdc,bar,control,FALSE);
    int dpi=dpi_of(window),gap=measured.cy;
    if(gap<=0)gap=px(8,dpi);
    int thickness=px(1,dpi);
    if(thickness<1)thickness=1;
    if(thickness>gap)thickness=gap;

    RECT row=*(RECT*)((char*)control+0xC0);
    RECT space={row.left,row.top-gap,row.right,row.top};
    if(space.left<client.left)space.left=client.left;
    if(space.right>client.right)space.right=client.right;
    if(space.top<client.top)space.top=client.top;
    if(space.bottom>client.bottom)space.bottom=client.bottom;
    if(space.right>space.left && space.bottom>space.top) {
        BOOL isdark=dark();
        solid(dc,&space,isdark?RGB(33,31,38):RGB(255,251,254));
        int y=row.top-gap+(gap-thickness)/2;
        if(y<space.top)y=space.top;
        RECT line={space.left,y,space.right,y+thickness};
        if(line.bottom>space.bottom)line.bottom=space.bottom;
        if(line.bottom>line.top)
            solid(dc,&line,isdark?RGB(77,70,86):RGB(225,216,232));
    }
    out->cx=out->cy=0;
    return out;
}

#endif
