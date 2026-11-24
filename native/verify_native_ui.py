"""Execute the actual appended AMD64 UI code in Unicorn, without opening Geek."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent
import pefile
import unicorn
from unicorn import x86_const as X

MASK64 = (1 << 64) - 1
NONVOLATILE = [X.UC_X86_REG_RBX, X.UC_X86_REG_RBP, X.UC_X86_REG_RSI, X.UC_X86_REG_RDI,
               X.UC_X86_REG_R12, X.UC_X86_REG_R13, X.UC_X86_REG_R14, X.UC_X86_REG_R15]
VOLATILE = [X.UC_X86_REG_RCX, X.UC_X86_REG_RDX, X.UC_X86_REG_R8, X.UC_X86_REG_R9,
            X.UC_X86_REG_R10, X.UC_X86_REG_R11]
ARGS = [X.UC_X86_REG_RCX, X.UC_X86_REG_RDX, X.UC_X86_REG_R8, X.UC_X86_REG_R9]


def px(value: int, dpi: int) -> int:
    return (value * dpi + 48) // 96


class Machine:
    def __init__(self, image: bytes, dpi: int = 96, dpi_function: bool = True):
        self.pe = pefile.PE(data=image, fast_load=True)
        self.u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.u.mem_map(self.base, (self.pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
        self.u.mem_write(self.base, self.pe.get_memory_mapped_image())
        self.scratch = 0x20000000
        self.u.mem_map(self.scratch, 0x200000)
        self.stop = self.scratch + 0x1000
        self.stack = self.scratch + 0x1E0008
        self.next_stub = self.scratch + 0x2000
        self.hooks = {}
        self.calls = []
        self.dpi = dpi
        self.dpi_function = dpi_function
        self.hwnd = 0x0000000076543210
        self.module = 0x0000000076500000
        self.self_ptr = self.scratch + 0x4000
        self.measure = self.scratch + 0x8000
        self.paint = self.scratch + 0xA000
        self.vtable = self.scratch + 0xB000
        self.control = self.scratch + 0xC000
        self.parent = self.scratch + 0xD000
        self.out = self.scratch + 0xE000
        self.dc = 0x11223344
        self.write64(self.self_ptr + 0x40, self.hwnd)
        self.write64(self.paint, self.vtable)
        self.write64(self.control + 0x128, self.parent)
        self.write64(self.parent + 0x40, self.hwnd)
        self.u.hook_add(unicorn.UC_HOOK_CODE, self._code)
        self.dpi_stub = self.stub("GetDpiForWindow", self._dpi)
        self.iat(0x140256AC0, "GetModuleHandleW", self._module)
        self.iat(0x140256A78, "GetProcAddress", self._proc)

    def write64(self, address, value):
        self.u.mem_write(address, struct.pack("<Q", value & MASK64))

    def read64(self, address):
        return struct.unpack("<Q", self.u.mem_read(address, 8))[0]

    def read32(self, address, signed=False):
        return struct.unpack("<i" if signed else "<I", self.u.mem_read(address, 4))[0]

    def string(self, address, wide=False):
        data = bytearray()
        step = 2 if wide else 1
        for index in range(512):
            part = bytes(self.u.mem_read(address + index * step, step))
            if not any(part):
                return data.decode("utf-16le" if wide else "ascii")
            data.extend(part)
        raise AssertionError("Unterminated API string")

    def arg(self, number):
        if number < 4:
            return self.u.reg_read(ARGS[number])
        return self.read64(self.u.reg_read(X.UC_X86_REG_RSP) + 0x28 + (number - 4) * 8)

    def stub(self, name, callback):
        address = self.next_stub
        self.next_stub += 0x20
        self.u.mem_write(address, b"\xC3")
        self.hooks[address] = (name, callback)
        return address

    def iat(self, slot, name, callback):
        address = self.stub(name, callback)
        self.write64(slot, address)
        return address

    def intercept(self, address, name, callback):
        self.hooks[address] = (name, callback)

    def _code(self, u, address, size, _):
        if address not in self.hooks:
            return
        name, callback = self.hooks[address]
        stack = u.reg_read(X.UC_X86_REG_RSP)
        assert stack % 16 == 8, (name, "callee stack alignment", hex(stack))
        args = [self.arg(i) for i in range(6)]
        self.calls.append({"api": name, "arguments": args})
        result = callback()
        # Windows calls may destroy all volatile registers. Poison them so the
        # real native code cannot accidentally rely on forgiving test stubs.
        for index, register in enumerate(VOLATILE):
            u.reg_write(register, 0xBAD0000000000000 + index)
        for index in range(6):
            u.reg_write(getattr(X, f"UC_X86_REG_XMM{index}"), 0xBAD00000000000000000000000000000 + index)
        u.reg_write(X.UC_X86_REG_RAX, int(result or 0) & MASK64)
        u.reg_write(X.UC_X86_REG_RSP, stack + 8)
        u.reg_write(X.UC_X86_REG_RIP, self.read64(stack))

    def _module(self):
        assert self.string(self.arg(0), wide=True) == "user32.dll"
        return self.module

    def _proc(self):
        assert self.arg(0) == self.module
        assert self.string(self.arg(1)) == "GetDpiForWindow"
        return self.dpi_stub if self.dpi_function else 0

    def _dpi(self):
        assert self.arg(0) in (self.hwnd, 0)
        return self.dpi

    def run(self, entry, *arguments):
        self.u.mem_write(self.stack - 0x4000, b"\xCC" * 0x5000)
        self.write64(self.stack, self.stop)
        for index, value in enumerate(arguments):
            if index < 4:
                self.u.reg_write(ARGS[index], value & MASK64)
            else:
                self.write64(self.stack + 0x28 + (index - 4) * 8, value)
        original = {}
        for index, register in enumerate(NONVOLATILE):
            original[register] = 0x1234567800000000 + index * 0x111111
            self.u.reg_write(register, original[register])
        for index in range(6, 16):
            register = getattr(X, f"UC_X86_REG_XMM{index}")
            original[register] = 0x123456789ABCDEF00011223344556600 + index
            self.u.reg_write(register, original[register])
        self.u.reg_write(X.UC_X86_REG_RSP, self.stack)
        self.u.emu_start(entry, self.stop, count=100000)
        assert self.u.reg_read(X.UC_X86_REG_RIP) == self.stop, "Instruction budget exhausted"
        assert self.u.reg_read(X.UC_X86_REG_RSP) == self.stack + 8, "Win64 stack imbalance"
        for register, value in original.items():
            assert self.u.reg_read(register) == value, ("Nonvolatile register modified", register)
        return self.u.reg_read(X.UC_X86_REG_RAX)


def measure_case(image, symbols, dpi, dpi_function=True, null=False):
    m = Machine(image, dpi, dpi_function)
    before_self = bytes(m.u.mem_read(m.self_ptr, 0x300))
    initial = bytearray(b"\xA5" * 32)
    struct.pack_into("<I", initial, 0, 102)  # ODT_LISTVIEW
    m.u.mem_write(m.measure, bytes(initial))
    m.run(symbols["ui_list_measure"], m.self_ptr, 0 if null else m.measure)
    effective = dpi if dpi_function and 96 <= dpi <= 768 else 96
    expected_height = px(42, effective)
    expected_self = bytearray(before_self)
    if not null:
        struct.pack_into("<I", initial, 16, expected_height)
        struct.pack_into("<I", expected_self, 0x240, expected_height)
    assert bytes(m.u.mem_read(m.measure, 32)) == initial, "MEASUREITEMSTRUCT corruption"
    assert bytes(m.u.mem_read(m.self_ptr, 0x300)) == expected_self, "List object field corruption"
    names = [call["api"] for call in m.calls]
    expected_names = [] if null else ["GetModuleHandleW", "GetProcAddress"] + (["GetDpiForWindow"] if dpi_function else [])
    assert names == expected_names
    return {"group": "list_measure", "dpi": dpi, "dpi_function_available": dpi_function,
            "null_measure": null, "effective_dpi": effective,
            "item_height": None if null else expected_height, "passed": True}


def menu_case(image, symbols, dpi, draw, base_size, null_parent=False, dpi_function=True):
    m = Machine(image, dpi, dpi_function)
    if null_parent:
        m.write64(m.control + 0x128, 0)
    return_pointer = m.out + 0x80
    original_calls = []

    def original():
        forwarded = [m.arg(i) for i in range(5)]
        forwarded[4] &= 0xFFFFFFFF  # BOOL is 32-bit; unused stack-slot high half is unspecified.
        expected_arguments = [m.paint, m.out, m.dc, m.control, int(draw)]
        assert forwarded == expected_arguments, ("Original argument forwarding", [hex(x) for x in forwarded], [hex(x) for x in expected_arguments])
        m.u.mem_write(m.out, struct.pack("<ii", *base_size))
        original_calls.append(True)
        return return_pointer

    original_stub = m.stub("original_virtual_A0", original)
    m.write64(m.vtable + 0xA0, original_stub)
    m.u.mem_write(m.out - 16, b"\x5A" * 160)
    result = m.run(symbols["ui_menu_measure"], m.paint, m.out, m.dc, m.control, int(draw))
    assert result == return_pointer, "Original CSize return pointer changed"
    assert len(original_calls) == 1
    effective = dpi if dpi_function and 96 <= dpi <= 768 else 96
    expected = base_size if draw else (base_size[0] + px(20, effective), max(base_size[1] + px(10, effective), px(36, effective)))
    actual = struct.unpack("<ii", m.u.mem_read(m.out, 8))
    assert actual == expected, (dpi, draw, actual, expected)
    assert bytes(m.u.mem_read(m.out - 16, 16)) == b"\x5A" * 16
    assert bytes(m.u.mem_read(m.out + 8, 136)) == b"\x5A" * 136
    names = [call["api"] for call in m.calls]
    expected_names = ["original_virtual_A0"]
    if not draw:
        expected_names += ["GetModuleHandleW", "GetProcAddress"] + (["GetDpiForWindow"] if dpi_function else [])
    assert names == expected_names, (names, expected_names)
    return {"group": "menu_measure", "dpi": dpi, "draw": draw,
            "original_size": list(base_size), "result_size": list(actual),
            "null_parent": null_parent, "dpi_function_available": dpi_function,
            "original_virtual_slot": "0xA0", "original_return_pointer_preserved": True, "passed": True}


def interaction_case(image, symbols, dpi, msg, region, row=3):
    m = Machine(image, dpi)
    callback = []
    messages = []
    default_calls = []
    client_width = px(800, dpi)
    row_height = px(42, dpi)
    if region == "former_button":
        x, y = client_width - px(40, dpi), row * row_height + row_height // 2
    elif region == "body":
        x, y = px(160, dpi), row * row_height + row_height // 2
    elif region == "right_gutter":
        x, y = client_width - px(6, dpi), row * row_height + row_height // 2
    elif region == "empty":
        x, y = client_width - px(40, dpi), 30 * row_height + row_height // 2
    else:
        raise AssertionError(region)
    wparam = 0x0005 if msg in (0x201, 0x203) else 0x0004
    packed = (x & 0xFFFF) | ((y & 0xFFFF) << 16)

    def send():
        hwnd, message, w, l = [m.arg(i) for i in range(4)]
        assert hwnd == m.hwnd
        messages.append(message)
        assert message in (0x1001, 0x1026), ("Removed button still intercepts list messages", hex(message), w, l)
        return 0

    def add_subclass():
        assert m.arg(0) == m.hwnd and m.arg(2) == 0x4D415455 and m.arg(3) == m.self_ptr
        callback.append(m.arg(1))
        return 1

    def original_pre():
        assert m.arg(0) == m.self_ptr
        return 0

    def default_subclass():
        actual = [m.arg(i) for i in range(4)]
        assert actual == [m.hwnd, msg, wparam, packed], ("Original mouse arguments changed", actual)
        default_calls.append(True)
        return 0x12345678

    m.iat(0x140256F28, "SendMessageW", send)
    m.iat(0x1402560A8, "SetWindowSubclass", add_subclass)
    m.iat(0x1402560B8, "DefSubclassProc", default_subclass)
    m.intercept(0x14001A4C4, "original_pre_subclass", original_pre)
    m.intercept(0x14000C49C, "original_is_dark", lambda: 0)
    m.run(symbols["ui_pre_subclass"], m.self_ptr)
    assert len(callback) == 1, "List subclass was not installed"
    messages.clear()
    m.calls.clear()
    result = m.run(callback[0], m.hwnd, msg, wparam, packed, 0x4D415455, m.self_ptr)
    assert result == 0x12345678 and len(default_calls) == 1
    assert not messages, "Click generated synthetic selection or context-menu messages"
    assert [call["api"] for call in m.calls] == ["DefSubclassProc"]
    return {"group": "list_interaction", "dpi": dpi, "message": hex(msg), "region": region,
            "original_arguments_preserved": True, "original_return_value_preserved": True,
            "synthetic_selection_or_context_messages": False, "passed": True}


def menu_background_case(image, symbols, scenario, dark):
    m = Machine(image, 144)
    cdc = m.scratch + 0x10000
    m.write64(cdc + 8, m.dc)
    m.write64(m.control + 0x40, m.hwnd)
    menubar = scenario == "menubar_type0"
    popup = scenario == "popup_type2"
    m.write64(m.control, 0x14028C560 if menubar else m.scratch + 0x11000)
    bar_type = 2 if popup else 1 if scenario == "other_type1" else 0
    m.u.mem_write(m.control + 0x180, struct.pack("<I", bar_type))
    rectangle = (0, 0, px(760, m.dpi), px(36 if menubar else 260, m.dpi))
    delegated = []
    fills = []
    rounds = []
    brushes = {}

    def original():
        arguments = [m.arg(i) for i in range(3)]
        assert arguments == [m.paint, cdc, m.control]
        delegated.append(arguments)
        return 0

    def client():
        assert m.arg(0) == m.hwnd
        m.u.mem_write(m.arg(1), struct.pack("<iiii", *rectangle))
        return 1

    def brush():
        handle = 0xB000 + len(brushes)
        brushes[handle] = m.arg(0) & 0xFFFFFFFF
        return handle

    def fill():
        assert m.arg(0) == m.dc
        fills.append((struct.unpack("<iiii", m.u.mem_read(m.arg(1), 16)), brushes[m.arg(2)]))
        return 1

    def rounded():
        assert m.arg(0) == m.dc
        rounds.append(tuple(m.arg(i) & 0xFFFFFFFF for i in range(1, 7)))
        return 1

    def save():
        assert m.arg(0) == m.dc
        return 1

    def restore():
        assert m.arg(0) == m.dc and m.arg(1) & 0xFFFFFFFF == 1
        return 1

    m.write64(m.vtable + 0xD8, m.stub("original_menu_background_D8", original))
    m.iat(0x140257200, "GetClientRect", client)
    m.iat(0x1402561E0, "SaveDC", save)
    m.iat(0x1402561D0, "RestoreDC", restore)
    m.iat(0x1402563D0, "CreateSolidBrush", brush)
    m.iat(0x140257268, "FillRect", fill)
    m.iat(0x140256378, "CreatePen", lambda: 0xC001)
    m.iat(0x1402563A8, "SelectObject", lambda: 0xC002)
    m.iat(0x1402563C8, "RoundRect", rounded)
    m.iat(0x140256380, "DeleteObject", lambda: 1)
    m.intercept(0x14000C49C, "original_is_dark", lambda: int(dark))
    m.run(symbols["ui_menu_background"], m.paint, cdc, m.control)
    if menubar or popup:
        rgb = (20, 18, 24) if dark and menubar else (246, 244, 249) if menubar else (33, 31, 38) if dark else (255, 251, 254)
        color = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16)
        assert not delegated and fills == [(rectangle, color)], ("Menu background incorrect", scenario, fills)
        expected_rounds = [rectangle + (px(36, m.dpi), px(36, m.dpi))] if popup else []
        assert rounds == expected_rounds, ("Only popup background should draw a rounded outline", scenario, rounds)
        assert m.calls[-1]["api"] == "RestoreDC"
    else:
        assert len(delegated) == 1 and not fills and not rounds
        assert [call["api"] for call in m.calls] == ["original_menu_background_D8"]
    return {"group": "menu_background", "scenario": scenario, "dark": dark, "dpi": m.dpi,
            "bar_type": bar_type, "native_MenuBar_vtable_identifies_top_bar": menubar,
            "unified_caption_list_background": menubar, "popup_rounded_background": popup,
            "unrelated_native_bar_delegated": not (menubar or popup), "passed": True}


def columns_case(image, symbols, dpi, scenario):
    m = Machine(image, dpi)
    header = 0x76543220
    row = (0, px(84, dpi), px(800, dpi), px(126, dpi))
    row_pointer = m.scratch + 0x10000
    output = m.scratch + 0x11000
    m.u.mem_write(row_pointer, struct.pack("<iiii", *row))
    m.u.mem_write(output - 16, b"\xA5" * 100)
    translate = (0, px(9, dpi))
    if scenario == "reordered":
        logical = [2, 0, 1]
        geometry = [(560, 800), (0, 420), (420, 560)]
        expected = [(0, 420), (420, 560), (560, 800)]
        visible = 7
    elif scenario == "hidden_size":
        logical = [0, 2]
        geometry = [(0, 600), (600, 800)]
        expected = [(0, 600), (0, 800), (600, 800)]
        visible = 5
    elif scenario == "horizontal_scroll":
        logical = [0, 1, 2]
        geometry = [(0, 420), (420, 560), (560, 800)]
        translate = (-px(120, dpi), px(9, dpi))
        expected = [(-120, 300), (300, 440), (440, 680)]
        visible = 7
    elif scenario == "missing_header":
        logical = []
        geometry = []
        expected = [(0, 576), (576, 668), (668, 800)]
        visible = 7
    else:
        raise AssertionError(scenario)
    expected_rects = [(px(left, dpi), row[1], px(right, dpi), row[3]) for left, right in expected]
    queried = []

    def send():
        hwnd, message, w, l = [m.arg(i) for i in range(4)]
        if hwnd == m.hwnd and message == 0x101F:
            return 0 if scenario == "missing_header" else header
        if hwnd == header and message == 0x1200:
            return len(logical)
        assert hwnd == header and message == 0x1207 and 0 <= w < len(geometry)
        queried.append(w)
        left, right = geometry[w]
        m.u.mem_write(l, struct.pack("<iiii", px(left, dpi), 0, px(right, dpi), px(28, dpi)))
        return 1

    def original_logical():
        assert m.arg(0) == m.self_ptr
        index = m.arg(1) & 0xFFFFFFFF
        assert index < len(logical)
        return logical[index]

    def map_points():
        assert [m.arg(i) for i in (0, 1, 3)] == [header, m.hwnd, 2]
        pointer = m.arg(2)
        left, top, right, bottom = struct.unpack("<iiii", m.u.mem_read(pointer, 16))
        dx, dy = translate
        m.u.mem_write(pointer, struct.pack("<iiii", left + dx, top + dy, right + dx, bottom + dy))
        return (dx & 0xFFFF) | ((dy & 0xFFFF) << 16)

    m.iat(0x140256F28, "SendMessageW", send)
    m.iat(0x140257118, "MapWindowPoints", map_points)
    m.intercept(0x140019158, "native_logical_column", original_logical)
    m.run(symbols["ui_card_columns"], m.self_ptr, m.hwnd, row_pointer, output)
    actual_rects = [struct.unpack("<iiii", m.u.mem_read(output + 16 * index, 16)) for index in range(3)]
    assert actual_rects == expected_rects, (scenario, dpi, actual_rects, expected_rects)
    assert m.read32(output + 48) == visible
    assert queried == list(range(len(geometry)))
    assert bytes(m.u.mem_read(row_pointer, 16)) == struct.pack("<iiii", *row)
    assert bytes(m.u.mem_read(output - 16, 16)) == b"\xA5" * 16
    assert bytes(m.u.mem_read(output + 52, 32)) == b"\xA5" * 32
    return {"group": "native_columns", "dpi": dpi, "scenario": scenario,
            "logical_rectangles": [list(rect) for rect in actual_rects],
            "visible_mask": visible, "row_vertical_bounds_preserved": True, "passed": True}


def draw_layout_case(image, symbols, dpi, name_width=420, scenario="normal"):
    """Check rendered cells against live native header geometry, not constants."""
    m = Machine(image, dpi)
    header = 0x76543220
    row_id = 2
    row_top = px(42, dpi) * row_id
    row_bottom = row_top + px(42, dpi)
    width = px(800, dpi)
    client_height = px(420, dpi)
    if scenario == "negative_top":
        row_top = -px(12, dpi)
        row_bottom = row_top + px(42, dpi)
    elif scenario == "partial_bottom":
        row_top = client_height - px(18, dpi)
        row_bottom = row_top + px(42, dpi)
    full_row = (0, row_top, width, row_bottom)
    target_clip = (0, 0, width, client_height)
    if scenario == "partial_clip":
        target_clip = (px(6, dpi), row_top + px(8, dpi), width - px(60, dpi), row_bottom - px(7, dpi))
    buffer_dc, buffer_handle = 0x22556677, 0x33557799
    begin_calls, alpha_calls, end_calls = [], [], []
    raster_writes, committed_rectangles = [], []
    allocated_objects, deleted_objects = set(), set()
    object_kinds = {0xE001: "brush", 0xE002: "pen", 0xE003: "font"}
    initial_selection = {kind: handle for handle, kind in object_kinds.items()}
    selected_objects = {dc: dict(initial_selection) for dc in (m.dc, buffer_dc)}
    saved_dcs = {}
    next_object = 0xB000
    missing_api = scenario.removeprefix("missing_") if scenario.startswith("missing_") else None
    pre_draw_failure = scenario in ("begin_null_handle", "begin_null_dc", "missing_begin", "missing_alpha", "missing_end")
    post_draw_failure = scenario in ("alpha_failure", "end_failure")
    atomic_success = not pre_draw_failure and not post_draw_failure
    boundaries = [0, px(name_width, dpi), px(name_width + 140, dpi), width]
    columns = [(boundaries[i], boundaries[i + 1]) for i in range(3)]
    item = m.scratch + 0x10000
    vector = m.scratch + 0x11000
    model = m.scratch + 0x12000
    model_vtable = 0x140261BC8
    manager = m.scratch + 0x14000
    manager_vtable = m.scratch + 0x15000
    text_base = m.scratch + 0x18000
    text_values = ["Example application", "123.4 MB", "2026/11/20"]
    metadata = "1.2.3   |   Example Publisher"
    returned_strings = {}
    freed_strings = []
    getter_columns = []
    text_draws = []
    icon_draws = []
    rounded_rects = []
    header_queries = []
    fonts = []
    draw_data = bytearray(64)
    struct.pack_into("<IIIII", draw_data, 0, 102, 1001, row_id, 1, 1)
    struct.pack_into("<QQiiiiQ", draw_data, 24, m.hwnd, m.dc, px(7, dpi), row_top,
                     width - px(9, dpi), row_bottom, 0x9988776655443322)
    m.u.mem_write(item - 16, b"\xA5" * 96)
    m.u.mem_write(item, bytes(draw_data))
    m.write64(m.self_ptr + 0x218, vector)
    m.write64(m.self_ptr + 0x220, vector + 3 * 8)
    m.write64(vector + row_id * 8, model)
    m.write64(model, model_vtable)
    m.write64(manager, manager_vtable)
    version = m.scratch + 0x22000
    publisher = m.scratch + 0x23000
    m.u.mem_write(version, "1.2.3\0".encode("utf-16le"))
    m.u.mem_write(publisher, "Example Publisher\0".encode("utf-16le"))
    m.write64(model + 0x80, version)
    m.write64(model + 0x88, publisher)

    def client():
        assert m.arg(0) == m.hwnd
        m.u.mem_write(m.arg(1), struct.pack("<iiii", 0, 0, width, client_height))
        return 1

    def signed32(value):
        value &= 0xFFFFFFFF
        return value - (1 << 32) if value & 0x80000000 else value

    def raster(name, rectangle=None):
        dc = m.arg(0)
        assert dc in (m.dc, buffer_dc), (name, "Unexpected drawing DC", hex(dc))
        if atomic_success:
            assert dc == buffer_dc, f"{name} wrote naked RGB into the live target DC before opaque row commit"
        elif dc == buffer_dc:
            assert begin_calls and scenario not in ("begin_null_handle", "begin_null_dc")
            assert not end_calls, "Drawing into an already-ended row buffer"
        else:
            if post_draw_failure:
                assert len(end_calls) == 1, "Fallback drew to target before the failed buffer was ended"
            assert not alpha_calls or post_draw_failure
        raster_writes.append((name, dc, rectangle))

    def begin_buffer():
        assert m.arg(0) == m.dc and m.arg(2) == 2 and m.arg(3) == 0, "Row buffer must use target clipping and 32-bit top-down pixels"
        rectangle = struct.unpack("<iiii", m.u.mem_read(m.arg(1), 16))
        assert rectangle == full_row, "Buffer omitted the full-width left/right gutter or changed row vertical bounds"
        assert not raster_writes and not getter_columns, "Live row content was drawn before buffer acquisition"
        begin_calls.append(rectangle)
        m.write64(m.arg(4), 0 if scenario == "begin_null_dc" else buffer_dc)
        return 0 if scenario == "begin_null_handle" else buffer_handle

    def alpha_buffer():
        assert [m.arg(0), m.arg(1), m.arg(2) & 0xFF] == [buffer_handle, 0, 255]
        assert len(begin_calls) == 1 and not end_calls and not alpha_calls
        assert getter_columns == [0, 1, 2] and len(text_draws) == 4 and len(icon_draws) == 1
        assert len(freed_strings) == 3 and not saved_dcs and allocated_objects == deleted_objects, "Alpha was applied before complete row drawing and cleanup"
        assert all(dc == buffer_dc for name, dc, rect in raster_writes), "Target DC was exposed before alpha correction"
        alpha_calls.append(255)
        return 0x80004005 if scenario == "alpha_failure" else 0

    def end_buffer():
        assert m.arg(0) == buffer_handle and not end_calls, "Row buffer ended twice or wrong handle released"
        commit = m.arg(1) & 0xFFFFFFFF
        assert commit == (0 if scenario in ("begin_null_dc", "alpha_failure") else 1)
        if commit:
            assert alpha_calls == [255] and allocated_objects == deleted_objects
            assert all(dc == buffer_dc for name, dc, rect in raster_writes)
            if scenario != "end_failure":
                clipped = (max(full_row[0], target_clip[0]), max(full_row[1], target_clip[1]),
                           min(full_row[2], target_clip[2]), min(full_row[3], target_clip[3]))
                committed_rectangles.append(clipped)
        end_calls.append(commit)
        return 0x80004005 if scenario == "end_failure" else 0

    def cursor():
        m.u.mem_write(m.arg(0), struct.pack("<ii", -1000, -1000))
        return 1

    def to_screen():
        assert m.arg(0) == m.hwnd
        return 1

    def send():
        hwnd, message, w, l = [m.arg(i) for i in range(4)]
        if hwnd == m.hwnd and message == 0x101F:  # LVM_GETHEADER
            return header
        if hwnd == header and message == 0x1200:  # HDM_GETITEMCOUNT
            return 3
        if hwnd == m.hwnd and message == 0x101D:  # LVM_GETCOLUMNWIDTH
            assert 0 <= w < 3
            header_queries.append(("width", w))
            return columns[w][1] - columns[w][0]
        if hwnd == header and message == 0x1207:  # HDM_GETITEMRECT
            assert 0 <= w < 3
            header_queries.append(("rectangle", w))
            m.u.mem_write(l, struct.pack("<iiii", columns[w][0], 0, columns[w][1], px(28, dpi)))
            return 1
        raise AssertionError(("Unexpected draw SendMessage", hex(hwnd), hex(message), w, l))

    def logical_column():
        assert m.arg(0) == m.self_ptr and m.arg(1) in (0, 1, 2)
        return m.arg(1)

    def map_points():
        assert [m.arg(i) for i in (0, 1, 3)] == [header, m.hwnd, 2]
        return 0

    def get_text():
        assert m.arg(0) == model
        column = m.arg(2) & 0xFFFFFFFF
        assert column in (0, 1, 2)
        getter_columns.append(column)
        data = text_base + column * 0x1000
        value = text_values[column]
        encoded = value.encode("utf-16le") + b"\0\0"
        m.u.mem_write(data, struct.pack("<QiiiI", manager, len(value), len(value), 1, 0))
        m.u.mem_write(data + 0x18, encoded)
        m.write64(m.arg(1), data + 0x18)
        returned_strings[column] = data
        return m.arg(1)

    def free_text():
        assert m.arg(0) == manager
        assert m.arg(1) in returned_strings.values()
        assert m.read32(m.arg(1) + 0x10, signed=True) == 0
        freed_strings.append(m.arg(1))
        return 0

    def get_icon():
        assert m.arg(0) == model
        assert m.arg(1) & 0xFFFFFFFF == 1
        return 0xF00D

    def draw_text():
        assert m.arg(2) & 0xFFFFFFFF == 0xFFFFFFFF
        value = m.string(m.arg(1), wide=True)
        rect = struct.unpack("<iiii", m.u.mem_read(m.arg(3), 16))
        flags = m.arg(4) & 0xFFFFFFFF
        raster("DrawTextW", rect)
        text_draws.append((value, rect, flags))
        return rect[3] - rect[1]

    def draw_icon():
        assert m.arg(3) == 0xF00D
        rectangle = tuple(signed32(m.arg(i)) for i in (1, 2, 4, 5))
        raster("DrawIconEx", rectangle)
        icon_draws.append(rectangle)
        return 1

    def round_rect():
        rectangle = tuple(signed32(m.arg(i)) for i in range(1, 7))
        raster("RoundRect", rectangle)
        rounded_rects.append(rectangle)
        return 1

    def fill_rect():
        rectangle = struct.unpack("<iiii", m.u.mem_read(m.arg(1), 16))
        assert rectangle == full_row
        raster("FillRect", rectangle)
        return 1

    def allocate_object(kind):
        nonlocal next_object
        handle = next_object
        next_object += 1
        allocated_objects.add(handle)
        object_kinds[handle] = kind
        return handle

    def delete_object():
        handle = m.arg(0)
        assert handle in allocated_objects and handle not in deleted_objects, "Unknown or duplicate GDI object deletion"
        assert all(handle not in selection.values() for selection in selected_objects.values()), "Deleted a GDI object while still selected into a DC"
        assert all(handle not in selection.values() for _, selection in saved_dcs.values()), "Deleted a GDI object still referenced by a saved DC state"
        deleted_objects.add(handle)
        return 1

    def select_object():
        dc, handle = m.arg(0), m.arg(1)
        assert dc in selected_objects and handle in object_kinds and handle not in deleted_objects
        kind = object_kinds[handle]
        previous = selected_objects[dc][kind]
        selected_objects[dc][kind] = handle
        return previous

    def save_dc():
        dc = m.arg(0)
        assert dc in (m.dc, buffer_dc) and dc not in saved_dcs
        saved_dcs[dc] = (1, dict(selected_objects[dc]))
        return 1

    def restore_dc():
        dc, saved = m.arg(0), m.arg(1)
        saved_id, selection = saved_dcs.pop(dc)
        assert saved_id == saved == 1
        selected_objects[dc] = selection
        return 1

    def font():
        height, weight = m.read32(m.arg(0), signed=True), m.read32(m.arg(0) + 16)
        assert m.string(m.arg(0) + 28, wide=True) == "Segoe UI"
        fonts.append((height, weight))
        return allocate_object("font")

    m.write64(model_vtable + 8, m.stub("model_get_text", get_text))
    m.write64(model_vtable + 16, m.stub("model_get_icon", get_icon))
    m.write64(manager_vtable + 8, m.stub("CString_manager_Free", free_text))
    m.iat(0x140257200, "GetClientRect", client)
    m.iat(0x140257210, "GetCursorPos", cursor)
    m.iat(0x1402570C8, "ClientToScreen", to_screen)
    m.iat(0x140256F28, "SendMessageW", send)
    m.iat(0x140257118, "MapWindowPoints", map_points)
    m.iat(0x1402571C0, "DrawTextW", draw_text)
    m.iat(0x1402572D8, "DrawIconEx", draw_icon)
    m.iat(0x1402563C8, "RoundRect", round_rect)
    m.iat(0x140256358, "CreateFontIndirectW", font)
    m.iat(0x1402561E0, "SaveDC", save_dc)
    m.iat(0x1402561D0, "RestoreDC", restore_dc)
    m.iat(0x1402563D0, "CreateSolidBrush", lambda: allocate_object("brush"))
    m.iat(0x140256378, "CreatePen", lambda: allocate_object("pen"))
    m.iat(0x1402563A8, "SelectObject", select_object)
    m.iat(0x140256380, "DeleteObject", delete_object)
    m.iat(0x140257268, "FillRect", fill_rect)
    m.iat(0x140256390, "SetTextColor", lambda: 0)
    m.iat(0x140256398, "SetBkMode", lambda: 1)
    for slot, name, callback in ((0x140257488, "begin", begin_buffer),
                                 (0x140257498, "alpha", alpha_buffer),
                                 (0x140257490, "end", end_buffer)):
        m.iat(slot, {"begin": "BeginBufferedPaint", "alpha": "BufferedPaintSetAlpha", "end": "EndBufferedPaint"}[name], callback)
        if missing_api == name:
            m.write64(slot, 0)
    m.intercept(0x14000C49C, "original_is_dark", lambda: int(scenario == "dark"))
    m.intercept(0x140019158, "native_logical_column", logical_column)
    m.run(symbols["ui_draw_card"], m.self_ptr, item)
    draw_count = 2 if post_draw_failure else 1
    assert getter_columns == [0, 1, 2] * draw_count, ("Wrong native model columns", getter_columns)
    assert sorted(freed_strings) == sorted(list(returned_strings.values()) * draw_count), "Temporary CString ownership mismatch"
    assert len(text_draws) == 4 * draw_count, ("Unexpected cell or button text", text_draws)
    drawn = {value: (rect, flags) for value, rect, flags in text_draws}
    assert set(drawn) == set(text_values + [metadata]), ("Name/metadata/size/install-time cells missing", drawn)
    for column, value in enumerate(text_values):
        rect, flags = drawn[value]
        left, right = columns[column]
        assert left <= rect[0] < rect[2] <= right, ("Text crosses its live header column", dpi, name_width, column, rect, columns[column])
        assert row_top <= rect[1] < rect[3] <= row_bottom, ("Text leaves compact row", rect, row_top, row_bottom)
        assert flags & 0x20, ("Text is not a single line", flags)
        if column:
            assert flags & 0x4, ("Size/date are not vertically centered", flags)
    detail_rect, detail_flags = drawn[metadata]
    assert columns[0][0] <= detail_rect[0] < detail_rect[2] <= columns[0][1], "Metadata crosses name column"
    assert row_top <= detail_rect[1] < detail_rect[3] <= row_bottom, "Metadata leaves compact row"
    assert detail_flags & 0x20, "Metadata is not single line"
    name_rect = drawn[text_values[0]][0]
    assert name_rect[3] <= detail_rect[1], "Name and metadata overlap vertically"
    assert header_queries, "Draw code ignored native header geometry"
    assert len(icon_draws) == draw_count
    ix, iy, iw, ih = icon_draws[0]
    assert 0 <= ix < ix + iw <= columns[0][1] and row_top <= iy < iy + ih <= row_bottom, "Icon leaves compact name cell"
    assert iw == ih and iw <= px(24, dpi), ("Icon too large for compact row", icon_draws)
    assert rounded_rects and all(row_top <= r[1] < r[3] <= row_bottom for r in rounded_rects), "Card leaves row bounds"
    assert bytes(m.u.mem_read(item, 64)) == draw_data, "DRAWITEMSTRUCT modified"
    assert bytes(m.u.mem_read(item - 16, 16)) == bytes(m.u.mem_read(item + 64, 16)) == b"\xA5" * 16
    assert allocated_objects == deleted_objects and not saved_dcs, "Row left a live GDI object or saved DC"
    assert all(selection == initial_selection for selection in selected_objects.values()), "Row changed caller or buffered DC object selection"
    expected_begin = [] if missing_api else [full_row]
    assert begin_calls == expected_begin
    assert alpha_calls == ([255] if atomic_success or post_draw_failure else [])
    assert end_calls == ([1] if atomic_success or scenario == "end_failure" else [0] if scenario in ("begin_null_dc", "alpha_failure") else [])
    target_writes = [name for name, dc, rect in raster_writes if dc == m.dc]
    assert bool(target_writes) == (not atomic_success)
    if atomic_success:
        assert len(committed_rectangles) == 1 and all(dc == buffer_dc for name, dc, rect in raster_writes)
        committed = committed_rectangles[0]
        assert committed[0] == target_clip[0] and committed[2] == target_clip[2]
        assert committed[1] >= max(0, row_top) and committed[3] <= min(client_height, row_bottom)
        assert committed[0] < px(12, dpi), "Opaque row commit omitted the exposed left gutter"
    else:
        assert not committed_rectangles
    return {"group": "draw_layout" if scenario == "normal" else "atomic_row_buffer", "dpi": dpi,
            "scenario": scenario, "name_column_width_96dpi": name_width,
            "native_columns": columns, "text_rectangles": {value: list(rect) for value, (rect, flags) in drawn.items()},
            "single_line_size_and_date_vertical_center": True, "icon_rectangle": list(icon_draws[0]),
            "native_model_text_and_CString_release": True, "header_queries": header_queries,
            "date_in_own_column": True, "name_metadata_do_not_overlap": True,
            "ellipsis_button_drawn": False, "whole_row_buffer_includes_gutters": begin_calls == [full_row],
            "no_naked_live_DC_raster_write_on_success": atomic_success,
            "target_clip": list(target_clip), "committed_rectangles": [list(rect) for rect in committed_rectangles],
            "clip_check_scope": "BeginBufferedPaint params=NULL; commit stub models target-clip intersection, not live GDI pixels",
            "alpha_after_complete_content_then_single_buffer_end": True,
            "fallback_target_writes": target_writes, "all_CStrings_GDI_objects_and_DC_states_balanced": True,
            "original_DRAWITEMSTRUCT_and_guards_preserved": True, "passed": True}


def draw_guard_case(image, symbols, null=False):
    """A missing/invalid row must return before reading self or calling an API."""
    m = Machine(image)
    item = m.scratch + 0x10000
    data = bytearray(b"\xA5" * 96)
    struct.pack_into("<I", data, 16 + 8, 0xFFFFFFFF)
    m.u.mem_write(item - 16, bytes(data))
    # A null self is deliberate: the empty-row guard must run first.
    m.run(symbols["ui_draw_card"], 0, 0 if null else item)
    assert not m.calls, "Empty row unexpectedly called a window or drawing API"
    assert bytes(m.u.mem_read(item - 16, 96)) == data, "Empty row guard mutated caller memory"
    return {"group": "atomic_row_guard", "null_item": null,
            "invalid_item_id": not null, "no_API_calls_or_self_access": True,
            "original_DRAWITEMSTRUCT_and_guards_preserved": True, "passed": True}


def list_hover_case(image, symbols, dpi, scenario):
    """Run the installed native callback through repeated and changing input."""
    m = Machine(image, dpi)
    callback, invalidations, registrations, defaults = [], [], [], []
    width, height, row_height = px(800, dpi), px(210, dpi), px(42, dpi)
    origin = (px(137, dpi), -px(63, dpi))
    view = {"top": 2, "first": 0, "count": 100, "x": px(100, dpi),
            "y": row_height // 2, "failure": None, "track_result": 1}
    active = {"default": False, "arguments": None, "mutate": None, "hit": None}
    cache = []

    def cache_write(u, access, address, size, value, user):
        if size >= 8 and value == m.hwnd and m.base <= address < m.base + m.pe.OPTIONAL_HEADER.SizeOfImage:
            if address not in cache:
                cache.append(address)

    def rect(index):
        top = view["first"] + (index - view["top"]) * row_height
        return (px(9, dpi), top, width - px(9, dpi), top + row_height)

    def dirty(index):
        left, top, right, bottom = rect(index)
        return (0, max(0, top), width, min(height, bottom))

    def before_geometry():
        assert active["default"], "Hover geometry was read before DefSubclassProc"

    def client():
        before_geometry()
        assert m.arg(0) == m.hwnd, "Hover queried client geometry from the wrong HWND"
        m.u.mem_write(m.arg(1), struct.pack("<iiii", 0, 0, width, height))
        return view["failure"] != "client"

    def cursor():
        before_geometry()
        m.u.mem_write(m.arg(0), struct.pack("<ii", origin[0] + view["x"], origin[1] + view["y"]))
        return view["failure"] != "cursor"

    def to_screen():
        before_geometry()
        assert m.arg(0) == m.hwnd
        assert struct.unpack("<ii", m.u.mem_read(m.arg(1), 8)) == (0, 0), "Hover origin was not initialized"
        m.u.mem_write(m.arg(1), struct.pack("<ii", *origin))
        return view["failure"] != "screen"

    def send():
        h, msg, w, l = [m.arg(i) for i in range(4)]
        assert h == m.hwnd
        if msg in (0x1001, 0x1026):  # Existing initialization only.
            assert not callback or active["arguments"] is None
            return 0
        before_geometry()
        if msg == 0x1004:
            return 0 if view["failure"] == "empty" else view["count"]
        if msg == 0x1027:
            return view["count"] if view["failure"] == "top" else view["top"]
        assert msg == 0x100E and 0 <= w < view["count"], ("Unexpected hover message", hex(msg), w)
        assert m.read32(l) == 0, "Native GETITEMRECT did not request LVIR_BOUNDS"
        if view["failure"] == "rect":
            return 0
        rectangle = list(rect(w))
        if view["failure"] == "height":
            rectangle[3] = rectangle[1]
        elif view["failure"] == "candidate" and w != view["top"]:
            rectangle[1] += row_height
            rectangle[3] += row_height
        m.u.mem_write(l, struct.pack("<iiii", *rectangle))
        return 1

    def default():
        assert not active["default"], "Native message was forwarded more than once"
        assert [m.arg(i) for i in range(4)] == active["arguments"], "Native message arguments changed"
        active["default"] = True
        defaults.append(active["arguments"])
        if active["mutate"]:
            active["mutate"]()
        return 0x12345678ABC00000 + len(defaults)

    def invalidate():
        before_geometry()
        assert m.arg(0) == m.hwnd and m.arg(2) & 0xFFFFFFFF == 0, "Hover invalidation enabled background erase"
        rectangle = None if not m.arg(1) else struct.unpack("<iiii", m.u.mem_read(m.arg(1), 16))
        invalidations.append(rectangle)
        assert cache, "The native hover cache was not initialized before invalidation"
        pointer = cache[-1]
        assert m.read64(pointer) == m.hwnd
        assert bool(m.read32(pointer + 12)) == (active["hit"] is not None), "Hover state was not cached before invalidation"
        if active["hit"] is not None:
            assert m.read32(pointer + 8, signed=True) == active["hit"], "Invalidated before caching the new row"
        return 1

    def track():
        before_geometry()
        p = m.arg(0)
        assert (m.read32(p), m.read32(p + 4), m.read64(p + 8), m.read32(p + 16)) == (24, 2, m.hwnd, 0)
        registrations.append(True)
        return view["track_result"]

    track_stub = m.stub("TrackMouseEvent", track)

    def proc():
        assert m.arg(0) == m.module
        name = m.string(m.arg(1))
        assert name in ("GetDpiForWindow", "TrackMouseEvent")
        return m.dpi_stub if name == "GetDpiForWindow" else track_stub

    def install():
        assert [m.arg(i) for i in (0, 2, 3)] == [m.hwnd, 0x4D415455, m.self_ptr]
        callback.append(m.arg(1))
        return 1

    m.iat(0x140257200, "GetClientRect", client)
    m.iat(0x140257210, "GetCursorPos", cursor)
    m.iat(0x1402570C8, "ClientToScreen", to_screen)
    m.iat(0x140256F28, "SendMessageW", send)
    m.iat(0x140256A78, "GetProcAddress", proc)
    m.iat(0x1402572A0, "InvalidateRect", invalidate)
    m.iat(0x1402560A8, "SetWindowSubclass", install)
    m.iat(0x1402560B8, "DefSubclassProc", default)
    m.intercept(0x14001A4C4, "original_pre_subclass", lambda: 0)
    m.intercept(0x14000C49C, "original_is_dark", lambda: 0)
    m.run(symbols["ui_pre_subclass"], m.self_ptr)
    assert len(callback) == 1
    m.u.hook_add(unicorn.UC_HOOK_MEM_WRITE, cache_write)

    def event(msg=0x200, hit=None, expected=(), mutate=None):
        before = len(invalidations)
        sequence = len(defaults) + 1
        arguments = [m.hwnd, msg, 0x1234000000000000 + sequence, 0x4567000000000000 + sequence]
        active.update(default=False, arguments=arguments, mutate=mutate, hit=hit)
        value = m.run(callback[0], *arguments, 0x4D415455, m.self_ptr)
        assert active["default"] and value == 0x12345678ABC00000 + len(defaults), "Native callback return changed"
        assert len(defaults) == sequence, "Native callback was not forwarded exactly once"
        assert invalidations[before:] == list(expected), (scenario, hex(msg), invalidations[before:], expected)

    top = view["top"]
    if scenario == "same_row":
        event(hit=top, expected=[dirty(top)])
        for repeat in range(64):
            view["x"], view["y"] = px(80 + repeat % 30, dpi), px(8 + repeat % 20, dpi)
            event(hit=top)
        assert len(registrations) == 1
    elif scenario == "cross_row_gutters":
        event(hit=top, expected=[dirty(top)])
        view["y"] += row_height
        event(hit=top + 1, expected=[dirty(top), dirty(top + 1)])
        view["x"] = px(3, dpi)
        event(expected=[dirty(top + 1)])
        event()
        view["x"], view["y"] = px(12, dpi), row_height // 2
        event(hit=top, expected=[dirty(top)])
        view["x"] = width - px(12, dpi)
        event(expected=[dirty(top)])
        for x, y in ((-1, 5), (width + 1, 5), (px(100, dpi), -1), (px(100, dpi), height)):
            view["x"], view["y"] = x, y
            event()
    elif scenario == "partial_first":
        view["first"], view["y"] = -px(18, dpi), px(4, dpi)
        event(hit=top, expected=[dirty(top)])
        view["y"] = row_height - px(18, dpi) + 1
        event(hit=top + 1, expected=[dirty(top), dirty(top + 1)])
    elif scenario == "empty_space":
        view["count"], view["y"] = top + 1, row_height * 3
        event()
        view["y"] = row_height // 2
        event(hit=top, expected=[dirty(top)])
        view["y"] = row_height + 1
        event(expected=[dirty(top)])
    elif scenario == "scroll_key_paint_sync":
        view["y"] = row_height + row_height // 2
        event(hit=top + 1, expected=[dirty(top + 1)])
        for msg in (0x20A, 0x20E, 0x115, 0x114, 0x1014, 0x1013, 0x100, 0xF):
            previous = view["top"] + 1
            # The native procedure moves rows before the hover code queries them.
            def scroll():
                view["top"] += 1
            old_top = view["top"]
            view["top"] += 1
            expected = [dirty(previous), dirty(previous + 1)]
            view["top"] = old_top
            event(msg, hit=previous + 1, expected=expected, mutate=scroll)
            event(0xF, hit=previous + 1)  # A resulting paint must not repaint again.
        assert len(registrations) == 1
    elif scenario == "leave_destroy":
        event(hit=top, expected=[dirty(top)])
        event(hit=top)
        assert len(registrations) == 1
        event(0x2A3, expected=[None])
        event(hit=top, expected=[dirty(top)])
        assert len(registrations) == 2
        event(0x82)
        assert cache and bytes(m.u.mem_read(cache[-1], 24)) == b"\0" * 24, "NCDESTROY did not clear hover cache"
        event(hit=top, expected=[dirty(top)])
        assert len(registrations) == 3
    elif scenario == "tracking_retry":
        view["track_result"] = 0
        event(hit=top, expected=[dirty(top)])
        assert len(registrations) == 1
        view["track_result"] = 1
        event(hit=top)
        event(hit=top)
        assert len(registrations) == 2, "Failed tracking was not retried or successful tracking repeated"
    elif scenario == "geometry_failures":
        for failure in ("client", "cursor", "screen", "empty", "top", "rect", "height", "candidate"):
            view["failure"] = failure
            view["y"] = row_height + row_height // 2
            event()
        view["failure"] = None
        event(hit=top + 1, expected=[dirty(top + 1)])
    else:
        raise AssertionError(scenario)
    return {"group": "list_hover_performance", "scenario": scenario, "dpi": dpi,
            "native_messages_forwarded_once": len(defaults), "mouse_tracking_attempts": len(registrations),
            "invalidated_rectangles": [list(r) if r else None for r in invalidations],
            "geometry_after_native_handler": True, "native_arguments_and_return_preserved": True,
            "row_cache_observed_before_invalidation": bool(cache), "passed": True}


def list_erase_case(image, symbols, dark, scenario):
    """Execute the compiled legacy list subclass's WM_ERASEBKGND branch."""
    allowed = {"normal", "success", "missing_begin", "missing_alpha", "missing_end",
               "begin_null_handle", "begin_null_dc", "alpha_failure", "end_failure"}
    assert scenario in allowed, scenario
    m = Machine(image, 144)
    rectangle = (0, 0, px(801, m.dpi), px(423, m.dpi))
    rgb = (20, 18, 24) if dark else (246, 244, 249)
    color = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16)
    buffer_dc, buffer_handle = 0x22667788, 0x33778899
    callbacks, initial_messages, native_pre = [], [], []
    events, fills, brushes, deleted = [], [], {}, []
    begins, alphas, ends, clients, defaults = [], [], [], [], []
    missing = scenario[8:] if scenario.startswith("missing_") else None
    success = scenario in ("normal", "success")
    active_buffer = scenario not in ("missing_begin", "missing_alpha", "missing_end",
                                     "begin_null_handle", "begin_null_dc")

    def original_pre():
        assert m.arg(0) == m.self_ptr
        native_pre.append(True)
        return 0

    def add_subclass():
        assert [m.arg(i) for i in (0, 2, 3)] == [m.hwnd, 0x4D415455, m.self_ptr]
        callbacks.append(m.arg(1))
        return 1

    def send():
        hwnd, message, wparam, lparam = [m.arg(i) for i in range(4)]
        assert hwnd == m.hwnd and wparam == 0
        assert message in (0x1001, 0x1026), ("Unexpected erase or initialization message", hex(message))
        assert lparam & 0xFFFFFFFF == (color if message == 0x1001 else 0xFFFFFFFF)
        initial_messages.append(message)
        return 0

    def client():
        assert m.arg(0) == m.hwnd
        clients.append(True)
        m.u.mem_write(m.arg(1), struct.pack("<iiii", *rectangle))
        return 1

    def default():
        defaults.append([m.arg(i) for i in range(4)])
        raise AssertionError("Handled WM_ERASEBKGND reached DefSubclassProc")

    def begin():
        assert [m.arg(i) for i in (0, 2)] == [m.dc, 2]
        assert struct.unpack("<iiii", m.u.mem_read(m.arg(1), 16)) == rectangle
        params = m.arg(3)
        assert params, "Erase buffer must pass initialized BP_PAINTPARAMS"
        assert (m.read32(params), m.read32(params + 4), m.read64(params + 8),
                m.read64(params + 16)) == (24, 0, 0, 0), "Wrong erase buffer flags/exclusion/blending"
        assert not begins and not fills and not brushes
        begins.append(rectangle)
        events.append("begin")
        m.write64(m.arg(4), 0 if scenario == "begin_null_dc" else buffer_dc)
        return 0 if scenario == "begin_null_handle" else buffer_handle

    def brush():
        assert m.arg(0) & 0xFFFFFFFF == color, "Erase fallback changed the native palette"
        handle = 0xB000 + len(brushes)
        brushes[handle] = color
        return handle

    def fill():
        dc, rect, handle = [m.arg(i) for i in range(3)]
        assert dc in (m.dc, buffer_dc) and handle in brushes and handle not in deleted
        assert struct.unpack("<iiii", m.u.mem_read(rect, 16)) == rectangle
        assert brushes[handle] == color
        if dc == buffer_dc:
            assert active_buffer and begins == [rectangle] and not alphas and not ends
            events.append("fill_buffer")
        else:
            assert not success, "Successful erase exposed uncorrected RGB on the live DC"
            if active_buffer or scenario == "begin_null_dc":
                assert len(ends) == 1, "Fallback filled the target before disposing its buffer"
            events.append("fill_target")
        fills.append((dc, rectangle, color, handle))
        return 1

    def delete():
        handle = m.arg(0)
        assert handle in brushes and handle not in deleted, "Unknown or duplicate erase brush deletion"
        assert sum(fill[3] == handle for fill in fills) == 1, "Erase brush deleted before use"
        deleted.append(handle)
        return 1

    def alpha():
        assert [m.arg(0), m.arg(1), m.arg(2) & 0xFF] == [buffer_handle, 0, 255]
        assert active_buffer and len(begins) == 1 and not alphas and not ends
        assert len(fills) == 1 and fills[0][0] == buffer_dc
        assert set(brushes) == set(deleted), "Erase alpha preceded completed drawing/resource cleanup"
        alphas.append(255)
        events.append("alpha255")
        return 0x80004005 if scenario == "alpha_failure" else 0

    def end():
        assert m.arg(0) == buffer_handle and not ends, "Wrong buffer handle or double EndBufferedPaint"
        commit = m.arg(1) & 0xFFFFFFFF
        expected_commit = 0 if scenario in ("begin_null_dc", "alpha_failure") else 1
        assert commit == expected_commit
        if commit:
            assert alphas == [255] and len(fills) == 1 and fills[0][0] == buffer_dc
        elif scenario == "begin_null_dc":
            assert not fills and not alphas and not brushes
        else:
            assert scenario == "alpha_failure" and alphas == [255]
        assert set(brushes) == set(deleted)
        ends.append(commit)
        events.append("end_commit" if commit else "end_discard")
        return 0x80004005 if scenario == "end_failure" else 0

    m.intercept(0x14001A4C4, "original_pre_subclass", original_pre)
    m.intercept(0x14000C49C, "original_is_dark", lambda: int(dark))
    m.iat(0x1402560A8, "SetWindowSubclass", add_subclass)
    m.iat(0x1402560B8, "DefSubclassProc", default)
    m.iat(0x140256F28, "SendMessageW", send)
    m.iat(0x140257200, "GetClientRect", client)
    m.iat(0x1402563D0, "CreateSolidBrush", brush)
    m.iat(0x140257268, "FillRect", fill)
    m.iat(0x140256380, "DeleteObject", delete)
    for slot, name, function in ((0x140257488, "begin", begin),
                                  (0x140257498, "alpha", alpha),
                                  (0x140257490, "end", end)):
        m.iat(slot, {"begin": "BeginBufferedPaint", "alpha": "BufferedPaintSetAlpha",
                     "end": "EndBufferedPaint"}[name], function)
        if missing == name:
            m.write64(slot, 0)
    m.run(symbols["ui_pre_subclass"], m.self_ptr)
    assert native_pre == [True] and len(callbacks) == 1
    assert initial_messages == [0x1001, 0x1026]
    before_self = bytes(m.u.mem_read(m.self_ptr, 0x300))
    m.calls.clear()
    result = m.run(callbacks[0], m.hwnd, 0x14, m.dc, 0x12345678, 0x4D415455, m.self_ptr)
    assert result == 1 and not defaults and clients == [True]
    assert bytes(m.u.mem_read(m.self_ptr, 0x300)) == before_self
    assert len(initial_messages) == 2, "Erase synthesized an unexpected list message"
    assert set(brushes) == set(deleted) and len(brushes) == len(deleted), "Erase brush leaked"
    expected_events = ["fill_target"]
    if success:
        expected_events = ["begin", "fill_buffer", "alpha255", "end_commit"]
    elif scenario == "begin_null_handle":
        expected_events = ["begin", "fill_target"]
    elif scenario == "begin_null_dc":
        expected_events = ["begin", "end_discard", "fill_target"]
    elif scenario == "alpha_failure":
        expected_events = ["begin", "fill_buffer", "alpha255", "end_discard", "fill_target"]
    elif scenario == "end_failure":
        expected_events = ["begin", "fill_buffer", "alpha255", "end_commit", "fill_target"]
    assert events == expected_events, (scenario, events, expected_events)
    assert begins == ([] if missing else [rectangle])
    assert alphas == ([255] if active_buffer else [])
    assert bool([fill for fill in fills if fill[0] == m.dc]) == (not success)
    return {"group": "legacy_list_erase_buffer", "dark": bool(dark), "scenario": scenario,
            "opaque_buffer_before_commit": success, "palette_RGB": list(rgb),
            "client_rectangle": list(rectangle), "events": events,
            "initialized_BP_PAINTPARAMS": bool(begins),
            "fallback_preserves_palette": not success, "brushes_balanced": True,
            "handled_return_one_without_default_proc": True,
            "original_list_object_preserved": True, "passed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "geek-ui-candidate-v4.exe")
    parser.add_argument("--symbols", type=Path, default=ROOT / "native-ui-symbols.json")
    parser.add_argument("--output", type=Path, default=ROOT / "native-ui-v4-verification.json")
    parser.add_argument("--skip-list-interaction", action="store_true")
    parser.add_argument("--skip-draw-layout", action="store_true")
    args = parser.parse_args()
    image = args.image.read_bytes()
    symbols_bytes = args.symbols.read_bytes()
    symbols = json.loads(symbols_bytes)
    results = []
    failures = []

    def check(name, function, *values, **keywords):
        try:
            results.append(function(image, symbols, *values, **keywords))
        except Exception as exc:
            failures.append({"case": name, "error": f"{type(exc).__name__}: {exc}"})

    for dpi in (96, 144, 192, 288):
        check(f"list_measure_{dpi}", measure_case, dpi)
        for base_size in ((260, 12), (260, 80)):
            check(f"menu_measure_{dpi}_{base_size[1]}", menu_case, dpi, False, base_size)
        check(f"menu_draw_{dpi}", menu_case, dpi, True, (260, 24))
    for dpi in (0, 95, 769):
        check(f"list_invalid_dpi_{dpi}", measure_case, dpi)
    check("list_dpi_api_unavailable", measure_case, 192, dpi_function=False)
    check("list_null_measure", measure_case, 192, null=True)
    check("menu_null_parent", menu_case, 144, False, (260, 24), null_parent=True)
    check("menu_dpi_api_unavailable", menu_case, 192, False, (260, 24), dpi_function=False)
    check("menu_invalid_dpi", menu_case, 769, False, (260, 24))
    if "ui_menu_background" in symbols:
        for scenario in ("menubar_type0", "popup_type2", "other_type0", "other_type1"):
            for dark in (False, True):
                check(f"menu_background_{scenario}_{dark}", menu_background_case, scenario, dark)
    if not args.skip_list_interaction:
        for dpi in (96, 144, 192, 288):
            for msg in (0x201, 0x202, 0x203, 0x204, 0x205, 0x206):
                check(f"delegate_former_button_{dpi}_{msg:x}", interaction_case, dpi, msg, "former_button")
            for region in ("body", "right_gutter", "empty"):
                check(f"delegate_{dpi}_{region}", interaction_case, dpi, 0x202, region)
    if not args.skip_draw_layout:
        for dpi in (96, 144, 192, 288):
            for scenario in ("reordered", "hidden_size", "horizontal_scroll", "missing_header"):
                check(f"native_columns_{dpi}_{scenario}", columns_case, dpi, scenario)
            for name_width in (340, 520):
                check(f"draw_layout_{dpi}_{name_width}", draw_layout_case, dpi, name_width)
            for scenario in ("negative_top", "partial_bottom", "partial_clip", "dark"):
                check(f"atomic_row_{dpi}_{scenario}", draw_layout_case, dpi, scenario=scenario)
        for scenario in ("begin_null_handle", "begin_null_dc", "missing_begin", "missing_alpha",
                         "missing_end", "alpha_failure", "end_failure"):
            check(f"atomic_row_fallback_{scenario}", draw_layout_case, 144, scenario=scenario)
        check("atomic_row_null_item", draw_guard_case, null=True)
        check("atomic_row_invalid_item_id", draw_guard_case)
        for dpi in (96, 144, 192, 288):
            for scenario in ("same_row", "cross_row_gutters", "partial_first", "empty_space",
                             "scroll_key_paint_sync", "leave_destroy", "tracking_retry", "geometry_failures"):
                check(f"list_hover_{dpi}_{scenario}", list_hover_case, dpi, scenario)
        for dark in (False, True):
            for scenario in ("normal", "missing_begin", "missing_alpha", "missing_end",
                             "begin_null_handle", "begin_null_dc", "alpha_failure", "end_failure"):
                check(f"legacy_list_erase_{dark}_{scenario}", list_erase_case, dark, scenario)
    report = {"image": str(args.image.resolve()), "image_sha256": hashlib.sha256(image).hexdigest(),
              "symbols_sha256": hashlib.sha256(symbols_bytes).hexdigest(),
              "verified_at_utc": datetime.now(timezone.utc).isoformat(),
              "engine": "Unicorn x86_64; real compiled candidate instructions",
              "abi_checks": ["16-byte call alignment", "RSP restoration", "all eight nonvolatile GPRs",
                             "XMM6-XMM15 preservation", "volatile GPR/XMM poisoning at mock calls",
                             "fifth and sixth stack arguments", "original virtual return pointer"],
              "passed": not failures, "passed_cases": len(results), "failed_cases": len(failures),
              "cases": results, "failures": failures}
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "cases"}, indent=2, ensure_ascii=False))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
