"""Measure Geek list movement through a PID-scoped, isolated test instance.

No global mouse/keyboard input, foreground activation, or desktop capture is
used. Native list/header pointers always refer to target-process allocation.
The report distinguishes native item geometry from optional animation state.
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test_copy", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sample-ms", type=float, default=8)
    parser.add_argument("--theme", choices=("default", "light", "dark"), default="default")
    parser.add_argument("--expect-native-pixel-motion", action="store_true")
    parser.add_argument("--state-address", type=lambda s: int(s, 0))
    parser.add_argument("--state-pointer", action="store_true",
                        help="state-address contains the heap-state pointer rather than the state itself")
    parser.add_argument("--state-select-list", action="store_true",
                        help="follow list-linked nodes (HWND at +8, next at +16) to the tested list")
    parser.add_argument("--state-size", type=int, default=256)
    parser.add_argument("--state-fields", type=Path,
                        help='JSON mapping names to [byte_offset, struct_format], e.g. {"offset":[24,"i"]}')
    parser.add_argument("--visual-position-field",
                        help="decoded state field containing the actual painted absolute pixel position")
    parser.add_argument("--frame-field", help="decoded state field counting successfully painted frames")
    parser.add_argument("--cache-opacity", action="store_true",
                        help="verify valid row-cache DIB slots are entirely opaque from target memory")
    parser.add_argument("--cache-image", type=Path,
                        help="save actual row-cache pixels from target memory; never captures the desktop")
    parser.add_argument("--modifier-cancel", action="store_true",
                        help="check that Ctrl/Shift wheel requests cancel an in-flight ordinary wheel animation")
    parser.add_argument("--redraw-cancel", action="store_true",
                        help="check that paired WM_SETREDRAW calls cancel animation and restore painting")
    parser.add_argument("--region-restored", action="store_true",
                        help="compare the exact native list window region before and after every case")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    test = args.test_copy.resolve(strict=True)
    meta = json.loads(test.with_suffix(".test-copy.json").read_text(encoding="utf-8"))
    assert test.is_relative_to(root) and meta["isolated_instance"]
    assert meta["execution_level"] == "asInvoker" and not meta["ui_access"]
    assert hashlib.sha256(test.read_bytes()).hexdigest() == meta["test_sha256"]
    if args.output:
        assert args.output.resolve().is_relative_to(root)
    fields = json.loads(args.state_fields.read_text(encoding="utf-8")) if args.state_fields else {}
    assert not fields or args.state_address

    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    gdi = c.WinDLL("gdi32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def proto(module, name, result, *arguments):
        fn = getattr(module, name)
        fn.restype, fn.argtypes = result, arguments
        return fn

    api = lambda name, result, *arguments: proto(user, name, result, *arguments)
    kapi = lambda name, result, *arguments: proto(kernel, name, result, *arguments)
    enum_windows = api("EnumWindows", w.BOOL, callback_type, w.LPARAM)
    enum_children = api("EnumChildWindows", w.BOOL, w.HWND, callback_type, w.LPARAM)
    get_pid = api("GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
    class_name = api("GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
    visible = api("IsWindowVisible", w.BOOL, w.HWND)
    get_client = api("GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
    to_screen = api("ClientToScreen", w.BOOL, w.HWND, c.POINTER(w.POINT))
    send_timeout = api("SendMessageTimeoutW", w.LPARAM, w.HWND, w.UINT, w.WPARAM, w.LPARAM,
                       w.UINT, w.UINT, c.POINTER(c.c_size_t))
    post = api("PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    gui_resources = api("GetGuiResources", w.DWORD, w.HANDLE, w.DWORD)
    get_window_region = api("GetWindowRgn", c.c_int, w.HWND, w.HRGN)
    make_region = proto(gdi, "CreateRectRgn", w.HRGN, c.c_int, c.c_int, c.c_int, c.c_int)
    region_data = proto(gdi, "GetRegionData", w.DWORD, w.HRGN, w.DWORD, c.c_void_p)
    delete_object = proto(gdi, "DeleteObject", w.BOOL, w.HGDIOBJ)
    dpi_context = api("SetThreadDpiAwarenessContext", c.c_void_p, c.c_void_p)
    dpi_context(c.c_void_p(-4))
    open_process = kapi("OpenProcess", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
    allocate = kapi("VirtualAllocEx", c.c_void_p, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD)
    read = kapi("ReadProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))
    write = kapi("WriteProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))
    free = kapi("VirtualFreeEx", w.BOOL, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD)
    close = kapi("CloseHandle", w.BOOL, w.HANDLE)

    report = {"test_copy": str(test), "test_sha256": meta["test_sha256"], "cases": []}
    active_cache_capture = None
    sandbox = root / ("scroll-qa-profile-" + meta["test_sha256"][:8])
    sandbox.mkdir(exist_ok=True)
    env = dict(os.environ, APPDATA=str(sandbox), LOCALAPPDATA=str(sandbox))
    proc = subprocess.Popen([str(test)], cwd=sandbox, env=env)
    handle = remote = None

    def scoped(hwnd):
        pid = w.DWORD()
        get_pid(hwnd, c.byref(pid))
        return pid.value == proc.pid

    def windows(parent=0):
        found = []

        @callback_type
        def visit(hwnd, _):
            if scoped(hwnd) and visible(hwnd):
                name = c.create_unicode_buffer(120)
                class_name(hwnd, name, len(name))
                found.append((hwnd, name.value))
            return True

        if parent:
            assert scoped(parent)
            enum_children(parent, visit, 0)
        else:
            enum_windows(visit, 0)
        return found

    def send(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd)
        returned = c.c_size_t()
        if not send_timeout(hwnd, msg, wp, lp, 2, 2500, c.byref(returned)):
            raise RuntimeError(f"send {msg:#x} timed out: {c.get_last_error()}, process={proc.poll()}")
        return c.c_ssize_t(returned.value).value

    def enqueue(hwnd, msg, wp=0, lp=0):
        assert scoped(hwnd)
        assert post(hwnd, msg, wp, lp), c.get_last_error()

    def pack(x, y):
        return (x & 0xFFFF) | ((y & 0xFFFF) << 16)

    def native_region():
        assert scoped(lv)
        region = make_region(0, 0, 0, 0)
        assert region
        try:
            kind = get_window_region(lv, region)
            if not kind:
                return {"kind": "none"}
            size = region_data(region, 0, None)
            assert size >= 32
            data = c.create_string_buffer(size)
            assert region_data(region, size, data) == size
            value = data.raw
            return {"kind": kind, "bytes": size, "rectangles": struct.unpack_from("<I", value, 8)[0],
                    "bounds": list(struct.unpack_from("<iiii", value, 16)),
                    "sha256": hashlib.sha256(value).hexdigest()}
        finally:
            assert delete_object(region)

    def read_bytes(address, size):
        buffer = c.create_string_buffer(size)
        count = c.c_size_t()
        assert read(handle, address, buffer, size, c.byref(count)) and count.value == size
        return buffer.raw

    def write_bytes(address, value):
        buffer = c.create_string_buffer(bytes(value))
        count = c.c_size_t()
        assert write(handle, address, buffer, len(value), c.byref(count)) and count.value == len(value)

    def item_rect(index):
        write_bytes(remote, struct.pack("<iiii", 0, 0, 0, 0))
        assert send(lv, 0x100E, index, remote), ("LVM_GETITEMRECT", index)
        return list(struct.unpack("<iiii", read_bytes(remote, 16)))

    def item_text(index):
        # LVITEMW has pointer alignment at offset 24 in AMD64. Text and
        # structure reside in the process that receives LVM_GETITEMTEXTW.
        value = bytearray(88)
        struct.pack_into("<Q", value, 24, remote + 256)
        struct.pack_into("<i", value, 32, 512)
        write_bytes(remote, value)
        length = send(lv, 0x1073, index, remote)
        assert 0 <= length < 512
        return read_bytes(remote + 256, length * 2).decode("utf-16le")

    def state():
        if not args.state_address:
            return None
        address = args.state_address
        if args.state_pointer:
            address = struct.unpack("<Q", read_bytes(address, 8))[0]
            if not address:
                return None
        if args.state_select_list:
            seen = set()
            while address and address not in seen:
                seen.add(address)
                window, following = struct.unpack_from("<QQ", read_bytes(address, 24), 8)
                if window == lv:
                    break
                address = following
            else:
                return None
        raw = read_bytes(address, args.state_size)
        if not fields:
            return {"raw": raw.hex()}
        result = {}
        for name, (offset, fmt) in fields.items():
            value = struct.unpack_from("<" + fmt, raw, offset)
            result[name] = value[0] if len(value) == 1 else list(value)
        return result

    def cache_opacity(label):
        snapshot = active_cache_capture[0] if active_cache_capture else state()
        assert snapshot and snapshot["pixels"] and snapshot["width"] > 0
        width, height, capacity = snapshot["width"], snapshot["row_height"], snapshot["capacity"]
        assert 0 < width * height * capacity < 18000000
        slot_bytes = width * height * 4
        raw = active_cache_capture[1] if active_cache_capture else read_bytes(snapshot["pixels"], slot_bytes * capacity)
        valid = [i for i, item in enumerate(snapshot["cache_ids"][:capacity]) if item >= 0]
        assert valid
        details = []
        for slot in valid:
            value = raw[slot * slot_bytes:(slot + 1) * slot_bytes]
            alpha = value[3::4]
            nonopaque = sum(x != 255 for x in alpha)
            details.append({"slot": slot, "item": snapshot["cache_ids"][slot],
                            "pixels": len(alpha), "nonopaque": nonopaque,
                            "alpha_min": min(alpha), "alpha_max": max(alpha)})
            assert not nonopaque, details[-1]
        report.setdefault("opaque_cache_checks", []).append({"label": label, "slots": details,
                                                             "pixel_total": width * height * len(valid),
                                                             "captured_while_active": bool(snapshot["active"])})
        if args.cache_image:
            import sys
            from PIL import Image
            output = args.cache_image.resolve()
            assert output.is_relative_to(root)
            # These are rendered cache rows, with their true slot order and
            # alpha. This artifact is deliberately not described as a screen.
            image = Image.frombytes("RGBA", (width, height * capacity), raw, "raw", "BGRA")
            image.save(output)
            report["actual_cache_image"] = str(output)

    def observation(start, native=True):
        result = {"ms": round((time.perf_counter() - start) * 1000, 2)}
        if native:
            r = item_rect(0)
            result.update({"top_index": send(lv, 0x1027), "row0": r,
                           "native_scroll_px": baseline_y - r[1]})
        if args.state_address:
            result["animation"] = state()
        return result

    def sample(label, events, duration=.7):
        nonlocal active_cache_capture
        start, index, samples = time.perf_counter(), 0, []
        samples.append(observation(start))
        while time.perf_counter() - start < duration:
            elapsed = time.perf_counter() - start
            while index < len(events) and elapsed >= events[index][0]:
                _, fn = events[index]
                fn()
                index += 1
            # Remote memory reads do not introduce synchronous UI messages in
            # every frame; this avoids starving the test subject's paint queue.
            observed = observation(start, native=not args.state_address)
            samples.append(observed)
            # Native layout/theme messages can invalidate row-cache IDs after
            # animation ends. Retain actual completed rows while animating,
            # rather than assuming the cache remains valid after settling.
            snapshot = observed.get("animation")
            if (args.cache_opacity and label == "one wheel notch" and not active_cache_capture
                    and snapshot and snapshot["active"] and not snapshot["busy"]
                    and snapshot["frames"] >= 4 and snapshot["pixels"]
                    and any(item >= 0 for item in snapshot["cache_ids"])):
                size = snapshot["width"] * snapshot["row_height"] * snapshot["capacity"] * 4
                assert 0 < size < 72000000
                active_cache_capture = (snapshot, read_bytes(snapshot["pixels"], size))
            time.sleep(max(0.001, args.sample_ms / 1000))
        samples.append(observation(start))
        native = [row for row in samples if "native_scroll_px" in row]
        positions = list(dict.fromkeys(row["native_scroll_px"] for row in native))
        deltas = [abs(b["native_scroll_px"] - a["native_scroll_px"]) for a, b in zip(native, native[1:])]
        case = {"label": label, "samples": samples, "native_distinct_positions": positions,
                "native_max_step_px": max(deltas, default=0),
                "count_after": send(lv, 0x1004)}
        report["cases"].append(case)
        if args.region_restored:
            case["native_region_after"] = native_region()
            assert case["native_region_after"] == report["native_region_before"], case
        last_state = samples[-1].get("animation")
        if last_state:
            case["settled_state"] = {name: last_state[name]
                                     for name in ("active", "queue", "pending", "ready", "frames")}
            assert not last_state["active"] and not last_state["queue"] and not last_state["pending"], case["settled_state"]
            if "precise_timer" in last_state:
                case["settled_state"]["precise_timer"] = last_state["precise_timer"]
                assert not last_state["precise_timer"], case["settled_state"]
            if "frame_interval_ms" in last_state:
                case["frame_interval_ms"] = last_state["frame_interval_ms"]
            if "redraw_disabled" in last_state:
                case["settled_state"]["redraw_disabled"] = last_state["redraw_disabled"]
                assert not last_state["redraw_disabled"], case["settled_state"]
            for name in ("suppressed_draws", "native_commits", "last_stop_message"):
                if name in last_state:
                    case[name] = last_state[name]
            for name in ("header", "shielded_commits", "suppressed_header_redraws"):
                if name in last_state:
                    case[name] = last_state[name]
        if args.visual_position_field:
            visual = [row["animation"][args.visual_position_field] for row in samples
                      if row.get("animation") and args.visual_position_field in row["animation"]]
            case["visual_distinct_positions"] = list(dict.fromkeys(visual))
            case["visual_max_step_px"] = max((abs(b-a) for a,b in zip(visual, visual[1:])), default=0)
            case["visual_subrow_intermediate_positions"] = sum(
                abs(position / row_height - round(position / row_height)) > 0.0001
                for position in case["visual_distinct_positions"])
        if args.frame_field:
            frames = [row["animation"][args.frame_field] for row in samples
                      if row.get("animation") and args.frame_field in row["animation"]]
            case["painted_frame_count_change"] = max(frames, default=0)-min(frames, default=0)
        assert case["count_after"] == count, case
        return case

    def wheel(delta, modifiers=0):
        enqueue(lv, 0x20A, ((delta & 0xFFFF) << 16) | modifiers, wheel_point)

    def reset():
        send(lv, 0x115, 6)  # Direct SB_TOP must cancel queued wheel movement.
        time.sleep(.35)
        assert send(lv, 0x1027) == 0
        assert item_rect(0)[1] == baseline_y

    def column_geometry():
        rect = w.RECT()
        assert get_client(lv, c.byref(rect))
        cells = []
        for i in range(3):
            assert send(header, 0x1207, i, remote)
            cells.append(list(struct.unpack("<iiii", read_bytes(remote, 16))))
        widths = [send(lv, 0x101D, i) for i in range(3)]
        assert send(header, 0x1211, 3, remote)
        order = list(struct.unpack("<iii", read_bytes(remote, 12)))
        assert order == [0, 1, 2]
        assert cells[0][0] == 0 and cells[-1][2] == rect.right
        assert sum(widths) == rect.right and all(x > 0 for x in widths)
        assert all(a[2] == b[0] for a, b in zip(cells, cells[1:]))
        return {"widths": widths, "cells": cells, "order": order}

    def header_click(index):
        cell = column_geometry()["cells"][index]
        point = pack((cell[0] + cell[2]) // 2, max(3, (cell[3] - cell[1]) // 2))
        enqueue(header, 0x201, 1, point)
        enqueue(header, 0x202, 0, point)

    try:
        for _ in range(240):
            candidates = [(h, child) for h, _ in windows() for child, cls in windows(h)
                          if cls == "SysListView32"]
            if len(candidates) == 1:
                main_window, lv = candidates[0]
                if send(lv, 0x1004) > 5:
                    break
            if proc.poll() is not None:
                raise RuntimeError(f"test exited early: {proc.returncode}")
            time.sleep(.05)
        else:
            raise RuntimeError("list did not load")
        time.sleep(.35)
        if args.theme != "default":
            for attempt in range(4):
                enqueue(main_window, 0x111, 0x809A if args.theme == "dark" else 0x8099)
                time.sleep(.8)
                current = send(lv, 0x1000) & 0xFFFFFF
                brightness = (current & 255) + ((current >> 8) & 255) + ((current >> 16) & 255)
                if (args.theme == "dark" and brightness < 384) or (args.theme == "light" and brightness > 600):
                    break
        handle = open_process(0x8 | 0x10 | 0x20 | 0x400, False, proc.pid)
        assert handle
        remote = allocate(handle, None, 4096, 0x1000 | 0x2000, 4)
        assert remote
        header, count = send(lv, 0x101F), send(lv, 0x1004)
        assert scoped(header)
        color = send(lv, 0x1000) & 0xFFFFFF
        rgb = [color & 255, (color >> 8) & 255, (color >> 16) & 255]
        report["requested_theme"], report["list_background_rgb"] = args.theme, rgb
        if args.theme == "dark":
            assert sum(rgb) < 384, rgb
        elif args.theme == "light":
            assert sum(rgb) > 600, rgb
        send(lv, 0x115, 6)
        initial_rect = item_rect(0)
        baseline_y, row_height = initial_rect[1], initial_rect[3] - initial_rect[1]
        report.update({"pid": proc.pid, "main_hwnd": hex(main_window), "list_hwnd": hex(lv),
                       "row_count": count, "row_height": row_height,
                       "columns_before": column_geometry(),
                       "first_names_before": [item_text(i) for i in range(3)]})
        if args.region_restored:
            report["native_region_before"] = native_region()
        client, point = w.RECT(), w.POINT()
        assert get_client(lv, c.byref(client))
        point.x, point.y = client.right // 2, max(baseline_y + row_height, client.bottom // 2)
        assert to_screen(lv, c.byref(point))
        wheel_point = pack(point.x, point.y)
        resources_before = gui_resources(handle, 0)
        one = sample("one wheel notch", [(0, lambda: wheel(-120))])
        assert one["samples"][-1]["native_scroll_px"] > 0
        if args.expect_native_pixel_motion:
            assert len(one["native_distinct_positions"]) > 3, one
            assert 0 < one["native_max_step_px"] < row_height, one
        if args.visual_position_field:
            assert len(one["visual_distinct_positions"]) > 4, one
            # An external sampling thread may miss frames while copying the
            # DIB or being descheduled. Actual successful paint counts and
            # multiple fractional positions prove pixel animation without
            # requiring every sampled delta to represent one render frame.
            assert one["visual_subrow_intermediate_positions"] >= 4, one
        if args.frame_field:
            assert one["painted_frame_count_change"] >= 4, one
        if args.cache_opacity:
            cache_opacity("after one notch")
        reset()
        repeated = sample("four fast notches", [(i * .035, lambda: wheel(-120)) for i in range(4)])
        assert repeated["samples"][-1]["native_scroll_px"] > one["samples"][-1]["native_scroll_px"]
        reset()
        partial = sample("four high resolution quarter notches", [(i * .025, lambda: wheel(-30)) for i in range(4)])
        assert partial["samples"][-1]["native_scroll_px"] > 0
        reset()
        reversed_case = sample("direction reversal", [(0, lambda: wheel(-120)), (.055, lambda: wheel(120))])
        assert reversed_case["samples"][-1]["native_scroll_px"] == 0
        reset()
        cancelled = sample("direct scrollbar cancels animation", [(0, lambda: wheel(-120)),
                           (.035, lambda: send(lv, 0x115, 6))])
        assert cancelled["samples"][-1]["native_scroll_px"] == 0
        if args.modifier_cancel:
            for label, modifier in (("Ctrl", 8), ("Shift", 4)):
                reset()
                case = sample(label + " wheel cancels animation", [(0, lambda: wheel(-120)),
                              (.035, lambda modifier=modifier: wheel(-120, modifier))])
                settled = [row["animation"] for row in case["samples"]
                           if row["ms"] >= 200 and row.get("animation")]
                assert settled and all(not row["active"] and not row["queue"] for row in settled)
        if args.redraw_cancel:
            reset()
            sample("redraw disabled then reenabled", [(0, lambda: wheel(-120)),
                   (.035, lambda: send(lv, 0xB, 0)), (.080, lambda: send(lv, 0xB, 1))])
            reset()
            resumed = sample("wheel after redraw reenabled", [(0, lambda: wheel(-120))])
            assert resumed["samples"][-1]["native_scroll_px"] > 0
            if args.frame_field:
                assert resumed["painted_frame_count_change"] >= 4
        for i, name in enumerate(("name", "size", "install_time")):
            reset()
            case = sample("sort while wheel pending: " + name,
                          [(0, lambda: wheel(-120)), (.05, lambda index=i: header_click(index))])
            assert case["samples"][-1]["native_scroll_px"] == 0
            assert case["samples"][-1]["top_index"] == 0
            case["first_names_after"] = [item_text(j) for j in range(3)]
        send(lv, 0x115, 7)  # SB_BOTTOM
        time.sleep(.2)
        end_before = observation(time.perf_counter())["native_scroll_px"]
        bottom = sample("lower boundary", [(0, lambda: wheel(-120)), (.035, lambda: wheel(-120))])
        assert bottom["samples"][-1]["native_scroll_px"] == end_before
        reset()
        upper = sample("upper boundary", [(0, lambda: wheel(120)), (.035, lambda: wheel(120))])
        assert upper["samples"][-1]["native_scroll_px"] == 0
        report["columns_after"] = column_geometry()
        assert report["columns_before"] == report["columns_after"]
        time.sleep(.2)
        resources_after = gui_resources(handle, 0)
        # A retained row cache may legitimately allocate on its first visit.
        # Exercise the same short range again and compare only warmed passes.
        def resource_pass():
            reset()
            for _ in range(4):
                wheel(-120)
                time.sleep(.035)
            time.sleep(.45)
            reset()
            return gui_resources(handle, 0)

        warm = resource_pass()
        repeated_resources = [resource_pass(), resource_pass()]
        report["gdi_resources"] = {"startup": resources_before, "after_cases": resources_after,
                                   "warmed": warm, "repeat_passes": repeated_resources,
                                   "warmed_growth": max(repeated_resources) - warm}
        assert max(repeated_resources) - warm <= 4
        report["passed"] = True
    finally:
        if remote and handle:
            free(handle, remote, 0, 0x8000)
        if handle:
            close(handle)
        # Terminate only the isolated test that this invocation created.
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
        report["test_process_exit"] = proc.returncode
        output = json.dumps(report, ensure_ascii=False, indent=2)
        if args.output:
            args.output.write_text(output + "\n", encoding="utf-8")
            summary = {key: value for key, value in report.items() if key != "cases"}
            summary["report_path"] = str(args.output.resolve())
            summary["cases"] = [{key: value for key, value in case.items() if key != "samples"}
                                for case in report["cases"]]
            print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
        else:
            print(output, flush=True)


if __name__ == "__main__":
    main()
