// Look tracking, head: ModOcarina's Player_UpdateLook (player_look.c, 13053ec), applied to Sands of Time's player.

#include "look.h"

#define PLAYER_STATE1_TALKING (1 << 6)
#define PLAYER_STATE1_PARALLEL (1 << 17)
#define PLAYER_STATE2_IDLE_FIDGET (1 << 28)

void func_8002DBD0(Actor* actor, Vec3f* result, Vec3f* arg2); // Actor_WorldToActorCoords

LookState gLook;

/**
 * Finds the closest drawn actor of `category`, searching in growing steps up to `maxDist` around Link.
 */
static Actor* Look_FindActor(PlayState* play, Actor* exclude, s32 category, f32 maxDist) {
    Actor* head = play->actorCtx.actorLists[category].head;
    Actor* actor = head;
    f32 step = maxDist * 0.125f;
    f32 dist = 20.0f;

    while (actor != NULL) {
        if ((exclude != NULL) && (actor == exclude)) {
            actor = actor->next;
            if (actor == NULL) {
                return NULL;
            }
        }

        if ((actor->xzDistToPlayer <= dist) && (actor->yDistToPlayer <= dist) && (actor->draw != NULL)) {
            return actor;
        }

        actor = actor->next;
        if (actor == NULL) {
            dist += step;
            if (maxDist < dist) {
                return NULL;
            }
            actor = head;
        }
    }

    return actor;
}

/**
 * Returns true if `actor` can not be looked at: missing, behind Link or too far above or below.
 * `relPos` gets the position of the actor relative to Link.
 */
static s32 Look_IsOutOfView(Player* this, Actor* actor, Vec3f* relPos, f32 range) {
    Vec3f pos;

    if (actor == NULL) {
        return true;
    }

    func_8002DBD0(&this->actor, &pos, &actor->world.pos);

    if (pos.z < -(range * 0.5f)) {
        return true;
    }

    if (range < fabsf(pos.y)) {
        return true;
    }

    *relPos = pos;
    return false;
}

/**
 * Turns Link's head toward the closest interesting actor: a boss, else an enemy, an NPC, a prop or a door. The
 * actor Link is locked on to has priority. His pupils follow it too, see Eyes_Update. When there is nothing to look
 * at, his head tilts into turns.
 */
