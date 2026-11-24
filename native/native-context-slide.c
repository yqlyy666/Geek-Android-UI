#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define API(name, address) ((__typeof__(&name))(*(void **)(address##ULL)))
#define NATIVE(type, address) ((type)(address##ULL))

typedef BOOL (*PositionPopup)(void *, void *, int, int, int, int, UINT);
typedef int (*PopupEffect)(void *);
typedef HMONITOR (WINAPI *MonitorForRect)(LPCRECT, DWORD);
typedef UINT (WINAPI *WindowDpi)(HWND);

#define POSITION NATIVE(PositionPopup, 0x1401C5AE8)
#define TICK API(GetTickCount, 0x140256950)
#define VISIBLE API(IsWindowVisible, 0x1402570F8)
#define WINDOW_RECT API(GetWindowRect, 0x140257208)
#define DURATION_MS 260
#define OPENING_COUNT 16
#define TIMER_ID ((UINT_PTR)0x47534C44)

/* Codejock tracks this popup synchronously on Geek's UI thread. */
typedef struct {
    void *bar;
    HWND window;
    UINT_PTR timer;
    DWORD start;
    int x, end_y, current_y, width, height, distance;
    BOOL occupied, custom;
} Opening;
_Static_assert(sizeof(Opening) == 64, "unexpected opening state layout");

/* The builder extends Geek's zero-filled .uidata for independent dropdowns. */
#define openings (*(Opening (*)[OPENING_COUNT])0x140412458ULL)

static Opening *find_opening(HWND window, void *bar, UINT_PTR timer) {
    for (int i = 0; i < OPENING_COUNT; ++i) {
        Opening *item = &openings[i];
        if (item->occupied && (!window || item->window == window) &&
            (!bar || item->bar == bar) && (!timer || item->timer == timer))
            return item;
    }
    return NULL;
}

static Opening *allocate_opening(void) {
    for (int i = 0; i < OPENING_COUNT; ++i) {
        if (!openings[i].occupied) {
            Opening *item = &openings[i];
            for (int j = 0; j < (int)sizeof(*item); ++j) ((char *)item)[j] = 0;
            item->occupied = TRUE;
            return item;
        }
    }
    return NULL;
}

static void cancel(Opening *item) {
    UINT_PTR timer = item->timer;
    if (timer) API(KillTimer, 0x1402570D8)(item->window, timer);
    for (int j = 0; j < (int)sizeof(*item); ++j) ((char *)item)[j] = 0;
}

static void CALLBACK animate(HWND window, UINT msg, UINT_PTR timer, DWORD tick) {
    (void)msg;
    (void)tick;
    Opening *item = find_opening(window, NULL, timer);
    if (!item || !item->timer) return;
    RECT rect;
    if (!VISIBLE(window) || !WINDOW_RECT(window, &rect) ||
        rect.left != item->x || rect.top != item->current_y ||
        rect.right - rect.left != item->width || rect.bottom - rect.top != item->height) {
        // Closing or native relayout takes precedence over the animation.
        cancel(item);
        return;
    }
    DWORD elapsed = TICK() - item->start;
    if (elapsed >= DURATION_MS) {
        if (item->current_y != item->end_y &&
            !API(SetWindowPos, 0x1402572F0)(window, NULL, item->x, item->end_y, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER))
            return;
        item->current_y = item->end_y;
        cancel(item);
        return;
    }
    int t = (int)elapsed;
    long long d = DURATION_MS;
    long long denominator = d * d * d * d * d;
    long long smooth = (long long)t * t * t *
                       (6LL * t * t - 15LL * d * t + 10LL * d * d);
    int progress = (int)(((long long)item->distance * smooth + denominator / 2) /
                         denominator);
    int y = item->end_y - item->distance + progress;
    if (y != item->current_y) {
        if (!API(SetWindowPos, 0x1402572F0)(window, NULL, item->x, y, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER)) return;
        item->current_y = y;
    }
    if (y == item->end_y) cancel(item);
}

