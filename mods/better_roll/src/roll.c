// Better Roll: controlled and faster rolling, from thinedave's ocarina-things (741b0f4, "Controlled/Faster rolling"),
// applied to Sands of Time's player (Player_Rolling and Player_StandingStill in its Player.c).

#include "sot_player.h"

#define PLAYER_STATE2_DISABLE_MOVE_ROTATION_WHILE_Z_TARGETING (1 << 5)

static LinkAnimationHeader* get_player_anim(Player* this, s32 group) {
    return SOT_PLAYER_DATA(LinkAnimationHeader*, SOT_PLAYER_ANIMS_STANDING_STILL)[group * PLAYER_ANIMTYPE_MAX +
                                                                                  this->modelAnimType];
}

// @mod Player_Rolling: attacking out of a roll waits for the end of the roll, the roll is 1.75 times faster than
// walking (instead of 1.5 times) and follows the control stick.
RECOMP_PATCH void Player_Rolling(Player* this, PlayState* play) {
    static PlayerAnimSfxEntry sAnimSfx_Roll[] = {
        { NA_SE_VO_LI_AUTO_JUMP, 0x2001 },
        { NA_SE_PL_WALK_GROUND, 0x1806 },
        { NA_SE_PL_ROLL, 0x806 },
        { 0, -0x2812 },
    };
    Actor* cylinderOc;
    s32 temp;
    s32 animFinished;
    DynaPolyActor* wallPolyActor;
    f32 speedTarget;
    s16 yawTarget;

    this->stateFlags2 |= PLAYER_STATE2_DISABLE_MOVE_ROTATION_WHILE_Z_TARGETING;

    cylinderOc = NULL;
    animFinished = LinkAnimation_Update(play, &this->skelAnime);

    if (LinkAnimation_OnFrame(&this->skelAnime, 8.0f)) {
        Player_SetupInvincibility_NoDamageFlash(this, -10);
    }

    if (Player_IsBusy(this, play) == 0) {
        if (this->av2.actionVar2 != 0) {
            Math_StepToF(&this->speedXZ, 0.0f, 2.0f);

            temp = Player_IsActionInterrupted(play, this, &this->skelAnime, 5.0f);
            if ((temp != 0) && ((temp > 0) || animFinished)) {
                Player_SetupReturnToStandStill(this, play);
            }
        } else {
            if (this->speedXZ >= 7.0f) {
                if (((this->actor.bgCheckFlags & BGCHECKFLAG_PLAYER_WALL_INTERACT) &&
                     (*SOT_PLAYER_DATA(s32, SOT_PLAYER_TOUCHED_WALL_YAW_2) < 0x2000)) ||
                    ((this->cylinder.base.ocFlags1 & OC1_HIT) &&
                     (cylinderOc = this->cylinder.base.oc,
                      ((cylinderOc->id == ACTOR_EN_WOOD02) &&
                       (ABS((s16)(this->actor.world.rot.y - cylinderOc->yawTowardsPlayer)) > 0x6000))))) {
                    if (cylinderOc != NULL) {
                        cylinderOc->home.rot.y = 1;
                    } else if (this->actor.wallBgId != BGCHECK_SCENE) {
                        wallPolyActor = DynaPoly_GetActor(&play->colCtx, this->actor.wallBgId);
                        if ((wallPolyActor != NULL) && (wallPolyActor->actor.id == ACTOR_OBJ_KIBAKO2)) {
                            wallPolyActor->actor.home.rot.z = 1;
                        }
                    }

                    Player_PlayAnimOnce(play, this, get_player_anim(this, PLAYER_ANIMGROUP_hip_down));
                    this->speedXZ = -this->speedXZ;
                    Player_SetQuake(play, 33267, 3, 12);
                    Player_SetRumble(this, 255, 20, 150, 0);
                    func_8002F7DC(&this->actor, NA_SE_PL_BODY_HIT);
                    Player_PlayVoiceSfxForAge(this, NA_SE_VO_LI_CLIMB_END);
                    this->av2.actionVar2 = 1;

                    return;
                }
            }

            // @mod Attacking out of the roll waits for the end of the roll (was: from frame 15)
            if (!animFinished || !Player_SetupStartMeleeWeaponAttack(this, play)) {
                if (this->skelAnime.curFrame >= 20.0f) {
                    Player_SetupReturnToStandStill(this, play);

                    return;
                }

                Player_GetTargetVelAndYaw(this, &speedTarget, &yawTarget, 0.018f, play);

                // @mod 1.75 times the walking speed (was 1.5)
                speedTarget *= 1.75f;
                if ((speedTarget < 3.0f) || (this->controlStickDirections[this->controlStickDataIndex] != 0)) {
                    speedTarget = 3.0f;
                }

                // @mod Roll toward the control stick (was: the way Link faces)
                Player_SetRunVelAndYaw(this, speedTarget, yawTarget);

                if (Player_SetupSpawnDustAtFeet(play, this)) {
                    func_8002F8F0(&this->actor, NA_SE_PL_ROLL_DUST - SFX_FLAG);
                }

                Player_PlayAnimSfx(this, sAnimSfx_Roll);
            }
        }
    }
}

