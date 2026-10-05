#!/bin/bash

### In PowerShell

# choco install cmake, ninja, make, clang
# Make sure they're all on PATH

### In WSL

# Install git lfs
# git clone --recurse-submodules ...

# Build N64Recomp for windows, since it's used in the build process as well


# run from WSL
rm -r out build RecompiledFuncs RecompiledPatches indigo.toml aspMain.toml njpgdspMain.toml rsp/aspMain.cpp rsp/aspMain.text.bin rsp/njpgdspMain.cpp rsp/njpgdspMain.text.bin

# Produce an N64 ROM to recompile
# build indigo with `make release RECOMP=1` or use the recomp-build.sh script in indigo

# activate the python venv if not already
. .venv/bin/activate

# Generate recomp configuration from ELF file
./.venv/bin/python3 gen_recomp.py "/home/kenton/decomp/oot/zelda_ocarina_mq_dbg.elf" \
    "W:\\\\home\\\\kenton\\\\decomp\\\\oot\\\\zelda_ocarina_mq_dbg.elf"

# Run N64Recomp on the config (yes run twice)
./N64Recomp.exe indigo.toml
./N64Recomp.exe indigo.toml --dump-context
./N64Recomp.exe patches.toml
./RSPRecomp.exe aspMain.toml
./RSPRecomp.exe njpgdspMain.toml

# Run RSPRecomp on the ucode configs



# run these from Powershell
# cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja -DCMAKE_BUILD_TYPE=Debug
# cmake --build build --target Zelda64Recompiled -j16 --config Debug

# cp -r assets/ build/
