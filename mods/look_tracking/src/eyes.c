// Look tracking, eyes: ModOcarina's Player_UpdateEyes (player_draw.c, 13053ec and b793191), applied to Sands of
// Time's Link.
//
// Like ModOcarina's adult head, Link's face is re-UV'd so that each eye is drawn from its own 32x32 texture,
// mirrored on S to make the 64 texel wide tile of the vanilla UVs (the -z side uses the first half, the +z side the
// mirrored half), drawn through segments 0x0A and 0x0B. The eye textures are ModOcarina's: Sands of Time's eyes are
// the same, 4 texels further right and 1 texel lower in its 64x32 texture of both eyes, so its eye UVs are moved by
// that much. The textures are composited every frame, with the pupils moved toward what Link looks at.

#include "look.h"
#include "eye_textures.h"

// Sands of Time's Player_DrawNew (uLib), which Player_DrawImpl calls
void func_8071921C(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount, s32 lod, s32 tunic, s32 boots,
                   s32 face, OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* data);

void* SystemArena_Malloc(u32 size);

extern u8 sEyeMouthIndices[][2]; // eyes and mouth textures of each face (sPlayerFaces)

// ---------------------------------------------------------------------------------------------------------------------
// The head: Sands of Time's adult Link (object 0x0014), the eyes part of its head display list

#define HEAD_EYES_CALL 0x19F98        // gSPDisplayList(0x06014F10): eyes material (CI8 64x32 texture of both eyes)
#define HEAD_EYES_START 0x19FA0       // eyes and the skin around them
#define HEAD_EYES_END 0x1A070         // gSPDisplayList(0x06015360): next material
#define EYES_MATERIAL 0x14F10         // eyes material display list
#define EYES_MATERIAL_TEXTURE 0x14F48 // its texture loading, after the render settings

#define EYE_TEX_SIZE 32
#define EYE_UV_SHIFT_S (4 << 5) // Sands of Time's eyes are 4 texels further right than ModOcarina's
#define EYE_UV_SHIFT_T (1 << 5) // and 1 texel lower
#define EYE_UV_MIDDLE (32 << 5) // the -z side's eye is in the first half

#define MAX_EYE_VTX 64
#define MAX_EYE_GFX 200

typedef struct {
    u32 eyeBuffers[2][EYE_TEX_SIZE * EYE_TEX_SIZE]; // composited eyes (RGBA32), segments 0x0A and 0x0B
    Vtx vtx[2][MAX_EYE_VTX];                        // re-UV'd eye vertices, for each side
    Gfx gfx[MAX_EYE_GFX];                           // eyes display list
} EyesData;

// In the game's memory, which the RDP reads
static EyesData* sEyes;
static s32 sEyesBuilt;

/**
 * Builds the eyes display list from the head's: each eye drawn from its own texture, with its re-UV'd vertices.
 * Returns false if the head isn't the one expected.
 */
