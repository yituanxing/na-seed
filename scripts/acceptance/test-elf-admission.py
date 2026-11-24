#!/usr/bin/env python3
"""Host-only tests of actual admission/hash helpers (never APK/QEMU proof)."""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[2]
DIR = ROOT / "scripts/acceptance"
HISTORY = DIR / "check-elf-history.sh"
HASH = DIR / "hash-original-elfs.sh"

def run(script, *args, cwd, status=0):
    p = subprocess.run(["sh", str(script), *map(str, args)], cwd=cwd,
                       text=True, capture_output=True)
    assert p.returncode == status, (script, args, p.returncode, p.stderr)
    return p.stdout

with tempfile.TemporaryDirectory() as d:
    cwd = Path(d)
    (cwd / "scripts").mkdir()
    current = Path("scripts/m999-original-elfs.txt")
    other = Path("scripts/m998-original-elfs.txt")
    (cwd / current).write_text("# comment\n\nbin/a\nbin/b\n")
    (cwd / other).write_text("bin/c\n")
    assert run(HISTORY, "M999", current, cwd=cwd) == ""
    assert run(HISTORY, "M999", current, "--count", cwd=cwd) == "2\n"
    assert run(HISTORY, "M999", current, "--count", "--distinct", cwd=cwd) == "2\n"
    (cwd / other).write_text("bin/b\n")
    run(HISTORY, "M999", current, cwd=cwd, status=1)
    (cwd / other).write_text("bin/c\n")
    (cwd / current).write_text("bin/a\nbin/a\n")
    assert run(HISTORY, "M999", current, "--count", cwd=cwd) == "2\n"
    run(HISTORY, "M999", current, "--count", "--distinct", cwd=cwd, status=1)
    (cwd / current).write_text("bin/a\nbin/b\n")
    root = cwd / "root"
    (root / "bin").mkdir(parents=True)
    (root / "bin/a").write_bytes(b"a")
    (root / "bin/b").write_bytes(b"b")
    output = cwd / "sha"
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd)
    expected = subprocess.check_output(["sha256sum", str(root / "bin/a"),
                                        str(root / "bin/b")], text=True)
    assert output.read_text() == expected
    (root / "bin/b").write_bytes(b"a")
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd, status=1)
    run(HASH, root, current, output, 2, "count", "M999", cwd=cwd)
    run(HASH, root, current, output, 3, "count", "M999", cwd=cwd, status=1)
    (root / "bin/b").unlink()
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd, status=1)

    # Verify every stronger mode with real symlinks, executable permissions,
    # an outside-root target, a multicall alias, and ELF magic rejection.
    a, b = root / "bin/a", root / "bin/b"
    a.write_bytes(b"\x7fELFa")
    b.write_bytes(b"\x7fELFb")
    a.chmod(0o755)
    b.chmod(0o755)
    modes = ("alias-path", "alias-exec", "contained-exec",
             "contained-file", "contained-elf", "contained-exec-elf")
    for mode in modes:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd)
        assert output.read_text() == subprocess.check_output(
            ["sha256sum", str(a), str(b)], text=True)
    b.chmod(0o644)
    run(HASH, root, current, output, 2, "alias-path", "M999", cwd=cwd)
    for mode in modes[1:]:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.chmod(0o755)
    b.write_bytes(b"not ELF")
    run(HASH, root, current, output, 2, "contained-file", "M999", cwd=cwd)
    for mode in ("contained-elf", "contained-exec-elf"):
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.rename(root / "bin/busybox")
    b.symlink_to("busybox")
    for mode in modes:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.unlink()
    outside = cwd / "outside"
    outside.write_bytes(b"\x7fELFb")
    outside.chmod(0o755)
    b.symlink_to(outside)
    for mode in modes[2:]:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.unlink()
    real_b = root / "bin/real-b"
    real_b.write_bytes(b"\x7fELFb")
    real_b.chmod(0o755)
    b.symlink_to("real-b")
    run(HASH, root, current, output, 2, "alias-path", "M999", cwd=cwd)
    assert str(b) in output.read_text()
    run(HASH, root, current, output, 2, "contained-elf", "M999", cwd=cwd)
    assert str(real_b) in output.read_text()

