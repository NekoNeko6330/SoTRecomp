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

static f32 sot_sqrtf(f32 x) {
    return __builtin_sqrtf(x);
}

static f32 sot_fabsf(f32 x) {
    return x < 0.0f ? -x : x;
}

// ---------------------------------------------------------------------------------------------------------------------
// Analog camera
//
// Sands of Time's Normal1 camera doesn't swing back behind Link while he stands still, and follows him from the
// camera's current position when he moves. So the right stick rotates the camera (eye and eyeNext around at) right
// before the game updates it.

#define ANALOG_CAM_YAW_SPEED 1500.0f
#define ANALOG_CAM_PITCH_SPEED 500.0f
#define ANALOG_CAM_THRESHOLD 0.1f

static void rotate_around(Vec3f* point, Vec3f* center, s16 yaw, s16 pitch) {
    f32 dx = point->x - center->x;
    f32 dy = point->y - center->y;
    f32 dz = point->z - center->z;
    f32 s = Math_SinS(yaw);
    f32 c = Math_CosS(yaw);
    f32 nx = dx * c + dz * s;
    f32 nz = dz * c - dx * s;
    f32 horizontal = sot_sqrtf(nx * nx + nz * nz);

    if ((pitch != 0) && (horizontal > 1.0f)) {
        f32 ps = Math_SinS(pitch);
        f32 pc = Math_CosS(pitch);
        f32 new_horizontal = horizontal * pc - dy * ps;
        f32 new_dy = horizontal * ps + dy * pc;
        f32 length = sot_sqrtf(new_horizontal * new_horizontal + new_dy * new_dy);

        // Keep the camera between about 60 degrees below and 75 degrees above the horizon
        if ((new_horizontal > 1.0f) && (new_dy < length * 0.96f) && (new_dy > -length * 0.86f)) {
            nx *= new_horizontal / horizontal;
            nz *= new_horizontal / horizontal;
            dy = new_dy;
        }
    }

    point->x = center->x + nx;
    point->y = center->y + dy;
    point->z = center->z + nz;
}

static void analog_cam_update(Camera* camera) {
    PlayState* play = camera->play;
    Player* player = GET_PLAYER(play);
    f32 input_x;
    f32 input_y;
    s32 inverted_x;
    s32 inverted_y;

    // Only the main camera in its normal behavior, with Link controllable
    if ((camera->camId != CAM_ID_MAIN) || (camera->status != CAM_STAT_ACTIVE) || (camera->mode != CAM_MODE_NORMAL) ||
        (play->csCtx.state != CS_STATE_IDLE) || (player == NULL) || Player_InCsMode(play) ||
        (player->unk_664 != NULL) || (play->pauseCtx.state != 0)) {
        return;
    }

    recomp_get_camera_inputs(&input_x, &input_y);
    if ((sot_fabsf(input_x) < ANALOG_CAM_THRESHOLD) && (sot_fabsf(input_y) < ANALOG_CAM_THRESHOLD)) {
        return;
    }

    recomp_get_analog_inverted_axes(&inverted_x, &inverted_y);
    if (inverted_x) {
        input_x = -input_x;
    }
    if (inverted_y) {
        input_y = -input_y;
    }

    s16 yaw = (s16)(-input_x * ANALOG_CAM_YAW_SPEED);
    s16 pitch = (s16)(input_y * ANALOG_CAM_PITCH_SPEED);

    rotate_around(&camera->eye, &camera->at, yaw, pitch);
    rotate_around(&camera->eyeNext, &camera->at, yaw, pitch);
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
