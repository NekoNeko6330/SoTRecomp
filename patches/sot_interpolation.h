#ifndef __SOT_INTERPOLATION_H__
#define __SOT_INTERPOLATION_H__

#include "patches.h"

// RT64 matrix group ids (see Zelda64Recomp's transform_ids.h)
#define PARTICLE_TRANSFORM_ID_START 0x200U
#define ACTOR_TRANSFORM_LIMB_COUNT 256
// One id for each limb and another one for each post-limb draw
#define ACTOR_TRANSFORM_ID_COUNT (ACTOR_TRANSFORM_LIMB_COUNT * 2)
#define ACTOR_TRANSFORM_ID_START 0x1000000U

// The matrix group id of an actor (0 if the pointer isn't a known actor)
u32 sot_actor_transform_id(void* actor);

// Push the matrix group of a skeleton limb (or of its post-limb draw) of an actor; does nothing if `arg` isn't an actor.
Gfx* sot_push_limb_group(Gfx* gfx, void* arg, s32 limb_index, bool post_limb);
// Pop the group pushed by sot_push_limb_group.
Gfx* sot_pop_group(Gfx* gfx, void* arg);

#endif
