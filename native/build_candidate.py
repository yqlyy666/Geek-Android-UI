import sys,struct,json,hashlib,ctypes,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parent
import pefile

def digest(b):return hashlib.sha256(b).hexdigest()
def patch_code(b,p,offset,expected,target):
    assert b[offset:offset+len(expected)]==expected,(hex(offset),b[offset:offset+len(expected)].hex())
    at=p.OPTIONAL_HEADER.ImageBase+p.get_rva_from_offset(offset)
    b[offset:offset+len(expected)]=b'\xe8'+struct.pack('<i',target-at-5)+b'\x90'*(len(expected)-5)
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',nargs='?',default='geek-ui-candidate.exe')
    parser.add_argument('--merged',type=Path,default=ROOT/'merged-native.bin')
    parser.add_argument('--symbols',type=Path,default=ROOT/'native-ui-symbols.json')
    args=parser.parse_args()
    b=bytearray(args.merged.read_bytes());p=pefile.PE(data=b)
    symbols=json.loads(args.symbols.read_text())
    # Accept readable report formats from the independent merge tool.
    symbols=symbols.get('symbols',symbols)
    symbols={k:(int(v,0) if isinstance(v,str) else v) for k,v in symbols.items()}
    changes=[]
    def ptr(offset,old,name):
        assert struct.unpack_from('<Q',b,offset)[0]==old,hex(offset)
        struct.pack_into('<Q',b,offset,symbols[name]);changes.append([hex(offset),name])
    for table in [0x25B568,0x2615C0]:
        ptr(table+0x2C8,0x1400176B4,'ui_draw_card')
        ptr(table+0xA0,0x14001A4C4,'ui_pre_subclass')
    if 'ui_about_init' in symbols:
        # Only CDlgAboutGeek::OnInitDialog. The wrapper calls the intact
        # original initializer before styling its existing modal HWND.
        ptr(0x257068,0x140004E08,'ui_about_init')
    if 'ui_scan_init' in symbols:
        # CWizardDlg::OnInitDialog for RT_DIALOG 219 (Scanning).
        ptr(0x262568,0x14005A80C,'ui_scan_init')
    ptr(0x327E18,0x140019904,'ui_list_measure')
    if 'ui_tooltip_text' in symbols:
        # Replace the native hover-text measurement entry with balanced DC
        # ownership while keeping its original notification and CString ABI.
        offset=0x18330
        assert b[offset:offset+5]==bytes.fromhex('4055535657')
        va=p.OPTIONAL_HEADER.ImageBase+p.get_rva_from_offset(offset)
        b[offset:offset+5]=b'\xe9'+struct.pack('<i',symbols['ui_tooltip_text']-va-5)
        changes.append([hex(offset),'ui_tooltip_text'])
    if 'ui_restore_main' in symbols:
        patch_code(b,p,0x2979C,bytes.fromhex('e8e7b52200'),symbols['ui_restore_main'])
    if 'ui_opaque_frame' in symbols:
        # Codejock's six-argument wrapper is also called after frame updates.
        # Preserve that ABI and keep every native glass extension at zero.
        patch_code(b,p,0x1557FE,bytes.fromhex('e8ed86fcff'),symbols['ui_opaque_frame'])
    if 'ui_menu_draw_opaque' in symbols:
        ptr(0x28B700,0x140110880,'ui_menu_draw_opaque')
    if 'ui_popup_draw_opaque' in symbols:
        ptr(0x27B470,0x140116970,'ui_popup_draw_opaque')
    if 'ui_status_draw' in symbols:
        patch_code(b,p,0xFFF34,bytes.fromhex('e857f6ffff'),symbols['ui_status_draw'])
    # The native layout already computes the DPI-scaled search height in ESI.
    # Move the same search HWND to y=0 and start the same list HWND below it.
    # Their existing heights and child overlay/filter handlers remain native.
    for offset,old,new in [
        (0x3B12A,'4533c9','448bce'),
        (0x3B1EE,'442bce','4533c9'),
    ]:
        assert b[offset:offset+3]==bytes.fromhex(old),hex(offset)
        b[offset:offset+3]=bytes.fromhex(new)
    if 'ui_search_layout' in symbols:
        patch_code(b,p,0x395E7,bytes.fromhex('e8c41a0000'),symbols['ui_search_layout'])
        # Native dark-mode edit painting adds an obsolete square inner frame.
        # The new rounded non-client frame owns this border in both themes.
        assert b[0xF567:0xF56D]==bytes.fromhex('0f84d3000000')
        b[0xF567:0xF56D]=bytes.fromhex('e9d400000090')
    for offset in [0x1168D4,0x116AB7,0x1172A6]:
        patch_code(b,p,offset,bytes.fromhex('ff90a8010000'),symbols.get('ui_popup_panel_round',symbols['ui_round_popup']))
    patch_code(b,p,0x102BEA,bytes.fromhex('41ff92a0000000'),symbols['ui_menu_measure'])
    if 'ui_menubar_item' in symbols:
        patch_code(b,p,0x102C05,bytes.fromhex('41ff92a8000000'),symbols['ui_menubar_item'])
    if 'ui_popup_separator' in symbols:
        patch_code(b,p,0x10FDC9,bytes.fromhex('ff9008010000'),symbols['ui_popup_separator'])
    offset=0x10FF8E
    assert b[offset:offset+7]==bytes.fromhex('4d8b88d8000000')
    va=p.OPTIONAL_HEADER.ImageBase+p.get_rva_from_offset(offset)
    b[offset:offset+7]=b'\x4c\x8d\x0d'+struct.pack('<i',symbols['ui_menu_background']-va-7)
    patch_code(b,p,0x16CE43,bytes.fromhex('e8b80efbff'),symbols['ui_menu_highlight'])
    # This is the same highlight path when UxTheme is unavailable.
    old=bytes(b[0x16CE6B:0x16CE70]);assert old[0]==0xe8
    assert 0x140000000+p.get_rva_from_offset(0x16CE6B)+5+struct.unpack_from('<i',old,1)[0]==0x140109670
    patch_code(b,p,0x16CE6B,old,symbols['ui_menu_highlight_fallback'])
    if 'ui_popup_panel_borders' in symbols:
        offset=0x16B990
        assert b[offset:offset+6]==bytes.fromhex('40534883ec20')
        va=p.OPTIONAL_HEADER.ImageBase+p.get_rva_from_offset(offset)
        b[offset:offset+6]=b'\xe9'+struct.pack('<i',symbols['ui_popup_panel_borders']-va-5)+b'\x90'
    else:
        for offset in [0x16B9A5,0x16B9AF,0x16B9B6,0x16B9BD]:
            assert struct.unpack_from('<I',b,offset)[0]==3
            struct.pack_into('<I',b,offset,8)
    checksum=p.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum')
    struct.pack_into('<I',b,checksum,0)
    struct.pack_into('<I',b,checksum,pefile.PE(data=b).generate_checksum())
    out=ROOT/args.output if not Path(args.output).is_absolute() else Path(args.output);out.write_bytes(b)
    report={'sha256':digest(b),'size':len(b),'pointer_hooks':changes,'symbols':symbols}
    (ROOT/'candidate-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
