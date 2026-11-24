"""Check actual embedded scroll-render helper machine code without a UI."""
from pathlib import Path
import hashlib
import json
import struct
import sys
from verify_native_ui import Machine

ROOT=Path(__file__).resolve().parent
IMAGE=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'geek-ui-smooth-scroll-v1.exe'
image=IMAGE.read_bytes()
report=json.loads(IMAGE.with_suffix('.build.json').read_text(encoding='utf-8'))
assert hashlib.sha256(image).hexdigest()==report['sha256']
symbols={k:int(v,0) for k,v in report['symbols'].items()}
FONT=0x140412C20
COL=0x140412858

def font_case():
 m=Machine(image)
 created=[]
 def create():
  data=bytes(m.u.mem_read(m.arg(0),92))
  height,width,esc,orient,weight=struct.unpack_from('<5i',data)
  assert width==esc==orient==0
  assert data[20:28]==bytes([0,0,0,1,0,0,5,0])
  assert data[28:].decode('utf-16le').rstrip('\0')=='Segoe UI'
  created.append({'height':height,'weight':weight})
  return 0xF000+len(created)
 m.iat(0x140256358,'CreateFontIndirectW',create)
 m.write64(FONT-8,0x12345678)
 m.write64(FONT+8*24,0xABCDEF98)
 expected=[]
 for dpi in [96,120,144,168,192,216,240,288]:
  for height in [14,11]:
   value=m.run(symbols['ui_cached_card_font'],dpi,0xDEAD0000|height)
   expected.append({'height':-(height*dpi+48)//96,'weight':500 if height==14 else 400})
   # Python // rounds negative differently; the native expression rounds the
   # positive pixel count before negating it.
   expected[-1]['height']=-((height*dpi+48)//96)
   assert value==0xF000+len(expected)
   assert m.run(symbols['ui_cached_card_font'],dpi,height)==value
 assert created==expected and len(created)==16
 # Full-cache fallback reuses nearest DPI; no seventeenth allocation.
 assert m.run(symbols['ui_cached_card_font'],300,14)==0xF00F
 assert m.run(symbols['ui_cached_card_font'],95,11)==0xF002
 assert m.run(symbols['ui_cached_card_font'],144,0)==0xF006
 assert len(created)==16
 assert m.read64(FONT-8)==0x12345678 and m.read64(FONT+8*24)==0xABCDEF98
 return {'group':'font_cache','exact_dpi_pairs':8,'repeated_allocations':0,
         'bounded_gdi_fonts':16,'guards_intact':True,'passed':True}

def columns_case(mode):
 m=Machine(image)
 row=m.scratch+0x10000;out=m.scratch+0x11000
 m.u.mem_write(row,struct.pack('<4i',0,-63,1092,0))
 m.u.mem_write(out-8,b'\xA5'*68)
 values=[500,250,342]
 state=bytearray(88)
 struct.pack_into('<QQQ',state,0,m.self_ptr,m.hwnd,0x123456)
 struct.pack_into('<3i',state,24,*values)
 struct.pack_into('<3i',state,36,0,1,2)
 struct.pack_into('<i',state,64,1092)
 struct.pack_into('<4i',state,72,1,0,0,1)
 if mode=='busy':struct.pack_into('<i',state,76,1)
 if mode=='not_normalized':struct.pack_into('<i',state,84,0)
 if mode=='wrong_self':struct.pack_into('<Q',state,0,0xBAD000)
 if mode=='bad_sum':struct.pack_into('<i',state,24,499)
 if mode=='bad_viewport':m.u.mem_write(row+8,struct.pack('<i',1091))
 if mode=='nonzero_left':m.u.mem_write(row,struct.pack('<i',1))
 m.u.mem_write(COL,bytes(state))
 calls=[]
 def original():
  assert [m.arg(i) for i in range(4)]==[m.self_ptr,m.hwnd,row,out]
  calls.append(True)
  return 0
 m.intercept(0x140457000,'ColumnFallback',original)
 m.run(symbols['ui_fast_card_columns'],m.self_ptr,m.hwnd,row,out)
 if mode=='valid':
  actual=struct.unpack('<13i',m.u.mem_read(out,52))
  assert actual==(0,-63,500,0,500,-63,750,0,750,-63,1092,0,7)
  assert not calls
 else:
  assert calls==[True] and bytes(m.u.mem_read(out,52))==b'\xA5'*52
 assert bytes(m.u.mem_read(out-8,8))==b'\xA5'*8
 assert bytes(m.u.mem_read(out+52,8))==b'\xA5'*8
 return {'group':'columns','mode':mode,'passed':True}

cases=[font_case()]+[columns_case(mode) for mode in
 ['valid','busy','not_normalized','wrong_self','bad_sum','bad_viewport','nonzero_left']]
result={'sha256':report['sha256'],'cases':cases,'passed':True,'ui_opened':False}
output=IMAGE.with_suffix('.render-compiled.json')
output.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
