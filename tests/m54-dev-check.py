# Unmodified signed CPython 3, examining a program GCC built on NaSeed.
import hashlib
import json
import os
import sqlite3
import subprocess
import sys
from pathlib import Path

project=Path("/tmp/m54-project")
expect=int(sys.argv[1])
assert expect in (7,9), expect
source=(project / "answer.c").read_bytes()
assert b"return "+str(expect).encode()+b";" in source
assert b"return 1;" not in source
assert (project / "main.o").stat().st_size>0
assert (project / "answer.o").stat().st_size>0
image=project / "app"
assert image.stat().st_size>0
assert image.read_bytes()[:4]==b"\x7fELF"

result=subprocess.run([str(image)],capture_output=True,text=True,check=True)
assert result.stdout == f"M54-APP answer={expect}\n",repr(result.stdout)
print(f"M54_PYTHON_EXEC_GUEST_COMPILED_{expect}_PASS",flush=True)

digest=hashlib.sha256(image.read_bytes()).hexdigest()
assert len(digest)==64
record={"answer":expect,"sha256":digest,"size":image.stat().st_size}
assert json.loads(json.dumps(record))==record
print(f"M54_PYTHON_HASH_JSON_{expect}_PASS",flush=True)

# SQLite is a second genuine app/library consuming output from guest GCC.
db=Path("/tmp/m54-build-history.sqlite")
con=sqlite3.connect(str(db))
con.execute("CREATE TABLE IF NOT EXISTS builds(answer INTEGER PRIMARY KEY, digest TEXT)")
con.execute("INSERT INTO builds VALUES (?,?)",(expect,digest))
con.commit()
assert con.execute("SELECT digest FROM builds WHERE answer=?",(expect,)).fetchone()[0]==digest
if expect==9:
    assert [row[0] for row in con.execute("SELECT answer FROM builds ORDER BY answer")]==[7,9]
con.close()
print(f"M54_PYTHON_SQLITE_GUEST_BUILD_{expect}_PASS",flush=True)

# Git and Python are independent processes inspecting the SAME tmpfs tree.
git=subprocess.run(["/usr/bin/git","-C",str(project),"status","--porcelain"],
                   capture_output=True,text=True,check=True)
assert "answer.c" in git.stdout, repr(git.stdout)
print(f"M54_PYTHON_GIT_WORKTREE_{expect}_PASS",flush=True)
