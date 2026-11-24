#ifndef GEEK_NATIVE_MENU_OPAQUE_H
#define GEEK_NATIVE_MENU_OPAQUE_H

/* CXTPMenuBar's own OnDraw slot, not the base command-bar or popup slot:
     vtable 0x14028C560 + 0x3A0 = 0x14028C900 (file offset 0x28B700).
   The original draw method keeps its controls, labels, fonts and hover state.
   Only this menu window's client rectangle is buffered and made opaque. */
typedef void (*GEEK_MENU_NATIVE_DRAW)(void*,void*,const RECT*);
typedef HANDLE (WINAPI *GEEK_MENU_BEGIN_PAINT)(HDC,const RECT*,int,const void*,HDC*);
typedef HRESULT (WINAPI *GEEK_MENU_ALPHA_PAINT)(HANDLE,const RECT*,BYTE);
typedef HRESULT (WINAPI *GEEK_MENU_END_PAINT)(HANDLE,BOOL);
typedef BOOL (WINAPI *GEEK_MENU_WINDOW_ORIGIN)(HDC,LPPOINT);
typedef DWORD (WINAPI *GEEK_MENU_DC_LAYOUT)(HDC);
typedef int (WINAPI *GEEK_MENU_DC_MODE)(HDC);

/* The native XTP buffer constructor creates an ordinary compatible DC with
   MM_TEXT and zero window/viewport origins.  Do not reinterpret an unusual
   print, mirrored or transformed DC as menu client pixels. */
static BOOL geek_menu_plain_dc(HDC dc,GEEK_MENU_WINDOW_ORIGIN origin,
                               GEEK_MENU_DC_LAYOUT layout,GEEK_MENU_DC_MODE graphics) {
    POINT viewport={0},window={0};
    return API(GetMapMode,0x140256240)(dc)==MM_TEXT &&
           API(GetViewportOrgEx,0x140256278)(dc,&viewport) &&
           origin(dc,&window) && !viewport.x && !viewport.y &&
           !window.x && !window.y && layout(dc)==0 &&
           graphics(dc)==GM_COMPATIBLE;
}

static void geek_menu_buffered_draw(void *bar,void *cdc,const RECT *clip,GEEK_MENU_NATIVE_DRAW original) {
    /* This fixed application menu does not expose the old toolbar gripper.
       Popup OnDraw otherwise repaints its rectangular native gutter after
       our rounded canvas. Its controls and keyboard handling remain native. */
    if(bar && (*(ULONG_PTR*)bar==0x14028C560ULL ||
               *(int*)((char*)bar+0x180)==2))
        *(DWORD*)((char*)bar+0x170)&=~0x00400000UL;
    GEEK_MENU_BEGIN_PAINT begin=(GEEK_MENU_BEGIN_PAINT)(*(void**)0x140257488ULL);
    GEEK_MENU_ALPHA_PAINT alpha=(GEEK_MENU_ALPHA_PAINT)(*(void**)0x140257498ULL);
    GEEK_MENU_END_PAINT end=(GEEK_MENU_END_PAINT)(*(void**)0x140257490ULL);
    if(!bar || !cdc || !clip || !begin || !alpha || !end) {
        original(bar,cdc,clip);return;
    }

    /* MFC CDC: +8=m_hDC, +0x10=m_hAttribDC.  Both fields must refer to the
       buffer while native CDC methods update output and attribute state. */
    HDC *output=(HDC*)((char*)cdc+8), *attribute=(HDC*)((char*)cdc+0x10);
    HDC target=*output, oldattribute=*attribute;
    HWND h=*(HWND*)((char*)bar+0x40);
    RECT client;
    if(!target || !h || !Client(h,&client) || client.left || client.top ||
       client.right<=0 || client.bottom<=0) {
        original(bar,cdc,clip);return;
    }

    HMODULE gdi=Module(L"gdi32.dll");
    GEEK_MENU_WINDOW_ORIGIN origin=(GEEK_MENU_WINDOW_ORIGIN)Proc(gdi,"GetWindowOrgEx");
    GEEK_MENU_DC_LAYOUT layout=(GEEK_MENU_DC_LAYOUT)Proc(gdi,"GetLayout");
    GEEK_MENU_DC_MODE graphics=(GEEK_MENU_DC_MODE)Proc(gdi,"GetGraphicsMode");
    GEEK_MENU_DC_MODE background=(GEEK_MENU_DC_MODE)Proc(gdi,"GetBkMode");
    if(!origin || !layout || !graphics || !background ||
       !geek_menu_plain_dc(target,origin,layout,graphics)) {
        original(bar,cdc,clip);return;
    }

    HDC bufferDC=0;
    /* BPBF_TOPDOWNDIB=2 supplies the 32-bit alpha channel that GDI text can
       otherwise leave transparent. Default buffered-paint clipping is kept. */
    HANDLE buffer=begin(target,&client,2,0,&bufferDC);
    if(!buffer || !bufferDC) {
        if(buffer)end(buffer,FALSE);
        original(bar,cdc,clip);return;
    }
    if(!geek_menu_plain_dc(bufferDC,origin,layout,graphics)) {
        end(buffer,FALSE);original(bar,cdc,clip);return;
    }
    int saved=Save(bufferDC);
    if(!saved) {
        end(buffer,FALSE);original(bar,cdc,clip);return;
    }

    /* OnDraw can skip controls outside clip. Seed the complete menu buffer
       with its existing RGB pixels before calling it, so a partial update
       cannot replace untouched menu pixels with an empty DIB. */
    if(!API(BitBlt,0x140256328)(bufferDC,0,0,client.right,client.bottom,
                              target,0,0,SRCCOPY)) {
        Restore(bufferDC,saved);end(buffer,FALSE);
        original(bar,cdc,clip);return;
    }
    const UINT objects[3]={OBJ_FONT,OBJ_PEN,OBJ_BRUSH};
    for(int i=0;i<3;++i) {
        HGDIOBJ object=API(GetCurrentObject,0x1402562B0)(target,objects[i]);
        if(object)Select(bufferDC,object);
    }
    TextColor(bufferDC,API(GetTextColor,0x140256340)(target));
    API(SetBkColor,0x1402563A0)(bufferDC,API(GetBkColor,0x1402561C8)(target));
    BkMode(bufferDC,background(target));
    API(SetTextAlign,0x140256210)(bufferDC,API(GetTextAlign,0x140256140)(target));

    *output=bufferDC;*attribute=bufferDC;
    original(bar,cdc,clip);
    *attribute=oldattribute;*output=target;
    Restore(bufferDC,saved);

    /* NULL means every buffer pixel, including areas not redrawn by this
       clip. Never commit a buffer whose alpha correction failed. */
    HRESULT result=alpha(buffer,0,255);
    if(FAILED(result)) {
        end(buffer,FALSE);original(bar,cdc,clip);return;
    }
    if(FAILED(end(buffer,TRUE)))original(bar,cdc,clip);
}

__declspec(dllexport) void ui_menu_draw_opaque(void *bar,void *cdc,const RECT *clip) {
    geek_menu_buffered_draw(bar,cdc,clip,(GEEK_MENU_NATIVE_DRAW)0x140110880ULL);
}

__declspec(dllexport) void ui_popup_draw_opaque(void *bar,void *cdc,const RECT *clip) {
    if(bar && *(int*)((char*)bar+0x180)==2)
        ui_popup_panel_ensure(((void*(*)(void*))0x140111650ULL)(bar),bar);
    geek_menu_buffered_draw(bar,cdc,clip,(GEEK_MENU_NATIVE_DRAW)0x140116970ULL);
}

#endif
