"""Sample only an isolated Geek ListView DC; detect actual rendered reversals.

This does not activate windows, send global input, capture the desktop, or use
PrintWindow (which would force painting and hide interleaved native frames).
The initial list is the visual template. Vertical template matching then tracks
actual glyph/card movement independently of the helper's state counters.
"""
from __future__ import annotations
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

import cv2
import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("test_copy", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--theme", choices=("light", "dark"), default="light")
    ap.add_argument("--sample-ms", type=float, default=2)
    ap.add_argument("--state-size", type=int, default=704)
    ap.add_argument("--state-fields", type=Path)
    ap.add_argument("--cases", type=int, default=4)
    ap.add_argument("--assert-monotonic", action="store_true")
    ap.add_argument("--notches", type=int, default=1)
    ap.add_argument("--spacing-ms", type=float, default=35)
    args = ap.parse_args()
    root = Path(__file__).resolve().parent
    test = args.test_copy.resolve(strict=True)
    output = args.output.resolve()
    meta = json.loads(test.with_suffix(".test-copy.json").read_text(encoding="utf-8"))
    assert test.is_relative_to(root) and output.is_relative_to(root)
    assert meta["isolated_instance"] and meta["execution_level"] == "asInvoker"
    assert not meta["ui_access"]
    assert hashlib.sha256(test.read_bytes()).hexdigest() == meta["test_sha256"]
    fields_file = args.state_fields or (root / "smooth-scroll-state-fields.json")
    fields = json.loads(fields_file.read_text(encoding="utf-8"))
    state_size = max(args.state_size, max(offset + struct.calcsize("<" + fmt)
                                        for offset, fmt in fields.values()))
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    gdi = c.WinDLL("gdi32", use_last_error=True)
    def proto(mod, name, res, *params):
        fn = getattr(mod, name)
        fn.restype, fn.argtypes = res, params
        return fn
    CB = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    get_pid = proto(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
    enum = proto(user, "EnumWindows", w.BOOL, CB, w.LPARAM)
    enum_child = proto(user, "EnumChildWindows", w.BOOL, w.HWND, CB, w.LPARAM)
    getclass = proto(user, "GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
    visible = proto(user, "IsWindowVisible", w.BOOL, w.HWND)
    getrect = proto(user, "GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
    screen = proto(user, "ClientToScreen", w.BOOL, w.HWND, c.POINTER(w.POINT))
    sm = proto(user, "SendMessageTimeoutW", w.LPARAM, w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t))
    pm = proto(user, "PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    proto(user, "SetThreadDpiAwarenessContext", c.c_void_p, c.c_void_p)(c.c_void_p(-4))
    op = proto(kernel, "OpenProcess", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
    rpm = proto(kernel, "ReadProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))
    alloc = proto(kernel, "VirtualAllocEx", c.c_void_p, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD)
    wpm = proto(kernel, "WriteProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))
    free = proto(kernel, "VirtualFreeEx", w.BOOL, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD)
    close = proto(kernel, "CloseHandle", w.BOOL, w.HANDLE)
    getdc = proto(user, "GetDC", w.HDC, w.HWND)
    release = proto(user, "ReleaseDC", c.c_int, w.HWND, w.HDC)
    compdc = proto(gdi, "CreateCompatibleDC", w.HDC, w.HDC)
    select = proto(gdi, "SelectObject", w.HANDLE, w.HDC, w.HANDLE)
    deletedc = proto(gdi, "DeleteDC", w.BOOL, w.HDC)
    deleteobj = proto(gdi, "DeleteObject", w.BOOL, w.HANDLE)
    bitblt = proto(gdi, "BitBlt", w.BOOL, w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int, w.DWORD)
    flush = proto(gdi, "GdiFlush", w.BOOL)
    dib = proto(gdi, "CreateDIBSection", w.HBITMAP, w.HDC, c.c_void_p, w.UINT, c.POINTER(c.c_void_p), w.HANDLE, w.DWORD)
    sandbox = root / ("scroll-pixel-qa-profile-" + meta["test_sha256"][:8])
    sandbox.mkdir(exist_ok=True)
    proc = subprocess.Popen([str(test)], cwd=sandbox, env=dict(os.environ, APPDATA=str(sandbox), LOCALAPPDATA=str(sandbox)))
    handle = remote = dc = memdc = bitmap = prev = None
    report = {"test_copy": str(test), "pid": proc.pid, "test_sha256": meta["test_sha256"], "theme": args.theme, "cases": []}
    def scoped(hwnd):
        pid = w.DWORD()
        get_pid(hwnd, c.byref(pid))
        return pid.value == proc.pid
    def wins(parent=0):
        found = []
        @CB
        def visit(hwnd, _):
            if scoped(hwnd) and visible(hwnd):
                name = c.create_unicode_buffer(128)
                getclass(hwnd, name, 128)
                found.append((hwnd, name.value))
            return True
        if parent:
            assert scoped(parent)
            enum_child(parent, visit, 0)
        else:
            enum(visit, 0)
        return found
    def send(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd)
        value = c.c_size_t()
        assert sm(hwnd, msg, wp, lp, 2, 2500, c.byref(value)), c.get_last_error()
        return c.c_ssize_t(value.value).value
    def post(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd) and pm(hwnd, msg, wp, lp)
    def read(addr, size):
        buf, got = c.create_string_buffer(size), c.c_size_t()
        assert rpm(handle, addr, buf, size, c.byref(got)) and got.value == size
        return buf.raw
    def state():
        address = struct.unpack("<Q", read(0x140412C18, 8))[0]
        seen = set()
        while address and address not in seen:
            seen.add(address)
            window, following = struct.unpack_from("<QQ", read(address, 24), 8)
            if window == lv:
                raw = read(address, state_size)
                result = {}
                for name, (offset, fmt) in fields.items():
                    value = struct.unpack_from("<"+fmt, raw, offset)
                    result[name] = value[0] if len(value) == 1 else list(value)
                return result
            address = following
        return None
    def capture():
        assert bitblt(memdc, 0, 0, width, height, dc, left, top, 0x00CC0020), c.get_last_error()
        flush()
        return np.ctypeslib.as_array((c.c_uint8 * (width*height*4)).from_address(bits.value)).reshape(height,width,4)[:,:,:3].copy()
    def reset():
        send(lv, 0x115, 6)
        time.sleep(.25)
    try:
        for _ in range(240):
            targets = [(h,ch) for h,_ in wins() for ch,cls in wins(h) if cls == "SysListView32"]
            if len(targets) == 1:
                main, lv = targets[0]
                if send(lv, 0x1004) > 5:
                    break
            assert proc.poll() is None, proc.returncode
            time.sleep(.05)
        else:
            raise RuntimeError("list did not load")
        time.sleep(.3)
        for _ in range(3):
            post(main, 0x111, 0x809A if args.theme == "dark" else 0x8099)
            time.sleep(.6)
            col = send(lv,0x1000)&0xffffff
            bright = sum((col>>(8*i))&255 for i in range(3))
            if (bright<384) == (args.theme == "dark"):
                break
        handle = op(0x8|0x10|0x20|0x400,False,proc.pid)
        assert handle
        remote = alloc(handle,None,4096,0x1000|0x2000,4)
        assert remote
        reset()
        buf, got = c.create_string_buffer(16), c.c_size_t()
        assert wpm(handle,remote,buf,16,c.byref(got))
        assert send(lv,0x100E,0,remote)
        rect0 = struct.unpack("<iiii",read(remote,16))
        cr = w.RECT()
        assert getrect(lv,c.byref(cr))
        # Match actual glyphs rather than the empty card edges. Keep the
        # right-aligned date/size columns outside this narrow capture.
        left, top = 65, rect0[1]+4
        width, height = min(440,cr.right-80), cr.bottom-top-4
        assert width>200 and height>400
        dc = getdc(lv)
        memdc = compdc(dc)
        info = struct.pack("<IiiHHIIiiII",40,width,-height,1,32,0,width*height*4,0,0,0,0)
        bits = c.c_void_p()
        bitmap = dib(dc,c.c_char_p(info),0,c.byref(bits),None,0)
        assert dc and memdc and bitmap and bits.value
        prev = select(memdc,bitmap)
        report.update({"list_hwnd":hex(lv),"capture_rect":[left,top,width,height],"row_height":rect0[3]-rect0[1]})
        base = capture()
        Image.fromarray(base[:,:,::-1]).save(output.with_suffix(".baseline.png"))
        point = w.POINT(cr.right//2,(cr.bottom+top)//2)
        assert screen(lv,c.byref(point))
        lp = (point.x&65535)|((point.y&65535)<<16)
        assert 1 <= args.notches <= 4
        max_offset = min(max(500, 189*args.notches+150),height-120)
        match_height = height-max_offset
        print(json.dumps({"pid":proc.pid,"baseline":str(output.with_suffix(".baseline.png")),"shape":list(base.shape)}),flush=True)
        # Capture first, analyze afterward so matching never delays frames.
        for case_index in range(args.cases):
            reset()
            start = time.perf_counter()
            captures = []
            posted = 0
            while time.perf_counter()-start < .40 + (args.notches-1)*args.spacing_ms/1000:
                now = time.perf_counter()-start
                if posted < args.notches and now>=.015+posted*args.spacing_ms/1000:
                    post(lv,0x20A,((-120&65535)<<16),lp)
                    posted += 1
                before = state()
                pixels = capture()
                after = state()
                captures.append((round(now*1000,3),before,after,pixels))
                time.sleep(max(.001,args.sample_ms/1000))
            samples = []
            for at,before,after,pixels in captures:
                matched = cv2.matchTemplate(base,pixels[:match_height],cv2.TM_SQDIFF_NORMED)
                minimum,_,loc,_ = cv2.minMaxLoc(matched)
                pos = loc[1]
                sample = {"ms":at,"offset":pos,"error":minimum,
                          "before":{"presented":before["presented"],"visual":before["visual"],"native_top":before["native_top"],"frames":before["frames"],"active":before["active"]},
                          "after":{"presented":after["presented"],"visual":after["visual"],"native_top":after["native_top"],"frames":after["frames"],"active":after["active"]}}
                samples.append(sample)
            reversals = [{"from":a,"to":b} for a,b in zip(samples,samples[1:]) if b["offset"]<a["offset"]-1]
            unique = []
            for sample in samples:
                if not unique or sample["offset"] != unique[-1]["offset"]:
                    unique.append(sample)
            case = {"index":case_index,"samples":samples,"distinct_motion":unique,"reversals":reversals,
                    "max_match_error":max(s["error"] for s in samples)}
            report["cases"].append(case)
            if case["max_match_error"] >= .001:
                worst = max(range(len(samples)), key=lambda i: samples[i]["error"])
                Image.fromarray(captures[worst][3][:,:,::-1]).save(output.with_suffix(f".case{case_index}.mismatch.png"))
            print(json.dumps({"case":case_index,"motion":[[s["ms"],s["offset"],round(s["error"],6)] for s in unique],"reversals":len(reversals)}),flush=True)
            # Save every distinct actual image from the first run, plus any
            # reversal image from later runs for direct visual inspection.
            if case_index==0:
                indices = []
                last = None
                for i,s in enumerate(samples):
                    if s["offset"] != last:
                        indices.append(i)
                        last=s["offset"]
                contact = Image.new("RGB",(width*len(indices),height))
                for n,i in enumerate(indices):
                    contact.paste(Image.fromarray(captures[i][3][:,:,::-1]),(n*width,0))
                contact.save(output.with_suffix(".frames.png"))
        report["rendered_reversal_count"] = sum(len(case["reversals"]) for case in report["cases"])
        report["motion_matching_valid"] = all(case["max_match_error"] < .001 for case in report["cases"])
        report["complete"] = True
        if args.assert_monotonic:
            assert report["motion_matching_valid"], "target DC did not match its own list pixels"
            assert report["rendered_reversal_count"] == 0, report["rendered_reversal_count"]
    finally:
        if prev and memdc: select(memdc,prev)
        if bitmap: deleteobj(bitmap)
        if memdc: deletedc(memdc)
        if dc: release(lv,dc)
        if remote and handle: free(handle,remote,0,0x8000)
        if handle: close(handle)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
        report["test_process_exit"] = proc.returncode
        output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(json.dumps({"output":str(output),"reversals":report.get("rendered_reversal_count"),"complete":report.get("complete",False)}),flush=True)


if __name__ == "__main__":
    main()
