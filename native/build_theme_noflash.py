"""Replace only the theme transition hooks in the verified scroll-v6 image."""
from pathlib import Path
import hashlib
import json
import struct
import sys

from merge_helper import align, checked_helper_unwind, pefile, require, runtime_functions, validate_windows_image

BASE_SHA="abc7b38919289f3716ca7ec7dfce935329ca5be2704b6b6ce02f44d0582a0a20"
HELPER_BASE=0x14045F000

def build(source,helper_path,output):
    original=source.read_bytes()
    require(hashlib.sha256(original).hexdigest()==BASE_SHA,"Unexpected scroll-v6 base")
    require(output.resolve() not in (source.resolve(),helper_path.resolve()),"Use a separate candidate")
    base=pefile.PE(data=original);helper=pefile.PE(str(helper_path))
    require(helper.OPTIONAL_HEADER.ImageBase==HELPER_BASE,"Unexpected helper image base")
    require([s.Name.rstrip(b"\0") for s in helper.sections]==[b".text",b".rdata",b".pdata"],"Unexpected helper sections")
    for i in (1,5,9,10,11,12,13,14):
        d=helper.OPTIONAL_HEADER.DATA_DIRECTORY[i]
        require(not(d.VirtualAddress or d.Size),f"Unsupported helper directory {i}")
    image_base=base.OPTIONAL_HEADER.ImageBase;offset=HELPER_BASE-image_base
    text,rdata,pdata=helper.sections;anim=base.sections[-1]
    require(anim.Name.rstrip(b"\0")==b".uianim","Unexpected final section")
    require(offset+text.VirtualAddress==base.OPTIONAL_HEADER.SizeOfImage,"Helper code not adjacent")
    entries=runtime_functions(helper);checked_helper_unwind(helper,entries)
    adjusted=[tuple(n+offset for n in row) for row in entries]
    combined=runtime_functions(base)+adjusted
    require(all(a[1]<=b[0] for a,b in zip(combined,combined[1:])),"Unwind overlap")
    exception=base.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    table_rva=exception.VirtualAddress+exception.Size
    table=b"".join(struct.pack("<III",*row) for row in adjusted)
    table_at=anim.PointerToRawData+table_rva-anim.VirtualAddress
    require(table_rva+len(table)<=0x457000,"Unwind entries collide with column code")
    require(not any(original[table_at:table_at+len(table)]),"Unwind tail occupied")
    image=bytearray(original);changed=set()
    def put(at,payload):
        end=at+len(payload)
        if end>len(image):image.extend(b"\0"*(end-len(image)))
        image[at:end]=payload;changed.update(range(at,min(end,len(original))))
    def field(owner,name,value):put(owner.get_field_absolute_offset(name),struct.pack("<I",value))
    max_end=base.OPTIONAL_HEADER.SizeOfImage
    for s in (text,rdata):
        rva=offset+s.VirtualAddress;at=anim.PointerToRawData+rva-anim.VirtualAddress
        put(at,helper.get_data(s.VirtualAddress,s.SizeOfRawData))
        max_end=max(max_end,rva+max(s.SizeOfRawData,s.Misc_VirtualSize))
    put(table_at,table)
    raw_size=align(max_end-anim.VirtualAddress,base.OPTIONAL_HEADER.FileAlignment)
    end=anim.PointerToRawData+raw_size
    if len(image)<end:image.extend(b"\0"*(end-len(image)))
    field(anim,"Misc_VirtualSize",max_end-anim.VirtualAddress);field(anim,"SizeOfRawData",raw_size)
    field(base.OPTIONAL_HEADER,"SizeOfImage",align(max_end,base.OPTIONAL_HEADER.SectionAlignment))
    field(base.OPTIONAL_HEADER,"SizeOfCode",base.OPTIONAL_HEADER.SizeOfCode+raw_size-anim.SizeOfRawData)
    field(exception,"Size",exception.Size+len(table))
    symbols={s.name.decode():HELPER_BASE+s.address for s in helper.DIRECTORY_ENTRY_EXPORT.symbols}
    at=0x2979C;va=image_base+base.get_rva_from_offset(at)
    old=original[at:at+5]
    require(old[0]==0xE8 and va+5+struct.unpack_from("<i",old,1)[0]==0x14045A610,"Unexpected restore hook")
    put(at,b"\xe8"+struct.pack("<i",symbols["ui_restore_main"]-va-5))
    for at,old,symbol in ((0x328088,0x14045A000,"ui_theme_light"),
                          (0x3280C8,0x14045A5D0,"ui_theme_dark"),
                          (0x328108,0x14045A5F0,"ui_theme_auto")):
        require(struct.unpack_from("<Q",original,at)[0]==old,f"Unexpected theme callback {at:#x}")
        put(at,struct.pack("<Q",symbols[symbol]))
    field(base.OPTIONAL_HEADER,"CheckSum",0)
    field(base.OPTIONAL_HEADER,"CheckSum",pefile.PE(data=image).generate_checksum())
    checked=pefile.PE(data=image)
    require(checked.verify_checksum() and runtime_functions(checked)==combined,"Invalid PE checksum/unwind")
    for s in (text,rdata):
        require(checked.get_data(offset+s.VirtualAddress,s.SizeOfRawData)==helper.get_data(s.VirtualAddress,s.SizeOfRawData),"Helper mapping mismatch")
    require(all(n in changed or a==b for n,(a,b) in enumerate(zip(original,image))),"Unrequested original image edits")
    for at,size in ((0x327E58,8),(0x25B608,8),(0x261660,8),(0x25B830,8),(0x261888,8),(0x3F6BC4,5)):
        require(image[at:at+size]==original[at:at+size],"Scroll/sort/columns patch changed")
    output.write_bytes(image);validate_windows_image(output)
    inherited=json.loads(source.with_suffix(".build.json").read_text(encoding="utf-8")).get("symbols",{})
    all_symbols={**inherited,**{n:hex(a) for n,a in symbols.items()}}
    report={"path":str(output),"base_sha256":BASE_SHA,"sha256":hashlib.sha256(image).hexdigest(),
            "size":len(image),"state_address":"0x140412b18","state_reserved_bytes":256,
            "symbols":all_symbols,"windows_loader_validated":True}
    output.with_suffix(".build.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))

if __name__=="__main__":build(Path(sys.argv[1]),Path(sys.argv[2]),Path(sys.argv[3]))
