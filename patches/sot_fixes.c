// Fixes for issues that only happen in the recompilation.

#include "patches.h"

#define osSpTaskYield osSpTaskYield_recomp
void osSpTaskYield(void);

// @recomp When an audio task is queued while an RSP task is running, the scheduler asks the running task to yield.
// On the console an audio task always ends before the next one is queued, but here a task can be delayed (e.g. while
// the window is in the background), so the running task can be an audio task: don't yield it (it asserted).
RECOMP_PATCH void Sched_Yield(Scheduler* sc) {
    if (!(sc->curRSPTask->state & OS_SC_YIELD)) {
        if (sc->curRSPTask->list.t.type == M_AUDTASK) {
            return;
        }

        sc->curRSPTask->state |= OS_SC_YIELD;

        // Send yield request
        osSpTaskYield();
    }
}

// Sands of Time's pause menu (ovl_kaleido_0000)
void ovl_kaleido_0000_8080EAA4(PlayState* play); // KaleidoScope_UpdateCursorSize

// Address of the pause menu's data at `vram` (the overlay isn't loaded at its vram).
static void* kaleido_data(u32 vram) {
    KaleidoMgrOverlay* ovl = &gKaleidoMgrOverlayTable[0];
    return (u8*)ovl->loadedRamAddr + (vram - (u32)ovl->vramStart);
}

// @recomp KaleidoScope_DrawCursor. The pause menu allocates new cursor vertices every frame and only sets the first
// one (the cursor's position) while drawing, the other corners being set by KaleidoScope_UpdateCursorSize in the next
// frame's update: on the console the RSP only reads them after that, but here the frame is rendered before, so the
// cursor was invisible. Set the corners while drawing, in a copy of the vertices.
RECOMP_PATCH void ovl_kaleido_0000_8080A25C(PlayState* play, u16 pageIndex) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 mainState = pauseCtx->mainState;

    if (!((((mainState == 0) || (mainState == 8)) && (pauseCtx->state == 6)) ||
          ((pauseCtx->pageIndex == PAUSE_QUEST) && ((mainState < 3) || (mainState == 5) || (mainState == 8))))) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_kaleido_scope_PAL.c", 955);

    if (pauseCtx->pageIndex == pageIndex) {
        s16(*cursorColors)[3] = kaleido_data(0x808151F4);
        void** cursorTexs = kaleido_data(0x80815208);
        s16* envColor = kaleido_data(0x80817178); // B, G, R
        Vtx* origVtx = pauseCtx->cursorVtx;
        Vtx* vtx = Graph_Alloc(play->state.gfxCtx, 20 * sizeof(Vtx));
        s16 i;
        s16 j;

        for (i = 0; i < 20; i++) {
            vtx[i] = origVtx[i];
        }
        pauseCtx->cursorVtx = vtx;
        ovl_kaleido_0000_8080EAA4(play);
        pauseCtx->cursorVtx = origVtx;

        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cursorColors[pauseCtx->cursorColorSet >> 2][0],
                        cursorColors[pauseCtx->cursorColorSet >> 2][1], cursorColors[pauseCtx->cursorColorSet >> 2][2],
                        255);
        gDPSetEnvColor(POLY_OPA_DISP++, envColor[2], envColor[1], envColor[0], 255);
        gSPVertex(POLY_OPA_DISP++, vtx, 16, 0);

        for (i = j = 0; i < 4; i++, j += 4) {
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, cursorTexs[i], G_IM_FMT_IA, 16, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, j, j + 2, j + 3, j + 1, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);

    CLOSE_DISPS(play->state.gfxCtx, "../z_kaleido_scope_PAL.c", 985);
}
