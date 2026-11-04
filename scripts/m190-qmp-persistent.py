#!/usr/bin/env python3
"""Real hardware keyboard -> original Vim/Nano -> worker-owned ext4 /async.

No synthetic rendering or file data. The second call reads the identical disk
after a genuine kernel restart and then reopens it in an original editor.
"""
from pathlib import Path
import runpy
import time
import re

def physical_terminal_text(sock,reader,outdir,phase):
    """Decode only NaSeed's OWN fixed 5x7 font from the real QEMU screendump.

    Diagnostic, not a success oracle. The original guest uses exact screen
    cells from user/desktop/naseed-terminal.c, not arbitrary OCR.
    """
    shot=m189['frame'](sock,reader,outdir,'nano-'+phase+'-diagnostic')
    data=ui['pixels'](shot)
    def rgb(x,y):
        index=(y*800+x)*3
        return data[index:index+3]
    atlas={}
    text=Path('user/desktop/text.c').read_text()
    patterns=re.findall(r"\{\s*'([^']+)'\s*,\s*\{([^}]*)\}\s*\}",text)
    for raw,nums in patterns:
        if len(raw)==1:
            vals=[int(i) for i in re.findall(r'\d+',nums)]
            if len(vals)==7:
                atlas[tuple(vals)]=raw
    terminal=Path('user/desktop/naseed-terminal.c').read_text()
    lower=terminal.split('static const uint8_t lower[26][7]={',1)[1].split('};',1)[0]
    rows=re.findall(r'\{([^{}]+)\}',lower)
    for idx,nums in enumerate(rows):
        vals=[int(i) for i in re.findall(r'\d+',nums)]
        if len(vals)==7:
            atlas[tuple(vals)]=chr(ord('a')+idx)
    # Selected punctuation not available in the original text.c glyph list.
    extra=terminal.split('static const struct extra punctuation[]={',1)[1].split('};',1)[0]
    for raw,nums in re.findall(r"\{\s*'([^']+)'\s*,\s*\{([^}]*)\}\s*\}",extra):
        if len(raw)==1:
            vals=[int(i) for i in re.findall(r'\d+',nums)]
            if len(vals)==7:
                atlas[tuple(vals)]=raw
    print('[M190 SCREEN DIAG] '+phase+' (real 800x600 QEMU screendump)',flush=True)
    for row in range(25):
        chars=[]
        for col in range(53):
            x=304+col*6
            y=189+row*9
            bg=rgb(x+5,y+3)
            glyph=tuple(sum((1<<(4-k)) for k in range(5)
                            if rgb(x+k,y+j)!=bg) for j in range(7))
            chars.append(' ' if not any(glyph) else atlas.get(glyph,'?'))
        if row>=16:
            print(f'[M190 VT ROW {row:02d}] '+''.join(chars).rstrip(),flush=True)

ui=runpy.run_path('scripts/m173-qmp-verify.py',run_name='m190_screen')
prior=runpy.run_path('scripts/m187-qmp-verify.py',run_name='m190_prior')
m188=runpy.run_path('scripts/m188-qmp-extras.py',run_name='m190_input')
m189=runpy.run_path('scripts/m189-qmp-sequence.py',run_name='m190_process')
base=runpy.run_path('scripts/m167-qmp-verify.py',run_name='m190_qmp')

def mount_ready(sock,reader,serial):
    prior['wait_native_marker'](
        sock,reader,serial,'[M180 FS USER IRQ] remounted /async',
        time.monotonic()+40)

def check_contents(sock,reader,serial):
    m188['keys'](sock,reader,'sh /tests/m190-durable-check.sh')
    prior['send_key'](sock,reader,'ret')
    prior['wait_native_marker'](
        sock,reader,serial,'M190_DURABLE_SAVED_FUNCTION_PASS',
        time.monotonic()+30)

def vim_close(sock,reader):
    base['send'](sock,reader,[base['key']('shift',True)])
    prior['send_key'](sock,reader,'semicolon')
    base['send'](sock,reader,[base['key']('shift',False)])
    m188['keys'](sock,reader,'wq')
    prior['send_key'](sock,reader,'ret')

def nano_save_finished(sock,reader,outdir,prompt,seconds):
    """Wait for the real Nano Save-As prompt to disappear and screen settle.

    On IRQ-backed ext4, a user syscall parks while the filesystem worker
    completes block DMA; the fixed tmpfs delay used by M189 is insufficient.
    A Ctrl+X typed while Nano still writes can be consumed before the editor
    resumes input. Two actual stable QEMU frames are required, not a timer.
    """
    previous=None
    stable=0
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        shot=m189['frame'](sock,reader,outdir,'nano-durable-saved')
        data=ui['pixels'](shot)
        try:
            ui['changed'](prompt,shot,309,350,615,410,50)
        except RuntimeError:
            stable=0
        else:
            if previous is not None:
                delta=0
                for y in range(350,410):
                    left=(y*800+309)*3
                    right=(y*800+615)*3
                    delta+=sum(a!=b for a,b in
                               zip(previous[left:right],data[left:right]))
                stable=stable+1 if delta<=20 else 0
                if stable>=2:
                    return shot
        previous=data
        time.sleep(.4)
    prior['fail_native'](
        sock,reader,'original Nano save prompt never returned to a '
                    'stable physical editor after IRQ-backed ext4 write')


