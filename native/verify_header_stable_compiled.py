"""Exercise the embedded header guard and region commit code, without UI input.

COFF symbols locate private functions in the exact helper that was merged into
the candidate. Win64 stack/register rules are checked by the shared Machine.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

from verify_native_ui import Machine, pefile

ROOT = Path(__file__).resolve().parent
SUBCLASS = 0x47535343


def coff_symbols(path, helper):
    data = path.read_bytes()
    _, count_sections, _, table, count, optional, _ = struct.unpack_from("<HHIIIHH", data)
    strings = table + count * 18
    sections = [data[20 + optional + i * 40:28 + optional + i * 40].rstrip(b"\0")
                for i in range(count_sections)]
    mapped = {s.Name.rstrip(b"\0"): s.VirtualAddress for s in helper.sections}
    result = {}
    i = 0
    while i < count:
        name, value, section, kind, _, auxiliary = struct.unpack_from("<8sIhHBB", data, table + i * 18)
        if name[:4] == b"\0" * 4:
            at = strings + struct.unpack_from("<I", name, 4)[0]
            name = data[at:data.index(b"\0", at)]
        else:
            name = name.rstrip(b"\0")
        if section > 0 and kind == 0x20 and sections[section - 1] in mapped:
            result[name.decode()] = helper.OPTIONAL_HEADER.ImageBase + mapped[sections[section - 1]] + value
        i += auxiliary + 1
    return result


class Harness:
    def __init__(self, data, symbols):
        self.m = Machine(data)
        self.symbols = symbols
        self.s = self.m.scratch + 0x10000
        self.header = self.m.hwnd + 0x100
        self.events = []
        self.top = 0
        self.region_kind = 0
        self.rect_success = True
        self.region_set_success = True
        self.region_create_failure = 0
        self.region_create_count = 0
        self.combine_failure = False
        self.region_owned = set()
        self.region_live = set()
        self.next_region = 0x700000
        self.regions = {}
        self.list_add_success = True
        self.header_add_success = True
        m = self.m
        m.write64(self.s, m.self_ptr)
        m.write64(self.s + 8, m.hwnd)
        m.write64(self.s + 720, self.header)
        m.write64(0x140412C18, self.s)
        self.set32(40, 63)
        self.set32(44, 40)
        self.set32(48, 760)
        self.set32(52, 1092)
        m.iat(0x1402560B8, "DefSubclassProc", lambda: self.record("default", *[m.arg(i) for i in range(4)]) or 123)
        m.iat(0x140256AC0, "GetModuleHandleW", lambda: self.record("module", m.string(m.arg(0), wide=True)) or 0x6000)
        remove = m.stub("RemoveWindowSubclass", lambda: self.record("remove", *[m.arg(i) for i in range(3)]) or 1)

        def proc():
            name = m.string(m.arg(1))
            assert name == "RemoveWindowSubclass", name
            return remove
        m.iat(0x140256A78, "GetProcAddress", proc)
        m.iat(0x140256A08, "GetProcessHeap", lambda: 0x8000)
        m.iat(0x140256A10, "HeapFree", lambda: self.record("free", m.arg(2)) or 1)
        m.iat(0x140256A18, "HeapAlloc", lambda: self.record("allocate", m.arg(2)) or self.s)
        m.iat(0x1402560A8, "SetWindowSubclass", self.add)
        m.iat(0x140256F28, "SendMessageW", self.send)
        m.iat(0x1402573A0, "DefWindowProcW", lambda: self.record("defredraw", *[m.arg(i) for i in range(4)]) or 0)
        m.iat(0x140257208, "GetWindowRect", self.window_rect)
        m.iat(0x140257200, "GetClientRect", self.client_rect)
        m.iat(0x1402570C8, "ClientToScreen", self.client_screen)
        m.iat(0x1402562A0, "CreateRectRgn", self.create_region)
        m.iat(0x140257138, "GetWindowRgn", lambda: self.region_kind)
        m.iat(0x140256290, "CombineRgn", self.combine)
        m.iat(0x140257158, "SetWindowRgn", self.set_region)
        m.iat(0x140256380, "DeleteObject", self.delete_region)
        m.intercept(0x140457660, "ExistingColumnInitializer", lambda: self.record("columns", m.arg(0)) or 0)

    def set32(self, offset, value):
        self.m.u.mem_write(self.s + offset, struct.pack("<I", value & 0xFFFFFFFF))

    def record(self, *event):
        self.events.append(event)
        return 0

    def add(self):
        m = self.m
        self.record("add", *[m.arg(i) for i in range(4)])
        return self.list_add_success if m.arg(0) == m.hwnd else self.header_add_success

    def send(self):
        m = self.m
        assert m.arg(0) == m.hwnd
        msg = m.arg(1)
        if msg == 0x1027:
            return self.top
        if msg == 0x101F:
            return self.header
        assert m.read32(self.s + 104) == 1
        if msg == 0xB:
            self.record("setredraw", m.arg(2))
            return 0
        assert msg == 0x1014, hex(msg)
        delta = m.arg(3)
        if delta >> 63:
            delta -= 1 << 64
        self.record("scroll", delta)
        self.top += delta // 63
        return 1

    def window_rect(self):
        m = self.m
        if not self.rect_success:
            return 0
        m.u.mem_write(m.arg(1), struct.pack("<iiii", 100, 100, 1200, 900))
        return 1

    def client_rect(self):
        self.m.u.mem_write(self.m.arg(1), struct.pack("<iiii", 0, 0, 1092, 760))
        return 1

    def client_screen(self):
        self.m.u.mem_write(self.m.arg(1), struct.pack("<ii", 104, 130))
        return 1

    def create_region(self):
        self.region_create_count += 1
        if self.region_create_count == self.region_create_failure:
            self.record("create_region_failed", self.region_create_count)
            return 0
        value = self.next_region
        self.next_region += 0x100
        self.regions[value] = tuple(self.m.arg(i) for i in range(4))
        self.region_live.add(value)
        self.record("create_region", value, self.regions[value])
        return value

    def combine(self):
        self.record("combine", *[self.m.arg(i) for i in range(4)])
        return 0 if self.combine_failure else 2

    def set_region(self):
        self.record("region", *[self.m.arg(i) for i in range(3)])
        if self.region_set_success:
            self.region_owned.clear()
            if self.m.arg(1):
                value = self.m.arg(1)
                assert value in self.region_live
                self.region_live.remove(value)
                self.region_owned.add(value)
        return self.region_set_success

    def delete_region(self):
        value = self.m.arg(0)
        self.record("delete", value)
        assert value in self.region_live, ("DeleteObject on invalid/transferred region", value)
        self.region_live.remove(value)
        return 1

    def header_call(self, message, w=0, l=0, ref=None, hwnd=None):
        return self.m.run(self.symbols["scroll_header_proc"], self.header if hwnd is None else hwnd,
                          message, w, l, SUBCLASS, self.s if ref is None else ref)


def run_cases(data, symbols):
    results = []
    def passed(name):
        results.append({"case": name, "passed": True})

    for enabled in (0, 1):
        h = Harness(data, symbols)
        h.set32(104, 1)
        assert h.header_call(0xB, enabled) == 0
        assert h.events == []
        assert h.m.read32(h.s + 732) == 1
        passed(f"internal_header_setredraw_{enabled}_suppressed")
    for enabled in (0, 1):
        h = Harness(data, symbols)
        assert h.header_call(0xB, enabled) == 123
        assert h.events == [("default", h.header, 0xB, enabled, 0)]
        assert h.m.read32(h.s + 732) == 0
        passed(f"external_header_setredraw_{enabled}_delegated")
    for message, w, l in ((0x200, 0, 0x10002), (0x201, 1, 0x20003), (0x202, 0, 0x20003), (0xF, 0, 0)):
        h = Harness(data, symbols)
        h.set32(104, 1)
        assert h.header_call(message, w, l) == 123
        assert h.events == [("default", h.header, message, w, l)]
        passed(f"ordinary_header_message_{message:x}_delegated_even_during_commit")
    for kind in ("null_reference", "wrong_header"):
        h = Harness(data, symbols)
        h.set32(104, 1)
        assert h.header_call(0xB, 0, ref=0 if kind == "null_reference" else h.s,
                             hwnd=h.header + 1 if kind == "wrong_header" else h.header) == 123
        assert h.events[0][0] == "default"
        passed(kind + "_safely_delegated")

    h = Harness(data, symbols)
    assert h.header_call(0x82) == 123
    assert [e[0] for e in h.events] == ["module", "remove", "default"]
    assert h.events[1][1:] == (h.header, symbols["scroll_header_proc"], SUBCLASS)
    assert h.m.read64(h.s + 720) == 0
    assert h.m.read64(0x140412C18) == h.s
    passed("header_destroy_unhooks_and_clears_reference_without_freeing_list_state")

    h = Harness(data, symbols)
    assert h.m.run(symbols["scroll_proc"], h.m.hwnd, 0x82, 0, 0, SUBCLASS, h.s) == 123
    assert [e[0] for e in h.events] == ["module", "remove", "remove", "default", "free"]
    assert h.events[1][1:] == (h.m.hwnd, symbols["scroll_proc"], SUBCLASS)
    assert h.events[2][1:] == (h.header, symbols["scroll_header_proc"], SUBCLASS)
    assert h.events[-1] == ("free", h.s)
    assert h.m.read64(0x140412C18) == 0
    assert h.m.read64(h.s + 720) == 0
    passed("list_destroy_removes_header_hook_before_free_and_unlinks_state")

    for list_ok, header_ok in ((True, True), (False, True), (True, False)):
        h = Harness(data, symbols)
        h.m.write64(0x140412C18, 0)
        h.m.u.mem_write(h.s, b"\0" * 736)
        h.list_add_success, h.header_add_success = list_ok, header_ok
        h.m.run(symbols["ui_pre_subclass"], h.m.self_ptr)
        assert h.events[0] == ("columns", h.m.self_ptr)
        assert h.events[1] == ("allocate", 736)
        adds = [e for e in h.events if e[0] == "add"]
        assert adds[0][1:] == (h.m.hwnd, symbols["scroll_proc"], SUBCLASS, h.s)
        if list_ok:
            assert adds[1][1:] == (h.header, symbols["scroll_header_proc"], SUBCLASS, h.s)
            assert h.m.read64(0x140412C18) == h.s
            assert h.m.read64(h.s + 720) == (h.header if header_ok else 0)
            assert not any(e[0] == "free" for e in h.events)
        else:
            assert len(adds) == 1
            assert h.events[-1] == ("free", h.s)
            assert h.m.read64(0x140412C18) == 0
        passed(f"initializer_list_{list_ok}_header_{header_ok}_lifetime_safe")

    for region_kind in (0, 1, 2, 3):
        h = Harness(data, symbols)
        h.region_kind = region_kind
        h.m.run(symbols["sync_native"], h.s, 189)
        assert h.top == 3 and h.m.read32(h.s + 104) == 0
        assert h.m.read32(h.s + 708) == 1 and h.m.read32(h.s + 728) == 1
        assert not any(e[0] == "defredraw" for e in h.events)
        assert list(h.regions.values()) == [(0, 0, 0, 0), (0, 0, 1100, 800), (4, 70, 1096, 790)]
        flow = [e for e in h.events if e[0] in ("region", "setredraw", "scroll")]
        assert flow == [("region", h.m.hwnd, 0x700100, 0), ("scroll", 189),
                        ("region", h.m.hwnd, 0x700000 if region_kind else 0, 0)], flow
        assert not h.region_live, ("Leaked region handles", h.region_live)
        assert h.region_owned == ({0x700000} if region_kind else set())
        assert h.m.read32(h.s + 112) == 3
        passed(f"region_{region_kind}_keeps_header_visible_restores_original_and_commits_once")

    h = Harness(data, symbols)
    h.rect_success = False
    h.m.run(symbols["sync_native"], h.s, 126)
    assert h.top == 2 and h.m.read32(h.s + 104) == 0
    flow = [e for e in h.events if e[0] in ("defredraw", "setredraw", "scroll")]
    assert flow == [("setredraw", 0), ("defredraw", h.m.hwnd, 0xB, 0, 0), ("scroll", 126),
                    ("defredraw", h.m.hwnd, 0xB, 1, 0), ("setredraw", 1)], flow
    assert h.m.read32(h.s + 728) == 0
    passed("failed_region_preserves_proven_native_copy_suppression_fallback")

    for failure in ("create_original", "create_restricted", "create_body", "combine", "set_region"):
        h = Harness(data, symbols)
        h.region_create_failure = {"create_original": 1, "create_restricted": 2, "create_body": 3}.get(failure, 0)
        h.combine_failure = failure == "combine"
        h.region_set_success = failure != "set_region"
        h.m.run(symbols["sync_native"], h.s, 126)
        assert h.top == 2 and h.m.read32(h.s + 104) == 0
        flow = [e for e in h.events if e[0] in ("defredraw", "setredraw", "scroll")]
        assert flow == [("setredraw", 0), ("defredraw", h.m.hwnd, 0xB, 0, 0), ("scroll", 126),
                        ("defredraw", h.m.hwnd, 0xB, 1, 0), ("setredraw", 1)], flow
        assert not h.region_live and not h.region_owned
        assert h.m.read32(h.s + 728) == 0 and h.m.read32(h.s + 708) == 1
        passed(failure + "_fallback_preserves_native_copy_suppression_without_region_leaks")

    h = Harness(data, symbols)
    h.set32(716, 1)
    h.m.run(symbols["sync_native"], h.s, 126)
    assert h.events == [("scroll", 126)] and h.top == 2
    assert h.m.read32(h.s + 716) == 1
    passed("external_redraw_disabled_is_preserved_without_region_or_visibility_changes")
    for enabled in (0, 1):
        h = Harness(data, symbols)
        assert h.m.run(symbols["scroll_proc"], h.m.hwnd, 0xB, enabled, 0, SUBCLASS, h.s) == 123
        assert h.m.read32(h.s + 716) == int(not enabled)
        assert h.events == [("default", h.m.hwnd, 0xB, enabled, 0)]
        passed(f"external_list_setredraw_{enabled}_recorded_and_forwarded_without_visibility_override")
    h = Harness(data, symbols)
    h.m.run(symbols["sync_native"], h.s, 0)
    assert h.events == [] and h.m.read32(h.s + 708) == 0
    passed("unchanged_native_position_does_not_toggle_redraw_or_regions")
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("helper", type=Path)
    parser.add_argument("--object", type=Path)
    args = parser.parse_args()
    data = args.image.read_bytes()
    report = json.loads(args.image.with_suffix(".build.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(data).hexdigest() == report["sha256"]
    candidate = pefile.PE(data=data)
    helper = pefile.PE(str(args.helper))
    for section in helper.sections:
        if section.Name.rstrip(b"\0") not in (b".text", b".rdata"):
            continue
        rva = helper.OPTIONAL_HEADER.ImageBase + section.VirtualAddress - candidate.OPTIONAL_HEADER.ImageBase
        assert candidate.get_data(rva, section.SizeOfRawData) == helper.get_data(section.VirtualAddress, section.SizeOfRawData)
    symbols = coff_symbols(args.object or args.helper.with_suffix(".obj"), helper)
    base = pefile.PE(str(ROOT / "geek-ui-theme-noflash-v1.exe"))
    for at, count in ((0x327E58, 8), (0x2979C, 5), (0x328088, 8), (0x3280C8, 8), (0x328108, 8), (0x3F6BC4, 5)):
        assert data[at:at + count] == base.__data__[at:at + count], ("Existing theme/sort/column hook changed", hex(at))
    previous_code = base.get_data(0x457000, 0xC000)
    assert candidate.get_data(0x457000, len(previous_code)) == previous_code, "Existing column, scroll and theme code changed"
    cases = run_cases(data, symbols)
    cases.append({"case": "existing_theme_sort_columns_hooks_and_helper_code_preserved", "passed": True})
    output = {"sha256": report["sha256"], "helper_sha256": hashlib.sha256(args.helper.read_bytes()).hexdigest(),
              "cases": cases, "passed": True, "ui_opened": False}
    args.image.with_suffix(".header-stable-compiled.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
