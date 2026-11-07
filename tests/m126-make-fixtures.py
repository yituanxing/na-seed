#!/usr/bin/env python3
"""Prepare all M126 GNU groff document and bibliography inputs on the host."""

from pathlib import Path
import sys


DOCUMENTS = {
    "basic.roff": b".nf\nM126DOCUMENTOPEN\nM126QUARTZTEXT\nM126DOCUMENTEND\n.fi\n",
    "blank.roff": b".nf\n.fi\n",
    "equation.roff": b".nf\nM126EQUATIONSTART\n.EQ\nx over y\n.EN\nM126EQUATIONEND\n.fi\n",
    "picture.roff": b".nf\nM126PICTURESTART\n.PS\nbox \"M126DIAGRAM\" width 1 height 0.5\n.PE\nM126PICTUREEND\n.fi\n",
    "table.roff": b".nf\nM126TABLESTART\n.TS\ntab(|);\nl l.\nM126PLANET|M126COUNT\nM126MARS|7\n.TE\nM126TABLEEND\n.fi\n",
    "include.roff": b"M126INCLUDEDLINE\n",
    "soelim.roff": b"M126BEFOREINCLUDE\n.so /tests/m126-include.roff\nM126AFTERINCLUDE\n",
    "utf8.roff": ".nf\nM126UNICODE café\n.fi\n".encode(),
    "references.bib": (
        b"%A Ada Kernel\n%T M126SNOWFLAKE Quantum System Design\n%D 2026\n"
        b"%K M126SNOWFLAKE\n\n"
        b"%A Grace Scheduler\n%T M126LANTERN Thread Scheduling\n%D 2025\n"
        b"%K M126LANTERN\n\n"
    ),
    "refer.roff": (
        b"M126CITATIONBEGIN\n.[\nM126SNOWFLAKE\n.]\n"
        b"M126CITATIONEND\n"
    ),
    "lookbib.query": b"M126SNOWFLAKE\n",
}


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m126-make-fixtures.py ROOTFS_TESTS_DIR")
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    for name, contents in DOCUMENTS.items():
        path = root / f"m126-{name}"
        path.write_bytes(contents)
        assert path.read_bytes() == contents
    assert len(DOCUMENTS) == 11
    print("[M126 HOST FIXTURES PASS] 11 deterministic roff/bib UTF-8 sources; no guest compilation")


if __name__ == "__main__":
    main()
