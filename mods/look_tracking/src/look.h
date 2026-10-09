#ifndef LOOK_H
#define LOOK_H

#include "sot_player.h"

// Link's look tracking state (fields ModOcarina adds to Player)
typedef struct {
    Actor* lookActor; // actor followed by the head and the pupils, NULL if none
    s16 lookPitch;
    s16 lookYaw;
    s16 lookYawLock; // yaw kept while the look actor is behind Link
    s16 lookRoll;    // head tilt into turns, when not looking at anything
    s16 turnYawDiff; // how much Link is turning this frame, set while moving
} LookState;

extern LookState gLook;

// Head (look.c)
void Look_Update(Player* this, PlayState* play);

// Eyes (eyes.c)
void Eyes_Update(Player* this, s32 eyesIndex);

#endif
