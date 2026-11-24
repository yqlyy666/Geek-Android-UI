"""Exercise the compiled reflected-sort wrapper in the specified Geek PE."""

from pathlib import Path
import sys

from verify_native_ui import Machine


def verify(image: bytes, initial_top: int, accepts_scroll_top: bool) -> None:
    machine = Machine(image)
    entry = machine.read64(machine.base + machine.pe.get_rva_from_offset(0x327E58))
    state = {"top": initial_top, "sort_called": False}
    actions = []
    notification = machine.scratch + 0x8000
    result = machine.scratch + 0x8018

    def send():
        hwnd, message, wparam, lparam = (machine.arg(i) for i in range(4))
        assert hwnd == machine.hwnd and lparam == 0 and not state["sort_called"]
        actions.append(message)
        if message == 0x1027:  # LVM_GETTOPINDEX
            assert wparam == 0
            return state["top"]
        if message == 0x115:  # WM_VSCROLL, SB_TOP
            assert wparam == 6
            if accepts_scroll_top:
                state["top"] = 0
            return 0
        assert message == 0x1013 and wparam == 0  # LVM_ENSUREVISIBLE
        state["top"] = 0
        return 1

    def native_sort():
        assert [machine.arg(i) for i in range(3)] == [machine.self_ptr, notification, result]
        assert state["top"] == 0 and not state["sort_called"]
        state["sort_called"] = True
        machine.write64(result, 42)

    machine.iat(0x140256F28, "SendMessageW", send)
    machine.intercept(0x140019970, "Geek native sort", native_sort)
    machine.run(entry, machine.self_ptr, notification, result)
    assert state["sort_called"] and machine.read64(result) == 42
    expected = [0x1027]
    if initial_top:
        expected.extend([0x115, 0x1027])
        if not accepts_scroll_top:
            expected.append(0x1013)
    assert actions == expected


if __name__ == "__main__":
    binary = Path(sys.argv[1]).read_bytes()
    tests = [(0, True), (6, True), (20, False), (-6, True)]
    for scenario in tests:
        verify(binary, *scenario)
    print(f"{len(tests)} compiled sort callback cases passed")
