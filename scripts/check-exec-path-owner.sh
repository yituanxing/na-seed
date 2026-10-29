#!/bin/sh
set -eu
mkdir -p build/exec-path-owner
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function -fno-builtin \
  -ffunction-sections -fdata-sections -I kernel -I scripts/m89-test-include \
  scripts/test-exec-path-owner-host.c -Wl,--gc-sections \
  -o build/exec-path-owner/test
build/exec-path-owner/test
# Fail closed on a future early syscall return accidentally bypassing the
# one source-owner cleanup. Before app_pinned=true no inode ref exists; after
# that point exactly one exec_fail handles all errors until process commit.
python3 - <<'PY'
from pathlib import Path
src=Path('kernel/task/exec.c').read_text()
body=src.split('uint64_t nas_task_execve(',1)[1]
owned=body.split('app_pinned=true;',1)[1].split('exec_fail:',1)[0]
returns=[line.strip() for line in owned.splitlines()
         if 'return ' in line and not line.lstrip().startswith('/*')
         and not line.lstrip().startswith('*')]
if returns != ['return nas_arch_saved_return(nas_current_task);']:
    raise SystemExit('[EXEC PATH OWNER FAIL] return bypasses shared unpin: '+repr(returns))
for token in ('app_pinned=false;', 'interpreter_pinned=false;',
              'exec_drop_image_sources(app_pinned?&app:NULL,',
              'goto exec_fail;'):
    if token not in body:
        raise SystemExit('[EXEC PATH OWNER FAIL] missing '+token)
print('[EXEC PATH OWNER STATIC PASS] no failure return escapes acquired inode lease')
PY
