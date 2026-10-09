// Zelda64Recomp's enhancements for Sands of Time: analog camera, gyro/mouse/right stick aiming, autosave,
// Z-targeting mode, low health beeps and background music volume.
//
// Sands of Time replaces many vanilla functions with its own (z64rom's uLib, at 0x80700000) and has its own
// player overlay, so these patches work around its code: its functions are named after their address here
// (e.g. func_8070BA9C), as the recompiled game names them.

#include "patches.h"
#include "input.h"
#include "sound.h"
#include "misc_funcs.h"

// Sands of Time's functions (uLib)
Vec3s func_8070BA9C(Camera* camera); // Camera_Update
s32 func_807008EC(s32 flag);          // CustomFlag_Get

// Sands of Time's player overlay
s32 ovl_kaleido_0001_8080205C(Player* this);           // Player_IsAimingReady_Boomerang
s32 ovl_kaleido_0001_80804140(Player* this, s32 arg1); // Player_UpdateLookRot

// Vanilla functions (z64hdr names)
void Play_SaveSceneFlags(PlayState* play);
void Sram_WriteSave(SramContext* sramCtx);
u32 Health_IsCritical(void);
void Audio_SequenceChannelProcessSound(SequenceChannel* channel, s32 recalculateVolume, s32 applyBend);
void AudioSeq_SequencePlayerDisable(SequencePlayer* seqPlayer);

extern u8 sOcarinaInstrumentId;

static f32 sot_fabsf(f32 x) {
    return x < 0.0f ? -x : x;
}

// ---------------------------------------------------------------------------------------------------------------------
// Analog camera: ModOcarina's controller 2 camera (bf713ce), on the right stick.
//
// The right stick moves the camera around Link (Y inverted, like ModOcarina, unless inverted in the options). While the
// stick is held, the camera stops following Link on its own; it does again once the stick is released. Sands of Time's
// Camera_Normal1 already keeps its swing timer (it doesn't swing back behind Link on its own), like ModOcarina's.

// Largest f32 (FLT_MAX): with this update rate the camera does not turn on its own
#define CAM_UPDATE_RATE_INV_NONE 3.40282347e+38f

// Camera speed, relative to Sunken Tower
#define CAM_STICK2_YAW_SPEED 2.25f
#define CAM_STICK2_PITCH_SPEED 2.5f

s32 Camera_ChangeMode(Camera* camera, s16 mode); // Camera_RequestMode

static Vec3f add_geo(Vec3f* origin, VecGeo* geo) {
    Vec3f v;
    f32 horizontal = geo->r * Math_CosS(geo->pitch);

    v.x = origin->x + horizontal * Math_SinS(geo->yaw);
    v.y = origin->y + geo->r * Math_SinS(geo->pitch);
    v.z = origin->z + horizontal * Math_CosS(geo->yaw);
    return v;
}

