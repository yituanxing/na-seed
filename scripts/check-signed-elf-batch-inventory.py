#!/usr/bin/env python3
"""Static original signed-ELF identity checks for M157/M158/M159/M163/M165.

The 5 distinct signed guest case sets stay separate. Only their repeated
manifest, loop, label, unique SHA identity and marker validation is shared.
This is NEVER evidence of a successful native QEMU boot.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
COUNTS={157:30,158:30,159:30,163:33,165:30}

def entries(file):
    return [s.strip() for s in file.read_text().splitlines()
            if s.strip() and not s.startswith("#")]

def verify(n):
    profile=f"m{n}"
    manifest=ROOT/"scripts"/f"{profile}-original-elfs.txt"
    paths=entries(manifest)
    apps=[Path(p).name for p in paths]
    body=(ROOT/"tests"/f"{profile}-batch-run.sh").read_text()
    k=COUNTS[n]
    assert len(paths)==len(set(paths))==len(set(apps))==k
    assert all(x.startswith("usr/bin/") for x in paths)
    loop=re.search(r"^for app in (.*?); do$",body,re.M|re.S)
    assert loop, f"{profile} guest case loop missing"
    called=re.findall(r"[a-z0-9-]+",loop.group(1).replace("\\",""))
    if n==157:
        cases=re.findall(r"^      ([a-z0-9-]+)\)",body,re.M)
    elif n==159:
        cases=[name for group in re.findall(r"^      ([a-z0-9-|]+)\)",body,re.M)
               for name in group.split("|")]
    else:
        section=body.split('case "$app" in',1)[1]
        section=section.split("    esac",1)[0] if n==158 else section.split("      *)",1)[0]
        cases=[name for group in re.findall(r"^      ([a-z0-9|-]+)\)",section,re.M)
               for name in group.split("|") if n!=158 or name in apps]
    assert set(apps)==set(called)==set(cases), f"{profile} guest semantic cases drift"
    if n!=157: assert len(set(cases))==k
    if n==158: assert len(cases)==k
    marks=subprocess.check_output([sys.executable,"scripts/acceptance/profile_markers.py",
                                  profile],cwd=ROOT,text=True).splitlines()
    expected={f"M{n}_{a.upper().replace('-','_')}_PASS" for a in apps}
    expected.add(f"M{n}_BATCH_PASS")
    assert len(marks)==len(set(marks))==k+1 and set(marks)==expected
    for prev in sorted((ROOT/"scripts").glob("m[0-9]*-original-elfs.txt")):
        if prev==manifest:continue
        overlap=set(paths)&set(entries(prev))
        assert not overlap, f"{profile}: older ELF {prev}: {overlap}"

    if n==158:
        assert body.count('format_check "$encoder" "$encoded"')==1
        assert body.count('image_check "$decoded"')==1
    elif n==159:
        assert apps[:8]==["svn","svnadmin","svndumpfilter","svnfsfs",
                           "svnlook","svnmucc","svnrdump","svnversion"]
        assert apps[8:13]==["certutil","crlutil","modutil","pk12util","shlibsign"]
        assert len(apps[13:])==17
    elif n==163:
        assert 'image_check "$out" "$work"' in body
        for command in ("ppmtopict","picttoppm","ppmtowinicon","winicontoppm",
                        "/usr/bin/pamfile","/usr/bin/pamcut","/usr/bin/pamtable"):
            assert command in body
    elif n==165:
        for pair in (("pamtojpeg2k","jpeg2ktopam"),("pgmtosbig","sbigtopgm"),
                     ("pnmtojbig","jbigtopnm")):
            assert all(x in apps and x in body for x in pair)
        assert 'image_check "$out" "$work"' in body
        hook=(ROOT/"scripts/host-configure-alpine-fixtures.sh").read_text()
        overlay=(ROOT/"scripts/stage-alpine-prebuilt-overlay.sh").read_text()
        assert "m163|m165)" in hook
        assert "m163-batch|m165-batch|" in overlay
        assert 'sh scripts/host-configure-alpine-fixtures.sh "$port" "$root"' in overlay
    print(f"[M{n} STATIC PASS] {k} independent original ELF guest cases and markers")

if __name__=="__main__":
    args=sys.argv[1:]
    if args and (len(set(args))!=len(args) or
                 any(not re.fullmatch(r"m(?:157|158|159|163|165)",a) for a in args)):
        sys.exit("usage: check-signed-elf-batch-inventory.py [m157 ... m165]")
    for n in ([int(x[1:]) for x in args] if args else COUNTS):
        verify(n)
