// Widescreen HUD: aligns the HUD to the sides of the screen like Zelda64Recomp (hearts, magic, rupees and keys on the
// left; minimap, buttons and items on the right).
//
// Sands of Time draws its HUD with its own functions (uLib): the vanilla functions only jump to them, so these
// patches set RT64's alignment around them. Texture rectangles (hearts, rupees, buttons...) follow the rectangle
// alignment, and what's drawn with vertices (the beating heart, the D-pad icons, the A button...) the viewport's.

#include "patches.h"

// Sands of Time's HUD functions (uLib)
void func_80710D48(PlayState* play); // Interface_Draw
void func_8070E258(PlayState* play); // Health_DrawMeter
void func_80710220(PlayState* play); // Magic_DrawMeter
void func_8070FF84(PlayState* play); // Minimap_Draw
void func_807108C0(PlayState* play); // Interface_DrawItemButtons

// How much closer to the edges of the screen the HUD is
#define HUD_MARGIN_REDUCTION 8

#define ALIGN_NONE 0
#define ALIGN_LEFT 1
#define ALIGN_RIGHT 2

// Whether the buttons' alignment is still set, until the HUD resets its view after drawing them
static bool sButtonsAligned = false;

static void set_rect_align(Gfx** gfx, s32 align) {
    switch (align) {
        case ALIGN_LEFT:
            gEXSetRectAlign((*gfx)++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, -HUD_MARGIN_REDUCTION * 4, 0,
                            -HUD_MARGIN_REDUCTION * 4, 0);
            break;
        case ALIGN_RIGHT:
            gEXSetRectAlign((*gfx)++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT,
                            -(SCREEN_WIDTH - HUD_MARGIN_REDUCTION) * 4, 0, -(SCREEN_WIDTH - HUD_MARGIN_REDUCTION) * 4,
                            0);
            break;
        default:
            gEXSetRectAlign((*gfx)++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
            break;
    }
}

static void set_viewport_align(Gfx** gfx, s32 align) {
    switch (align) {
        case ALIGN_LEFT:
            gEXSetViewportAlign((*gfx)++, G_EX_ORIGIN_LEFT, -HUD_MARGIN_REDUCTION * 4, 0);
            break;
        case ALIGN_RIGHT:
            gEXSetViewportAlign((*gfx)++, G_EX_ORIGIN_RIGHT, -(SCREEN_WIDTH - HUD_MARGIN_REDUCTION) * 4, 0);
            break;
        default:
            gEXSetViewportAlign((*gfx)++, G_EX_ORIGIN_NONE, 0, 0);
            break;
    }
}

// Scissors follow the viewport's alignment (the HUD's views set the scissor to their viewport, e.g. the A button's
// label); without alignment, they cover the whole screen.
static void set_scissor_align(Gfx** gfx, s32 align) {
    switch (align) {
        case ALIGN_LEFT:
            gEXSetScissorAlign((*gfx)++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, -HUD_MARGIN_REDUCTION, 0,
                               -HUD_MARGIN_REDUCTION, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
        case ALIGN_RIGHT:
            gEXSetScissorAlign((*gfx)++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -(SCREEN_WIDTH - HUD_MARGIN_REDUCTION),
                               0, -(SCREEN_WIDTH - HUD_MARGIN_REDUCTION), 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
        default:
            gEXSetScissorAlign((*gfx)++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, -SCREEN_WIDTH, 0, 0, 0,
                               SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
    }
}

static void apply_ortho_view(InterfaceContext* interfaceCtx) {
    SET_FULLSCREEN_VIEWPORT(&interfaceCtx->view);
    View_ApplyOrthoToOverlay(&interfaceCtx->view);
}

// Set the alignment of what's drawn next, and apply the HUD's view again for its viewport to be aligned.
static void hud_align(PlayState* play, s32 rect, s32 viewport) {
    OPEN_DISPS(play->state.gfxCtx, "../z_parameter.c", 0);
    set_rect_align(&OVERLAY_DISP, rect);
    set_viewport_align(&OVERLAY_DISP, viewport);
    set_scissor_align(&OVERLAY_DISP, (rect == viewport) ? viewport : ALIGN_NONE);
    CLOSE_DISPS(play->state.gfxCtx, "../z_parameter.c", 0);
    apply_ortho_view(&play->interfaceCtx);
}

// Reset the alignment; `scissor`: also stop covering the whole screen with scissors (after the HUD).
static void hud_reset_align(GraphicsContext* gfxCtx, bool scissor) {
    OPEN_DISPS(gfxCtx, "../z_parameter.c", 0);
    set_rect_align(&OVERLAY_DISP, ALIGN_NONE);
    set_viewport_align(&OVERLAY_DISP, ALIGN_NONE);
    if (scissor) {
        gEXSetScissorAlign(OVERLAY_DISP++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0, 0, 0, SCREEN_WIDTH,
                           SCREEN_HEIGHT);
    } else {
        set_scissor_align(&OVERLAY_DISP, ALIGN_NONE);
    }
    CLOSE_DISPS(gfxCtx, "../z_parameter.c", 0);
}

// @recomp Interface_Draw: let scissors cover the whole screen while drawing the HUD, and reset the alignment after.
RECOMP_PATCH void Interface_Draw(PlayState* play) {
    // @recomp Sands of Time's Interface_Draw reads Link's state: don't draw the HUD without him.
    if (GET_PLAYER(play) == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_parameter.c", 0);
    set_scissor_align(&OVERLAY_DISP, ALIGN_NONE);
    CLOSE_DISPS(play->state.gfxCtx, "../z_parameter.c", 0);

    sButtonsAligned = false;
    func_80710D48(play);
    sButtonsAligned = false;

    hud_reset_align(play->state.gfxCtx, true);
}

// @recomp Health_DrawMeter: hearts on the left. What's drawn right after: the D-pad icons (vertices) on the right, and
// the rupees and keys (rectangles) on the left.
RECOMP_PATCH void Health_DrawMeter(PlayState* play) {
    hud_align(play, ALIGN_LEFT, ALIGN_LEFT);
    func_8070E258(play);
    hud_align(play, ALIGN_LEFT, ALIGN_RIGHT);
}

// @recomp Magic_DrawMeter: magic meter on the left.
RECOMP_PATCH void Magic_DrawMeter(PlayState* play) {
    hud_align(play, ALIGN_LEFT, ALIGN_LEFT);
    func_80710220(play);
}

// @recomp Minimap_Draw: minimap on the right, then no alignment for the Z-target reticle drawn next.
RECOMP_PATCH void Minimap_Draw(PlayState* play) {
    hud_align(play, ALIGN_RIGHT, ALIGN_RIGHT);
    func_8070FF84(play);
    hud_align(play, ALIGN_NONE, ALIGN_NONE);
}

// @recomp Interface_DrawItemButtons: buttons on the right, along with the items, ammo counts and A button drawn after
// them, until the HUD resets its view.
RECOMP_PATCH void Interface_DrawItemButtons(PlayState* play) {
    hud_align(play, ALIGN_RIGHT, ALIGN_RIGHT);
    sButtonsAligned = true;
    func_807108C0(play);
}

// @recomp Interface_SetOrthoView: the HUD resets its view after drawing the buttons; reset the alignment there too.
RECOMP_PATCH void func_8008A994(InterfaceContext* interfaceCtx) {
    if (sButtonsAligned) {
        sButtonsAligned = false;
        hud_reset_align(interfaceCtx->view.gfxCtx, false);
    }
    apply_ortho_view(interfaceCtx);
}
