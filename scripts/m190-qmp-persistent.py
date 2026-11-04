#!/usr/bin/env python3
"""Real hardware keyboard -> original Vim/Nano -> worker-owned ext4 /async.

No synthetic rendering or file data. The second call reads the identical disk
after a genuine kernel restart and then reopens it in an original editor.
"""
from pathlib import Path
import runpy
import time
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
    m188['changed_until'](sock,reader,outdir,'durable-nano-prompt',
                          edited,(309,360,615,410),40,12)
    prior['send_key'](sock,reader,'ret')
    time.sleep(.5)
    m189['ctrl'](sock,reader,'x')
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