static s32 Eyes_Build(u8* obj) {
    Gfx* gfx;
    Gfx* gfxEnd;
    Gfx* src;
    s32 side;
    s32 i;

    if (sEyes == NULL) {
        sEyes = SystemArena_Malloc(sizeof(EyesData));
        if (sEyes == NULL) {
            return false;
        }
    }

    gfx = sEyes->gfx;
    gfxEnd = gfx + MAX_EYE_GFX - 1;

    // The eyes material's render settings
    for (src = (Gfx*)(obj + EYES_MATERIAL); src < (Gfx*)(obj + EYES_MATERIAL_TEXTURE); src++) {
        *gfx++ = *src;
    }

    for (side = 0; side < 2; side++) {
        Vtx* vtx = sEyes->vtx[side];
        Vtx* loaded[64]; // vertex buffer: the head's vertices (with their original texture coordinates)
        s32 vtxCount = 0;

        for (i = 0; i < ARRAY_COUNT(loaded); i++) {
            loaded[i] = NULL;
        }

        gDPPipeSync(gfx++);
        gDPSetTextureLUT(gfx++, G_TT_NONE);
        gDPLoadTextureBlock(gfx++, (side == 0) ? 0x0A000000 : 0x0B000000, G_IM_FMT_RGBA, G_IM_SIZ_32b, EYE_TEX_SIZE,
                            EYE_TEX_SIZE, 0, G_TX_MIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 5, 5, G_TX_NOLOD,
                            G_TX_NOLOD);
        gDPSetTileSize(gfx++, G_TX_RENDERTILE, 0, 0, (EYE_TEX_SIZE * 2 - 1) << 2, (EYE_TEX_SIZE - 1) << 2);

        // The head's vertices and triangles, keeping the triangles of this side's eye (by their texture coordinates)
        for (src = (Gfx*)(obj + HEAD_EYES_START); src < (Gfx*)(obj + HEAD_EYES_END); src++) {
            u32 w0 = src->words.w0;
            u32 w1 = src->words.w1;
            u8 cmd = w0 >> 24;

            if (gfx >= gfxEnd - 2) {
                return false;
            }

            if (cmd == G_VTX) {
                s32 n = (w0 >> 12) & 0xFF;
                s32 v0 = ((w0 >> 1) & 0x7F) - n;
                Vtx* srcVtx = (Vtx*)(obj + (w1 & 0xFFFFFF));

                if ((vtxCount + n > MAX_EYE_VTX) || (v0 < 0) || (v0 + n > 64) || ((w1 >> 24) != 0x06)) {
                    return false;
                }

                for (i = 0; i < n; i++) {
                    Vtx* v = &vtx[vtxCount + i];

                    *v = srcVtx[i];
                    v->v.tc[0] += (side == 0) ? -EYE_UV_SHIFT_S : EYE_UV_SHIFT_S;
                    v->v.tc[1] -= EYE_UV_SHIFT_T;
                    loaded[v0 + i] = &srcVtx[i];
                }
                gSPVertex(gfx++, &vtx[vtxCount], n, v0);
                vtxCount += n;
            } else if ((cmd == G_TRI1) || (cmd == G_TRI2)) {
                u32 tris[2] = { w0 & 0xFFFFFF, w1 & 0xFFFFFF };
                s32 t;

                for (t = 0; t < ((cmd == G_TRI2) ? 2 : 1); t++) {
                    s32 a = ((tris[t] >> 16) & 0xFF) / 2;
                    s32 b = ((tris[t] >> 8) & 0xFF) / 2;
                    s32 c = (tris[t] & 0xFF) / 2;
                    s32 s;

                    if ((loaded[a] == NULL) || (loaded[b] == NULL) || (loaded[c] == NULL)) {
                        return false;
                    }

                    s = loaded[a]->v.tc[0] + loaded[b]->v.tc[0] + loaded[c]->v.tc[0];
                    if ((s < EYE_UV_MIDDLE * 3) == (side == 0)) {
                        gSP1Triangle(gfx++, a, b, c, 0);
                    }
                }
            } else {
                return false;
            }
        }
    }

    gSPEndDisplayList(gfx++);
    return true;
}

/**
 * Draws Link's eyes with the composited textures: the head calls the eyes display list instead of its eyes, then
 * goes on with the rest of the head. Done when Link's object is (re)loaded.
 */
