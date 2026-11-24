"""Capture only an isolated Geek header during wheel scrolling, without input hooks.

Uses target-window DCs only, never desktop DCs, PrintWindow, activation, CUA or
global mouse/keyboard input. Frames are copied before analysis so template work
does not disturb the application's native commit timings.
"""
from __future__ import annotations
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('test_copy', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--theme', choices=('light', 'dark'), default='light')
    ap.add_argument('--cases', type=int, default=32)
    ap.add_argument('--sample-ms', type=float, default=.25)
    ap.add_argument('--assert-stable', action='store_true')
    ap.add_argument('--state-size', type=int, default=720)
    ap.add_argument('--body-strip', action='store_true')
    args = ap.parse_args()
    root = Path(__file__).resolve().parent
    test, output = args.test_copy.resolve(strict=True), args.output.resolve()
    meta = json.loads(test.with_suffix('.test-copy.json').read_text(encoding='utf-8'))
    assert test.is_relative_to(root) and output.is_relative_to(root)
    assert meta['isolated_instance'] and meta['execution_level'] == 'asInvoker' and not meta['ui_access']
    assert hashlib.sha256(test.read_bytes()).hexdigest() == meta['test_sha256']
    u, k, g = [c.WinDLL(n, use_last_error=True) for n in ('user32','kernel32','gdi32')]
    def fn(mod, name, res, *ps):
        f = getattr(mod,name); f.restype=res; f.argtypes=ps; return f
    CB=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    pidof=fn(u,'GetWindowThreadProcessId',w.DWORD,w.HWND,c.POINTER(w.DWORD))
    enum=fn(u,'EnumWindows',w.BOOL,CB,w.LPARAM)
    children=fn(u,'EnumChildWindows',w.BOOL,w.HWND,CB,w.LPARAM)
    cls=fn(u,'GetClassNameW',c.c_int,w.HWND,w.LPWSTR,c.c_int)
    visible=fn(u,'IsWindowVisible',w.BOOL,w.HWND)
    rect=fn(u,'GetClientRect',w.BOOL,w.HWND,c.POINTER(w.RECT))
    mappts=fn(u,'MapWindowPoints',c.c_int,w.HWND,w.HWND,c.POINTER(w.POINT),w.UINT)
    sm=fn(u,'SendMessageTimeoutW',w.LPARAM,w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t))
    pm=fn(u,'PostMessageW',w.BOOL,w.HWND,w.UINT,w.WPARAM,w.LPARAM)
    getlong=fn(u,'GetWindowLongPtrW',c.c_ssize_t,w.HWND,c.c_int)
    getdpi=fn(u,'GetDpiForWindow',w.UINT,w.HWND)
    fn(u,'SetThreadDpiAwarenessContext',c.c_void_p,c.c_void_p)(c.c_void_p(-4))
    op=fn(k,'OpenProcess',w.HANDLE,w.DWORD,w.BOOL,w.DWORD)
    rpm=fn(k,'ReadProcessMemory',w.BOOL,w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t))
    close=fn(k,'CloseHandle',w.BOOL,w.HANDLE)
    getdc=fn(u,'GetDC',w.HDC,w.HWND)
    release=fn(u,'ReleaseDC',c.c_int,w.HWND,w.HDC)
    compdc=fn(g,'CreateCompatibleDC',w.HDC,w.HDC)
    select=fn(g,'SelectObject',w.HANDLE,w.HDC,w.HANDLE)
    deletedc=fn(g,'DeleteDC',w.BOOL,w.HDC)
    delete=fn(g,'DeleteObject',w.BOOL,w.HANDLE)
    blt=fn(g,'BitBlt',w.BOOL,w.HDC,c.c_int,c.c_int,c.c_int,c.c_int,w.HDC,c.c_int,c.c_int,w.DWORD)
    flush=fn(g,'GdiFlush',w.BOOL)
    dib=fn(g,'CreateDIBSection',w.HBITMAP,w.HDC,c.c_void_p,w.UINT,c.POINTER(c.c_void_p),w.HANDLE,w.DWORD)
    profile=root/('header-pixel-qa-profile-'+meta['test_sha256'][:8]);profile.mkdir(exist_ok=True)
    proc=subprocess.Popen([str(test)],cwd=profile,env=dict(os.environ,APPDATA=str(profile),LOCALAPPDATA=str(profile)))
    report={'test_copy':str(test),'test_sha256':meta['test_sha256'],'pid':proc.pid,'theme':args.theme,'cases':[]}
    handle=None; surfaces=[]
    def scoped(hwnd):
        p=w.DWORD();pidof(hwnd,c.byref(p));return p.value==proc.pid
    def wins(parent=0):
        result=[]
        @CB
        def visit(hwnd,_):
            if scoped(hwnd) and visible(hwnd):
                n=c.create_unicode_buffer(128);cls(hwnd,n,128);result.append((hwnd,n.value))
            return True
        if parent: children(parent,visit,0)
        else: enum(visit,0)
        return result
    def send(hwnd,msg,wp=0,lp=0):
        assert scoped(hwnd); r=c.c_size_t();assert sm(hwnd,msg,wp,lp,2,2500,c.byref(r));return c.c_ssize_t(r.value).value
    def post(hwnd,msg,wp=0,lp=0):
        assert scoped(hwnd) and pm(hwnd,msg,wp,lp)
    def read(addr,size):
        b=c.create_string_buffer(size);got=c.c_size_t();assert rpm(handle,addr,b,size,c.byref(got)) and got.value==size;return b.raw
    def state():
        p=struct.unpack('<Q',read(0x140412C18,8))[0];seen=set()
        while p and p not in seen:
            seen.add(p);raw=read(p,args.state_size)
            if struct.unpack_from('<Q',raw,8)[0]==lv:
                fields=[('active',96,'i'),('internal',104,'i'),('native_top',112,'i'),('frames',116,'i'),('commits',708,'I')]
                if args.state_size >= 736: fields += [('header',720,'Q'),('shielded_commits',728,'I'),('suppressed_header_redraw',732,'I')]
                return {n:struct.unpack_from('<'+f,raw,o)[0] for n,o,f in fields}
            p=struct.unpack_from('<Q',raw,16)[0]
        return None
    def surface(hwnd,origin,width,height,label):
        dc=getdc(hwnd);mem=compdc(dc);bits=c.c_void_p()
        info=struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0)
        bitmap=dib(dc,c.c_char_p(info),0,c.byref(bits),None,0);assert dc and mem and bitmap and bits.value
        prev=select(mem,bitmap)
        s={'hwnd':hwnd,'dc':dc,'mem':mem,'bitmap':bitmap,'prev':prev,'bits':bits.value,'width':width,'height':height,'origin':origin,'label':label}
        surfaces.append(s);return s
    def capture(s):
        # Sentinel pixels distinguish clipping/unwritten output from real black.
        array=np.ctypeslib.as_array((c.c_uint8*(s['width']*s['height']*4)).from_address(s['bits'])).reshape(s['height'],s['width'],4)
        array[:]=[211,19,181,255]
        ok=blt(s['mem'],0,0,s['width'],s['height'],s['dc'],*s['origin'],0x00CC0020);flush()
        return bool(ok),array[:,:,:3].copy()
    try:
        for _ in range(240):
            targets=[(h,ch) for h,_ in wins() for ch,cl in wins(h) if cl=='SysListView32']
            if len(targets)==1:
                main,lv=targets[0]
                if send(lv,0x1004)>5: break
            assert proc.poll() is None;time.sleep(.05)
        else: raise RuntimeError('list did not load')
        header=send(lv,0x101F);assert header and scoped(header)
        for _ in range(3):
            post(main,0x111,0x809A if args.theme=='dark' else 0x8099);time.sleep(.6)
            color=send(lv,0x1000)&0xffffff
            if (sum((color>>(8*i))&255 for i in range(3))<384)==(args.theme=='dark'): break
        handle=op(0x10|0x400,False,proc.pid);assert handle
        hr=w.RECT();assert rect(header,c.byref(hr))
        width,height=hr.right,hr.bottom
        hp=w.POINT(0,0);mappts(header,main,c.byref(hp),1)
        lp=w.POINT(0,0);mappts(header,lv,c.byref(lp),1)
        surface(header,(0,0),width,height,'header')
        surface(lv,(lp.x,lp.y),width,height,'list-header-region')
        surface(main,(hp.x,hp.y),width,height,'main-header-region')
        if args.body_strip:
            lr=w.RECT();assert rect(lv,c.byref(lr))
            body_origin=w.POINT(65,height+3);mappts(lv,main,c.byref(body_origin),1)
            body_width,body_height=min(440,lr.right-80),lr.bottom-height-6
            surface(lv,(65,height+3),body_width,body_height,'list-body-strip')
            surface(main,(body_origin.x,body_origin.y),body_width,body_height,'main-body-strip')
        send(lv,0x115,6);time.sleep(.3)
        baselines=[capture(s)[1] for s in surfaces]
        def glyph_count(p):
            if args.theme=='light': return int(np.count_nonzero(np.max(p,axis=2)<100))
            return int(np.count_nonzero(np.min(p,axis=2)>160))
        baseline_glyphs=[glyph_count(b) for b in baselines]
        for s,b in zip(surfaces,baselines): Image.fromarray(b[:,:,::-1]).save(output.with_suffix('.'+s['label']+'.baseline.png'))
        report.update(header_hwnd=hex(header),list_hwnd=hex(lv),header_size=[width,height],window_dpi={n:getdpi(h) for n,h in [('main',main),('list',lv),('header',header)]},origins={'list':[lp.x,lp.y],'main':[hp.x,hp.y]},baseline_glyphs={s['label']:n for s,n in zip(surfaces,baseline_glyphs)})
        print(json.dumps({'pid':proc.pid,'header':[width,height],'surfaces':[s['label'] for s in surfaces]}),flush=True)
        anomalies=0; hidden=0;total=0
        for ci in range(args.cases):
            if ci%12==0: send(lv,0x115,6);time.sleep(.2)
            captures=[];start=time.perf_counter();post(lv,0x20A,((-120&65535)<<16),0)
            while time.perf_counter()-start<.26:
                before=state();v=[bool(visible(h)) for h in (main,lv,header)];style=[bool(getlong(h,-16)&0x10000000) for h in (lv,header)]
                frames=[capture(s) for s in surfaces];after=state()
                captures.append((round((time.perf_counter()-start)*1000,3),before,after,v,style,frames))
                if args.sample_ms: time.sleep(args.sample_ms/1000)
            records=[];badframes=[]
            for ix,(at,before,after,v,style,frames) in enumerate(captures):
                total+=1; hidden+=not all(v)
                changes=[]
                for s,b,(ok,p) in zip(surfaces,baselines,frames):
                    difference=np.max(abs(p.astype(np.int16)-b.astype(np.int16)),axis=2)
                    changes.append({'surface':s['label'],'ok':ok,'changed_pixels':int(np.count_nonzero(difference>8)),'mean_abs_diff':round(float(difference.mean()),4),'sentinel':int(np.count_nonzero(np.all(p==[211,19,181],axis=2))),'glyph_pixels':glyph_count(p)})
                # Parent/list DCs may intentionally clip children. Header DC
                # alone supplies the stable visible glyph test.
                bad=changes[0]['changed_pixels']>10 or not all(v)
                anomalies+=bad
                if bad and len(badframes)<4: badframes.append((ix,frames[0][1]))
                records.append({'ms':at,'before':before,'after':after,'visible':v,'style_visible':style,'pixels':changes})
            report['cases'].append({'index':ci,'samples':records,'anomalies':len([r for r in records if r['pixels'][0]['changed_pixels']>10 or not all(r['visible'])])})
            for ix,p in badframes: Image.fromarray(p[:,:,::-1]).save(output.with_suffix(f'.case{ci}.frame{ix}.png'))
            print(json.dumps({'case':ci,'samples':len(records),'anomalies':report['cases'][-1]['anomalies'],'commit':records[-1]['after']['commits']}),flush=True)
        report.update(sample_count=total,header_anomalies=anomalies,hidden_samples=hidden,complete=True)
        if args.assert_stable: assert not anomalies, f'{anomalies} header changes/hidden samples'
    finally:
        for s in surfaces:
            select(s['mem'],s['prev']);delete(s['bitmap']);deletedc(s['mem']);release(s['hwnd'],s['dc'])
        if handle: close(handle)
        if proc.poll() is None: proc.terminate();proc.wait(timeout=5)
        report['exit']=proc.returncode
        output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'output':str(output),'complete':report.get('complete',False),'anomalies':report.get('header_anomalies')}),flush=True)


if __name__=='__main__': main()
