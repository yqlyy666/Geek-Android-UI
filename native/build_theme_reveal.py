"""Extend the verified Geek image with its native theme-reveal animation."""
from pathlib import Path
import hashlib
import json
import struct
import sys

from merge_helper import align, checked_helper_unwind, pefile, require, runtime_functions, validate_windows_image

BASE_SHA = "8b087eb271c62b7fabc6623fab346d6a52eb549c7c1ca678bfd14ec21f43ef8a"
HELPER_BASE = 0x140459000
STATE_RVA = 0x412B18
STATE_BYTES = 256

def build(source: Path, helper_path: Path, output: Path):
    original = source.read_bytes()
    require(hashlib.sha256(original).hexdigest() == BASE_SHA, "Unexpected Geek base")
    require(output.resolve() not in (source.resolve(), helper_path.resolve()), "Use a separate candidate")
    base = pefile.PE(data=original)
    helper = pefile.PE(str(helper_path))
    require(helper.OPTIONAL_HEADER.ImageBase == HELPER_BASE, "Unexpected helper base")
    require([s.Name.rstrip(b"\0") for s in helper.sections] == [b".text", b".rdata", b".pdata"],
            "Expected import-free helper with no writable state")
    for index in (1, 5, 9, 10, 11, 12, 13, 14):
        d = helper.OPTIONAL_HEADER.DATA_DIRECTORY[index]
        require(not (d.VirtualAddress or d.Size), f"Unsupported helper directory {index}")
    image_base = base.OPTIONAL_HEADER.ImageBase
    offset = HELPER_BASE - image_base
    text, rdata, pdata = helper.sections
    anim = base.sections[-1]
    require(anim.Name.rstrip(b"\0") == b".uianim", "Unexpected final section")
    require(offset + text.VirtualAddress == base.OPTIONAL_HEADER.SizeOfImage, "Code is not adjacent")
    functions = runtime_functions(helper)
    checked_helper_unwind(helper, functions)
    adjusted = [tuple(n + offset for n in row) for row in functions]
    combined = runtime_functions(base) + adjusted
    require(all(a[1] <= b[0] for a, b in zip(combined, combined[1:])), "Overlapping unwind entries")
    exception = base.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    table_rva = exception.VirtualAddress + exception.Size
    table = b"".join(struct.pack("<III", *row) for row in adjusted)
    table_at = anim.PointerToRawData + table_rva - anim.VirtualAddress
    require(table_rva + len(table) <= 0x457000, "Unwind entries would overwrite column helper")
    require(not any(original[table_at:table_at + len(table)]), "Occupied unwind table tail")
    image = bytearray(original)
    changed = set()
    def put(at, data):
        end = at + len(data)
        if end > len(image):image.extend(b"\0" * (end - len(image)))
        image[at:end] = data
        changed.update(range(at, min(end, len(original))))
    def field(owner, name, value):
        put(owner.get_field_absolute_offset(name), struct.pack("<I", value))
    max_end = anim.VirtualAddress + max(anim.Misc_VirtualSize, anim.SizeOfRawData)
    for section in (text, rdata):
        rva = offset + section.VirtualAddress
        raw = anim.PointerToRawData + rva - anim.VirtualAddress
        put(raw, helper.get_data(section.VirtualAddress, section.SizeOfRawData))
        max_end = max(max_end, rva + max(section.SizeOfRawData, section.Misc_VirtualSize))
    put(table_at, table)
    raw_size = align(max_end - anim.VirtualAddress, base.OPTIONAL_HEADER.FileAlignment)
    end = anim.PointerToRawData + raw_size
    if len(image) < end:image.extend(b"\0" * (end - len(image)))
    field(anim, "Misc_VirtualSize", max_end - anim.VirtualAddress)
    field(anim, "SizeOfRawData", raw_size)
    field(base.OPTIONAL_HEADER, "SizeOfImage", align(max_end, base.OPTIONAL_HEADER.SectionAlignment))
    field(base.OPTIONAL_HEADER, "SizeOfCode", base.OPTIONAL_HEADER.SizeOfCode + raw_size - anim.SizeOfRawData)
    field(exception, "Size", exception.Size + len(table))
    state = next(s for s in base.sections if s.Name.rstrip(b"\0") == b".uidata")
    require(state.VirtualAddress + state.Misc_VirtualSize == STATE_RVA and not state.SizeOfRawData,
            "Animation state would overwrite existing helpers")
    require(STATE_RVA + STATE_BYTES <= 0x413000, "Animation state exceeds zero-filled section")
    field(state, "Misc_VirtualSize", state.Misc_VirtualSize + STATE_BYTES)
    symbols = {s.name.decode(): HELPER_BASE + s.address for s in helper.DIRECTORY_ENTRY_EXPORT.symbols}
    hook = 0x2979C
    require(original[hook:hook + 5] == bytes.fromhex("e8ffc13d00"), "Unexpected original restore hook")
    address = image_base + base.get_rva_from_offset(hook)
    put(hook, b"\xe8" + struct.pack("<i", symbols["ui_restore_main"] - address - 5))
    for at, old, symbol in ((0x328088,0x1400289E4,"ui_theme_light"),
                            (0x3280C8,0x1400289AC,"ui_theme_dark"),
                            (0x328108,0x140028978,"ui_theme_auto")):
        require(struct.unpack_from("<Q",original,at)[0]==old,
                f"Unexpected theme command callback at {at:#x}")
        put(at,struct.pack("<Q",symbols[symbol]))
    field(base.OPTIONAL_HEADER, "CheckSum", 0)
    field(base.OPTIONAL_HEADER, "CheckSum", pefile.PE(data=image).generate_checksum())
    candidate = pefile.PE(data=image)
    require(candidate.verify_checksum() and runtime_functions(candidate) == combined, "Invalid checksum or unwind mapping")
    for section in (text, rdata):
        require(candidate.get_data(offset + section.VirtualAddress, section.SizeOfRawData) ==
                helper.get_data(section.VirtualAddress, section.SizeOfRawData), "Helper mapping mismatch")
    require(all(n in changed or a == b for n, (a, b) in enumerate(zip(original, image))),
            "Unexpected change in original code/resources")
    require(image[0x327E58:0x327E60] == original[0x327E58:0x327E60], "Sort callback changed")
    output.write_bytes(image)
    validate_windows_image(output)
    report = {"path": str(output), "base_sha256": BASE_SHA,
              "sha256": hashlib.sha256(image).hexdigest(), "size": len(image),
              "state_address": hex(image_base + STATE_RVA), "state_reserved_bytes": STATE_BYTES,
              "symbols": {n: hex(a) for n, a in symbols.items()}, "windows_loader_validated": True}
    output.with_suffix(".build.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    build(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
