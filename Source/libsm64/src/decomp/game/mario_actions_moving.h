#ifndef MARIO_ACTIONS_MOVING
#define MARIO_ACTIONS_MOVING

#include "../include/PR/ultratypes.h"

#include "../include/types.h"

void play_step_sound(struct MarioState *m, s16 frame1, s16 frame2);
s32 mario_execute_moving_action(struct MarioState *m);
void anim_and_audio_for_walk(struct MarioState* m);

#endif // MARIO_ACTIONS_MOVING
