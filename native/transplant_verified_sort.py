"""Apply the tested sort callback to the desktop v38 image without replacing its UI code."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from merge_helper import align, pefile, runtime_functions, validate_windows_image

ROOT = Path(__file__).resolve().parent
BASE_SHA = "be59448ef76c66396b27b2058532d560a82af84ad0e5fe39e02529f6671d0747"
DONOR_SHA = "7cf70b1f5c17fc9fa227e146c2d5f1358d186c5d40ff83913e187257193ce192"
CALLBACK_OFFSET = 0x327E58
ORIGINAL_CALLBACK = 0x140019970


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build(base_file: Path, donor_file: Path, output: Path) -> dict:
    output = output.resolve()
    if not output.is_relative_to(ROOT) or output.exists():
        raise ValueError("Write a fresh candidate beneath native-ui-tools")
    original, donor_bytes = base_file.read_bytes(), donor_file.read_bytes()
    if sha(original) != BASE_SHA or sha(donor_bytes) != DONOR_SHA:
        raise ValueError("Expected the exact verified v38 and sort donor inputs")
    image = bytearray(original)
    base, donor = pefile.PE(data=original), pefile.PE(data=donor_bytes)
    image_base = base.OPTIONAL_HEADER.ImageBase
    assert donor.OPTIONAL_HEADER.ImageBase == image_base
    assert struct.unpack_from("<Q", image, CALLBACK_OFFSET)[0] == ORIGINAL_CALLBACK
    donor_entry = struct.unpack_from("<Q", donor_bytes, CALLBACK_OFFSET)[0] - image_base
    begin, end, unwind = next(r for r in runtime_functions(donor) if r[0] == donor_entry)
    # This exact wrapper uses absolute legacy code/IAT addresses and internal
    # branches only, so its body can move without adjusting instructions.
    body = donor.get_data(begin, end - begin)
    header = donor.get_data(unwind, 4)
    assert len(header) == 4 and header[0] == 1
    unwind_info = donor.get_data(unwind, align(4 + header[2] * 2, 4))
    changes = []

    def write(offset: int, payload: bytes, name: str) -> None:
        image[offset:offset + len(payload)] = payload
        changes.append({"name": name, "offset": offset, "size": len(payload)})

    def put_int(owner, field: str, value: int) -> None:
        write(owner.get_field_absolute_offset(field), struct.pack("<I", value), field)

    def append(section_name: bytes, data: bytes, alignment: int, filler: int) -> int:
        section = next(s for s in base.sections if s.Name.rstrip(b"\0") == section_name)
        start = align(section.Misc_VirtualSize, alignment)
        finish = start + len(data)
        assert finish <= section.SizeOfRawData
        offset = section.PointerToRawData + start
        assert image[offset:offset + len(data)] == bytes([filler]) * len(data)
        write(offset, data, section_name.decode() + " padding")
        put_int(section, "Misc_VirtualSize", finish)
        return section.VirtualAddress + start

    new_code = append(b".uitext", body, 16, 0xCC)
    new_unwind = append(b".uirdata", unwind_info, 4, 0)
    directory = base.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    old_records = runtime_functions(base)
    new_record = new_code, new_code + len(body), new_unwind
    assert old_records[-1][1] <= new_code
    new_entry = append(b".uipdata", struct.pack("<III", *new_record), 4, 0)
    assert new_entry == directory.VirtualAddress + directory.Size
    put_int(directory, "Size", directory.Size + 12)
    write(CALLBACK_OFFSET, struct.pack("<Q", image_base + new_code), "reflected LVN_COLUMNCLICK callback")
    put_int(base.OPTIONAL_HEADER, "CheckSum", 0)
    put_int(base.OPTIONAL_HEADER, "CheckSum", pefile.PE(data=image).generate_checksum())

    checked = pefile.PE(data=image)
    assert checked.verify_checksum() and runtime_functions(checked) == old_records + [new_record]
    assert checked.get_data(new_code, len(body)) == body
    assert checked.get_data(new_unwind, len(unwind_info)) == unwind_info
    allowed = {i for row in changes for i in range(row["offset"], row["offset"] + row["size"])}
    assert len(image) == len(original)
    assert all(i in allowed for i, (old, new) in enumerate(zip(original, image)) if old != new)
    output.write_bytes(image)
    validate_windows_image(output)
    return {"path": str(output), "base_sha256": BASE_SHA, "donor_sha256": DONOR_SHA,
            "sha256": sha(image), "callback_address": hex(image_base + new_code),
            "changes": changes, "windows_image_loader_validated": True}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--base", type=Path, default=ROOT / "geek-ui-candidate-v38.exe")
    parser.add_argument("--donor", type=Path, default=ROOT / "geek-ui-candidate-sort-from-top.exe")
    args = parser.parse_args()
    print(json.dumps(build(args.base, args.donor, args.output), indent=2))


if __name__ == "__main__":
    main()
