# Original CPython 3.12 interpreter and stdlib execute inside NaSeed QEMU.
import sys
assert sys.implementation.name == 'cpython'
assert sys.version_info[:2] == (3, 12)
assert sum(x*x for x in range(101)) == 338350
assert (2**130 >> 127) == 8
print("M52_PYTHON_BYTECODE_PASS", flush=True)

import json, hashlib, zlib
value = {'values':[1,2,3],'name':'NaSeed','ready':True}
text = json.dumps(value, sort_keys=True)
assert json.loads(text) == value
assert hashlib.sha256(b'NaSeed').hexdigest() == '897fe5c33c82f410b17dbbf95105957e66848f5477e2f6d3cefd4bf60cd2138e'
assert zlib.decompress(zlib.compress(text.encode())) == text.encode()
print("M52_PYTHON_STDLIB_JSON_HASH_ZLIB_PASS",flush=True)

from pathlib import Path
root=Path('/tmp/m52-python')
root.mkdir(exist_ok=True)
p=root/'result.txt'
p.write_text('alpha\nbeta\n', encoding='utf-8')
assert p.read_text(encoding='utf-8') == 'alpha\nbeta\n'
p.rename(root/'renamed.txt')
assert not p.exists()
assert (root/'renamed.txt').stat().st_size == 11
(root/'renamed.txt').unlink()
print("M52_PYTHON_TMPFS_PATHLIB_PASS",flush=True)

import sqlite3
db=root/'data.db'
db_con=sqlite3.connect(str(db))
db_con.execute('CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)')
db_con.executemany('INSERT INTO t(v) VALUES (?)',[('one',),('two',),('three',)])
db_con.commit()
db_con.close()
db_con=sqlite3.connect(str(db))
assert db_con.execute('SELECT v FROM t ORDER BY id').fetchall()==[('one',),('two',),('three',)]
db_con.close()
print("M52_PYTHON_SQLITE_FILE_PASS",flush=True)

import threading
out=[]
def work(n): out.append(n*n)
threads=[threading.Thread(target=work,args=(i,)) for i in range(4)]
for t in threads: t.start()
for t in threads: t.join()
assert sorted(out)==[0,1,4,9]
print("M52_PYTHON_PTHREAD_PASS",flush=True)

import subprocess
run=subprocess.run(['/bin/busybox','echo','python-child'],capture_output=True,text=True,check=True)
assert run.stdout=='python-child\n'
print("M52_PYTHON_SUBPROCESS_PASS",flush=True)
