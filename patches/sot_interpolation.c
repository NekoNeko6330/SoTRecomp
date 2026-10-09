// High framerate interpolation: tag what's drawn with RT64 matrix groups, so that RT64 interpolates each actor, each
// limb of a skeleton and each particle with its own transform (like Zelda64Recomp). Without them, RT64 has to guess
// which matrices match between two frames, which mixes up limbs (Link's legs going through his tunic) and particles.

#include "patches.h"
#include "sot_interpolation.h"

// Sands of Time's own Actor_Init and Actor_Delete (uLib)
void func_8070A6D0(Actor* actor, PlayState* play);
Actor* func_8070B038(ActorContext* actorCtx, Actor* actor, PlayState* play);

// Vanilla functions (z64hdr names)
void func_80026400(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026608(PlayState* play);
void func_80026860(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026A6C(PlayState* play);

extern EffectSsInfo sEffectSsInfo;

// ---------------------------------------------------------------------------------------------------------------------
// Actor transform ids: an open addressing hash table from the actor's address to its id, set when the actor is
// initialized (a new actor at the same address gets a new id, so it isn't interpolated from the previous one).

#define ACTOR_ID_TABLE_SIZE 2048 // power of 2
#define ACTOR_ID_COUNT 0x7000    // ids cycle through this many actors

typedef struct {
    u32 actor; // 0: empty, 1: deleted
    u32 id;
} ActorIdEntry;

static ActorIdEntry actor_ids[ACTOR_ID_TABLE_SIZE];
static u32 actor_id_used;
static u32 next_actor_id;

static u32 actor_id_hash(u32 actor) {
    return (actor >> 3) * 0x9E3779B1U >> 21;
}

static ActorIdEntry* actor_id_find(u32 actor) {
    u32 i = actor_id_hash(actor) & (ACTOR_ID_TABLE_SIZE - 1);
    u32 n;
    for (n = 0; n < ACTOR_ID_TABLE_SIZE; n++) {
        ActorIdEntry* entry = &actor_ids[i];
        if (entry->actor == actor) {
            return entry;
        }
        if (entry->actor == 0) {
            return NULL;
        }
        i = (i + 1) & (ACTOR_ID_TABLE_SIZE - 1);
    }
    return NULL;
}

static void actor_id_insert(u32 actor, u32 id) {
    u32 i = actor_id_hash(actor) & (ACTOR_ID_TABLE_SIZE - 1);
    for (;;) {
        ActorIdEntry* entry = &actor_ids[i];
        if ((entry->actor == 0) || (entry->actor == 1) || (entry->actor == actor)) {
            if (entry->actor == 0) {
                actor_id_used++;
            }
            entry->actor = actor;
            entry->id = id;
            return;
        }
        i = (i + 1) & (ACTOR_ID_TABLE_SIZE - 1);
    }
}

static void actor_id_set(Actor* actor) {
    ActorIdEntry* entry = actor_id_find((u32)actor);
    u32 id = ACTOR_TRANSFORM_ID_START + (next_actor_id % ACTOR_ID_COUNT) * ACTOR_TRANSFORM_ID_COUNT;
    next_actor_id++;

    if (entry != NULL) {
        entry->id = id;
        return;
    }

    // Rebuild the table when deleted entries fill it up
    if (actor_id_used >= ACTOR_ID_TABLE_SIZE / 2) {
        static ActorIdEntry old[ACTOR_ID_TABLE_SIZE];
        u32 i;
        for (i = 0; i < ACTOR_ID_TABLE_SIZE; i++) {
            old[i] = actor_ids[i];
            actor_ids[i].actor = 0;
        }
        actor_id_used = 0;
        for (i = 0; i < ACTOR_ID_TABLE_SIZE; i++) {
            if (old[i].actor > 1) {
                actor_id_insert(old[i].actor, old[i].id);
            }
        }
    }
    actor_id_insert((u32)actor, id);
}

static void actor_id_remove(Actor* actor) {
    ActorIdEntry* entry = actor_id_find((u32)actor);
    if (entry != NULL) {
        entry->actor = 1;
    }
}

u32 sot_actor_transform_id(void* actor) {
    ActorIdEntry* entry;
    if (actor == NULL) {
        return 0;
    }
    entry = actor_id_find((u32)actor);
    return (entry != NULL) ? entry->id : 0;
}

Gfx* sot_push_limb_group(Gfx* gfx, void* arg, s32 limb_index, bool post_limb) {
    u32 id = sot_actor_transform_id(arg);
    if (id != 0) {
        id += limb_index + (post_limb ? ACTOR_TRANSFORM_LIMB_COUNT : 0);
        gEXMatrixGroupDecomposedNormal(gfx++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
    }
    return gfx;
}

Gfx* sot_pop_group(Gfx* gfx, void* arg) {
    if (sot_actor_transform_id(arg) != 0) {
        gEXPopMatrixGroup(gfx++, G_MTX_MODELVIEW);
    }
    return gfx;
}

// @recomp Give the actor a transform id, then Sands of Time's Actor_Init.
RECOMP_PATCH void Actor_Init(Actor* actor, PlayState* play) {
    actor_id_set(actor);
    func_8070A6D0(actor, play);
}

// @recomp Sands of Time's Actor_Delete, then forget the actor's transform id.
RECOMP_PATCH Actor* Actor_Delete(ActorContext* actorCtx, Actor* actor, PlayState* play) {
    Actor* ret = func_8070B038(actorCtx, actor, play);
    actor_id_remove(actor);
    return ret;
}

// ---------------------------------------------------------------------------------------------------------------------
// Actors drawn with a single matrix (not a skeleton) are tagged with their id, like Zelda64Recomp does: the space for
// the matrix group command is reserved before the actor is drawn, and filled if it drew exactly one matrix.

static s32 count_matrices(Gfx* start, Gfx* end) {
    s32 count = 0;
    Gfx* cur;
    for (cur = start; cur != end; cur++) {
        if ((cur->words.w0 >> 24) == G_MTX) {
            count++;
        }
    }
    return count;
}

// @recomp Patched to tag the actor's matrix (Sands of Time's version only adds its own fault screen information).
RECOMP_PATCH void Actor_Draw(PlayState* play, Actor* actor) {
    Lights* lights;
    Gfx* opa_tag;
    Gfx* xlu_tag;
    u32 id;

    OPEN_DISPS(play->state.gfxCtx, "../z_actor.c", 6035);

    lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);

    Lights_BindAll(lights, play->lightCtx.listHead,
                   (actor->flags & ACTOR_FLAG_IGNORE_POINT_LIGHTS) ? NULL : &actor->world.pos);
    Lights_Draw(lights, play->state.gfxCtx);

    if (actor->flags & ACTOR_FLAG_IGNORE_QUAKE) {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x + play->mainCamera.quakeOffset.x,
                                     actor->world.pos.y +
                                         ((actor->shape.yOffset * actor->scale.y) + play->mainCamera.quakeOffset.y),
                                     actor->world.pos.z + play->mainCamera.quakeOffset.z, &actor->shape.rot);
    } else {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + (actor->shape.yOffset * actor->scale.y),
                                     actor->world.pos.z, &actor->shape.rot);
    }

    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    Actor_SetObjectDependency(play, actor);

    gSPSegment(POLY_OPA_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);

    if (actor->colorFilterTimer != 0) {
        Color_RGBA8 color = { 0, 0, 0, 255 };

        if (actor->colorFilterParams & COLORFILTER_COLORFLAG_GRAY) {
            color.r = color.g = color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else if (actor->colorFilterParams & COLORFILTER_COLORFLAG_RED) {
            color.r = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else {
            color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        }

        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026860(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        } else {
            func_80026400(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        }
    }

    // @recomp Reserve space for a matrix group command in each display list.
    opa_tag = POLY_OPA_DISP;
    xlu_tag = POLY_XLU_DISP;
    gDPNoOp(POLY_OPA_DISP++);
    gDPNoOp(POLY_OPA_DISP++);
    gDPNoOp(POLY_XLU_DISP++);
    gDPNoOp(POLY_XLU_DISP++);

    actor->draw(actor, play);

    // @recomp Tag the actor's matrix if it drew a single one in each display list.
    id = sot_actor_transform_id(actor);
    if (id != 0) {
        s32 opa_matrices = count_matrices(opa_tag + 2, POLY_OPA_DISP);
        s32 xlu_matrices = count_matrices(xlu_tag + 2, POLY_XLU_DISP);

        if ((opa_matrices == 1 || xlu_matrices == 1) && opa_matrices <= 1 && xlu_matrices <= 1) {
            if (opa_matrices == 1) {
                gEXMatrixGroupDecomposedNormal(opa_tag, id + ACTOR_TRANSFORM_ID_COUNT - 2, G_EX_PUSH, G_MTX_MODELVIEW,
                                               G_EX_EDIT_ALLOW);
                gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
            }
            if (xlu_matrices == 1) {
                gEXMatrixGroupDecomposedNormal(xlu_tag, id + ACTOR_TRANSFORM_ID_COUNT - 1, G_EX_PUSH, G_MTX_MODELVIEW,
                                               G_EX_EDIT_ALLOW);
                gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);
            }
        }
    }

    if (actor->colorFilterTimer != 0) {
        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026A6C(play);
        } else {
            func_80026608(play);
        }
    }

    if (actor->shape.shadowDraw != NULL) {
        actor->shape.shadowDraw(actor, lights, play);
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_actor.c", 6119);
}

// ---------------------------------------------------------------------------------------------------------------------
// Particles (soft sprite effects: Navi's sparkles, fire, dust, ...): each slot of the effect table has its own matrix
// group, and a new effect in a slot isn't interpolated from the previous one.

#define PARTICLE_SLOTS 0x400

static s16 particle_prev_life[PARTICLE_SLOTS];

// @recomp Draw each particle in its own matrix group.
RECOMP_PATCH void EffectSs_Draw(PlayState* play, s32 index) {
    EffectSs* effectSs = &sEffectSsInfo.table[index];

    if (effectSs->draw != NULL) {
        u32 id = PARTICLE_TRANSFORM_ID_START + index;
        // The life of an effect counts down: a new effect in this slot if it went up
        bool is_new = (index < PARTICLE_SLOTS) && (effectSs->life > particle_prev_life[index]);

        OPEN_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);
        if (is_new) {
            gEXMatrixGroupDecomposedSkipAll(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
            gEXMatrixGroupDecomposedSkipAll(POLY_XLU_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        } else {
            gEXMatrixGroupDecomposedNormal(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
            gEXMatrixGroupDecomposedNormal(POLY_XLU_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        }
        CLOSE_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);

        effectSs->draw(play, index, effectSs);

        OPEN_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);
        gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
        gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);
        CLOSE_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);
    }

    if (index < PARTICLE_SLOTS) {
        particle_prev_life[index] = (effectSs->draw != NULL) ? effectSs->life : -1;
    }
}