# Verify all migrated shell sources parse and reference the original manifests.
# Compare historical admission against the original exact-line rule using
# the real repository manifests, including any pre-existing overlaps.
scripts = ROOT / "scripts"
manifests = {p.name: p.read_text().splitlines()
             for p in scripts.glob("m[0-9]*-original-elfs.txt")}
migrated = 0
for script in sorted(scripts.glob("stage-m*-prebuilt.sh")):
    body = script.read_text()
    if "scripts/acceptance/check-elf-history.sh" not in body and "scripts/acceptance/hash-original-elfs.sh" not in body:
        continue
    subprocess.run(["sh", "-n", str(script)], check=True)
    number = re.fullmatch(r"stage-m(\d+)-prebuilt.sh", script.name)[1]
    name = f"m{number}-original-elfs.txt"
    entries = [x for x in manifests[name] if x and not x.startswith("#")]
    if "scripts/acceptance/check-elf-history.sh" in body:
        call = next(x for x in body.splitlines() if "scripts/acceptance/check-elf-history.sh" in x)
        conflict = any(x in rows for x in entries for k, rows in manifests.items() if k != name)
        distinct = "--distinct" in call
        conflict = conflict or (distinct and len(entries) != len(set(entries)))
        args = ["--count"] if "--count" in call else []
        if distinct: args.append("--distinct")
        result = run(HISTORY, f"M{number}", "scripts/" + name, *args, cwd=ROOT, status=int(conflict))
        if not conflict and args: assert result == f"{len(entries)}\n"
    if "scripts/acceptance/hash-original-elfs.sh" in body:
        call = next(x for x in body.splitlines() if "scripts/acceptance/hash-original-elfs.sh" in x)
        assert "scripts/" + name in call or ('"$manifest"' in call and "manifest=scripts/" + name in body)
        expected = re.search(r' (\d+) (count|unique|alias-path|alias-exec|contained-exec|contained-file|contained-elf|contained-exec-elf) M\d+$', call)
        assert expected and int(expected[1]) == len(entries), (script, call, len(entries))
    migrated += 1
# Verify shared staging with the original per-profile counts and hash policy.
shared = scripts / "stage-alpine-signed-batch.sh"
subprocess.run(["sh", "-n", str(shared)], check=True)
profiles = {}
for names, count, mode in re.findall(r"^    (m\d+(?:\|m\d+)*)\) count=(\d+); mode=([\w-]+) ;;$", shared.read_text(), re.M):
    for name in names.split("|"):
        assert name not in profiles
        profiles[name] = (int(count), mode)
expected_profiles = {f"m{n}": (
    21 if n == 110 else 68 if n == 141 else 20 if n >= 136 else 15,
    "count" if n <= 115 else "unique" if n <= 123 else "alias-path" if n <= 130
    else "alias-exec" if n <= 138 else "contained-exec" if n <= 140 else "contained-file")
    for n in range(109, 142)}
expected_profiles.update({
    "m146": (24, "contained-elf"),
    "m148": (35, "contained-elf"),
    "m150": (26, "contained-elf"),
    "m151": (35, "contained-elf"),
    "m153": (36, "contained-elf"),
    "m154": (30, "contained-elf"),
    "m155": (33, "contained-elf"),
    "m157": (30, "contained-elf"),
    "m158": (30, "contained-elf"),
    "m159": (30, "contained-elf"),
    "m163": (33, "contained-exec-elf"),
    "m165": (30, "contained-exec-elf"),
})
assert profiles == expected_profiles
for profile, (count, mode) in profiles.items():
    entries = [x for x in manifests[profile + "-original-elfs.txt"] if x and not x.startswith("#")]
    assert len(entries) == count, (profile, count, len(entries))
    if int(profile[1:]) >= 121:
        name = profile + "-original-elfs.txt"
        conflict = any(x in rows for x in entries for k, rows in manifests.items() if k != name)
        args = ["--count"] if int(profile[1:]) >= 141 else []
        if profile in {"m150", "m151", "m153", "m155", "m157", "m158", "m159", "m163", "m165"}:
            args.append("--distinct")
            conflict = conflict or len(entries) != len(set(entries))
        result = run(HISTORY, profile.upper(), "scripts/" + name, *args, cwd=ROOT, status=int(conflict))
        if args and not conflict:
            assert result == f"{count}\n"

