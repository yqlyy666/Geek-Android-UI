#ifndef GEEK_NATIVE_STATUS_LAYOUT_H
#define GEEK_NATIVE_STATUS_LAYOUT_H

/* Include after native-ui.c's API/Send macros and dpi_of/px helpers. */
#include "native-status-style.h"
typedef struct {
    HWND frame;
    HWND status;
    int minimum_height;
    BOOL busy;
} GEEK_STATUS_LAYOUT_STATE;

static GEEK_STATUS_LAYOUT_STATE geek_status_layout_state;

static void geek_status_layout(HWND h) {
    if(!h || geek_status_layout_state.busy)return;

    HWND status=API(GetDlgItem,0x1402570A0)(h,0xE801);
    if(!status)return;

    /* MFC returns the existing permanent CWnd for the native main dialog.
       Check its known type before accessing the inline CXTPStatusBar. */
    void *frame=((void *(*)(HWND))0x1401BEF3CULL)(h);
    if(!frame || *(ULONG_PTR*)frame!=0x14025D480ULL ||
       *(HWND*)((char*)frame+0x310)!=status)return;

    geek_status_layout_state.busy=TRUE;
    BOOL changed=geek_status_apply(status,(char*)frame+0x2D0);
    int minimum=geek_status_minimum(status);
    int current=*(int*)((char*)frame+0x2D0+0x170);
    if(geek_status_layout_state.frame==h &&
       geek_status_layout_state.status==status &&
       geek_status_layout_state.minimum_height==minimum && current==minimum && !changed) {
        geek_status_layout_state.busy=FALSE;return;
    }

    /* The native SB_SETMINHEIGHT handler updates status+0x170 only. Native
       frame layout then measures the status font and reserves at least this
       many physical pixels. Guard both operations: layout can send WM_SIZE. */
    Send(status,SB_SETMINHEIGHT,(WPARAM)minimum,0);
    if(*(int*)((char*)frame+0x2D0+0x170)==minimum) {
        geek_status_layout_state.frame=h;
        geek_status_layout_state.status=status;
        geek_status_layout_state.minimum_height=minimum;
        ((void (*)(void *))0x14002A028ULL)(frame);
    }
    geek_status_layout_state.busy=FALSE;
}

#endif
