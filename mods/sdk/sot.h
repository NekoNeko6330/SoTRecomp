#ifndef SOT_H
#define SOT_H

// Sands of Time: Recompiled mod SDK.
//
// Mods are built with the OoT decomp's headers (lib/oot-decomp, the debug version Sands of Time is built on), but
// they link against the recompiled game's symbols (sot.syms.toml and sot.datasyms.toml, generated from the ROM by
// tools/gen_sot_syms.py), which use z64hdr's names. Functions the two name differently are declared below with their
// z64hdr names.

#include "modding.h"

// Decomp names of variables and functions that the recompiled game names differently (z64hdr names)
#define gRegEditor gGameInfo
#define gAudioCtx gAudioContext
#define THGA_Init THGA_Ct
#define DmaMgr_RequestSync DmaMgr_SendRequest0
#define Sfx_PlaySfxCentered func_80078884
#define gRandFloat sRandFloat

#include "global.h"

// Sands of Time is built on the debug version (OOT_DEBUG): no debug prints
#undef PRINTF
#define PRINTF(...) (void)0

// Vanilla functions, z64hdr names
void func_8002F7DC(Actor* actor, u16 sfxId); // Actor_PlaySfx
void func_8002F8F0(Actor* actor, u16 sfxId); // Actor_PlaySfx_Flagged2 (looping sfx)

// Kaleido overlays (the pause menu and the player) are loaded at a different address than the one they're linked at:
// this gives the address in RAM of a player overlay variable from its address in the ROM's overlay (vram).
void* KaleidoManager_GetRamAddr(void* vram);
#define SOT_PLAYER_DATA(type, vram) ((type*)KaleidoManager_GetRamAddr((void*)(vram)))

#endif
