#!/usr/bin/env python3
"""Exercise real host fixture dispatch with isolated generators, not guest evidence."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HOOK = ROOT / "scripts/host-configure-alpine-fixtures.sh"
FILES = {
    "m120": "m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw".split(),
    "m121": "m121-original.pdf m121-invalid.pdf".split(),
    "m122": "m122-original.ppm m122-red.ppm m122-blue.ppm m122-black.pbm m122-white.pgm m122-flipped.ppm m122-cropped.ppm m122-inverted.ppm m122-red-channel.pgm m122-red8.ppm m122-red31.ppm m122-red-blue.ppm m122-original.png m122-invalid.ppm".split(),
    "m123": "m123-ext2.img m123-empty-ext.img m123-empty-fat.img".split(),
    "m124": "m124-empty-minix.img m124-empty-swap.img m124-cramfs-source/hello.txt m124-cramfs-source/nested/entry.txt m124-look-words.txt".split(),
    "m125": "m125-pattern.ppm m125-red.ppm m125-blue.ppm m125-magenta.ppm m125-black.ppm m125-cyan.ppm m125-checker.ppm m125-bw-checker.ppm m125-bw-checker.pgm m125-gray.pgm m125-gray-to-red.ppm m125-threshold.pbm m125-indices.pgm m125-lookup.ppm m125-odd.ppm m125-ascii.txt m125-tile_0_0.ppm m125-tile_0_1.ppm m125-tile_1_0.ppm m125-tile_1_1.ppm".split(),
    "m126": "m126-basic.roff m126-blank.roff m126-equation.roff m126-picture.roff m126-table.roff m126-include.roff m126-soelim.roff m126-utf8.roff m126-references.bib m126-refer.roff m126-lookbib.query".split(),
    "m127": "m127-tone.wav m127-tone.raw m127-tone.flac m127-tone.ogg m127-tone.mp3 m127-tone.spx m127-tone.wv m127-vorbis-comments.txt".split(),
    "m128": "m128-input.tar m128-blank-exfat.img m128-tree/alpha.txt m128-tree/nested/beta.txt".split(),
}
with tempfile.TemporaryDirectory() as d:
    cwd = Path(d)
    (cwd / "tests").mkdir()
    # Fake only the generator; run the actual shell hook and its file gates.
    for profile, files in FILES.items():
        (cwd / "tests" / (profile + "-make-fixtures.py")).write_text(
            "from pathlib import Path\nimport sys\n"
            "root = Path(sys.argv[1])\n"
            "for name in " + repr(files) + ":\n"
            "    p = root / name\n"
            "    p.parent.mkdir(parents=True, exist_ok=True)\n"
            "    p.write_bytes(b'x' * (256 if name.endswith('gray.raw') else 1))\n")
    binpath = cwd / "bin"
    binpath.mkdir()
    (binpath / "mke2fs").write_text("#!/bin/sh\nexit 0\n")
    (binpath / "msgfmt").write_text('#!/bin/sh\n[ "$1" = -o ] && printf fixture > "$2"\n')
    for path in binpath.iterdir():
        path.chmod(0o755)
    env = dict(os.environ, PATH=str(binpath) + ":" + os.environ["PATH"])
    def run(port, root, profile, expected):
        p = subprocess.run(["sh", HOOK, port, root, profile], cwd=cwd, env=env,
                           text=True, capture_output=True)
        assert p.returncode == expected, (profile, p.stdout, p.stderr)
    for profile in ["m119", *FILES]:
        for port in ("amd64", "riscv64", "arm64"):
            root = f"build/external/{profile}-batch/{port}/rootfs"
            (cwd / root).mkdir(parents=True)
            run(port, root, profile, 0)
            if profile != "m119":
                generator = cwd / "tests" / (profile + "-make-fixtures.py")
                old = generator.read_text()
                generator.write_text("pass\n")
                (cwd / root / "tests" / FILES[profile][0]).unlink()
                run(port, root, profile, 1)
                generator.write_text(old)
    for profile in ("m143", "m144", "m147", "m149", "m152"):
        for port in ("amd64", "riscv64", "arm64"):
            root = f"build/external/{profile}-batch/{port}/rootfs"
            (cwd / root).mkdir(parents=True)
            run(port, root, profile, 1)
            etc = cwd / root / "etc"
            etc.mkdir()
            run(port, root, profile, 0)
            assert (etc / "passwd").read_text() == "root:x:0:0:root:/tmp:/bin/sh\n"
            assert (etc / "group").read_text() == "root:x:0:\n"
            (etc / "passwd").write_text("existing-account\n")
            (etc / "group").write_text("existing-group\n")
            run(port, root, profile, 0)
            assert (etc / "passwd").read_text() == "existing-account\n"
            assert (etc / "group").read_text() == "existing-group\n"
    # Real deterministic image generation, with identical content in both profiles.
    reference = {}
    for profile in ("m163", "m165"):
        for port in ("amd64", "riscv64", "arm64"):
            root = f"build/external/{profile}-batch/{port}/rootfs"
            (cwd / root).mkdir(parents=True)
            run(port, root, profile, 0)
            files = {p.name: p.read_bytes() for p in (cwd / root / "tests").iterdir()}
            assert set(files) == {"m163-color.ppm", "m163-gray.pgm", "m163-bw.pbm", "m163-rgb.txt"}
            assert files["m163-color.ppm"].startswith(b"P3\n8 8\n255\n")
            assert files["m163-gray.pgm"].startswith(b"P2\n8 8\n255\n")
            if reference:
                assert files == reference
            else:
                reference = files
    # Real host x86_64 static builds of the original input/desktop sources.
    import shutil
    shutil.copyfile(ROOT / "tests/m166-input-display.c", cwd / "tests/m166-input-display.c")
    shutil.copytree(ROOT / "user/desktop", cwd / "user/desktop")
    for profile, expected in (("m166", ["m166-input-display"]),
                              ("m167", ["naseed-desktop", "naseed-image", "naseed-terminal"]),
                              ("m168", ["naseed-desktop", "naseed-image", "naseed-terminal", "naseed-browser"])):
        root = f"build/external/{profile}-batch/amd64/rootfs"
        (cwd / root).mkdir(parents=True)
        run("amd64", root, profile, 0)
        directory = cwd / root / ("tests" if profile == "m166" else "usr/bin")
        assert sorted(p.name for p in directory.iterdir()) == sorted(expected)
        hashfile = "m166-test-application.sha256" if profile == "m166" else profile + "-native-applications.sha256"
        subprocess.run(["sha256sum", "-c", str((cwd / root).parent / hashfile)], cwd=cwd, check=True)
        run("amd64", "bad", profile, 2)
    run("invalid", "bad", "m120", 2)
    run("amd64", "bad", "m999", 2)
    run("amd64", "bad", "m126", 2)
print("[HOST FIXTURE DISPATCH PASS] 20 profiles, real image fixtures and x86 static builds, missing-file and invalid input rejection")
