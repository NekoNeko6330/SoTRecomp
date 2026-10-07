# Building Sands of Time: Recompiled

You need:
- the Sands of Time 1.22 ROM (`OoT SoT 1.22.z64`, md5 `33273fb052fa8ea94410352a6ce52f8f`), in big-endian `.z64` format
- Python 3.10+ with `pip install spimdisasm rabbitizer crunch64 xxhash`
- CMake, Ninja, Clang and `ld.lld` (LLVM)

## 1. Clone with submodules

```bash
git clone --recurse-submodules <this repository>
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

## 2. Install dependencies

### Linux (Ubuntu)
```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

### Windows
Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with:
- Desktop development with C++
- C++ Clang Compiler for Windows
- C++ CMake tools for Windows

Also install `make` (e.g. `choco install make`) and Python 3.

## 3. Generate the recompiled code from your ROM

```bash
python3 tools/generate.py "path/to/OoT SoT 1.22.z64"
```

This builds N64Recomp and RSPRecomp (`lib/N64ModernRuntime/N64Recomp`), generates `sot.syms.toml`,
`sot.datasyms.toml` and `sot_decompressed.z64`, then writes the recompiled game to
`RecompiledFuncs/` and the RSP microcodes to `rsp/`.

## 4. Build

### Linux
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j$(nproc)
cp -r assets build/
```

### Windows
Open the folder in Visual Studio, choose the `x64-Release` (Clang) configuration and build
`Zelda64Recompiled.exe`, or from a Developer PowerShell:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build
```
Copy `assets/` next to the executable.

## 5. Play

Run the executable, select your `OoT SoT 1.22.z64` ROM in the launcher, then start the game.