def write(sock,reader,serial:Path,outdir:Path):
    mount_ready(sock,reader,serial)
    before=m189['frame'](sock,reader,outdir,'before-durable-vim')
    after=len(serial.read_bytes())
    m188['keys'](sock,reader,'/usr/bin/vim /async/m190-editor.txt')
    prior['send_key'](sock,reader,'ret')
    pid,started=m189['process_started'](sock,reader,serial,'/usr/bin/vim',after,35)
    frame=m189['settled_editor'](sock,reader,outdir,before,'durable-vim',25)
    prior['send_key'](sock,reader,'i')
    m188['keys'](sock,reader,'m190vimoriginal')
    m188['changed_until'](sock,reader,outdir,'durable-vim-typed',
                          frame,(309,199,615,410),65,15)
    prior['send_key'](sock,reader,'esc')
    time.sleep(.6)
    vim_close(sock,reader)
    m189['process_reaped'](sock,reader,serial,'durable Vim',pid,started,35)
    prior['shell_frame_ready'](sock,reader,outdir,frame,time.monotonic()+20)
    # A separate native Linux ABI diagnostic checks the PREEXISTING inode.
    # Its target open is nonmutating: original Nano still does the real save.
    probe_start=len(serial.read_bytes())
    m188['keys'](sock,reader,'/usr/bin/naseed-m190-open-probe')
    prior['send_key'](sock,reader,'ret')
    probe_pid,probe_exec=m189['process_started'](
        sock,reader,serial,'/usr/bin/naseed-m190-open-probe',
        probe_start,35)
    m189['process_reaped'](
        sock,reader,serial,'M190 original existing-inode ABI probe',
        probe_pid,probe_exec,30)
    prior['wait_native_marker'](
        sock,reader,serial,'M190_VECTORED_IRQ_FS_FUNCTION_PASS',
        time.monotonic()+15)
    physical_terminal_text(sock,reader,outdir,'open-abi-probe')
    before=m189['frame'](sock,reader,outdir,'before-durable-nano')
    after=len(serial.read_bytes())
    m188['keys'](sock,reader,'/usr/bin/nano /async/m190-editor.txt')
    prior['send_key'](sock,reader,'ret')
    pid,started=m189['process_started'](sock,reader,serial,'/usr/bin/nano',after,35)
    frame=m189['settled_editor'](sock,reader,outdir,before,'durable-nano',25)
    prior['send_key'](sock,reader,'end')
    prior['send_key'](sock,reader,'ret')
    m188['keys'](sock,reader,'m190nanopersist')
    edited=m188['changed_until'](sock,reader,outdir,'durable-nano-typed',
                                 frame,(309,199,615,410),100,15)
    m189['ctrl'](sock,reader,'o')
    prompted=m188['changed_until'](
        sock,reader,outdir,'durable-nano-prompt',
        edited,(309,360,615,410),40,12)
    prior['send_key'](sock,reader,'ret')
    nano_save_finished(sock,reader,outdir,prompted,30)
    physical_terminal_text(sock,reader,outdir,'after-save')
    m189['ctrl'](sock,reader,'x')
    time.sleep(.9)
    physical_terminal_text(sock,reader,outdir,'after-ctrlx')
    m189['process_reaped'](sock,reader,serial,'durable Nano',pid,started,35)
    prior['shell_frame_ready'](sock,reader,outdir,edited,time.monotonic()+20)
    check_contents(sock,reader,serial)
    m189['frame'](sock,reader,outdir,'durable-written')

def reopen(sock,reader,serial:Path,outdir:Path):
    mount_ready(sock,reader,serial)
    # The new kernel may not silently recreate the expected file.
    check_contents(sock,reader,serial)
    before=m189['frame'](sock,reader,outdir,'before-reboot-vim')
    after=len(serial.read_bytes())
    m188['keys'](sock,reader,'/usr/bin/vim /async/m190-editor.txt')
    prior['send_key'](sock,reader,'ret')
    pid,started=m189['process_started'](sock,reader,serial,'/usr/bin/vim',after,35)
    frame=m189['settled_editor'](sock,reader,outdir,before,'reboot-vim',25)
    base['send'](sock,reader,[base['key']('shift',True)])
    prior['send_key'](sock,reader,'semicolon')
    base['send'](sock,reader,[base['key']('shift',False)])
    prior['send_key'](sock,reader,'q')
    prior['send_key'](sock,reader,'ret')
    m189['process_reaped'](sock,reader,serial,'reboot Vim',pid,started,35)
    prior['shell_frame_ready'](sock,reader,outdir,frame,time.monotonic()+20)
    m189['frame'](sock,reader,outdir,'durable-reopened')
