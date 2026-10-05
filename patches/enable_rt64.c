#include "patches.h"
#include "transform_ids.h"
#include "global.h"
#include "vt.h"
#include "misc_funcs.h"

extern OSTime sGraphUpdateTime;
extern FaultClient sGraphUcodeFaultClient;
void Graph_UCodeFaultClient(Gfx* workBuf);

int extra_vis = 0;

#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

typedef struct {
    Gfx polyXluBuffer[0x8000];
    Gfx overlayBuffer[0x4000];
    Gfx workBuffer[0x400];
    Gfx debugBuffer[0x400];
    Gfx polyOpaBuffer[0x33800];
} BiggerGfxPool;

BiggerGfxPool gBiggerGfxPools[2];

RECOMP_PATCH void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx % 2];
    BiggerGfxPool* bigger_pool = &gBiggerGfxPools[gfxCtx->gfxPoolIdx % 2];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;

    THGA_Ct(&gfxCtx->polyOpa, bigger_pool->polyOpaBuffer, sizeof(bigger_pool->polyOpaBuffer));
    THGA_Ct(&gfxCtx->polyXlu, bigger_pool->polyXluBuffer, sizeof(bigger_pool->polyXluBuffer));
    THGA_Ct(&gfxCtx->overlay, bigger_pool->overlayBuffer, sizeof(bigger_pool->overlayBuffer));
    THGA_Ct(&gfxCtx->work, bigger_pool->workBuffer, sizeof(bigger_pool->workBuffer));

    gfxCtx->polyOpaBuffer = bigger_pool->polyOpaBuffer;
    gfxCtx->polyXluBuffer = bigger_pool->polyXluBuffer;
    gfxCtx->overlayBuffer = bigger_pool->overlayBuffer;
    gfxCtx->workBuffer = bigger_pool->workBuffer;

    gfxCtx->curFrameBuffer = (u16*)SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;

    // @recomp Enable RT64 extended GBI mode and set the current framerate
    OPEN_DISPS(gfxCtx);
    gEXEnable(POLY_OPA_DISP++);
    CLOSE_DISPS(gfxCtx);
}

