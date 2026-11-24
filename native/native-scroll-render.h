#ifndef GEEK_NATIVE_SCROLL_RENDER_H
#define GEEK_NATIVE_SCROLL_RENDER_H

/* Narrow helpers for the installed native row renderer.  Font handles are
   process-owned and never evicted while a drawing DC might select one. */
#include <windows.h>

typedef struct {
    int dpi;
    int reserved;
    HFONT main_font;
    HFONT sub_font;
} GEEK_SCROLL_FONT_CACHE;
_Static_assert(sizeof(GEEK_SCROLL_FONT_CACHE)==24,"unexpected font-cache size");
#define GEEK_SCROLL_FONT_SLOTS 8
#define GEEK_SCROLL_FONTS ((GEEK_SCROLL_FONT_CACHE *)0x140412C20ULL)

typedef HFONT (WINAPI *GEEK_SCROLL_CREATE_FONT)(const LOGFONTW *);

__declspec(dllexport) HFONT ui_cached_card_font(UINT dpi,BYTE height) {
    if(dpi<96 || dpi>768)dpi=96;
    if(height!=14)height=11;
    GEEK_SCROLL_FONT_CACHE *entry=0,*unused=0,*nearest=0;
    UINT nearest_distance=0xFFFFFFFFU;
    for(int i=0;i<GEEK_SCROLL_FONT_SLOTS;++i) {
        GEEK_SCROLL_FONT_CACHE *candidate=&GEEK_SCROLL_FONTS[i];
        if(candidate->dpi==(int)dpi) {entry=candidate;break;}
        if(!candidate->dpi) {if(!unused)unused=candidate;continue;}
        UINT distance=candidate->dpi>(int)dpi?(UINT)candidate->dpi-dpi:dpi-(UINT)candidate->dpi;
        if(distance<nearest_distance) {nearest=candidate;nearest_distance=distance;}
    }
    if(!entry) {
        entry=unused?unused:nearest;
        if(!entry)return 0;
        if(!entry->dpi)entry->dpi=(int)dpi;
    }
    HFONT *slot=height==14?&entry->main_font:&entry->sub_font;
    if(!*slot) {
        LOGFONTW font={0};
        font.lfHeight=-(height*entry->dpi+48)/96;
        font.lfWeight=height==14?FW_MEDIUM:FW_NORMAL;
        font.lfCharSet=DEFAULT_CHARSET;
        font.lfQuality=CLEARTYPE_QUALITY;
        const WCHAR *face=L"Segoe UI";
        for(int i=0;face[i];++i)font.lfFaceName[i]=face[i];
        GEEK_SCROLL_CREATE_FONT create=(GEEK_SCROLL_CREATE_FONT)(*(void **)0x140256358ULL);
        *slot=create(&font);
    }
    return *slot;
}

/* Layout state belongs to the installed fixed-endpoint column helper.  Its
   widths are updated after native width changes, DPI changes and divider
   dragging.  Reusing those widths avoids header queries for every row. */
typedef struct {
    void *self;
    HWND list,header;
    int width[3],index[3],start_width[3];
    int start_x,client_width,divider;
    BOOL installed,busy,dragging,normalized;
} GEEK_SCROLL_COLUMN_STATE;
_Static_assert(sizeof(GEEK_SCROLL_COLUMN_STATE)==88,"unexpected column-state size");
typedef struct {RECT cell[3];UINT visible;} GEEK_SCROLL_COLUMNS;
#define GEEK_SCROLL_COLUMNS_STATE ((GEEK_SCROLL_COLUMN_STATE *)0x140412858ULL)

__declspec(dllexport) void ui_fast_card_columns(void *self,HWND list,
                                               const RECT *row,void *output) {
    if(row && output && row->left==0)for(int i=0;i<8;++i) {
        GEEK_SCROLL_COLUMN_STATE *state=&GEEK_SCROLL_COLUMNS_STATE[i];
        if(state->list!=list || state->self!=self || !state->installed ||
           !state->normalized || state->busy)continue;
        int first=state->width[0],second=state->width[1],third=state->width[2];
        if(first<=0 || second<=0 || third<=0 ||
           first+second+third!=state->client_width ||
           row->right!=state->client_width)break;
        GEEK_SCROLL_COLUMNS *columns=(GEEK_SCROLL_COLUMNS *)output;
        int x=0;
        for(int logical=0;logical<3;++logical) {
            columns->cell[logical]=*row;
            columns->cell[logical].left=x;
            x+=state->width[logical];
            columns->cell[logical].right=x;
        }
        columns->visible=7;
        return;
    }
    ((void (*)(void *,HWND,const RECT *,void *))0x140457000ULL)(self,list,row,output);
}

#endif
