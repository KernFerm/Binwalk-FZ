#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare temporary device fixtures from the exact inspected upstream checkout."""

from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "upstream-binwalk"
OUTPUT = ROOT / "build-host-tests" / "device-fixtures"

if not (UPSTREAM / ".git").exists():
    raise SystemExit(f"Missing upstream checkout: {UPSTREAM}")

OUTPUT.mkdir(parents=True, exist_ok=True)
for name in ("7z.bin", "gzip.bin", "jpeg.bin", "pdf.bin", "riff.bin", "zip.bin"):
    shutil.copyfile(UPSTREAM / "tests" / "inputs" / name, OUTPUT / name)

# A real upstream PNG is placed so its 16-byte signature crosses the 512-byte
# scanner boundary. The prefix is outside the embedded PNG and is not analyzed
# as claimed content.
with (OUTPUT / "boundary_png.bin").open("wb") as destination:
    destination.write(bytes(507))
    destination.write((UPSTREAM / "images" / "binwalk.png").read_bytes())

(OUTPUT / "zero.bin").write_bytes(b"")
print(OUTPUT)
