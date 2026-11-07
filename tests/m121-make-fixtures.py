#!/usr/bin/env python3
"""Linux-HOST-only PDF fixture generator; NaSeed never builds test inputs."""

import pathlib
import sys
import zlib


def stream_object(data: bytes, dictionary: str = "") -> bytes:
    return (
        f"<< {dictionary} /Length {len(data)} >>\nstream\n".encode("ascii")
        + data
        + b"\nendstream\n"
    )


def pdf_bytes() -> bytes:
    page1 = b"BT /F1 12 Tf 20 168 Td (M121_PAGE_ONE_ALPHA) Tj ET\n"
    page1 += b"q 64 0 0 64 20 70 cm /Im1 Do Q\n"
    page2 = b"BT /F1 12 Tf 20 168 Td (M121_PAGE_TWO_BETA) Tj ET\n"
    rgb = bytes(((x * 47 + y * 17) & 255 for y in range(4) for x in range(12)))
    image = zlib.compress(rgb, 9)
    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>\n",
        2: b"<< /Type /Pages /Count 2 /Kids [3 0 R 4 0 R] >>\n",
        3: (
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            b"/Resources << /Font << /F1 5 0 R >> /XObject << /Im1 7 0 R >> >> "
            b"/Contents 6 0 R >>\n"
        ),
        4: (
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
            b"/Resources << /Font << /F1 5 0 R >> >> /Contents 8 0 R >>\n"
        ),
        5: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n",
        6: stream_object(page1),
        7: stream_object(
            image,
            "/Type /XObject /Subtype /Image /Width 4 /Height 4 "
            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode",
        ),
        8: stream_object(page2),
        9: b"<< /Title (M121-NaSeed-Signed-PDF) /Producer (NaSeed host fixture) >>\n",
    }
    output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number in range(1, 10):
        offsets.append(len(output))
        output.extend(f"{number} 0 obj\n".encode("ascii"))
        output.extend(objects[number])
        output.extend(b"endobj\n")
    xref = len(output)
    output.extend(b"xref\n0 10\n0000000000 65535 f \n")
    for offset in offsets[1:]:
        output.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    output.extend(
        b"trailer\n<< /Size 10 /Root 1 0 R /Info 9 0 R >>\n"
        + f"startxref\n{xref}\n%%EOF\n".encode("ascii")
    )
    return bytes(output)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m121-make-fixtures.py ROOTFS_TESTS_DIR")
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / "m121-original.pdf"
    target.write_bytes(pdf_bytes())
    (directory / "m121-invalid.pdf").write_bytes(b"NOT_A_PDF_M121_NEGATIVE_ORACLE\n")
    assert target.stat().st_size > 500
    print("[M121 HOST PDF PASS] original 2-page tagged PDF+FlateRGB/Helvetica, no guest builds")


if __name__ == "__main__":
    main()
