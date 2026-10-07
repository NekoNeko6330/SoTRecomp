// Patches for Sands of Time.
//
// Sands of Time is an Ocarina of Time (Master Quest Debug) hack made with z64rom. Some vanilla
// functions call into z64rom's uLib (e.g. Graph_Update), so only functions without such hooks are
// patched here. Function names are the ones of the recompiled game (z64hdr's).

#include "patches.h"
#include "misc_funcs.h"

// Initialized patch data (the patch data binary can't be empty)
const char gSotRecompPatchesId[] = "Sands of Time: Recompiled patches";

#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

// Larger graphics buffers for the enhancements (widescreen, high framerate interpolation, ...)
typedef struct {
    Gfx polyXluBuffer[0x8000];
    Gfx overlayBuffer[0x4000];
    Gfx workBuffer[0x400];
    Gfx debugBuffer[0x400];
    Gfx polyOpaBuffer[0x33800];
} BiggerGfxPool;

BiggerGfxPool gBiggerGfxPools[2];

// @recomp Use larger graphics buffers, enable RT64's extended GBI and send the game's framerate.
// (Graph_Update can't be patched: Sands of Time hooks it.)
RECOMP_PATCH void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
    BiggerGfxPool* bigger_pool = &gBiggerGfxPools[gfxCtx->gfxPoolIdx & 1];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;

    THGA_Init(&gfxCtx->polyOpa, bigger_pool->polyOpaBuffer, sizeof(bigger_pool->polyOpaBuffer));
    THGA_Init(&gfxCtx->polyXlu, bigger_pool->polyXluBuffer, sizeof(bigger_pool->polyXluBuffer));
    THGA_Init(&gfxCtx->overlay, bigger_pool->overlayBuffer, sizeof(bigger_pool->overlayBuffer));
    THGA_Init(&gfxCtx->work, bigger_pool->workBuffer, sizeof(bigger_pool->workBuffer));

    gfxCtx->polyOpaBuffer = bigger_pool->polyOpaBuffer;
    gfxCtx->polyXluBuffer = bigger_pool->polyXluBuffer;
    gfxCtx->overlayBuffer = bigger_pool->overlayBuffer;
    gfxCtx->workBuffer = bigger_pool->workBuffer;

    gfxCtx->curFrameBuffer = SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;

    // @recomp Enable RT64 extended GBI mode and set the current framerate
    OPEN_DISPS(gfxCtx, "../graph.c", 0);
    gEXEnable(POLY_OPA_DISP++);
    gEXSetRefreshRate(POLY_OPA_DISP++, 60 / (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 3));
    CLOSE_DISPS(gfxCtx, "../graph.c", 0);
}

// Overlay loading is hooked in sot.toml (Overlay_Load).
