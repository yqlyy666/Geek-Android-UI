"""Verify fixed outer header bounds and adjustable dividers in an isolated Geek.

Input is addressed only to the spawned process's HWNDs. HDM pointer arguments
use target-process memory; this script never moves the global cursor.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main() -> None:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test_copy", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--observe-only", action="store_true")
    args = parser.parse_args()
    test = args.test_copy.resolve(strict=True)
    meta = json.loads(test.with_suffix(".test-copy.json").read_text(encoding="utf-8"))
    assert test.is_relative_to(root) and meta["isolated_instance"] and meta["execution_level"] == "asInvoker"
    assert hashlib.sha256(test.read_bytes()).hexdigest() == meta["test_sha256"]

    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def prototype(module, name, result, *arguments):
        fn = getattr(module, name)
        fn.restype, fn.argtypes = result, arguments
        return fn

    api = lambda name, result, *arguments: prototype(user, name, result, *arguments)
    kapi = lambda name, result, *arguments: prototype(kernel, name, result, *arguments)
    enum_windows = api("EnumWindows", w.BOOL, callback_type, w.LPARAM)
    enum_children = api("EnumChildWindows", w.BOOL, w.HWND, callback_type, w.LPARAM)
    owner = api("GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
    class_name = api("GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
    visible = api("IsWindowVisible", w.BOOL, w.HWND)
    rect = api("GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
    client_rect = api("GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
    points = api("MapWindowPoints", c.c_int, w.HWND, w.HWND, c.POINTER(w.POINT), w.UINT)
    send_timeout = api("SendMessageTimeoutW", w.LPARAM, w.HWND, w.UINT, w.WPARAM, w.LPARAM,
                       w.UINT, w.UINT, c.POINTER(c.c_size_t))
    post = api("PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    position = api("SetWindowPos", w.BOOL, w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT)
    dpi_context = api("SetThreadDpiAwarenessContext", c.c_void_p, c.c_void_p)
    dpi_context(c.c_void_p(-4))
    open_process = kapi("OpenProcess", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
    allocate = kapi("VirtualAllocEx", c.c_void_p, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD)
    read = kapi("ReadProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))
    free = kapi("VirtualFreeEx", w.BOOL, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD)
    close = kapi("CloseHandle", w.BOOL, w.HANDLE)

    report = {"test_copy": str(test), "test_sha256": meta["test_sha256"]}
    sandbox = root / ("column-qa-profile-" + meta["test_sha256"][:8])
    sandbox.mkdir(exist_ok=True)
    env = dict(os.environ, APPDATA=str(sandbox), LOCALAPPDATA=str(sandbox))
    proc = subprocess.Popen([str(test)], cwd=sandbox, env=env)
    handle, remote = None, None

    def scoped(hwnd):
        pid = w.DWORD()
        owner(hwnd, c.byref(pid))
        return pid.value == proc.pid

    def list_windows(parent=0):
        windows = []

        @callback_type
        def visit(hwnd, _):
            if scoped(hwnd) and visible(hwnd):
                name = c.create_unicode_buffer(100)
                class_name(hwnd, name, len(name))
                windows.append((hwnd, name.value))
            return True

        if parent:
            assert scoped(parent)
            enum_children(parent, visit, 0)
        else:
            enum_windows(visit, 0)
        return windows

    def send(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd), hex(hwnd)
        returned = c.c_size_t(0)
        if not send_timeout(hwnd, msg, wp, lp, 2, 2500, c.byref(returned)):
            raise RuntimeError(f"send {msg:#x} timed out: {c.get_last_error()}, process={proc.poll()}")
        return c.c_ssize_t(returned.value).value

    def enqueue(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd), hex(hwnd)
        if not post(hwnd, msg, wp, lp):
            raise c.WinError(c.get_last_error())

    def pack(x, y):
        return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

    def read_remote(obj):
        count = c.c_size_t()
        assert read(handle, remote, c.byref(obj), c.sizeof(obj), c.byref(count)) and count.value == c.sizeof(obj)
        return obj

    def geometry(label):
        lc, hc = w.RECT(), w.RECT()
        assert client_rect(lv, c.byref(lc)) and client_rect(header, c.byref(hc))
        widths = [send(lv, 0x101D, i) for i in range(3)]
        cells = []
        for i in range(3):
            assert send(header, 0x1207, i, remote), "HDM_GETITEMRECT failed"
            r = read_remote(w.RECT())
            pts = (w.POINT * 2)(w.POINT(r.left, r.top), w.POINT(r.right, r.bottom))
            points(header, lv, pts, 2)
            cells.append([pts[0].x, pts[0].y, pts[1].x, pts[1].y])
        assert send(header, 0x1211, 3, remote), "HDM_GETORDERARRAY failed"
        order = list(read_remote((c.c_int * 3)()))
        item = {"label": label, "list_width": lc.right - lc.left, "header_width": hc.right - hc.left,
                "widths": widths, "cells": cells, "order": order}
        report.setdefault("geometry", []).append(item)
        return item

    def invariant(item):
        assert item["order"] == [0, 1, 2], item
        assert item["cells"][0][0] == 0, item
        assert item["cells"][-1][2] == item["list_width"], item
        assert sum(item["widths"]) == item["list_width"], item
        assert all(a[2] == b[0] for a, b in zip(item["cells"], item["cells"][1:])), item
        assert all(width > 0 for width in item["widths"]), item

    def drag(label, divider, delta, verify=True):
        before = geometry(label + " before")
        x = before["cells"][divider][2]
        y = max(4, (before["cells"][divider][3] - before["cells"][divider][1]) // 2)
        enqueue(header, 0x201, 1, pack(x, y))
        for step in (delta // 3, delta * 2 // 3, delta):
            enqueue(header, 0x200, 1, pack(x + step, y))
        enqueue(header, 0x202, 0, pack(x + delta, y))
        time.sleep(.25)
        after = geometry(label + " after")
        if verify:
            invariant(after)
            assert before["widths"] != after["widths"], (before, after)
            if divider == 0:
                assert before["widths"][2] == after["widths"][2], (before, after)
            elif divider == 1:
                assert before["widths"][0] == after["widths"][0], (before, after)
        return before, after

    try:
        for _ in range(200):
            tops = list_windows()
            candidates = [(h, child) for h, _ in tops for child, cls in list_windows(h) if cls == "SysListView32"]
            if len(candidates) == 1:
                main_window, lv = candidates[0]
                if send(lv, 0x1004) > 5:
                    break
            if proc.poll() is not None:
                raise RuntimeError(f"test exited prematurely: {proc.returncode}")
            time.sleep(.05)
        else:
            raise RuntimeError("list failed to load")
        time.sleep(.35)
        header = send(lv, 0x101F)
        assert scoped(header)
        handle = open_process(0x8 | 0x10 | 0x20, False, proc.pid)
        assert handle
        remote = allocate(handle, None, 512, 0x1000 | 0x2000, 4)
        assert remote
        initial = geometry("initial")
        if args.observe_only:
            drag("name separator", 0, 30, False)
            drag("size separator", 1, -30, False)
        else:
            invariant(initial)
            drag("name separator right", 0, 30)
            drag("name separator left", 0, -50)
            drag("size separator right", 1, 35)
            drag("size separator left", 1, -50)
            before, after = drag("far right edge", 2, -100, False)
            invariant(after)
            assert before["widths"] == after["widths"], (before, after)
            # Each internal divider can reach its minimum without moving an
            # outside edge, overflowing the header, or reversing cell order.
            drag("name minimum", 0, -10000)
            drag("size minimum", 1, -10000)
            bounds = w.RECT()
            assert rect(main_window, c.byref(bounds))
            for delta in (160, -80, 0):
                assert scoped(main_window)
                assert position(main_window, None, 0, 0, bounds.right - bounds.left + delta,
                                bounds.bottom - bounds.top, 0x2 | 0x4 | 0x10)
                time.sleep(.20)
                invariant(geometry("resize " + str(delta)))
            count = send(lv, 0x1004)
            report["sort"] = []
            for index, label in enumerate(("name", "size", "install_time")):
                current = geometry("before sort " + label)
                send(lv, 0x115, 6)
                for _ in range(5):
                    send(lv, 0x115, 1)
                top_before = send(lv, 0x1027)
                assert top_before > 0
                r = current["cells"][index]
                point = pack((r[0] + r[2]) // 2, max(4, (r[3] - r[1]) // 2))
                enqueue(header, 0x201, 1, point)
                enqueue(header, 0x202, 0, point)
                time.sleep(.20)
                top_after, count_after = send(lv, 0x1027), send(lv, 0x1004)
                row = {"column": label, "top_before": top_before, "top_after": top_after,
                       "count_before": count, "count_after": count_after}
                report["sort"].append(row)
                assert top_after == 0 and count_after == count, row
            report["passed"] = True
    finally:
        if remote and handle:
            free(handle, remote, 0, 0x8000)
        if handle:
            close(handle)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
        text = json.dumps(report, indent=2)
        if args.output:
            args.output.write_text(text + "\n", encoding="utf-8")
        print(text, flush=True)


if __name__ == "__main__":
    main()
