"""Real signed guest CPython independently checks Bash/Vim/Git artifacts."""
import hashlib
import json
import pathlib
import sqlite3
import sys

assert len(sys.argv) in (2, 3)
root = pathlib.Path(sys.argv[1])
text = (root / "items.txt").read_text(encoding="utf-8")
assert text == "alpha\nBETA\ngamma\n", repr(text)
copy = (root / "source-copy.txt").read_text(encoding="utf-8")
assert copy == "alpha\nbeta\ngamma\n"
patch = (root / "actual.patch").read_text(encoding="utf-8")
assert "-beta" in patch and "+BETA" in patch
lines = text.splitlines()
digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
expected = {"count": len(lines), "sha256": digest, "items": lines}
if len(sys.argv) == 3:
    assert json.loads((root / "results.json").read_text()) == expected
else:
    (root / "results.json").write_text(json.dumps(expected, sort_keys=True))
    with sqlite3.connect(":memory:") as db:
        db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
        db.executemany("INSERT INTO items (name) VALUES (?)", [(x,) for x in lines])
        rows = [row[0] for row in db.execute("SELECT name FROM items ORDER BY id")]
        assert rows == lines
print("M79_PYTHON_DATA_VALIDATED", digest)
