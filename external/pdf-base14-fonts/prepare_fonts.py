#!/usr/bin/env python3
"""Reproduce the PDFium Base14 CFF assets without compiling or installing fonts.

Normal operation is offline. --fetch retrieves only the pinned upstream inputs.
--check verifies inputs and generated assets without writing any files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from urllib.request import urlopen

REVISION = "a84323421e94f484faca52dd9d027934eba42ab8"
UPSTREAM = "https://raw.githubusercontent.com/chromium/pdfium/" + REVISION
SOURCE_DIR = "core/fxge/fontdata/chromefontdata"
FONTS = {
    "Courier": "FoxitFixed",
    "Courier-Bold": "FoxitFixedBold",
    "Courier-Oblique": "FoxitFixedItalic",
    "Courier-BoldOblique": "FoxitFixedBoldItalic",
    "Helvetica": "FoxitSans",
    "Helvetica-Bold": "FoxitSansBold",
    "Helvetica-Oblique": "FoxitSansItalic",
    "Helvetica-BoldOblique": "FoxitSansBoldItalic",
    "Times-Roman": "FoxitSerif",
    "Times-Bold": "FoxitSerifBold",
    "Times-Italic": "FoxitSerifItalic",
    "Times-BoldItalic": "FoxitSerifBoldItalic",
    "Symbol": "FoxitSymbol",
    "ZapfDingbats": "FoxitDingbats",
}
ROOT = Path(__file__).resolve().parent


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract(data: bytes, stem: str) -> bytes:
    text = data.decode("utf-8")
    for notice in (
        "Copyright 2014 The PDFium Authors",
        "governed by a BSD-style license",
        "Original code copyright 2014 Foxit Software Inc.",
    ):
        if notice not in text:
            raise ValueError(f"{stem}: expected upstream license notice not found")
    pattern = (
        r"const\s+std::array<uint8_t,\s*(\d+)>\s+k"
        + re.escape(stem)
        + r"FontData\s*=\s*\{\{(.*?)\}\};"
    )
    matches = list(re.finditer(pattern, text, re.S))
    if len(matches) != 1:
        raise ValueError(f"{stem}: expected exactly one known array")
    match = matches[0]
    body = match[2]
    if re.sub(r"0x[0-9a-fA-F]{1,2}|[\s,]", "", body):
        raise ValueError(f"{stem}: unexpected data in array initializer")
    result = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{1,2}", body))
    if len(result) != int(match[1]) or result[:2] != b"\x01\x00":
        raise ValueError(f"{stem}: array length or CFF1 signature mismatch")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="fetch pinned upstream inputs")
    parser.add_argument("--check", action="store_true", help="verify everything, write nothing")
    args = parser.parse_args()
    if args.fetch and args.check:
        parser.error("--fetch and --check are mutually exclusive")

    inputs = {
        "LICENSE": "LICENSE",
        "upstream/chromefontdata.h": SOURCE_DIR + "/chromefontdata.h",
        **{f"upstream/{stem}.cpp": f"{SOURCE_DIR}/{stem}.cpp" for stem in FONTS.values()},
    }
    manifest_path = ROOT / "manifest.json"
    expected = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else None
    if args.check and expected is None:
        raise ValueError("manifest.json is required for verification")
    if expected is not None and expected["revision"] != REVISION:
        raise ValueError("manifest revision does not match extractor pin")

    hashes = {}
    for local, remote in inputs.items():
        target = ROOT / local
        if args.fetch:
            with urlopen(UPSTREAM + "/" + remote, timeout=60) as response:
                data = response.read()
            if expected is not None and sha256(data) != expected["files"][local]:
                raise ValueError(f"{local}: fetched source checksum mismatch")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        else:
            data = target.read_bytes()
        hashes[local] = sha256(data)
        if expected is not None and hashes[local] != expected["files"][local]:
            raise ValueError(f"{local}: source checksum mismatch")

    fonts = []
    for base14, stem in FONTS.items():
        data = extract((ROOT / f"upstream/{stem}.cpp").read_bytes(), stem)
        relative = f"fonts/{stem}.cff"
        hashes[relative] = sha256(data)
        if expected is not None and hashes[relative] != expected["files"][relative]:
            raise ValueError(f"{relative}: extracted checksum mismatch")
        target = ROOT / relative
        if args.check:
            if target.read_bytes() != data:
                raise ValueError(f"{relative}: generated asset differs from upstream array")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        fonts.append({"base14": base14, "file": relative, "bytes": len(data), "sha256": hashes[relative]})

    manifest = {
        "repository": "https://github.com/chromium/pdfium",
        "revision": REVISION,
        "upstream_directory": SOURCE_DIR,
        "fonts": fonts,
        "files": dict(sorted(hashes.items())),
    }
    if args.check:
        if manifest != expected:
            raise ValueError("manifest metadata does not match pinned inputs")
    else:
        manifest_path.write_bytes((json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
    print(f"Verified {len(fonts)} Base14 fonts; {sum(f['bytes'] for f in fonts)} CFF bytes; PDFium {REVISION}")


if __name__ == "__main__":
    main()