// @mod Player_StandingStill: only slow Link down when he doesn't start moving (or while he's stunned by a fall), so
// he keeps his speed when he starts moving.
RECOMP_PATCH void Player_StandingStill(Player* this, PlayState* play) {
    s32 idleAnim;
    s32 animFinished;
    f32 speedTarget;
    s16 yawTarget;
    s16 temp;

    idleAnim = Player_IsPlayingIdleAnim(this);
    animFinished = LinkAnimation_Update(play, &this->skelAnime);

    if (idleAnim > 0) {
        Player_PlayIdleAnimSfx(this, idleAnim - 1);
    }

    if (animFinished != 0) {
        if (this->av2.actionVar2 != 0) {
            if (DECR(this->av2.actionVar2) == 0) {
                this->skelAnime.endFrame = this->skelAnime.animLength - 1.0f;
            }
            this->skelAnime.jointTable[0].y =
                (this->skelAnime.jointTable[0].y + ((this->av2.actionVar2 & 1) * 0x50)) - 0x28;
        } else {
            Player_EndAnimMovement(this);
            Player_SetupIdleAnim(play, this);
        }
    }

    if (this->av2.actionVar2 == 0) {
        if (!Player_SetupAction(play, this, SOT_PLAYER_DATA(s8, SOT_PLAYER_ACTION_STAND_STILL), 1)) {
            if (Player_SetupStartEnemyZTargeting(this)) {
                Player_SetupUnfriendlyZTarget(this, play);

                return;
            }

            if (Player_IsFriendlyZTargeting(this)) {
                Player_SetupFriendlyZTargetingStandStill(this, play);

                return;
            }

            Player_GetTargetVelAndYaw(this, &speedTarget, &yawTarget, 0.018f, play);

            if (speedTarget != 0.0f) {
                // Player_SetupZTargetRunning
                this->actor.shape.rot.y = this->yaw = yawTarget;
                Player_SetupRun(this, play);

                return;
            }

            // @mod Slow down here (was: before handling the actions)
            Player_StepLinearVelToZero(this);

            temp = yawTarget - this->actor.shape.rot.y;
            if (ABS(temp) > 800) {
                Player_SetupTurn(play, this, yawTarget);

                return;
            }

            Math_ScaledStepToS(&this->actor.shape.rot.y, yawTarget, 1200);
            this->yaw = this->actor.shape.rot.y;
            if (get_player_anim(this, PLAYER_ANIMGROUP_wait) == this->skelAnime.animation) {
                Player_SetLookAngle(this, play);
            }
        }
    } else {
        // @mod Stunned by a fall
        Player_StepLinearVelToZero(this);
    }
}