static void analog_cam_update(Camera* camera) {
    static s32 sStickActive = false;
    PlayState* play = camera->play;
    f32 stickX;
    f32 stickY;
    s32 inverted_x;
    s32 inverted_y;
    VecGeo geo;
    s16 speed;
    s16 step;

    // First person and aiming use the right stick to aim instead
    if ((camera->status != CAM_STAT_ACTIVE) || (play->pauseCtx.state != 0) ||
        (camera->mode == CAM_MODE_FIRST_PERSON) || (camera->mode == CAM_MODE_AIM_ADULT) ||
        (camera->mode == CAM_MODE_AIM_CHILD) || (camera->mode == CAM_MODE_AIM_BOOMERANG)) {
        stickX = stickY = 0.0f;
    } else {
        recomp_get_camera_inputs(&stickX, &stickY);
        // Like controller 2's stick: up is positive
        stickY = -stickY;

        recomp_get_analog_inverted_axes(&inverted_x, &inverted_y);
        if (inverted_x) {
            stickX = -stickX;
        }
        if (inverted_y) {
            stickY = -stickY;
        }
    }

    // Dead zone
    if (sot_fabsf(stickX) < 0.02f) {
        stickX = 0.0f;
    }
    if (sot_fabsf(stickY) < 0.02f) {
        stickY = 0.0f;
    }

    if ((stickX == 0.0f) && (stickY == 0.0f)) {
        if (sStickActive) {
            // Let the camera follow Link again
            camera->yawUpdateRateInv = camera->pitchUpdateRateInv = 6900.0f;
            sStickActive = false;
        }
        return;
    }

    sStickActive = true;

    {
        // Square the magnitude, boost the diagonals
        Vec3f zero = { 0.0f, 0.0f, 0.0f };
        Vec3f stick;
        s16 angle;
        s16 yaw;
        f32 mag;
        f32 diagonalBoost;

        stick.x = stickX;
        stick.y = 0.0f;
        stick.z = stickY;

        angle = Math_Vec3f_Yaw(&zero, &stick);
        if (angle < 0) {
            angle = -angle;
        }
        mag = Math_Vec3f_DistXZ(&zero, &stick);
        mag = mag * mag;
        if (angle > 0x4000) {
            angle -= 0x4000;
        }
        angle = ABS((s16)(angle - 0x2000));
        if (angle > 0x2000) {
            angle = 0x2000;
        }
        diagonalBoost = SQ(angle * (1.0f / 0x2000) - 1.0f) * 1.1f + 1.0f;

        yaw = Math_Vec3f_Yaw(&zero, &stick);
        stickX = Math_SinS(yaw) * mag * 1.35f * diagonalBoost;
        stickY = Math_CosS(yaw) * mag * 1.35f * diagonalBoost;
    }

    if ((camera->mode == CAM_MODE_JUMP) || (camera->mode == CAM_MODE_LEDGE_HANG) ||
        (camera->mode == CAM_MODE_FREE_FALL)) {
        Camera_ChangeMode(camera, CAM_MODE_NORMAL);
    }

    camera->pitchUpdateRateInv = camera->yawUpdateRateInv = CAM_UPDATE_RATE_INV_NONE;
    camera->rUpdateRateInv = 17.0f;

    if ((camera->mode == CAM_MODE_NORMAL) && (camera->fov >= 58.9f)) {
        camera->fov = 58.9f;
    }

    geo.r = camera->dist;
    geo.yaw = Math_Vec3f_Yaw(&camera->at, &camera->eye);
    geo.pitch = Math_Vec3f_Pitch(&camera->eye, &camera->at);

    speed = camera->dist * 0.2f + 512;

    // Yaw
    if (stickX != 0.0f) {
        step = speed * sot_fabsf(stickX);
        step = step * 1.75f * CAM_STICK2_YAW_SPEED;
        if (stickX > 0.0f) {
            step = -step;
        }

        geo.yaw += step;

        if (camera->mode == CAM_MODE_Z_TARGET_FRIENDLY) {
            camera->paramData.para1.rwData.yawTarget += step;
        }
    }

    // Pitch, inverted Y
    if (stickY != 0.0f) {
        step = speed * sot_fabsf(stickY) * CAM_STICK2_PITCH_SPEED;

        if (stickY > 0.0f) {
            geo.pitch = CLAMP_MAX((s16)(geo.pitch + step), 12000);
        } else {
            geo.pitch = CLAMP_MIN((s16)(geo.pitch - step), -8000);
        }
    }

    camera->eye = camera->eyeNext = add_geo(&camera->at, &geo);

    if (camera->mode == CAM_MODE_NORMAL) {
        // No automatic swing behind Link right after
        camera->paramData.norm1.rwData.startSwingTimer = 5;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Autosave: saves like the pause menu does (Link restarts at the area's entrance, like after saving in the pause menu),
// every few minutes of gameplay and shortly after entering a new area, when it's safe.

#define AUTOSAVE_INTERVAL_FRAMES (20 * 60 * 3)
#define AUTOSAVE_NEW_AREA_FRAMES (20 * 5)

static u32 autosave_frames = 0;
static u32 autosave_area_frames = 0;
static s16 autosave_last_scene = -1;
static bool autosave_area_saved = false;

static bool can_autosave(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if ((player == NULL) || (gSaveContext.fileNum < 0) || (gSaveContext.fileNum > 2) ||
        (gSaveContext.gameMode != GAMEMODE_NORMAL)) {
        return false;
    }
    if ((play->pauseCtx.state != 0) || (play->msgCtx.msgMode != MSGMODE_NONE) || (play->csCtx.state != CS_STATE_IDLE) ||
        (play->transitionTrigger != TRANS_TRIGGER_OFF) || (play->transitionMode != TRANS_MODE_OFF) ||
        (play->gameOverCtx.state != GAMEOVER_INACTIVE) || (play->shootingGalleryStatus != 0)) {
        return false;
    }
    if (Player_InCsMode(play) || Play_InCsMode(play) || (gSaveContext.timerState != TIMER_STATE_OFF) ||
        (gSaveContext.subTimerState != SUBTIMER_STATE_OFF) || (gSaveContext.save.info.playerData.health <= 0)) {
        return false;
    }
    // Standing on the ground, and not dead, riding, swimming, climbing, hanging from a ledge or in first person
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
        (player->stateFlags1 & (PLAYER_STATE1_7 | PLAYER_STATE1_10 | PLAYER_STATE1_11 | PLAYER_STATE1_13 | PLAYER_STATE1_14 |
                                PLAYER_STATE1_20 | PLAYER_STATE1_21 | PLAYER_STATE1_23 | PLAYER_STATE1_27 |
                                PLAYER_STATE1_29))) {
        return false;
    }
    return true;
}

static void autosave_update(PlayState* play) {
    if (!recomp_get_autosave_enabled()) {
        autosave_frames = 0;
        return;
    }

    if (play->sceneId != autosave_last_scene) {
        autosave_last_scene = play->sceneId;
        autosave_area_frames = 0;
        autosave_area_saved = false;
    }

    if (!can_autosave(play)) {
        return;
    }

    autosave_frames++;
    autosave_area_frames++;

    if ((autosave_frames >= AUTOSAVE_INTERVAL_FRAMES) ||
        (!autosave_area_saved && (autosave_area_frames >= AUTOSAVE_NEW_AREA_FRAMES))) {
        Play_SaveSceneFlags(play);
        gSaveContext.save.info.playerData.savedSceneId = play->sceneId;
        Sram_WriteSave(&play->sramCtx);
        autosave_frames = 0;
        autosave_area_saved = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera_Update: called every frame for each active camera

// @recomp Analog camera, autosave and options, then Sands of Time's camera update.
RECOMP_PATCH Vec3s Camera_Update(Camera* camera) {
    if (camera->camId == CAM_ID_MAIN) {
        PlayState* play = camera->play;

        // Z-targeting mode from the options menu (0: switch, 1: hold)
        gSaveContext.zTargetSetting = recomp_get_targeting_mode();

        // The right stick controls the camera with the analog camera, and is mapped to the C buttons otherwise
        // (and when playing the ocarina).
        recomp_set_right_analog_suppressed(recomp_get_analog_cam_enabled() && (sOcarinaInstrumentId == 0));

        if (recomp_get_analog_cam_enabled()) {
            analog_cam_update(camera);
        }

        autosave_update(play);
    }

    return func_8070BA9C(camera);
}

// ---------------------------------------------------------------------------------------------------------------------
// Aiming: gyro, mouse and right stick (with the analog camera) aiming in first person.
// Sands of Time's Player_SetFirstPersonAimLookAngles (its own player overlay).

// @recomp Patched to add gyro, mouse and right stick aiming, and the aiming inversion options.
RECOMP_PATCH s16 ovl_kaleido_0001_8081A728(PlayState* play, Player* this, s32 arg2, s16 arg3) {
    Input* input = &play->state.input[0];
    // Sands of Time's "don't invert the Y axis" option
    u8 no_invert_y = func_807008EC(7) & 0xFF;
    bool free_look = !func_8002DD78(this) && !ovl_kaleido_0001_8080205C(this) && (arg2 == 0);
    f32 move_speed = free_look ? 1500.0f : 1100.0f;
    s32 inverted_x;
    s32 inverted_y;
    s32 stick_x;
    s32 stick_y;
    s32 limit;
    s16 yaw;
    s16 delta;

    recomp_get_inverted_axes(&inverted_x, &inverted_y);
    if (no_invert_y == 1) {
        inverted_y = !inverted_y;
    }

    stick_x = input->rel.stick_x;
    stick_y = input->rel.stick_y;

    // @recomp Right stick aiming with the analog camera
    if (recomp_get_analog_cam_enabled()) {
        f32 analog_x;
        f32 analog_y;
        recomp_get_camera_inputs(&analog_x, &analog_y);
        stick_x += (s32)(analog_x * 127.0f);
        stick_y += (s32)(analog_y * 127.0f);
    }
    stick_x = CLAMP(stick_x, -60, 60);
    stick_y = CLAMP(stick_y, -60, 60);

    // @recomp Gyro and mouse aiming (not in free look, like Zelda64Recomp)
    static f32 total_gyro_x;
    static f32 total_gyro_y;
    static f32 total_mouse_x;
    static f32 total_mouse_y;
    static s32 applied_aim_x;
    static s32 applied_aim_y;
    f32 delta_gyro_x;
    f32 delta_gyro_y;
    f32 delta_mouse_x;
    f32 delta_mouse_y;
    s32 target_aim_x;
    s32 target_aim_y;

    recomp_get_gyro_deltas(&delta_gyro_x, &delta_gyro_y);
    recomp_get_mouse_deltas(&delta_mouse_x, &delta_mouse_y);
    total_gyro_x += delta_gyro_x;
    total_gyro_y += delta_gyro_y;
    total_mouse_x += delta_mouse_x;
    total_mouse_y += delta_mouse_y;
    target_aim_x = (s32)(total_gyro_x * -3.0f + total_mouse_y * 20.0f);
    target_aim_y = (s32)(total_gyro_y * 3.0f + total_mouse_x * -20.0f);
    if (free_look) {
        target_aim_x = applied_aim_x;
        target_aim_y = applied_aim_y;
    }

    // Pitch (the game's default is inverted: stick up looks down)
    limit = (this->stateFlags1 & PLAYER_STATE1_23) ? 3500 : 14000;
    delta = ((stick_y >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stick_y * 200)) * move_speed);
    if (!inverted_y) {
        delta = -delta;
    }
    this->actor.focus.rot.x += delta + (s16)(target_aim_x - applied_aim_x);
    applied_aim_x = target_aim_x;
    this->actor.focus.rot.x = CLAMP(this->actor.focus.rot.x, -limit, limit);

    // Yaw
    yaw = this->actor.focus.rot.y - this->actor.shape.rot.y;
    delta = ((stick_x >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stick_x * 200)) * -move_speed);
    if (inverted_x) {
        delta = -delta;
    }
    yaw += delta + (s16)(target_aim_y - applied_aim_y);
    applied_aim_y = target_aim_y;
    this->actor.focus.rot.y = CLAMP(yaw, -19114, 19114) + this->actor.shape.rot.y;

    this->unk_6AE |= 2;

    return ovl_kaleido_0001_80804140(this, (play->shootingGalleryStatus != 0) || func_8002DD78(this) ||
                                               ovl_kaleido_0001_8080205C(this)) -
           arg3;
}

// ---------------------------------------------------------------------------------------------------------------------
// Low health beeps

// @recomp Patched to make the low health beeps optional.
RECOMP_PATCH void Health_UpdateBeatingHeart(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    if (interfaceCtx->beatingHeartOscillatorDirection != 0) {
        interfaceCtx->beatingHeartOscillator--;
        if (interfaceCtx->beatingHeartOscillator <= 0) {
            interfaceCtx->beatingHeartOscillator = 0;
            interfaceCtx->beatingHeartOscillatorDirection = 0;
            if (!Player_InCsMode(play) && !IS_PAUSED(&play->pauseCtx) && Health_IsCritical() && !Play_InCsMode(play) &&
                recomp_get_low_health_beeps_enabled()) {
                Sfx_PlaySfxCentered(NA_SE_SY_HITPOINT_ALARM);
            }
        }
    } else {
        interfaceCtx->beatingHeartOscillator++;
        if (interfaceCtx->beatingHeartOscillator >= 10) {
            interfaceCtx->beatingHeartOscillator = 10;
            interfaceCtx->beatingHeartOscillatorDirection = 1;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Background music volume

// @recomp Patched to apply the background music volume option to the background music players.
RECOMP_PATCH void Audio_SequencePlayerProcessSound(SequencePlayer* seqPlayer) {
    static f32 prev_bgm_volume = -1.0f;
    s32 i;
    bool is_bgm = (seqPlayer == &gAudioCtx.seqPlayers[SEQ_PLAYER_BGM_MAIN]) ||
                  (seqPlayer == &gAudioCtx.seqPlayers[SEQ_PLAYER_BGM_SUB]);
    f32 bgm_volume = is_bgm ? recomp_get_bgm_volume() : 1.0f;

    if (is_bgm && (bgm_volume != prev_bgm_volume)) {
        prev_bgm_volume = bgm_volume;
        seqPlayer->recalculateVolume = true;
    }

    if (seqPlayer->fadeTimer != 0) {
        seqPlayer->fadeVolume += seqPlayer->fadeVelocity;
        seqPlayer->recalculateVolume = true;

        if (seqPlayer->fadeVolume > 1.0f) {
            seqPlayer->fadeVolume = 1.0f;
        }
        if (seqPlayer->fadeVolume < 0.0f) {
            seqPlayer->fadeVolume = 0.0f;
        }

        seqPlayer->fadeTimer--;
        if (seqPlayer->fadeTimer == 0 && seqPlayer->state == 2) {
            AudioSeq_SequencePlayerDisable(seqPlayer);
            return;
        }
    }

    if (seqPlayer->recalculateVolume) {
        seqPlayer->appliedFadeVolume = seqPlayer->fadeVolume * seqPlayer->fadeVolumeScale * bgm_volume;
    }

    for (i = 0; i < 16; i++) {
        if (seqPlayer->channels[i]->enabled == 1) {
            Audio_SequenceChannelProcessSound(seqPlayer->channels[i], seqPlayer->recalculateVolume,
                                              seqPlayer->applyBend);
        }
    }

    seqPlayer->recalculateVolume = false;
}
