#!/usr/bin/env python3
"""
Generate everything the build needs from your Sands of Time ROM.

    python3 tools/generate.py "OoT SoT 1.22.z64"

Steps:
1. Build N64Recomp and RSPRecomp (lib/N64ModernRuntime/N64Recomp, with the patch in tools/n64recomp-traps.patch)
2. Decompress the ROM, find the code and generate the recompiler inputs (tools/gen_sot_syms.py)
3. Recompile the game (RecompiledFuncs/), the RSP microcodes (rsp/*.cpp) and the patches (RecompiledPatches/)

Then build with CMake (see BUILDING.md).
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ".exe" if os.name == "nt" else ""


def run(cmd, cwd=ROOT):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def build_recompilers():
    n64recomp = ROOT / f"N64Recomp{EXE}"
    rsprecomp = ROOT / f"RSPRecomp{EXE}"
    if n64recomp.exists() and rsprecomp.exists():
        return n64recomp, rsprecomp
    src = ROOT / "lib" / "N64ModernRuntime" / "N64Recomp"
    if not (src / "CMakeLists.txt").exists():
        raise SystemExit("lib/N64ModernRuntime/N64Recomp is missing: run `git submodule update --init --recursive`")
    # Ignore trap instructions (used by z64rom's GCC-built code)
    patch = ROOT / "tools" / "n64recomp-traps.patch"
    applied = subprocess.run(["git", "apply", "--ignore-whitespace", "--reverse", "--check", str(patch)], cwd=src, capture_output=True)
    if applied.returncode != 0:
        run(["git", "apply", "--ignore-whitespace", str(patch)], cwd=src)
    build = src / "build"
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    # On Windows, build with MSVC (N64Recomp's fmt doesn't build with recent clang)
    compilers = ["-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"] if os.name == "nt" else []
    run(["cmake", "-S", src, "-B", build, *generator, *compilers, "-DCMAKE_BUILD_TYPE=Release"])
    run(["cmake", "--build", build, "--config", "Release", "--target", "N64RecompCLI", "RSPRecomp", "--parallel"])
    for name, dst in (("N64Recomp", n64recomp), ("RSPRecomp", rsprecomp)):
        candidates = [build / f"{name}{EXE}", build / "Release" / f"{name}{EXE}"]
        found = next((c for c in candidates if c.exists()), None)
        if found is None:
            raise SystemExit(f"{name} was not built")
        shutil.copy2(found, dst)
    return n64recomp, rsprecomp


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    rom = Path(sys.argv[1]).resolve()

    n64recomp, rsprecomp = build_recompilers()

    run([sys.executable, ROOT / "tools" / "gen_sot_syms.py", rom, ROOT / "lib" / "z64hdr", ROOT])

    out = ROOT / "RecompiledFuncs"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    run([n64recomp, "sot.toml"])
    run([rsprecomp, "aspMain.toml"])
    run([rsprecomp, "njpgdspMain.toml"])

    if os.environ.get("SOT_SKIP_PATCHES"):
        # Patches built elsewhere (they need clang with the MIPS target, e.g. not available on Windows CI)
        print("Skipping the patches (SOT_SKIP_PATCHES)")
        return

    # Patches: build and recompile them once, so that the CMake build finds all its inputs
    # PATCHES_CC / PATCHES_LD: clang and ld.lld to use (needs LLVM with the MIPS target)
    overrides = []
    for var in ("CC", "LD"):
        value = os.environ.get("PATCHES_" + var)
        if value:
            overrides.append(f'{var}="{value}"' if " " in value else f"{var}={value}")
    run(["make", "-C", "patches", *overrides])
    (ROOT / "RecompiledPatches").mkdir(exist_ok=True)
    run([n64recomp, "patches.toml"])
    print("Done. Now build with CMake (see BUILDING.md).")


if __name__ == "__main__":
    main()