RECOMP_PATCH void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {
    u32 problem;

    gameState->unk_A0 = 0;
    Graph_InitTHGA(gfxCtx);

    OPEN_DISPS(gfxCtx, "../graph.c", 966);

    // @recomp Send the current framerate to RT64, including any extra VI interrupt periods.
    gEXSetRefreshRate(POLY_OPA_DISP++, 60 / (R_UPDATE_RATE + extra_vis));

    // @recomp Edit billboard groups to skip interpolation if the camera also skipped.
    if (gameState->destroy == Play_Destroy) {
        PlayState* play = (PlayState*)gameState;
        //edit_billboard_groups(play);
    }

    // @recomp Clear the camera skip state.
    clear_camera_skipped();

    gDPNoOpString(WORK_DISP++, "WORK_DISP 開始", 0);
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 開始", 0);
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 開始", 0);
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 開始", 0);

    CLOSE_DISPS(gfxCtx, "../graph.c", 975);

    GameState_ReqPadData(gameState);
    GameState_Update(gameState);

    OPEN_DISPS(gfxCtx, "../graph.c", 987);

    gDPNoOpString(WORK_DISP++, "WORK_DISP 終了", 0);
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 終了", 0);
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 終了", 0);
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 終了", 0);

    CLOSE_DISPS(gfxCtx, "../graph.c", 996);

    OPEN_DISPS(gfxCtx, "../graph.c", 999);

    gSPBranchList(WORK_DISP++, gfxCtx->polyOpaBuffer);
    gSPBranchList(POLY_OPA_DISP++, gfxCtx->polyXluBuffer);
    gSPBranchList(POLY_XLU_DISP++, gfxCtx->overlayBuffer);
    gDPPipeSync(OVERLAY_DISP++);
    gDPFullSync(OVERLAY_DISP++);
    gSPEndDisplayList(OVERLAY_DISP++);

    CLOSE_DISPS(gfxCtx, "../graph.c", 1028);

    if (HREG(80) == 10 && HREG(93) == 2) {
        HREG(80) = 7;
        HREG(81) = -1;
        HREG(83) = HREG(92);
    }

    if (HREG(80) == 7 && HREG(81) != 0) {
        if (HREG(82) == 3) {
            Fault_AddClient(&sGraphUcodeFaultClient, Graph_UCodeFaultClient, gfxCtx->workBuffer, "do_count_fault");
        }

        Graph_DisassembleUCode(gfxCtx->workBuffer);

        if (HREG(82) == 3) {
            Fault_RemoveClient(&sGraphUcodeFaultClient);
        }

        if (HREG(81) < 0) {
            LogUtils_LogHexDump((void*)&HW_REG(SP_MEM_ADDR_REG, u32), 0x20);
            LogUtils_LogHexDump((void*)&DPC_START_REG, 0x20);
        }

        if (HREG(81) < 0) {
            HREG(81) = 0;
        }
    }

    problem = false;

    {
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

        if (pool->headMagic != GFXPOOL_HEAD_MAGIC) {
            //recomp_crash("GfxPool headMagic integrity check failed!");

            //! @bug (?) : "problem = true;" may be missing
            osSyncPrintf("%c", BEL);
            // "Dynamic area head is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域先頭が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash("../graph.c", 1070);
        }
        if (pool->tailMagic != GFXPOOL_TAIL_MAGIC) {
            //recomp_crash("GfxPool tailMagic integrity check failed!");

            problem = true;
            osSyncPrintf("%c", BEL);
            // "Dynamic region tail is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域末尾が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash("../graph.c", 1076);
        }
    }

    if (THGA_IsCrash(&gfxCtx->polyOpa)) {
        //recomp_crash("gfxCtx->polyOpa overflow!");

        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 0 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ0は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    if (THGA_IsCrash(&gfxCtx->polyXlu)) {
        //recomp_crash("gfxCtx->polyXlu overflow!");

        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 1 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ1は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    if (THGA_IsCrash(&gfxCtx->overlay)) {
        //recomp_crash("gfxCtx->overlay overflow!");

        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 4 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ4は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }

    if (!problem) {
        // @recomp Temporarily adjust the framerate divisor to include any extra VI interrupt periods.
        u8 old_divisor = R_UPDATE_RATE;
        R_UPDATE_RATE += extra_vis;

        Graph_TaskSet00(gfxCtx);

        // @recomp Restore the old framerate divisor.
        R_UPDATE_RATE = old_divisor;

        gfxCtx->gfxPoolIdx++;
        gfxCtx->fbIdx++;
    }

    // @recomp Clear any extra VI interrupt periods.
    extra_vis = 0;

    func_800F3054();

    {
        OSTime time = osGetTime();
        s32 pad[4];

        D_8016A538 = gRSPGFXTotalTime;
        D_8016A530 = gRSPAudioTotalTime;
        D_8016A540 = gRDPTotalTime;
        gRSPGFXTotalTime = 0;
        gRSPAudioTotalTime = 0;
        gRDPTotalTime = 0;

        if (sGraphUpdateTime != 0) {
            D_8016A548 = time - sGraphUpdateTime;
        }
        sGraphUpdateTime = time;
    }

    if (gIsCtrlr2Valid && PreNmiBuff_IsResetting(gAppNmiBufferPtr) && !gameState->unk_A0) {
        // "To reset mode"
        osSyncPrintf(VT_COL(YELLOW, BLACK) "PRE-NMIによりリセットモードに移行します\n" VT_RST);
        SET_NEXT_GAMESTATE(gameState, PreNMI_Init, PreNMIState);
        gameState->running = false;
    }
}

void* proutPrintf(void* dst, const char* fmt, u32 size) {
    recomp_puts(fmt, size);
    return (void*)1;
}

RECOMP_PATCH int recomp_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int ret = _Printf(&proutPrintf, NULL, fmt, args);

    va_end(args);

    return ret;
}


