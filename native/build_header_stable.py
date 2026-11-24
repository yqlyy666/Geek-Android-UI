"""Replace only list hooks, preserving the installed theme and scroll fixes."""
from pathlib import Path
import hashlib
import json
import struct
import sys

from merge_helper import align, checked_helper_unwind, pefile, require, runtime_functions, validate_windows_image

BASE_SHA="3e2de8f854a939b2862e0426560cb440872304619f904fdbedcb0314820eee7c"
HELPER_BASE=0x140462000

def build(source,helper_path,output):
    original=source.read_bytes()
    require(hashlib.sha256(original).hexdigest()==BASE_SHA,"Unexpected installed base")
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
    for at,old,symbol in ((0x25B608,0x14045C000,"ui_pre_subclass"),
                          (0x261660,0x14045C000,"ui_pre_subclass"),
                          (0x25B830,0x14045CB10,"ui_draw_card"),
                          (0x261888,0x14045CB10,"ui_draw_card")):
        require(struct.unpack_from("<Q",original,at)[0]==old,f"Unexpected list callback {at:#x}")
        put(at,struct.pack("<Q",symbols[symbol]))
    field(base.OPTIONAL_HEADER,"CheckSum",0)
    field(base.OPTIONAL_HEADER,"CheckSum",pefile.PE(data=image).generate_checksum())
    checked=pefile.PE(data=image)
    require(checked.verify_checksum() and runtime_functions(checked)==combined,"Invalid PE checksum/unwind")
    for s in (text,rdata):
        require(checked.get_data(offset+s.VirtualAddress,s.SizeOfRawData)==helper.get_data(s.VirtualAddress,s.SizeOfRawData),"Helper mapping mismatch")
    require(all(n in changed or a==b for n,(a,b) in enumerate(zip(original,image))),"Unrequested original image edits")
    for at,size in ((0x327E58,8),(0x2979C,5),(0x328088,8),(0x3280C8,8),(0x328108,8),(0x3F6BC4,5)):
        require(image[at:at+size]==original[at:at+size],"Theme/sort/columns patch changed")
    output.write_bytes(image);validate_windows_image(output)
    inherited=json.loads(source.with_suffix(".build.json").read_text(encoding="utf-8")).get("symbols",{})
    all_symbols={**inherited,**{n:hex(a) for n,a in symbols.items()}}
    report={"path":str(output),"base_sha256":BASE_SHA,"sha256":hashlib.sha256(image).hexdigest(),
            "size":len(image),"state_address":"0x140412b18","state_reserved_bytes":256,
            "symbols":all_symbols,"windows_loader_validated":True}
    output.with_suffix(".build.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))

if __name__=="__main__":build(Path(sys.argv[1]),Path(sys.argv[2]),Path(sys.argv[3]))
