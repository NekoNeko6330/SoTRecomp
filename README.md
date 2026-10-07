# Sands of Time: Recompiled

A native PC port of **The Legend of Zelda: Ocarina of Time – Sands of Time**, made with
[N64: Recompiled](https://github.com/N64Recomp/N64Recomp) static recompilation and the
[RT64](https://github.com/rt64/rt64) renderer.

It is built on the current [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) runtime
(N64ModernRuntime and RT64), following the approach of the `indigo` branch of
[krm01/Zelda64Recomp](https://github.com/krm01/Zelda64Recomp/tree/indigo) (a recompilation of the Indigo hack).

**This repository doesn't contain any game code or assets. You need your own copy of the Sands of
Time 1.22 ROM (`OoT SoT 1.22.z64`) to build and play.**

## How it works

Sands of Time is an Ocarina of Time (Master Quest Debug) hack made with [z64rom](https://github.com/z64tools/z64rom).
Unlike Indigo, it isn't built from the decompilation, so there is no ELF file to recompile from.
Everything is recovered from the ROM instead (`tools/gen_sot_syms.py`):

- the ROM is decompressed, and the code is located:
  - `boot` and `code`: the vanilla MQ Debug code, as modified by z64rom (its replaced functions jump
    into the user library)
  - z64rom's user library (uLib), loaded at `0x80700000`
  - every overlay: actors and particle effects from z64rom's extended tables (stored in the
    `dmadata` file), game states and pause menu overlays from the vanilla tables
- functions are found with [spimdisasm](https://github.com/Decompollaborate/spimdisasm) and the
  MQ Debug names from [z64hdr](https://github.com/z64tools/z64hdr)
- overlay relocations are read from the overlays' relocation tables

N64Recomp then recompiles everything from the ROM, in its symbol file mode (`sot.toml`).

At runtime:
- overlays are registered with the runtime by a hook in `Overlay_Load` (see `sot.toml`); they are
  identified by their VROM address
- the code that's always loaded but isn't in the first MB of the ROM (`code` and the uLib) is
  registered when the game starts (`zelda64::load_static_sections`)
- `patches/sot_patches.c` enables RT64's extended display lists and larger display list buffers.
  Functions that Sands of Time hooks itself (e.g. `Graph_Update`) are left untouched.

## Command line

- `--rom <path>`: select the ROM without the launcher
- `--start`: start the game directly
- `SOTRECOMP_FPS=1` (environment variable): print the game framerate and rendering times every 5 seconds

## Building

See [BUILDING.md](BUILDING.md).

## Status

- The whole ROM recompiles: 462 code sections (boot, code, uLib and 458 overlays), about 11,000 functions.
- The game hasn't been tested much yet: expect bugs. Report crashes with the console output (e.g.
  `Failed to find function at 0x...`, which means a function wasn't found in the ROM).
- The enhancements of Zelda64Recomp's Majora's Mask version (widescreen HUD, gyro, autosave,
  dual analog, ...) rely on patches of Majora's Mask code and aren't available.

## Credits

- Sands of Time by its authors (source: [SoT-Fork](https://github.com/NekoNeko6330/SoT-Fork))
- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) by Wiseguy and contributors, and
  krm01's `indigo` branch
- [N64Recomp](https://github.com/N64Recomp/N64Recomp), [RT64](https://github.com/rt64/rt64)
- [zeldaret/oot](https://github.com/zeldaret/oot) (headers for the patches), [z64hdr](https://github.com/z64tools/z64hdr) (symbols)

The original Zelda64Recomp README is in [docs/README.Zelda64Recomp.md](docs/README.Zelda64Recomp.md).