void Look_Update(Player* this, PlayState* play) {
    Vec3s* jointTable = this->skelAnime.jointTable;
    Actor* focusActor = this->unk_664;
    Vec3f relPos = { 0.0f, 0.0f, 0.0f };
    Actor* found;
    Actor* target;
    s16 pitch;
    s16 yaw;
    f32 scale;

    gLook.lookActor = NULL;

    if (((this->stateFlags1 & (PLAYER_STATE1_29 | PLAYER_STATE1_TALKING)) == PLAYER_STATE1_29) ||
        (this->stateFlags1 & (PLAYER_STATE1_PARALLEL | PLAYER_STATE1_20)) ||
        (this->stateFlags2 & PLAYER_STATE2_IDLE_FIDGET) || Player_InCsMode(play)) {
        goto reset;
    }

    found = play->actorCtx.actorLists[ACTORCAT_BOSS].head;

    if (found == NULL) {
        found = Look_FindActor(play, this->heldActor, ACTORCAT_ENEMY, 360.0f);
        if (Look_IsOutOfView(this, found, &relPos, 360.0f)) {
            found = Look_FindActor(play, this->heldActor, ACTORCAT_NPC, 160.0f);
            if (Look_IsOutOfView(this, found, &relPos, 160.0f)) {
                found = Look_FindActor(play, this->heldActor, ACTORCAT_PROP, 160.0f);
                if (Look_IsOutOfView(this, found, &relPos, 160.0f)) {
                    found = Look_FindActor(play, this->heldActor, ACTORCAT_DOOR, 160.0f);
                    if (!Look_IsOutOfView(this, found, &relPos, 80.0f)) {
                        // Look at the middle of the door
                        found->focus.pos.y = found->world.pos.y + 35.0f;
                    } else {
                        found = NULL;
                    }
                }
            }
        }
    }

    target = (focusActor != NULL) ? focusActor : found;
    if (target == NULL) {
        goto reset;
    }

    pitch = Math_Vec3f_Pitch(&this->actor.focus.pos, &target->focus.pos);
    yaw = (s16)(Math_Vec3f_Yaw(&this->actor.focus.pos, &target->focus.pos) - this->actor.shape.rot.y);

    // Too close to look at, except for bosses
    if ((fabsf(relPos.z) < 15.0f) && (fabsf(relPos.x) < 15.0f) && (target->category != ACTORCAT_BOSS)) {
        yaw = 0;
    }

    // Keep the yaw while the target is behind
    if ((yaw > 30947) || (yaw < -30947)) {
        if (gLook.lookYawLock == 0) {
            gLook.lookYawLock = yaw;
        }
        yaw = gLook.lookYawLock;
    } else {
        gLook.lookYawLock = 0;
    }

    // Only enemies and bosses are followed far on the sides
    if (((yaw >= 21846) || (yaw < -21845)) && (target->category != ACTORCAT_ENEMY) &&
        (target->category != ACTORCAT_BOSS)) {
        yaw = 0;
        pitch = 0;
    } else {
        gLook.lookActor = target;
    }

    // Mouth open while looking at an enemy that is not locked on to
    if ((focusActor != target) && ((target->category == ACTORCAT_ENEMY) || (target->category == ACTORCAT_BOSS))) {
        jointTable[22].x = (jointTable[22].x & 0xF) + 0x20;
    }

    Math_ApproachS(&gLook.lookPitch, CLAMP(pitch, -4000, 4000), 7, 1500);
    Math_ApproachS(&gLook.lookYaw, CLAMP(yaw, -8000, 8000) + jointTable[PLAYER_LIMB_HEAD].y, 7, 1500);
    Math_ApproachS(&gLook.lookRoll, 0, 7, 1500);
    goto apply;

reset:
    Math_ApproachS(&gLook.lookPitch, 0, 7, 1500);
    Math_ApproachS(&gLook.lookYaw, 0, 7, 1500);
    gLook.lookYawLock = 0;
    Math_ApproachS(&gLook.lookRoll, CLAMP(gLook.turnYawDiff, -8191, 8191), 7, 1500);

apply:
    // Look less while moving
    scale = CLAMP(1.0f - this->speedXZ / 7.0f, 0.5f, 1.0f);

    // Head limb rotation (headLimbRot.x and .y)
    this->unk_6B6 = gLook.lookPitch * scale;
    this->unk_6B8 = (s16)(gLook.lookYaw - gLook.lookRoll) * scale;

    // Set again while moving, until the next update
    gLook.turnYawDiff = 0;
}

// @mod Player_SetRunVelAndYaw: head tilt into turns.
RECOMP_PATCH void Player_SetRunVelAndYaw(Player* this, f32 arg1, s16 arg2) {
    gLook.turnYawDiff = this->yaw - arg2;
    Math_AsymStepToF(&this->speedXZ, arg1, REG(19) / 100.0f, 1.5f);
    Math_ScaledStepToS(&this->yaw, arg2, REG(27));
}

// @mod Player_AdjustMidairMovement (func_8083DFE0): head tilt into turns.
RECOMP_PATCH void Player_AdjustMidairMovement(Player* this, f32* arg1, s16* arg2) {
    s16 yawDiff = this->yaw - *arg2;

    gLook.turnYawDiff = yawDiff;

    if (this->meleeWeaponState == 0) {
        this->speedXZ = CLAMP(this->speedXZ, -(R_RUN_SPEED_LIMIT / 100.0f), (R_RUN_SPEED_LIMIT / 100.0f));
    }

    if (ABS(yawDiff) > 0x6000) {
        if (Math_StepToF(&this->speedXZ, 0.0f, 1.0f)) {
            this->yaw = *arg2;
        }
    } else {
        Math_AsymStepToF(&this->speedXZ, *arg1, 0.05f, 0.1f);
        Math_ScaledStepToS(&this->yaw, *arg2, 200);
    }
}
