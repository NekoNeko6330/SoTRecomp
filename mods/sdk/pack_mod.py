#!/usr/bin/env python3
"""Packs a mod without code (e.g. a texture pack with an rt64.json) into an .nrm: a zip of its mod.json and files."""

import sys
import zipfile
from pathlib import Path

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pack_mod.py <mod directory> <output .nrm>")
    mod_dir = Path(sys.argv[1])
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in mod_dir.rglob("*") if p.is_file() and p.name != "Makefile" and "build" not in p.parts)
    if not (mod_dir / "mod.json").is_file():
        raise SystemExit(f"{mod_dir}: missing mod.json")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, p.relative_to(mod_dir).as_posix())
    print(f"{out}: {len(files)} files")

if __name__ == "__main__":
    main()
