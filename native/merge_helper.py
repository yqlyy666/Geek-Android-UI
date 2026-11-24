"""Embed an import-free fixed-base AMD64 helper PE in an existing AMD64 PE.

Only the output and symbol JSON are written. Original sections, resources,
imports, certificates, and overlay bytes retain their original file offsets.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

import pefile


class MergeError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise MergeError(message)


def align(value: int, alignment: int) -> int:
    require(alignment > 0 and alignment & (alignment - 1) == 0,
            f"Invalid alignment {alignment:#x}")
    return (value + alignment - 1) & -alignment


def read_rva(pe: pefile.PE, rva: int, size: int) -> bytes:
    data = pe.get_data(rva, size)
    require(len(data) == size, f"Truncated RVA range {rva:#x}+{size:#x}")
    return data


def runtime_functions(pe: pefile.PE) -> list[tuple[int, int, int]]:
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    if not directory.Size:
        require(not directory.VirtualAddress, "Empty exception directory has nonzero RVA")
        return []
    require(directory.VirtualAddress > 0 and directory.Size % 12 == 0,
            "Invalid x64 exception directory")
    raw = read_rva(pe, directory.VirtualAddress, directory.Size)
    result = list(struct.iter_unpack("<III", raw))
    for begin, end, unwind in result:
        require(0 < begin < end and unwind > 0,
                f"Invalid runtime function ({begin:#x}, {end:#x}, {unwind:#x})")
    return result


def checked_helper_unwind(pe: pefile.PE, records: list[tuple[int, int, int]]) -> None:
    for begin, end, unwind in records:
        head = read_rva(pe, unwind, 4)
        version, flags, count = head[0] & 7, head[0] >> 3, head[2]
        require(version in (1, 2), f"Unsupported unwind version {version} at {unwind:#x}")
        require(flags == 0,
                f"Helper unwind flags {flags:#x} at {unwind:#x} need handler/chain RVA translation")
        read_rva(pe, unwind, align(4 + count * 2, 4))
        section = pe.get_section_by_rva(begin)
        require(section is not None and end <= section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData),
                f"Helper runtime function crosses its section at {begin:#x}")


def new_section_name(section: pefile.SectionStructure, index: int, used: set[bytes]) -> bytes:
    original = section.Name.rstrip(b"\0")
    known = {b".text": b".uitext", b".rdata": b".uirdata", b".data": b".uidata",
             b".bss": b".uibss", b".pdata": b".uipdata"}
    name = known.get(original, f".ui{index}".encode("ascii"))
    if name in used:
        name = f".u{index:06x}".encode("ascii")
    require(name not in used and len(name) <= 8, "Cannot allocate unique section name")
    used.add(name)
    return name.ljust(8, b"\0")


def validate_windows_image(path: Path) -> None:
    """Ask the Windows image loader to validate layout without executing code."""
    if sys.platform != "win32":
        return
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CreateFileMappingW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                         wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR]
    kernel.CreateFileMappingW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path.resolve()), 0x80000000, 7, None, 3, 0, None)
    require(handle != ctypes.c_void_p(-1).value,
            f"Cannot open merged image for loader validation: {ctypes.get_last_error()}")
    try:
        # PAGE_READONLY | SEC_IMAGE_NO_EXECUTE: headers are validated; code cannot execute.
        mapping = kernel.CreateFileMappingW(handle, None, 0x11000002, 0, 0, None)
        require(bool(mapping), f"Windows image loader rejected output: Win32 {ctypes.get_last_error()}")
        kernel.CloseHandle(mapping)
    finally:
        kernel.CloseHandle(handle)


def merge(base_path: Path, helper_path: Path, output_path: Path,
          symbols_path: Path, rva_offset: int) -> dict:
    require(base_path.resolve() != output_path.resolve(), "Output must differ from the base input")
    require(helper_path.resolve() != output_path.resolve(), "Output must differ from the helper input")
    base_bytes = base_path.read_bytes()
    helper_bytes = helper_path.read_bytes()
    base = pefile.PE(data=base_bytes)
    helper = pefile.PE(data=helper_bytes)
    for label, pe in (("base", base), ("helper", helper)):
        require(pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.Magic == 0x20B,
                f"{label} must be an AMD64 PE32+ image")
    require(helper.OPTIONAL_HEADER.ImageBase == base.OPTIONAL_HEADER.ImageBase + rva_offset,
            "Helper ImageBase does not match base ImageBase plus RVA offset")
    for index, name in ((1, "imports"), (5, "base relocations"), (9, "TLS"),
                        (10, "load configuration"), (11, "bound imports"), (12, "IAT"),
                        (13, "delay imports"), (14, "CLR")):
        directory = helper.OPTIONAL_HEADER.DATA_DIRECTORY[index]
        require(not directory.VirtualAddress and not directory.Size,
                f"Helper contains unsupported {name}")
    require(len(helper.sections) > 0, "Helper contains no sections")

    base_records = runtime_functions(base)
    helper_records = runtime_functions(helper)
    require(helper_records, "Helper must contain x64 unwind records")
    checked_helper_unwind(helper, helper_records)
    adjusted = [tuple(value + rva_offset for value in record) for record in helper_records]
    require(all(value <= 0xFFFFFFFF for record in adjusted for value in record),
            "Adjusted runtime function RVA exceeds 32 bits")
    combined = sorted(base_records + adjusted)
    for previous, current in zip(combined, combined[1:]):
        require(previous[1] <= current[0],
                f"Runtime functions overlap at {previous[0]:#x} and {current[0]:#x}")
    pdata_bytes = b"".join(struct.pack("<III", *record) for record in combined)
    helper_exception = helper.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata_section = helper.get_section_by_rva(helper_exception.VirtualAddress)
    require(pdata_section is not None, "Helper exception table has no section")
    require(pdata_section is max(helper.sections, key=lambda section: section.VirtualAddress),
            "Helper exception table must be in its final virtual section")
    require(pdata_section.Name.rstrip(b"\0") == b".pdata",
            "Helper exception table must be in .pdata")
    pdata_relative = helper_exception.VirtualAddress - pdata_section.VirtualAddress
    pdata_end = pdata_relative + helper_exception.Size
    require(pdata_end <= pdata_section.Misc_VirtualSize, "Helper exception table exceeds .pdata")
    original_pdata = read_rva(helper, pdata_section.VirtualAddress, pdata_section.Misc_VirtualSize)
    require(not any(original_pdata[pdata_end:]),
            "Nonzero data after helper exception table would be overwritten")

    file_alignment = base.OPTIONAL_HEADER.FileAlignment
    section_alignment = base.OPTIONAL_HEADER.SectionAlignment
    table_offset = base.sections[0].get_file_offset()
    table_end = table_offset + 40 * len(base.sections)
    new_table_end = table_end + 40 * len(helper.sections)
    first_raw = min((section.PointerToRawData for section in base.sections if section.SizeOfRawData),
                    default=base.OPTIONAL_HEADER.SizeOfHeaders)
    require(new_table_end <= min(first_raw, base.OPTIONAL_HEADER.SizeOfHeaders),
            "Insufficient PE header slack for helper section headers")
    require(not any(base_bytes[table_end:new_table_end]),
            "New section headers would overwrite nonzero header bytes")

    output = bytearray(base_bytes)
    used_names = {section.Name.rstrip(b"\0") for section in base.sections}
    original_ranges = [(section.VirtualAddress,
                        align(section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData), section_alignment))
                       for section in base.sections]
    new_sections = []
    code_size = base.OPTIONAL_HEADER.SizeOfCode
    initialized_size = base.OPTIONAL_HEADER.SizeOfInitializedData
    uninitialized_size = base.OPTIONAL_HEADER.SizeOfUninitializedData
    image_end = base.OPTIONAL_HEADER.SizeOfImage

    for index, section in enumerate(helper.sections):
        virtual_address = section.VirtualAddress + rva_offset
        virtual_size = section.Misc_VirtualSize
        payload = bytearray(helper_bytes[section.PointerToRawData:section.PointerToRawData + section.SizeOfRawData]) if section.SizeOfRawData else bytearray()
        require(len(payload) == section.SizeOfRawData, "Truncated helper raw section")
        if section is pdata_section:
            virtual_size = max(virtual_size, pdata_relative + len(pdata_bytes))
            if len(payload) < virtual_size:
                payload.extend(b"\0" * (virtual_size - len(payload)))
            payload[pdata_relative:pdata_relative + len(pdata_bytes)] = pdata_bytes
        raw_size = align(len(payload), file_alignment) if payload else 0
        raw_pointer = align(len(output), file_alignment) if raw_size else 0
        mapped_end = align(virtual_address + max(virtual_size, raw_size), section_alignment)
        require(virtual_address % section_alignment == 0,
                "Helper RVA is not aligned to the base SectionAlignment")
        require(virtual_address >= base.OPTIONAL_HEADER.SizeOfHeaders,
                "Mapped helper section collides with base headers")
        require(all(mapped_end <= begin or virtual_address >= end for begin, end in original_ranges),
                f"Helper section at {virtual_address:#x} overlaps a base section")
        require(all(mapped_end <= previous["rva"] or virtual_address >= previous["mapped_end"]
                    for previous in new_sections), "Mapped helper sections overlap")
        require(mapped_end <= 0xFFFFFFFF, "Merged SizeOfImage exceeds 32 bits")
        if raw_size:
            output.extend(b"\0" * (raw_pointer - len(output)))
            output.extend(payload)
            output.extend(b"\0" * (raw_size - len(payload)))
        name = new_section_name(section, index, used_names)
        header = struct.pack("<8sIIIIIIHHI", name, virtual_size, virtual_address,
                             raw_size, raw_pointer, 0, 0, 0, 0, section.Characteristics)
        header_offset = table_end + 40 * index
        output[header_offset:header_offset + 40] = header
        if section.Characteristics & 0x20:
            code_size += raw_size
        if section.Characteristics & 0x40:
            initialized_size += raw_size
        if section.Characteristics & 0x80:
            uninitialized_size += virtual_size
        image_end = max(image_end, mapped_end)
        new_sections.append({"name": name.rstrip(b"\0").decode("ascii"), "source": section.Name.rstrip(b"\0").decode("ascii"),
                             "rva": virtual_address, "virtual_size": virtual_size,
                             "raw_offset": raw_pointer, "raw_size": raw_size,
                             "mapped_end": mapped_end})

    def put_field(structure, field: str, value: int, fmt: str = "<I") -> None:
        struct.pack_into(fmt, output, structure.get_field_absolute_offset(field), value)

    # The Windows SEC_IMAGE loader requires adjacent virtual sections. The fixed
    # helper base can leave a gap after Geek's final resource section. Extend
    # only the preceding section's zero-filled virtual tail to cover that gap.
    # This changes no original raw bytes and keeps every helper RVA unchanged.
    # NumberOfSections is not updated yet, so combine original and new headers.
    layout = [(section.VirtualAddress, section.Misc_VirtualSize, section.SizeOfRawData,
               section.get_field_absolute_offset("Misc_VirtualSize"), section.Name.rstrip(b"\0").decode("ascii"))
              for section in base.sections]
    layout.extend((section["rva"], section["virtual_size"], section["raw_size"],
                   table_end + index * 40 + 8, section["name"])
                  for index, section in enumerate(new_sections))
    require([item[0] for item in layout] == sorted(item[0] for item in layout),
            "Merged section headers are not in ascending virtual order")
    virtual_gap_fills = []
    for previous, following in zip(layout, layout[1:]):
        previous_rva, previous_size, previous_raw, size_field, name = previous
        expected_next = align(previous_rva + max(previous_size, previous_raw), section_alignment)
        require(expected_next <= following[0], "Merged virtual sections overlap")
        if expected_next < following[0]:
            new_size = following[0] - previous_rva
            struct.pack_into("<I", output, size_field, new_size)
            virtual_gap_fills.append({"section": name, "old_virtual_size": previous_size,
                                      "new_virtual_size": new_size})

    put_field(base.FILE_HEADER, "NumberOfSections", len(base.sections) + len(helper.sections), "<H")
    put_field(base.OPTIONAL_HEADER, "SizeOfCode", code_size)
    put_field(base.OPTIONAL_HEADER, "SizeOfInitializedData", initialized_size)
    put_field(base.OPTIONAL_HEADER, "SizeOfUninitializedData", uninitialized_size)
    put_field(base.OPTIONAL_HEADER, "SizeOfImage", align(image_end, section_alignment))
    put_field(base.OPTIONAL_HEADER.DATA_DIRECTORY[3], "VirtualAddress", helper_exception.VirtualAddress + rva_offset)
    put_field(base.OPTIONAL_HEADER.DATA_DIRECTORY[3], "Size", len(pdata_bytes))
    put_field(base.OPTIONAL_HEADER, "CheckSum", 0)

    merged = pefile.PE(data=bytes(output))
    checksum = merged.generate_checksum()
    put_field(base.OPTIONAL_HEADER, "CheckSum", checksum)
    merged = pefile.PE(data=bytes(output))
    require(merged.verify_checksum(), "Merged checksum verification failed")
    require(runtime_functions(merged) == combined, "Merged exception table verification failed")
    for source, destination in zip(helper.sections, new_sections):
        if source is pdata_section:
            continue
        require(read_rva(merged, destination["rva"], source.SizeOfRawData) ==
                helper_bytes[source.PointerToRawData:source.PointerToRawData + source.SizeOfRawData],
                f"Helper section mapping mismatch: {destination['name']}")
    for section in base.sections:
        start, end = section.PointerToRawData, section.PointerToRawData + section.SizeOfRawData
        require(output[start:end] == base_bytes[start:end], "An original section was modified")
    original_raw_end = max((section.PointerToRawData + section.SizeOfRawData for section in base.sections), default=0)
    require(output[original_raw_end:len(base_bytes)] == base_bytes[original_raw_end:],
            "Original overlay/certificate bytes were modified")
    require(merged.OPTIONAL_HEADER.DATA_DIRECTORY[4].VirtualAddress == base.OPTIONAL_HEADER.DATA_DIRECTORY[4].VirtualAddress and
            merged.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size == base.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size,
            "Original certificate directory changed")

    symbols = {}
    if hasattr(helper, "DIRECTORY_ENTRY_EXPORT"):
        for symbol in helper.DIRECTORY_ENTRY_EXPORT.symbols:
            require(symbol.forwarder is None, "Forwarded helper exports are unsupported")
            require(symbol.name is not None, "Helper exports must have names")
            name = symbol.name.decode("ascii")
            require(helper.get_section_by_rva(symbol.address) is not None, f"Unmapped export {name}")
            symbols[name] = base.OPTIONAL_HEADER.ImageBase + rva_offset + symbol.address
    require(symbols, "Helper contains no named exports")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    symbols_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name(output_path.name + ".tmp")
    temporary.write_bytes(output)
    validate_windows_image(temporary)
    temporary.replace(output_path)
    symbols_path.write_text(json.dumps(symbols, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"output": str(output_path), "symbols": str(symbols_path),
            "base_size": len(base_bytes), "output_size": len(output),
            "image_base": hex(base.OPTIONAL_HEADER.ImageBase), "rva_offset": hex(rva_offset),
            "original_runtime_functions": len(base_records), "helper_runtime_functions": len(helper_records),
            "combined_runtime_functions": len(combined), "checksum": hex(checksum),
            "sections": new_sections, "virtual_gap_fills": virtual_gap_fills,
            "windows_image_loader_validated": sys.platform == "win32",
            "exports": {name: hex(value) for name, value in symbols.items()}}


def main() -> int:
    folder = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", nargs="?", type=Path, default=folder / "base-native.bin")
    parser.add_argument("helper", nargs="?", type=Path, default=folder / "native-ui.exe")
    parser.add_argument("output", nargs="?", type=Path, default=folder / "merged-native.bin")
    parser.add_argument("--symbols", type=Path, default=folder / "native-ui-symbols.json")
    parser.add_argument("--rva-offset", type=lambda value: int(value, 0), default=0x400000)
    args = parser.parse_args()
    try:
        print(json.dumps(merge(args.base, args.helper, args.output, args.symbols, args.rva_offset), indent=2))
    except (MergeError, OSError, pefile.PEFormatError) as exc:
        parser.exit(1, f"merge_helper: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