__declspec(dllexport) BOOL ui_position_context_slide(void *bar, void *after,
        int x, int y, int width, int height, UINT flags) {
    if (!bar || *(int *)((char *)bar + 0x180) != 2 ||
        (flags & (SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE)) != SWP_SHOWWINDOW ||
        width <= 0 || height <= 0)
        return POSITION(bar, after, x, y, width, height, flags);
    HWND window = *(HWND *)((char *)bar + 0x40);
    Opening *item = find_opening(NULL, bar, 0);
    if (item) {
        if (VISIBLE(window))
            return POSITION(bar, after, x, y, width, height, flags);
        cancel(item);
    }
    BOOL menu = FALSE, effects = TRUE;
    if (!window || VISIBLE(window) ||
        !API(SystemParametersInfoW, 0x140257278)(SPI_GETMENUANIMATION, 0, &menu, 0) ||
        !menu)
        return POSITION(bar, after, x, y, width, height, flags);
    API(SystemParametersInfoW, 0x140257278)(SPI_GETCLIENTAREAANIMATION, 0, &effects, 0);
    if (!effects) return POSITION(bar, after, x, y, width, height, flags);

    HMODULE user = API(GetModuleHandleW, 0x140256AC0)(L"user32.dll");
    MonitorForRect monitor_for_rect =
        (MonitorForRect)API(GetProcAddress, 0x140256A78)(user, "MonitorFromRect");
    WindowDpi window_dpi =
        (WindowDpi)API(GetProcAddress, 0x140256A78)(user, "GetDpiForWindow");
    RECT rect = {x, y, x + width, y + height};
    MONITORINFO info;
    info.cbSize = sizeof(info);
    HMONITOR monitor = monitor_for_rect ? monitor_for_rect(&rect, MONITOR_DEFAULTTONEAREST) : NULL;
    if (!monitor || !API(GetMonitorInfoW, 0x1402573E0)(monitor, &info) || y <= info.rcWork.top)
        return POSITION(bar, after, x, y, width, height, flags);
    UINT dpi = window_dpi ? window_dpi(window) : 96;
    if (dpi < 96 || dpi > 768) dpi = 96;
    int distance = (12 * (int)dpi + 48) / 96;
    if (distance > y - info.rcWork.top) distance = y - info.rcWork.top;

    item = allocate_opening();
    if (!item) return POSITION(bar, after, x, y, width, height, flags);
    item->bar = bar;
    item->window = window;
    item->x = x;
    item->end_y = y;
    item->current_y = y - distance;
    item->width = width;
    item->height = height;
    item->distance = distance;
    item->start = TICK();
    int index = (int)(item - openings);
    item->timer = API(SetTimer, 0x1402570E0)(window, TIMER_ID + index, 8, animate);
    if (!item->timer) {
        item->occupied = FALSE;
        return POSITION(bar, after, x, y, width, height, flags);
    }
    item->custom = TRUE;
    if (!POSITION(bar, after, x, item->current_y, width, height, flags & ~SWP_SHOWWINDOW)) {
        cancel(item);
        return POSITION(bar, after, x, y, width, height, flags);
    }
    void *paint = ((void *(*)(void *))0x140111650ULL)(bar);
    if (!paint) {
        cancel(item);
        return POSITION(bar, after, x, y, width, height, flags);
    }
    ((void (*)(void *, void *))0x1404040E0ULL)(paint, bar);
    if (POSITION(bar, after, x, item->current_y, width, height, flags)) return TRUE;
    cancel(item);
    return POSITION(bar, after, x, y, width, height, flags);
}

__declspec(dllexport) int ui_context_popup_effect(void *bar) {
    Opening *item = find_opening(NULL, bar, 0);
    if (item && item->custom) return 5;
    return NATIVE(PopupEffect, 0x140116C40)(bar);
}
