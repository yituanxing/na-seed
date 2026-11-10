#!/usr/bin/env python3
"""Real QMP input -> upstream LVGL button -> client memfd -> QEMU pixels."""
import argparse
from pathlib import Path
import runpy
import sys
import time
base=runpy.run_path('scripts/m167-qmp-verify.py',run_name='m171_lib')
qmp=base['qmp']
BG=(16,32,48)
MAGENTA=(255,0,255)
TITLE=(240,168,40)

def shot(sock,reader,outdir,label,samples):
    dst=outdir/f'm171-{label}.ppm'
    end=time.monotonic()+12
    while True:
        dst.unlink(missing_ok=True)
        qmp['qmp_command'](sock,reader,'screendump',{'filename':str(dst.resolve())})
        try:
            qmp['verify_ppm'](dst,'M171 '+label,samples)
            return dst
        except RuntimeError:
            if time.monotonic()>=end:raise
            time.sleep(.2)

def pixels(path):
    tokens,data=qmp['ppm_tokens'](path.read_bytes())
    if tokens!=[b'P6',b'800',b'600',b'255'] or len(data)!=800*600*3:
        raise RuntimeError('not 800x600 real physical QEMU framebuffer')
    return data

def distinct_gui_pixels(path):
    data=pixels(path)
    colors=set()
    bright=0
    for y in range(399,558,3):
        for x in range(88,301,3):
            i=(y*800+x)*3
            value=tuple(data[i:i+3])
            colors.add(value)
            if all(c>215 for c in value):bright+=1
    if len(colors)<7 or bright<5:
        raise RuntimeError(f'actual LVGL text/button pixel variety absent: {len(colors)} colors, {bright} glyphs')

def difference(first,second):
    a,b=pixels(first),pixels(second)
    differences=0
    for y in range(435,527):
        left=(y*800+90)*3;right=(y*800+307)*3
        differences+=sum(x!=y for x,y in zip(a[left:right],b[left:right]))
    if differences<300:
        raise RuntimeError(f'LVGL click did not repaint button pixels ({differences} differences)')

def send_pointer(sock,reader,x,y,down):
    base['send'](sock,reader,base['pointer'](x,y))
    time.sleep(.6)
    base['send'](sock,reader,[base['button'](down)])

def press_key(sock,reader,key):
    base['send'](sock,reader,[base['key'](key,True)])

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--serial',required=True,type=Path)
    ap.add_argument('--qmp',required=True,type=Path)
    ap.add_argument('--outdir',required=True,type=Path)
    ap.add_argument('--timeout',type=int,default=700)
    args=ap.parse_args()
    deadline=time.monotonic()+args.timeout
    args.outdir.mkdir(parents=True,exist_ok=True)
    base['wait_marker'](args.serial,'M171_DESKTOP_INITIAL_VISIBLE',deadline)
    sock,reader=base['connect'](args.qmp,deadline)
    with sock:
        first=shot(sock,reader,args.outdir,'initial',
                   ((20,20,BG),(100,150,MAGENTA),(160,371,TITLE)))
        distinct_gui_pixels(first)
        # x=180 y=480 is inside the REAL lv_button in the fourth client.
        send_pointer(sock,reader,180,480,True)
        time.sleep(.35)
        base['send'](sock,reader,[base['button'](False)])
        base['wait_marker'](args.serial,'M171_LVGL_BUTTON_CLICK_VISIBLE',deadline)
        after=shot(sock,reader,args.outdir,'clicked',
                   ((20,20,BG),(100,150,MAGENTA),(160,371,TITLE)))
        distinct_gui_pixels(after)
        difference(first,after)
        # The client's own GUI window can now be retired without closing
        # the terminal, browser or image process.
        send_pointer(sock,reader,305,376,True)
        base['wait_marker'](args.serial,'M171_LVGL_CLOSED_VISIBLE',deadline)
        closed=shot(sock,reader,args.outdir,'closed',
                    ((20,20,BG),(100,150,MAGENTA),(150,470,BG)))
        base['send'](sock,reader,[base['button'](False)])
        time.sleep(.3)
        press_key(sock,reader,'q')
        base['wait_marker'](args.serial,'M171_DESKTOP_PASS',deadline)
    print('[M171 QMP LVGL PASS] three real QEMU frames, upstream LVGL GUI '
          'button, real pointer press/release, changed pixels and isolated GUI close',
          flush=True)

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('[M171 QMP FAIL]',exc,file=sys.stderr,flush=True)
        raise SystemExit(1)
