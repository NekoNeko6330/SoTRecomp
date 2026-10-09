# Sands of Time: Recompiled mods

Mods are `.nrm` files: put them in the `mods` folder of Sands of Time: Recompiled (`~/.config/SoTRecompiled/mods`
on Linux, `%LOCALAPPDATA%\SoTRecompiled\mods` on Windows; the Mods menu of the launcher can open it), and enable them
in the Mods menu. Their game id is `sot`.

| Mod | What it does |
| --- | --- |
| [better_roll](better_roll) | Controlled and faster rolling, from thinedave's ocarina-things (741b0f4) |
| [look_tracking](look_tracking) | Link's head and eye tracking, from ModOcarina (13053ec, b793191) |
| [fairy_text_1_0](fairy_text_1_0) | Restores the Great Fairy of Wisdom's line about Big Brother from Sands of Time 1.0 |
| [torch_texture](torch_texture) | Test texture pack: replaces the torches' flame guard texture (object 0x00A4, 06002490) |

## Building

The code mods need:
- clang and ld.lld (with the MIPS target), and Python 3
- RecompModTool: `cmake --build lib/N64ModernRuntime/N64Recomp/build --target RecompModTool`
- the game's symbol files, `sot.syms.toml` and `sot.datasyms.toml` at the root of the repository, which
  `tools/generate.py` generates from the ROM

Then `make` (in this folder) builds every mod into `out/`. `make -C <mod>` builds one mod into `<mod>/build`.

## Making a mod

Copy `better_roll` and edit `mod.toml` (id, name, description...) and `src/`. See the
[Zelda64Recomp mod template](https://github.com/Zelda64Recomp/MMRecompModTemplate) for how mods work: `RECOMP_PATCH`
replaces a function of the game, `RECOMP_IMPORT` imports a function from another mod or the game (`"*"`), for
example `recomp_printf`.

- Mods are built with the OoT decomp's headers (`lib/oot-decomp`, the debug version that Sands of Time is built on),
  through `sdk/sot.h`. The recompiled game names its functions like z64hdr (e.g. `func_8002F7DC` for
  `Actor_PlaySfx`): `sot.h` declares some of them, see `sot.syms.toml` for the others.
- Sands of Time replaces many vanilla functions with its own (z64rom's uLib, at 0x80700000): the vanilla function
  only jumps to it, so patch the uLib function (`func_807XXXXX`), or the vanilla function if it does more.
- Sands of Time has its own player (`ovl_kaleido_0001`): `sdk/sot_player.h` names its functions, and the addresses of
  some of its variables (to use with `SOT_PLAYER_DATA`, the player overlay being loaded somewhere else in memory).
- Hooks (`RECOMP_HOOK`) aren't supported yet: patch the whole function instead.
- Display lists and textures read by the RDP must be in the game's memory (e.g. `SystemArena_Malloc`), not in the
  mod's.

A texture pack is an `rt64.json` with its textures (see `torch_texture`): `make` packs it with `sdk/pack_mod.py`. RT64's
texture dumping (in its developer tools) gives the textures' hashes, named `<hash>.v5`.