static s32 Eyes_SetupHead(void) {
    u8* obj = (u8*)PHYSICAL_TO_VIRTUAL(gSegments[6]);
    Gfx* call = (Gfx*)(obj + HEAD_EYES_CALL);

    if (sEyesBuilt && (call[0].words.w0 == ((u32)G_DL << 24)) &&
        (call[0].words.w1 == (u32)VIRTUAL_TO_PHYSICAL(sEyes->gfx))) {
        return true; // already done
    }

    // Only Sands of Time's adult Link
    if ((call[0].words.w0 != ((u32)G_DL << 24)) || (call[0].words.w1 != (u32)(0x06000000 | EYES_MATERIAL)) ||
        (((Gfx*)(obj + HEAD_EYES_END))->words.w0 != ((u32)G_DL << 24))) {
        return false;
    }

    if (!sEyesBuilt) {
        if (!Eyes_Build(obj)) {
            return false;
        }
        sEyesBuilt = true;
    }

    gSPDisplayList(&call[0], VIRTUAL_TO_PHYSICAL(sEyes->gfx));
    gSPBranchList(&call[1], 0x06000000 | HEAD_EYES_END);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The eyes

typedef enum EyeState {
    /* 0 */ EYE_OPEN,
    /* 1 */ EYE_HALF,
    /* 2 */ EYE_CLOSED,
    /* 3 */ EYE_WIDE, // uses the small pupil
    /* 4 */ EYE_WINCING
} EyeState;

// PlayerEyes
#define PLAYER_EYES_LEFT 3
#define PLAYER_EYES_RIGHT 4
#define PLAYER_EYES_WIDE 5
#define PLAYER_EYES_DOWN 6
#define PLAYER_EYES_WINCING 7

static u32* sEyeBaseTextures[] = {
    gLinkAdultEyeOpenTex, gLinkAdultEyeHalfTex, gLinkAdultEyeClosedTex, gLinkAdultEyeWideTex, gLinkAdultEyeWincingTex,
};

static u32* sEyePupilTextures[] = { gLinkAdultPupilTex, gLinkAdultPupilSmallTex };

static u32* sEyeMaskTextures[] = { gLinkAdultEyeMaskOpenTex, gLinkAdultEyeMaskHalfTex, gLinkAdultEyeMaskWideTex };

static s32 sEyeBuffersInitialized;
static s32 sEyeState;
static s16 sPupilX;
static s16 sPupilY;

static s32 Eyes_WrapPupil(s32 v) {
    if (v < 0) {
        return v + EYE_TEX_SIZE;
    }
    if (v > EYE_TEX_SIZE) {
        return v - EYE_TEX_SIZE;
    }
    return v;
}

/**
 * Blends `pupil` over `base` by `mask` (0 to 255).
 */
static u32 Eyes_BlendPixel(u32 base, u32 pupil, u32 mask) {
    if (mask < 225) {
        f32 baseWeight = (255 - mask) * (1.0f / 255.0f);
        f32 pupilWeight = mask * (1.0f / 255.0f);
        u32 r = (u32)(((base >> 24) & 0xFF) * baseWeight + ((pupil >> 24) & 0xFF) * pupilWeight);
        u32 g = (u32)(((base >> 16) & 0xFF) * baseWeight + ((pupil >> 16) & 0xFF) * pupilWeight);
        u32 b = (u32)(((base >> 8) & 0xFF) * baseWeight + ((pupil >> 8) & 0xFF) * pupilWeight);

        return (r << 24) | (g << 16) | (b << 8) | (base & 0xFF);
    }
    return pupil;
}

/**
 * Builds Link's eye textures. The pupils follow the actor Link looks at (see Look_Update), or the direction given
 * by the animation, and are composited over the eye through the eye mask. The second eye is drawn mirrored, so its
 * pupil moves the other way in the texture.
 *
 * @param eyesIndex eye texture requested by the animation or the face, see PlayerEyes
 */
void Eyes_Update(Player* this, s32 eyesIndex) {
    s32 animEyesIndex = (this->skelAnime.jointTable[22].x & 0xF) - 1;
    s32 prevEyeState = sEyeState;
    s16 prevPupilX = sPupilX;
    s16 prevPupilY = sPupilY;
    s16 lookPitch = 0; // angles from where the head faces to the look actor
    s16 lookYaw = 0;
    f32 pupilX;
    f32 pupilY;
    u32* base;
    u32* pupil;
    u32* mask;
    s32 row;
    s32 col;

    if (gLook.lookActor != NULL) {
        // Where the head faces: Link's facing turned by the head limb rotation (see Look_Update)
        lookPitch = Math_Vec3f_Pitch(&this->actor.focus.pos, &gLook.lookActor->focus.pos) - this->unk_6B6;
        lookYaw = Math_Vec3f_Yaw(&this->actor.focus.pos, &gLook.lookActor->focus.pos) - this->actor.shape.rot.y -
                  this->unk_6B8;
    }

    // Looking left, right or down is done by moving the pupils
    if ((eyesIndex == PLAYER_EYES_LEFT) || (eyesIndex == PLAYER_EYES_RIGHT) || (eyesIndex == PLAYER_EYES_DOWN)) {
        sEyeState = EYE_OPEN;
    } else if (eyesIndex == PLAYER_EYES_WIDE) {
        sEyeState = EYE_WIDE;
    } else if (eyesIndex >= PLAYER_EYES_WINCING) {
        sEyeState = EYE_WINCING;
    } else {
        sEyeState = eyesIndex;
    }

    pupilX = BINANG_TO_DEG(lookYaw) * (7.0f / 45.0f) * 0.5f;
    pupilX = CLAMP(pupilX, -7.0f, 7.0f);
    pupilY = BINANG_TO_DEG(lookPitch) * (7.0f / 45.0f) * 0.75f;
    pupilY = CLAMP(pupilY, -6.0f, 6.0f);

    if ((animEyesIndex != -1) && (lookYaw == 0)) {
        switch (animEyesIndex) {
            case PLAYER_EYES_RIGHT:
                pupilX -= 6.0f;
                break;

            case PLAYER_EYES_DOWN:
                pupilY += 4.0f;
                break;

            case PLAYER_EYES_LEFT:
                pupilX += 6.0f;
                break;

            default:
                break;
        }
    }

    Math_StepToS(&sPupilX, pupilX, fabsf(pupilX - sPupilX) * 0.125f + 1.0f);
    Math_StepToS(&sPupilY, pupilY, fabsf(pupilY - sPupilY) * 0.125f + 1.0f);

    if (sEyeBuffersInitialized && (prevEyeState == sEyeState) && (prevPupilX == sPupilX) && (prevPupilY == sPupilY)) {
        return;
    }

    base = sEyeBaseTextures[sEyeState];
    for (row = 0; row < EYE_TEX_SIZE * EYE_TEX_SIZE; row++) {
        sEyes->eyeBuffers[0][row] = base[row];
        sEyes->eyeBuffers[1][row] = base[row];
    }
    sEyeBuffersInitialized = true;

    if ((sEyeState != EYE_OPEN) && (sEyeState != EYE_HALF) && (sEyeState != EYE_WIDE)) {
        return;
    }

    mask = sEyeMaskTextures[(sEyeState < EYE_CLOSED) ? sEyeState : 2];
    pupil = sEyePupilTextures[sEyeState == EYE_WIDE];

    // Only the area of the eye can show the pupil
    for (row = 16; row < 30; row++) {
        for (col = 5; col < 23; col++) {
            s32 i = row * EYE_TEX_SIZE + col;
            u32 m = mask[i] >> 24;
            s32 pupilRow;
            s32 pupilCol0;
            s32 pupilCol1;

            if (m == 0) {
                continue;
            }

            pupilRow = Eyes_WrapPupil(row - sPupilY);
            pupilCol0 = Eyes_WrapPupil(col - sPupilX);
            pupilCol1 = Eyes_WrapPupil(col + sPupilX);

            sEyes->eyeBuffers[0][i] = Eyes_BlendPixel(base[i], pupil[pupilRow * EYE_TEX_SIZE + pupilCol0], m);
            sEyes->eyeBuffers[1][i] = Eyes_BlendPixel(base[i], pupil[pupilRow * EYE_TEX_SIZE + pupilCol1], m);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------

// @mod Player_DrawImpl: looking around (once per frame, before drawing Link), and Link's eyes.
RECOMP_PATCH void Player_DrawImpl(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount, s32 lod,
                                  s32 tunic, s32 boots, s32 face, OverrideLimbDrawOpa overrideLimbDraw,
                                  PostLimbDrawOpa postLimbDraw, void* data) {
    static u32 sLastLookFrame = 0xFFFFFFFF;

    if (LINK_IS_ADULT && Eyes_SetupHead()) {
        if (data == GET_PLAYER(play)) {
            Player* this = (Player*)data;
            s32 eyesIndex;

            if (play->gameplayFrames != sLastLookFrame) {
                sLastLookFrame = play->gameplayFrames;
                Look_Update(this, play);
            }

            eyesIndex = (jointTable[22].x & 0xF) - 1;
            if (eyesIndex < 0) {
                eyesIndex = sEyeMouthIndices[face][0];
            }
            Eyes_Update(this, eyesIndex);
        } else if (!sEyeBuffersInitialized) {
            s32 i;

            // Open eyes until Link is drawn in gameplay
            for (i = 0; i < EYE_TEX_SIZE * EYE_TEX_SIZE; i++) {
                sEyes->eyeBuffers[0][i] = sEyes->eyeBuffers[1][i] = gLinkAdultEyeOpenTex[i];
            }
        }

        OPEN_DISPS(play->state.gfxCtx, "", 0);
        gSPSegment(POLY_OPA_DISP++, 0x0A, sEyes->eyeBuffers[0]);
        gSPSegment(POLY_OPA_DISP++, 0x0B, sEyes->eyeBuffers[1]);
        CLOSE_DISPS(play->state.gfxCtx, "", 0);
    }

    func_8071921C(play, skeleton, jointTable, dListCount, lod, tunic, boots, face, overrideLimbDraw, postLimbDraw,
                  data);
}