# Host-only synthetic overlay; the production shared stage and hash helpers run.
with tempfile.TemporaryDirectory() as d:
    import shutil
    cwd = Path(d)
    (cwd / "scripts/acceptance").mkdir(parents=True)
    shutil.copyfile(HASH, cwd / "scripts/acceptance/hash-original-elfs.sh")
    shutil.copyfile(HISTORY, cwd / "scripts/acceptance/check-elf-history.sh")
    (cwd / "scripts/stage-alpine-prebuilt-overlay.sh").write_text("#!/bin/sh\nexit 0\n")
    for profile, (count, mode) in profiles.items():
        work = cwd / "build/external" / (profile + "-batch") / "amd64"
        root = work / "rootfs"
        (root / "lib/apk/db").mkdir(parents=True)
        packages = {
            "m124": "util-linux-misc", "m125": "netpbm", "m126": "groff",
            "m127": "wavpack vorbis-tools speex-tools flac lame mpg123",
            "m128": "exfatprogs squashfs-tools squashfs-tools-ng blkid wipefs",
            "m129": "procps-ng psmisc util-linux-misc flock tmux",
            "m130": "netpbm binutils libarchive-tools zip attr acl e2fsprogs-extra",
            "m131": "netpbm",
            "m132": "icu libwebp-tools zopfli pcre2-tools bzip3",
            "m133": "graphviz xz diffutils ed mtools",
            "m134": "graphviz lua5.4 dash mawk xxd gperf ninja-build fish",
            "m135": "cmake libjpeg-turbo-utils tidyhtml gdbm-tools graphviz netpbm",
            "m136": "flex byacc re2c ncurses ncurses-terminfo-base util-linux-misc uuidgen findmnt flock lscpu",
            "m137": "lmdb-tools fontconfig font-dejavu protoc flatc swig ragel xorriso graphicsmagick mdbook rclone restic recode fdupes zint zbar",
            "m138": "llvm21",
            "m139": "fontconfig font-dejavu netpbm util-linux-misc mcookie gnutls-utils gnupg-gpgconf",
            "m140": "coreutils netpbm",
            "m146": "netpbm imagemagick php84 pax-utils scanelf",
            "m148": "netpbm",
            "m150": "lrzip par2cmdline pixz plzip sleuthkit",
            "m151": "libsndfile aubio heif-thumbnailer faad2 fdkaac audiofile rubberband soundtouch aom libvpx-utils libjxl-tools libheif-tools jasper netpbm",
            "m153": "binutils gcc ccache clang21-extra-tools radare2 pahole moreutils nasm yara bloaty cflow difftastic mold",
            "m154": "a2ps barcode chafa csview csvq enca enscript figlet gnuplot ghostscript html2text jql libxml2-utils mandoc pango-tools potrace psutils rsvg-convert taplo unrtf font-dejavu",
            "m155": "btrfs-progs btrfs-progs-extra xfsprogs xfsprogs-extra f2fs-tools erofs-utils ntfs-3g-progs gptfdisk parted",
            "m157": "netpbm giflib-utils tiff-tools libpng-utils",
            "m158": "netpbm",
            "m159": "subversion nss-tools netpbm",
            "m163": "netpbm",
            "m165": "netpbm",
        }.get(profile, "fixture").split()
        installed = root / "lib/apk/db/installed"
        installed.write_text("".join(f"P:{p}\n" for p in packages))
        (work / "rootfs.cpio").write_bytes(b"host fixture")
        rows = [f"usr/bin/{profile}-{i}" for i in range(count)]
        (root / "usr/bin").mkdir(parents=True)
        for row in rows:
            (root / row).write_bytes(b"\x7fELF" + row.encode())
            (root / row).chmod(0o755)
        (cwd / "scripts" / (profile + "-original-elfs.txt")).write_text("\n".join(rows) + "\n")
        fixtures = {
            "m118": "m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so",
            "m119": "m119-catalog.po m119-batch-run.sh m119-catalog.mo",
            "m120": "m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw",
            "m121": "m121-original.pdf m121-invalid.pdf m121-attachment.txt",
            "m122": " ".join("m122-" + x for x in "original.ppm red.ppm blue.ppm black.pbm white.pgm flipped.ppm cropped.ppm inverted.ppm red-channel.pgm red8.ppm red31.ppm red-blue.ppm original.png invalid.ppm".split()),
            "m123": "m123-ext2.img m123-empty-ext.img m123-empty-fat.img",
            "m124": "m124-empty-minix.img m124-empty-swap.img m124-cramfs-source/hello.txt m124-look-words.txt",
            "m150": "m150-ext2.img m150-mbr.img m150-input.txt",
            "m151": "m151-mono.wav m151-stereo.wav m151-beats.wav m151-source.png m151-source.ppm m151-source.y4m",
            "m153": "m153-native.o m153-native-v2.o m153-fixture.c m153-fixture-v2.c m153-style.c m153-rule.yar m153-assembly.asm m153-text.txt m153-text-v2.txt",
            "m155": "m155-erofs.img m155-input.txt m155-seed/evidence.txt",
            "m157": "m157-color.ppm m157-bw.pbm m157-gray.pgm m157-color.png m157-bad-itxt.png m157-color.gif m157-paletted.tif m157-fax.raw",
            "m158": "m158-color.ppm m158-palette.ppm m158-gray.pgm m158-bw.pbm",
            "m159": "m159-color.ppm m159-gray.pgm m159-bw.pbm m159-multi.pnm m159-rgb.txt m159-svn-note.txt m159-test-cert.p12",
            "m163": "m163-batch-run.sh m163-color.ppm m163-gray.pgm m163-bw.pbm m163-rgb.txt",
            "m165": "m165-batch-run.sh m163-color.ppm m163-gray.pgm m163-bw.pbm m163-rgb.txt",
        }.get(profile, "").split()
        for fixture in fixtures:
            p = root / "tests" / fixture
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"fixture")
        if profile == "m126":
            for resource in ("font", "tmac"):
                p = root / "usr/share/groff/1.23.0" / resource / "fixture"
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b"fixture")
        if profile == "m155":
            import gzip
            for kind in ("btrfs", "xfs", "f2fs", "ntfs", "gpt"):
                (root / "tests" / f"m155-{kind}.img.gz").write_bytes(gzip.compress(b"fixture"))
        run(shared, "amd64", profile, cwd=cwd)
        hashes = work / (profile + "-original-signed-elfs.sha256")
        assert len(hashes.read_text().splitlines()) == count
        if fixtures:
            p = root / "tests" / fixtures[0]
            p.unlink()
            run(shared, "amd64", profile, cwd=cwd, status=1)
            p.write_bytes(b"fixture")
        if profile == "m126":
            (root / "usr/share/groff/1.23.0/font/fixture").unlink()
            run(shared, "amd64", profile, cwd=cwd, status=1)
            (root / "usr/share/groff/1.23.0/font/fixture").write_bytes(b"fixture")
        if int(profile[1:]) >= 124 and profile != "m141":
            installed.write_text("P:wrong-package\n")
            run(shared, "amd64", profile, cwd=cwd, status=1)
            installed.write_text("".join(f"P:{p}\n" for p in packages))
        if int(profile[1:]) >= 121:
            collision = cwd / "scripts/m999-original-elfs.txt"
            collision.write_text(rows[0] + "\n")
            run(shared, "amd64", profile, cwd=cwd, status=1)
            collision.unlink()
        if profile == "m155":
            p = root / "tests/m155-btrfs.img.gz"
            p.write_bytes(b"invalid gzip")
            run(shared, "amd64", profile, cwd=cwd, status=1)
            p.write_bytes(gzip.compress(b"fixture"))
        if mode in {"contained-elf", "contained-exec-elf"}:
            p = root / rows[0]
            p.write_bytes(b"not ELF")
            run(shared, "amd64", profile, cwd=cwd, status=1)
            p.write_bytes(b"\x7fELF" + rows[0].encode())
        if int(profile[1:]) >= 141:
            manifest = cwd / "scripts" / (profile + "-original-elfs.txt")
            manifest.write_text("\n".join(rows[:-1]) + "\n")
            run(shared, "amd64", profile, cwd=cwd, status=1)
            manifest.write_text("\n".join(rows) + "\n")
    run(shared, "amd64", "m999", cwd=cwd, status=2)
    run(shared, "unknown", "m109", cwd=cwd, status=2)
migrated += len(profiles)
assert migrated >= 52, migrated
print(f"[ELF ADMISSION HOST PASS] {migrated} migrated scripts; exact path, count, hash and negative cases")
