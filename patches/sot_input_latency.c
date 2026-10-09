// Input latency, like Zelda64Recomp: the controllers are read when the game asks for its inputs (instead of on every
// retrace), and the game waits for each frame to be shown before it starts the next one, so the inputs it reads are
// as recent as possible.

#include "patches.h"

void recomp_set_current_frame_poll_id(void);

// Sands of Time's Graph_TaskSet00 (uLib)
void func_8070DD64(GraphicsContext* gfxCtx);

OSMesgQueue* PadMgr_AcquireSerialEventQueue(PadMgr* padMgr);
void PadMgr_ReleaseSerialEventQueue(PadMgr* padMgr, OSMesgQueue* serialEventQueue);
void PadMgr_LockPadData(PadMgr* padMgr);
void PadMgr_UnlockPadData(PadMgr* padMgr);
void PadMgr_UpdateRumble(PadMgr* padMgr);
void PadMgr_RumbleStop(PadMgr* padMgr);
void PadMgr_UpdateInputs(PadMgr* padMgr);

extern FaultMgr gFaultMgr;
extern IrqMgr gIrqMgr;

static void poll_inputs(PadMgr* padMgr) {
    OSMesgQueue* serialEventQueue = PadMgr_AcquireSerialEventQueue(padMgr);
    u32 mask;
    s32 i;

    // Begin reading controller data
    osContStartReadData(serialEventQueue);

    // Wait for controller data
    osRecvMesg(serialEventQueue, NULL, OS_MESG_BLOCK);
    osContGetReadData(padMgr->pads);

    // If resetting, clear all controllers
    if (padMgr->isResetting) {
        bzero(padMgr->pads, sizeof(padMgr->pads));
    }

    // Update input data
    PadMgr_UpdateInputs(padMgr);

    // Query controller status for all controllers
    osContStartQuery(serialEventQueue);
    osRecvMesg(serialEventQueue, NULL, OS_MESG_BLOCK);
    osContGetQuery(padMgr->padStatus);

    PadMgr_ReleaseSerialEventQueue(padMgr, serialEventQueue);

    // Update the state of connected controllers
    mask = 0;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        if ((padMgr->padStatus[i].errno == 0) && (padMgr->padStatus[i].type == CONT_TYPE_NORMAL)) {
            mask |= 1 << i;
        }
    }
    padMgr->validCtrlrsMask = mask;
}

// @recomp Patched to only update rumble: the controllers are read in PadMgr_RequestPadData.
RECOMP_PATCH void PadMgr_HandleRetrace(PadMgr* padMgr) {
    // Execute retrace callback
    if (padMgr->retraceCallback != NULL) {
        padMgr->retraceCallback(padMgr, padMgr->retraceCallbackArg);
    }

    if (gFaultMgr.msgId != 0) {
        // If fault is active, no rumble
        PadMgr_RumbleStop(padMgr);
    } else if (padMgr->rumbleOffTimer > 0) {
        // If the rumble off timer is active, no rumble
        --padMgr->rumbleOffTimer;
        PadMgr_RumbleStop(padMgr);
    } else if (padMgr->rumbleOnTimer == 0) {
        // If the rumble on timer is inactive, no rumble
        PadMgr_RumbleStop(padMgr);
    } else if (!padMgr->isResetting) {
        // If not resetting, update rumble
        PadMgr_UpdateRumble(padMgr);
        --padMgr->rumbleOnTimer;
    }
}

// @recomp Patched to read the controllers right when the game asks for its inputs.
RECOMP_PATCH void PadMgr_RequestPadData(PadMgr* padMgr, Input* inputs, s32 gameRequest) {
    s32 i;
    Input* inputIn;
    Input* inputOut;
    s32 buttonDiff;

    if (gameRequest) {
        poll_inputs(padMgr);
        // @recomp Tag the current frame's input polling id for latency tracking.
        recomp_set_current_frame_poll_id();
    }

    PadMgr_LockPadData(padMgr);

    for (inputIn = &padMgr->inputs[0], inputOut = &inputs[0], i = 0; i < MAXCONTROLLERS; i++, inputIn++, inputOut++) {
        if (gameRequest) {
            // Copy inputs as-is, press and rel are calculated prior in `PadMgr_UpdateInputs`
            *inputOut = *inputIn;
            // Zero parts of the press and rel inputs in the polled inputs so they are not read more than once
            inputIn->press.button = 0;
            inputIn->press.stick_x = 0;
            inputIn->press.stick_y = 0;
            inputIn->rel.button = 0;
        } else {
            // Take as the previous inputs the inputs that are currently in the destination array
            inputOut->prev = inputOut->cur;
            // Copy current inputs from the polled inputs
            inputOut->cur = inputIn->cur;
            // Calculate press and rel from these
            buttonDiff = inputOut->prev.button ^ inputOut->cur.button;
            inputOut->press.button = inputOut->cur.button & buttonDiff;
            inputOut->rel.button = inputOut->prev.button & buttonDiff;
            PadUtils_UpdateRelXY(inputOut);
            inputOut->press.stick_x += (s8)(inputOut->cur.stick_x - inputOut->prev.stick_x);
            inputOut->press.stick_y += (s8)(inputOut->cur.stick_y - inputOut->prev.stick_y);
        }
    }

    PadMgr_UnlockPadData(padMgr);
}

// @recomp Patched to wait for the graphics task to complete and for its frame to be shown right after sending it, so
// the next frame starts (and reads its inputs) as late as possible.
RECOMP_PATCH void Graph_TaskSet00(GraphicsContext* gfxCtx) {
    static IrqMgrClient irq_client;
    static OSMesgQueue vi_queue;
    static OSMesg vi_buf[8];
    static bool created = false;
    OSMesg msg;

    if (!created) {
        created = true;
        osCreateMesgQueue(&vi_queue, vi_buf, ARRAY_COUNT(vi_buf));
        IrqMgr_AddClient(&gIrqMgr, &irq_client, &vi_queue);
    }

    // Sands of Time's Graph_TaskSet00: waits for the previous task (already done), then sends this one
    func_8070DD64(gfxCtx);

    // Wait for the task to complete
    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_BLOCK);

    // Wait for the frame to be shown
    if (gfxCtx->task.flags & OS_SC_SWAPBUFFER) {
        s32 vi_count = 0;

        while (osViGetCurrentFramebuffer() != gfxCtx->curFrameBuffer) {
            osRecvMesg(&vi_queue, NULL, OS_MESG_BLOCK);
            vi_count++;
        }

        // If we didn't wait the full number of VIs needed between frames then wait one extra VI afterwards.
        if (vi_count < R_UPDATE_RATE) {
            osRecvMesg(&vi_queue, NULL, OS_MESG_BLOCK);
        }
    }

    // Flush any extra messages from the VI queue.
    while (osRecvMesg(&vi_queue, NULL, OS_MESG_NOBLOCK) == 0) {
        ;
    }

    // The game waits for the task before the next one (and in GameState_Destroy): it's already done.
    osSendMesg(&gfxCtx->queue, NULL, OS_MESG_NOBLOCK);
}
