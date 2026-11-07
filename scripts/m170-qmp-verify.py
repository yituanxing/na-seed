#!/usr/bin/env python3
"""Independent M170 physical QEMU oracle: two commands in one PTY, close two clients."""
import argparse
from pathlib import Path
import runpy
import sys
import time
base=runpy.run_path('scripts/m167-qmp-verify.py',run_name='m170_library')
qmp=base['qmp']
BG=(16,32,48)
IMAGE=(255,0,255)
BROWSER=(34,95,72)
TERM=(20,68,204)
TITLE=(240,168,40)

def snapshot(sock,reader,outdir,label,samples):
    path=outdir/f'm170-{label}.ppm'
    deadline=time.monotonic()+10
    while True:
        path.unlink(missing_ok=True)
        qmp['qmp_command'](sock,reader,'screendump',{'filename':str(path.resolve())})
        try:
            qmp['verify_ppm'](path,'M170 '+label,samples)
            return path
        except RuntimeError:
            if time.monotonic()>=deadline:raise
            time.sleep(.15)

def guest_key(sock,reader,name):
    base['send'](sock,reader,[base['key'](name,True)])
    base['send'](sock,reader,[base['key'](name,False)])
    time.sleep(.14)

def command(sock,reader,digit):
    for name in ('e','c','h','o','spc',digit,'ret'):
        guest_key(sock,reader,name)

def pixels(path):
    tokens,rgb=qmp['ppm_tokens'](path.read_bytes())
    if tokens!=[b'P6',b'800',b'600',b'255'] or len(rgb)!=800*600*3:
        raise RuntimeError('not a real 800x600 QEMU framebuffer')
    return rgb

def check_text(path):
    rgb=pixels(path)
    bright=0
    for y in range(210,390):
        for x in range(308,618):
            at=(y*800+x)*3
            if rgb[at]>=240 and rgb[at+1]>=240 and rgb[at+2]>=240:
                bright+=1
    if bright<40:raise RuntimeError('not enough visible real PTY text glyph pixels')

def check_changed(first,second):
    a,b=pixels(first),pixels(second)
    diff=0
    for y in range(210,390):
        left=(y*800+308)*3
        right=(y*800+618)*3
        diff+=sum(p!=q for p,q in zip(a[left:right],b[left:right]))
    if diff<25:raise RuntimeError('second real shell command did not repaint terminal text')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--serial',required=True,type=Path)
    ap.add_argument('--qmp',required=True,type=Path)
    ap.add_argument('--outdir',required=True,type=Path)
    ap.add_argument('--timeout',type=int,default=700)
    args=ap.parse_args()
    deadline=time.monotonic()+args.timeout
    args.outdir.mkdir(parents=True,exist_ok=True)
    base['wait_marker'](args.serial,'M170_DESKTOP_INITIAL_VISIBLE',deadline)
    sock,reader=base['connect'](args.qmp,deadline)
    with sock:
        snapshot(sock,reader,args.outdir,'initial',
                 ((20,20,BG),(100,150,IMAGE),(700,250,BROWSER),(400,300,TERM)))
        guest_key(sock,reader,'c')
        base['wait_marker'](args.serial,'M170_BROWSER_CLOSED_VISIBLE',deadline)
        snapshot(sock,reader,args.outdir,'browser-closed',
                 ((20,20,BG),(100,150,IMAGE),(700,250,BG),(500,172,TITLE)))
        base['send'](sock,reader,base['pointer'](550,370))
        time.sleep(.6)
        base['send'](sock,reader,[base['button'](True)])
        time.sleep(.5)
        base['send'](sock,reader,[base['button'](False)])
        time.sleep(.5)
        command(sock,reader,'1')
        base['wait_marker'](args.serial,'M170_FIRST_COMMAND_VISIBLE',deadline)
        first=snapshot(sock,reader,args.outdir,'terminal-first',
                ((20,20,BG),(700,250,BG),(500,172,TITLE)))
        check_text(first)
        command(sock,reader,'2')
        base['wait_marker'](args.serial,'M170_SECOND_COMMAND_VISIBLE',deadline)
        second=snapshot(sock,reader,args.outdir,'terminal-second',
                ((20,20,BG),(700,250,BG),(500,172,TITLE)))
        check_text(second)
        check_changed(first,second)
        base['send'](sock,reader,base['pointer'](620,170))
        time.sleep(.6)
        base['send'](sock,reader,[base['button'](True)])
        base['wait_marker'](args.serial,'M170_TERMINAL_CLOSED_VISIBLE',deadline)
        snapshot(sock,reader,args.outdir,'terminal-closed',
                 ((20,20,BG),(230,170,IMAGE),(500,250,BG),(700,250,BG)))
        base['send'](sock,reader,[base['button'](False)])
        time.sleep(.3)
        guest_key(sock,reader,'q')
        base['wait_marker'](args.serial,'M170_DESKTOP_PASS',deadline)
    print('[M170 QMP PERSISTENT SHELL PASS] five true hardware screenshots, '
          'two separate BusyBox shell commands and visible text changes, '
          'browser IPC close and terminal titlebar close',flush=True)
if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('[M170 QMP FAIL]',exc,file=sys.stderr,flush=True)
        raise SystemExit(1)
