#ifndef SOT_PLAYER_H
#define SOT_PLAYER_H

// Sands of Time's player (its own player overlay, ovl_kaleido_0001, built from src/system/kaleido/0x01-Player/Player.c
// in the Sands of Time source): names of its functions, which the recompiled game names after their address, and the
// addresses of its variables (use them with SOT_PLAYER_DATA).
//
// Found by building Player.c and matching its functions with the ROM's, and checked against the calls in the ROM.

#include "sot.h"

typedef struct {
    /* 0x00 */ u16 sfxId;
    /* 0x02 */ s16 field;
} PlayerAnimSfxEntry; // struct_80832924

// Functions
#define Player_PlayAnimOnce                       ovl_kaleido_0001_80800708
#define Player_SetRumble                          ovl_kaleido_0001_80800A7C
#define Player_PlayVoiceSfxForAge                 ovl_kaleido_0001_80800ABC
#define Player_PlayAnimSfx                        ovl_kaleido_0001_80800D58
#define Player_EndAnimMovement                    ovl_kaleido_0001_808014B4
#define Player_IsPlayingIdleAnim                  ovl_kaleido_0001_80801F4C
#define Player_PlayIdleAnimSfx                    ovl_kaleido_0001_80801FA0
#define Player_IsFriendlyZTargeting               ovl_kaleido_0001_80802654
#define Player_SetupStartEnemyZTargeting          ovl_kaleido_0001_80802668
#define Player_StepLinearVelToZero                ovl_kaleido_0001_80804784
#define Player_GetTargetVelAndYaw                 ovl_kaleido_0001_8080484C
#define Player_SetupInvincibility_NoDamageFlash   ovl_kaleido_0001_80804D34
#define Player_SetupAction                        ovl_kaleido_0001_80806B00
#define Player_IsActionInterrupted                ovl_kaleido_0001_80806C2C
#define Player_SetupFriendlyZTargetingStandStill  ovl_kaleido_0001_8080999C
#define Player_SetupReturnToStandStill            ovl_kaleido_0001_80809AE8
#define Player_SetupStartMeleeWeaponAttack        ovl_kaleido_0001_8080E0A0
#define Player_SetupRun                           ovl_kaleido_0001_8080E144
#define Player_SetupTurn                          ovl_kaleido_0001_8080E77C
#define Player_SetupUnfriendlyZTarget             ovl_kaleido_0001_8080E8B8
#define Player_SetLookAngle                       ovl_kaleido_0001_8080FA5C
#define Player_SetRunVelAndYaw                    ovl_kaleido_0001_8080FD24
#define Player_AdjustMidairMovement               ovl_kaleido_0001_8080FD9C // func_8083DFE0
#define Player_SetupIdleAnim                      ovl_kaleido_0001_80812AF0
#define Player_StandingStill                      ovl_kaleido_0001_80812CE4 // action: standing still (Player_Action_Idle)
#define Player_SetupSpawnDustAtFeet               ovl_kaleido_0001_80813F3C
#define Player_IsBusy                             ovl_kaleido_0001_808143AC
#define Player_SetQuake                           ovl_kaleido_0001_8081440C
#define Player_Rolling                            ovl_kaleido_0001_80814498 // action: rolling (Player_Action_Roll)
#define Player_SetFirstPersonAimLookAngles        ovl_kaleido_0001_8081A728
#define Player_UpdateCommon                       ovl_kaleido_0001_8081FC40

void Player_PlayAnimOnce(PlayState* play, Player* this, LinkAnimationHeader* anim);
void Player_SetRumble(Player* this, s32 arg1, s32 arg2, s32 arg3, s32 arg4);
void Player_PlayVoiceSfxForAge(Player* this, u16 sfxId);
void Player_PlayAnimSfx(Player* this, PlayerAnimSfxEntry* entry);
void Player_EndAnimMovement(Player* this);
s32 Player_IsPlayingIdleAnim(Player* this);
void Player_PlayIdleAnimSfx(Player* this, s32 arg1);
s32 Player_IsFriendlyZTargeting(Player* this);
s32 Player_SetupStartEnemyZTargeting(Player* this);
s32 Player_StepLinearVelToZero(Player* this);
s32 Player_GetTargetVelAndYaw(Player* this, f32* arg1, s16* arg2, f32 arg3, PlayState* play);
void Player_SetupInvincibility_NoDamageFlash(Player* this, s32 timer);
s32 Player_SetupAction(PlayState* play, Player* this, s8* arg2, s32 arg3);
s32 Player_IsActionInterrupted(PlayState* play, Player* this, SkelAnime* skelAnime, f32 arg3);
void Player_SetupFriendlyZTargetingStandStill(Player* this, PlayState* play);
void Player_SetupReturnToStandStill(Player* this, PlayState* play);
s32 Player_SetupStartMeleeWeaponAttack(Player* this, PlayState* play);
void Player_SetupRun(Player* this, PlayState* play);
void Player_SetupTurn(PlayState* play, Player* this, s16 yaw);
void Player_SetupUnfriendlyZTarget(Player* this, PlayState* play);
void Player_SetLookAngle(Player* this, PlayState* play);
void Player_SetRunVelAndYaw(Player* this, f32 arg1, s16 arg2);
void Player_AdjustMidairMovement(Player* this, f32* arg1, s16* arg2);
void Player_SetupIdleAnim(PlayState* play, Player* this);
void Player_StandingStill(Player* this, PlayState* play);
s32 Player_SetupSpawnDustAtFeet(PlayState* play, Player* this);
s32 Player_IsBusy(Player* this, PlayState* play);
void Player_SetQuake(PlayState* play, s32 speed, s32 y, s32 countdown);
void Player_Rolling(Player* this, PlayState* play);
void Player_UpdateCommon(Player* this, PlayState* play, Input* input);

// Variables (addresses in the ROM's overlay)
#define SOT_PLAYER_ANIMS_STANDING_STILL 0x80821AA4 // LinkAnimationHeader* [PLAYER_MODELTYPE...]: GET_PLAYER_ANIM(PLAYER_ANIMGROUP_0, modelAnimType)
#define SOT_PLAYER_ACTION_STAND_STILL   0x8082198C // s8 sAction_StandStill[]
#define SOT_PLAYER_TOUCHED_WALL_YAW_2   0x80827900 // s32 sTouchedWallYaw2

#endif
