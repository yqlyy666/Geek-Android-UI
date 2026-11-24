"""Embed the context menu animation in the verified sort-fixed Geek build."""

import argparse
import hashlib
import struct
from pathlib import Path

from merge_helper import (
    align, checked_helper_unwind, pefile, read_rva, require,
    runtime_functions, validate_windows_image,
)

BASE_HASH = "37e9a13065eb74b9ef42eb47dbb38964271e6bee2c5d88f85eab803b219d63ea"
HOOKS = {
    0x1180CA: (0x1401C5AE8, b"ui_position_context_slide"),
    0x114D43: (0x140116C40, b"ui_context_popup_effect"),
    0x11687A: (0x140116C40, b"ui_context_popup_effect"),
}


def build(source, helper_path, output):
    original = source.read_bytes()
    require(hashlib.sha256(original).hexdigest() == BASE_HASH, "Unexpected Geek base")
    image = bytearray(original)
    base = pefile.PE(data=original)
    helper = pefile.PE(str(helper_path))
    offset = helper.OPTIONAL_HEADER.ImageBase - base.OPTIONAL_HEADER.ImageBase
    require(offset == 0x433000 and helper.FILE_HEADER.Machine == 0x8664, "Unexpected helper layout")
    for index in (1, 5, 9, 10, 11, 12, 13, 14):
        entry = helper.OPTIONAL_HEADER.DATA_DIRECTORY[index]
        require(not (entry.VirtualAddress or entry.Size), f"Unsupported helper directory {index}")

    sections = helper.sections
    require([s.Name.rstrip(b"\0") for s in sections] == [b".text", b".rdata", b".pdata"],
            "Expected import-free helper without writable state")
    start = sections[0].VirtualAddress
    section_rva = offset + start
    require(section_rva == base.OPTIONAL_HEADER.SizeOfImage, "Invalid section start")
    base_records = runtime_functions(base)
    helper_records = runtime_functions(helper)
    checked_helper_unwind(helper, helper_records)
    combined = base_records + [tuple(offset + n for n in row) for row in helper_records]
    require(all(a[1] <= b[0] for a, b in zip(combined, combined[1:])),
            "Overlapping unwind entries")
    pdata = helper.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    payload = bytearray(pdata.VirtualAddress - start)
    for section in sections[:2]:
        at = section.VirtualAddress - start
        data = read_rva(helper, section.VirtualAddress, section.Misc_VirtualSize)
        require(at + len(data) <= len(payload), "Invalid helper payload range")
        payload[at:at + len(data)] = data
    table = b"".join(struct.pack("<III", *row) for row in combined)
    payload.extend(table)

    layout = base.OPTIONAL_HEADER
    header_at = base.sections[-1].get_file_offset() + 40
    require(header_at + 40 <= layout.SizeOfHeaders and not any(image[header_at:header_at + 40]),
            "No unused PE section header")
    raw_at = align(len(original), layout.FileAlignment)
    raw_size = align(len(payload), layout.FileAlignment)
    changed = set()

    def put(at, data):
        image[at:at + len(data)] = data
        changed.update(range(at, at + len(data)))

    def field(owner, name, val, fmt="<I"):
        put(owner.get_field_absolute_offset(name), struct.pack(fmt, val))

    put(header_at, struct.pack("<8sIIIIIIHHI", b".uianim", len(payload), section_rva,
                               raw_size, raw_at, 0, 0, 0, 0, 0x60000020))
    image.extend(b"\0" * (raw_at - len(original)))
    image.extend(payload)
    image.extend(b"\0" * (raw_size - len(payload)))
    field(base.FILE_HEADER, "NumberOfSections", len(base.sections) + 1, "<H")
    field(layout, "SizeOfImage", align(section_rva + raw_size, layout.SectionAlignment))
    field(layout, "SizeOfCode", layout.SizeOfCode + raw_size)
    field(layout.DATA_DIRECTORY[3], "VirtualAddress", offset + pdata.VirtualAddress)
    field(layout.DATA_DIRECTORY[3], "Size", len(table))
    state = next(s for s in base.sections if s.Name.rstrip(b"\0") == b".uidata")
    require(state.VirtualAddress + state.Misc_VirtualSize == 0x412458 and
            not state.SizeOfRawData and state.Characteristics & 0x80000000,
            "Slide state would overlap an existing variable")
    field(state, "Misc_VirtualSize", state.Misc_VirtualSize + 16 * 64)

    exports = {s.name: layout.ImageBase + offset + s.address
               for s in helper.DIRECTORY_ENTRY_EXPORT.symbols if not s.forwarder}
    for raw, (old, symbol) in HOOKS.items():
        addr = layout.ImageBase + base.get_rva_from_offset(raw)
        require(image[raw:raw + 5] == b"\xe8" + struct.pack("<i", old - addr - 5),
                f"Unexpected original instruction at {raw:#x}")
        put(raw, b"\xe8" + struct.pack("<i", exports[symbol] - addr - 5))
    field(layout, "CheckSum", 0)
    field(layout, "CheckSum", pefile.PE(data=image).generate_checksum())
    candidate = pefile.PE(data=image)
    require(candidate.verify_checksum() and runtime_functions(candidate) == combined,
            "Candidate checksum or unwind validation failed")
    require(all(i in changed or a == b for i, (a, b) in enumerate(zip(original, image))),
            "Unexpected bytes changed in original file")
    for section in sections[:2]:
        size = section.Misc_VirtualSize
        require(read_rva(candidate, offset + section.VirtualAddress, size) ==
                read_rva(helper, section.VirtualAddress, size), "Incorrect helper mapping")
    require(image[0x327E58:0x327E60] == original[0x327E58:0x327E60], "Sort callback changed")
    require(output.resolve() not in (source.resolve(), helper_path.resolve()), "Use a separate candidate")
    output.write_bytes(image)
    validate_windows_image(output)
    print(f"Verified native candidate: {hashlib.sha256(image).hexdigest()}  {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "helper", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    build(args.source, args.helper, args.output)
