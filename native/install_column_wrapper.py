"""Install a small fixed-column wrapper into the existing animation section."""
from pathlib import Path
import hashlib, struct, sys
import pefile
from merge_helper import align, checked_helper_unwind, read_rva, require, runtime_functions, validate_windows_image

BASE_SHA = "fc14fd5af1abfdb021420435f162961f817ee98caf32d1b25bad741161886d5e"
HELPER_BASE = 0x140456000
IMAGE_BASE = 0x140000000
STATE_RVA = 0x412858
STATE_BYTES = 8 * 88

def build(base_path: Path, helper_path: Path, out_path: Path) -> None:
    original = base_path.read_bytes()
    require(hashlib.sha256(original).hexdigest() == BASE_SHA, "unexpected animated desktop base")
    image = bytearray(original)
    base = pefile.PE(data=original)
    helper = pefile.PE(str(helper_path))
    require(helper.OPTIONAL_HEADER.ImageBase == HELPER_BASE, "unexpected helper image base")
    require([s.Name.rstrip(b"\0") for s in helper.sections] == [b".text", b".rdata", b".pdata"],
            "column wrapper must be import-free code/rdata/pdata")
    for i in (1, 5, 9, 10, 11, 12, 13, 14):
        d = helper.OPTIONAL_HEADER.DATA_DIRECTORY[i]
        require(not (d.VirtualAddress or d.Size), f"unsupported helper directory {i}")
    anim = next(s for s in base.sections if s.Name.rstrip(b"\0") == b".uianim")
    helper_text = next(s for s in helper.sections if s.Name.rstrip(b"\0") == b".text")
    helper_rdata = next(s for s in helper.sections if s.Name.rstrip(b"\0") == b".rdata")
    helper_pdata = next(s for s in helper.sections if s.Name.rstrip(b"\0") == b".pdata")
    # Keep the old exception table in place and use its zero tail for the tiny
    # wrapper's 12-byte records. The wrapper code/rdata occupy the next slots.
    old_exception = base.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    old_end = old_exception.VirtualAddress + old_exception.Size
    assert old_end <= anim.VirtualAddress + anim.Misc_VirtualSize
    code_rva = anim.VirtualAddress + align(anim.Misc_VirtualSize, 0x1000)
    require(code_rva == HELPER_BASE - IMAGE_BASE + helper_text.VirtualAddress,
            "helper code linked at the wrong address")
    rdata_rva = code_rva + (helper_rdata.VirtualAddress - helper_text.VirtualAddress)
    pdata_rva = old_end + 0x0  # append records directly after the old table
    offset = HELPER_BASE - IMAGE_BASE
    adjusted = [tuple(value + offset for value in row) for row in runtime_functions(helper)]
    checked_helper_unwind(helper, runtime_functions(helper))
    table = b"".join(struct.pack("<III", *row) for row in adjusted)
    table_off = anim.PointerToRawData + (old_end - anim.VirtualAddress)
    code_off = anim.PointerToRawData + (code_rva - anim.VirtualAddress)
    rdata_off = anim.PointerToRawData + (rdata_rva - anim.VirtualAddress)
    require(table_off + len(table) <= code_off, "helper unwind table would overwrite code")
    require(not any(original[table_off:table_off+len(table)]), "unwind table tail is occupied")
    need_end = max(table_off + len(table),
                   code_off + helper_text.SizeOfRawData,
                   rdata_off + helper_rdata.SizeOfRawData)
    raw_end = anim.PointerToRawData + align(max(anim.SizeOfRawData, need_end - anim.PointerToRawData),
                                            base.OPTIONAL_HEADER.FileAlignment)
    if len(image) < raw_end:image.extend(b"\0" * (raw_end - len(image)))
    def put(at, payload): image[at:at + len(payload)] = payload
    put(code_off, helper.get_data(helper_text.VirtualAddress, helper_text.SizeOfRawData))
    put(rdata_off, helper.get_data(helper_rdata.VirtualAddress, helper_rdata.SizeOfRawData))
    put(table_off, table)
    # Extend the existing animation section in place; its code/read-only data
    # protection is sufficient for this wrapper (state lives in .uidata).
    new_virtual = max(anim.Misc_VirtualSize, need_end - anim.PointerToRawData)
    struct.pack_into("<I", image, anim.get_field_absolute_offset("Misc_VirtualSize"), new_virtual)
    struct.pack_into("<I", image, anim.get_field_absolute_offset("SizeOfRawData"), raw_end - anim.PointerToRawData)
    struct.pack_into("<I", image, base.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage"),
                     align(anim.VirtualAddress + new_virtual, base.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into("<I", image, base.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfCode"),
                     base.OPTIONAL_HEADER.SizeOfCode + (raw_end - anim.PointerToRawData - anim.SizeOfRawData))
    struct.pack_into("<I", image, old_exception.get_field_absolute_offset("Size"), old_exception.Size + len(table))
    # Reserve state after the animation helper's existing 0x400-byte array.
    uidata = next(s for s in base.sections if s.Name.rstrip(b"\0") == b".uidata")
    require(uidata.VirtualAddress + uidata.Misc_VirtualSize == STATE_RVA,
            "column state would overlap existing animation state")
    struct.pack_into("<I", image, uidata.get_field_absolute_offset("Misc_VirtualSize"),
                     uidata.Misc_VirtualSize + STATE_BYTES)
    # Redirect the existing card-column function and install the header hook
    # through both native list instances. The old code remains intact behind
    # the jump so all unrelated helpers stay byte-for-byte unchanged.
    exports = {s.name.decode(): HELPER_BASE + s.address for s in helper.DIRECTORY_ENTRY_EXPORT.symbols}
    column_offset = 0x3F6BC4
    column_va = IMAGE_BASE + base.get_rva_from_offset(column_offset)
    require(image[column_offset:column_offset+5] == bytes.fromhex("e8c7f8ffff"),
            "unexpected card-column call")
    put(column_offset, b"\xe8" + struct.pack("<i", exports["ui_card_columns"] - column_va - 5))
    for ptr in (0x25B608, 0x261660):
        old = struct.unpack_from("<Q", image, ptr)[0]
        require(old == 0x140402320, f"unexpected list init pointer at {ptr:#x}")
        struct.pack_into("<Q", image, ptr, exports["ui_pre_subclass"])
    checksum = base.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", image, checksum, 0)
    checked = pefile.PE(data=image)
    struct.pack_into("<I", image, checksum, checked.generate_checksum())
    checked = pefile.PE(data=image)
    require(checked.verify_checksum(), "checksum failed")
    require(runtime_functions(checked) == runtime_functions(base) + adjusted,
            "exception table mismatch")
    require(checked.get_data(HELPER_BASE-IMAGE_BASE+helper_text.VirtualAddress,
                             helper_text.SizeOfRawData) ==
            helper.get_data(helper_text.VirtualAddress,helper_text.SizeOfRawData),
            "helper code mapping mismatch")
    require(checked.get_data(HELPER_BASE-IMAGE_BASE+helper_rdata.VirtualAddress,
                             helper_rdata.SizeOfRawData) ==
            helper.get_data(helper_rdata.VirtualAddress,helper_rdata.SizeOfRawData),
            "helper rdata mapping mismatch")
    require(image[0x327E58:0x327E60] == original[0x327E58:0x327E60],
            "sort callback was modified")
    out_path.write_bytes(image)
    validate_windows_image(out_path)
    print(hashlib.sha256(image).hexdigest(), len(image), out_path)

if __name__ == "__main__":
    build(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
