#ifndef SM64_LIB_EXPORT
#define SM64_LIB_EXPORT
#endif

#include "libsm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "decomp/include/PR/os_cont.h"
#include "decomp/engine/math_util.h"
#include "decomp/include/sm64.h"
#include "decomp/shim.h"
#include "decomp/memory.h"
#include "decomp/global_state.h"
#include "decomp/game/mario.h"
#include "decomp/game/object_stuff.h"
#include "decomp/engine/surface_collision.h"
#include "decomp/engine/graph_node.h"
#include "decomp/engine/geo_layout.h"
#include "decomp/game/rendering_graph_node.h"
#include "decomp/mario/geo.inc.h"
#include "decomp/game/platform_displacement.h"
#include "decomp/include/mario_animation_ids.h"
#include "decomp/include/mario_geo_switch_case_ids.h"

#include "debug_print.h"
#include "load_surfaces.h"
#include "gfx_adapter.h"
#include "load_anim_data.h"
#include "load_tex_data.h"
#include "obj_pool.h"

static struct AllocOnlyPool *s_mario_geo_pool = NULL;
static struct GraphNode *s_mario_graph_node = NULL;

static bool s_init_global = false;
static bool s_init_one_mario = false;

struct MarioInstance {
    struct GlobalState *globalState;
};
struct ObjPool s_mario_instance_pool = { 0, 0 };

static void update_button(bool on, u16 button) {
    gController.buttonPressed &= ~button;

    if (on) {
        if ((gController.buttonDown & button) == 0)
            gController.buttonPressed |= button;

        gController.buttonDown |= button;
    } else {
        gController.buttonDown &= ~button;
    }
}

static struct Area *allocate_area(void) {
    struct Area *result = malloc(sizeof(struct Area));
    memset(result, 0, sizeof(struct Area));

    result->flags = 1;
    result->camera = malloc(sizeof(struct Camera));
    memset(result->camera, 0, sizeof(struct Camera));

    return result;
}

static void free_area(struct Area *area) {
    free(area->camera);
    free(area);
}

void sm64_global_init(uint8_t *rom, uint8_t *outTexture, SM64DebugPrintFunctionPtr debugPrintFunction) {
    if (s_init_global)
        sm64_global_terminate();

    s_init_global = true;
    g_debug_print_func = debugPrintFunction;

    load_mario_textures_from_rom(rom, outTexture);
    load_mario_anims_from_rom(rom);

    memory_init();
}

void sm64_global_terminate(void) {
    if (!s_init_global)
        return;

    global_state_bind(NULL);

    if (s_init_one_mario) {
        for (int i = 0; i < s_mario_instance_pool.size; ++i)
            if (s_mario_instance_pool.objects[i] != NULL)
                sm64_mario_delete(i);

        obj_pool_free_all(&s_mario_instance_pool);
    }

    s_init_global = false;
    s_init_one_mario = false;

    alloc_only_pool_free(s_mario_geo_pool);
    surfaces_unload_all();
    unload_mario_anims();
    memory_terminate();
}

void sm64_static_surfaces_load(const struct SM64Surface *surfaceArray, uint32_t numSurfaces) {
    surfaces_load_static(surfaceArray, numSurfaces);
}

int32_t sm64_mario_create(float x, float y, float z) {
    int32_t marioIndex = obj_pool_alloc_index(&s_mario_instance_pool, sizeof(struct MarioInstance));
    struct MarioInstance *newInstance = s_mario_instance_pool.objects[marioIndex];

    newInstance->globalState = global_state_create();
    global_state_bind(newInstance->globalState);

    if (!s_init_one_mario) {
        s_init_one_mario = true;
        s_mario_geo_pool = alloc_only_pool_init();
        s_mario_graph_node = process_geo_layout(s_mario_geo_pool, mario_geo_ptr);
    }

    gCurrSaveFileNum = 1;
    gMarioObject = hack_allocate_mario();
    gCurrentArea = allocate_area();
    gCurrentObject = gMarioObject;

    gMarioSpawnInfoVal.startPos[0] = x;
    gMarioSpawnInfoVal.startPos[1] = y;
    gMarioSpawnInfoVal.startPos[2] = z;

    gMarioSpawnInfoVal.startAngle[0] = 0;
    gMarioSpawnInfoVal.startAngle[1] = 0;
    gMarioSpawnInfoVal.startAngle[2] = 0;

    gMarioSpawnInfoVal.areaIndex = 0;
    gMarioSpawnInfoVal.activeAreaIndex = 0;
    gMarioSpawnInfoVal.behaviorArg = 0;
    gMarioSpawnInfoVal.behaviorScript = NULL;
    gMarioSpawnInfoVal.unk18 = NULL;
    gMarioSpawnInfoVal.next = NULL;

    init_mario_from_save_file();

    if (init_mario() < 0) {
        sm64_mario_delete(marioIndex);
        return -1;
    }

    set_mario_action(gMarioState, ACT_SPAWN_SPIN_AIRBORNE, 0);
    find_floor(x, y, z, &gMarioState->floor);

    return marioIndex;
}

uint8_t sm64_mario_bind(int32_t marioId) {
    if (marioId >= s_mario_instance_pool.size || s_mario_instance_pool.objects[marioId] == NULL) {
        DEBUG_PRINT("Tried to bind non-existant Mario with ID: %u", marioId);
        return FALSE;
    }

    global_state_bind(((struct MarioInstance *) s_mario_instance_pool.objects[marioId])->globalState);
    return TRUE;
}

void sm64_mario_delete(int32_t marioId) {
    if (marioId >= s_mario_instance_pool.size || s_mario_instance_pool.objects[marioId] == NULL) {
        DEBUG_PRINT("Tried to delete non-existant Mario with ID: %u", marioId);
        return;
    }

    struct GlobalState *globalState =
        ((struct MarioInstance *) s_mario_instance_pool.objects[marioId])->globalState;
    global_state_bind(globalState);

    free(gMarioObject);
    free_area(gCurrentArea);

    global_state_delete(globalState);
    obj_pool_free_index(&s_mario_instance_pool, marioId);
}

void sm64_mario_tick(const struct SM64MarioInputs *inputs, struct SM64MarioState *outState,
                     struct SM64MarioGeometryBuffers *outBuffers) {
    outState->isUpdateFrame = get_interpolation_should_update();

    if (outState->isUpdateFrame) {
        vec3f_copy(gMarioState->prevPos, gMarioState->pos);
        vec3f_copy(gMarioState->prevVel, gMarioState->vel);
        vec3s_copy(gMarioState->prevFaceAngle, gMarioState->faceAngle);

        vec3s_copy(gMarioObject->header.gfx.prevAngle, gMarioObject->header.gfx.angle);
        vec3f_copy(gMarioObject->header.gfx.prevPos, gMarioObject->header.gfx.pos);
        vec3f_copy(gMarioObject->header.gfx.prevScale, gMarioObject->header.gfx.scale);

        gMarioObject->header.gfx.hasPrevThrowMatrix = gMarioObject->header.gfx.throwMatrix != NULL;

        if (gMarioObject->header.gfx.hasPrevThrowMatrix)
            mtxf_copy(gMarioObject->header.gfx.prevThrowMatrix, *gMarioObject->header.gfx.throwMatrix);

        gMarioObject->header.gfx.throwMatrix = NULL;

        update_button(inputs->buttonA | g_state->mButtonA, A_BUTTON);
        update_button(inputs->buttonB | g_state->mButtonB, B_BUTTON);
        update_button(inputs->buttonZ | g_state->mButtonZ, Z_TRIG);

        g_state->mButtonA = FALSE;
        g_state->mButtonB = FALSE;
        g_state->mButtonZ = FALSE;

        gMarioState->area->camera->yaw = atan2s(inputs->camLookZ, inputs->camLookX);

        gController.stickX = 64.0f * inputs->stickX;
        gController.stickY = 64.0f * inputs->stickY;
        gController.stickMag =
            sqrtf(gController.stickX * gController.stickX + gController.stickY * gController.stickY);

        apply_mario_platform_displacement();
        bhv_mario_update();
        update_mario_platform(); // TODO platform grabbed here and used next tick could be a
                                 // use-after-free
    } else {
        g_state->mButtonA = inputs->buttonA;
        g_state->mButtonB = inputs->buttonB;
        g_state->mButtonZ = inputs->buttonZ;
    }

    vec3f_copy(outState->position, gMarioState->pos);
    outState->faceAngle = gMarioState->faceAngle[1] / 32768.0f * M_PI;

    gfx_adapter_bind_output_buffers(outBuffers);
    geo_process_root_hack_single_node(s_mario_graph_node);
}

extern void sm64_mario_post_tick(struct SM64MarioState *outState) {
    vec3f_copy(outState->position, gMarioState->pos);
    outState->faceAngle = gMarioState->faceAngle[1] / 32768.0f * M_PI;

    vec3f_interpolate(outState->interpolatedPosition, gMarioState->prevPos, gMarioState->pos);
    vec3f_interpolate(outState->interpolatedVelocity, gMarioState->prevVel, gMarioState->vel);
    outState->interpolatedFaceAngle =
        s16_angle_interpolate(gMarioState->prevFaceAngle[1], gMarioState->faceAngle[1]) / 32768.0f
        * M_PI;
    vec3f_interpolate(outState->interpolatedGfxPosition, gMarioObject->header.gfx.prevPos,
                      gMarioObject->header.gfx.pos);

    increment_interpolation_frame();
    gAreaUpdateCounter = get_interpolation_area_update_counter();
}

void sm64_mario_set_position(float x, float y, float z, uint8_t overrideHistory) {
    vec3f_set(gMarioState->pos, x, y, z);
    vec3f_copy(gMarioObject->header.gfx.pos, gMarioState->pos);

    if (overrideHistory) {
        vec3f_copy(gMarioState->prevPos, gMarioState->pos);
        vec3f_copy(gMarioObject->header.gfx.prevPos, gMarioState->pos);
    }
}

void sm64_mario_set_velocity(float x, float y, float z, float forwardVel) {
    vec3f_set(gMarioState->vel, x, y, z);
    vec3f_copy(gMarioState->prevVel, gMarioState->vel);
    gMarioState->forwardVel = forwardVel;
}

void sm64_mario_set_health(int16_t health) {
    gMarioState->health = health;
}

void sm64_mario_set_face_angle(float x, float y, float z) {
    s32 sx = x / M_PI * 32768.0f;
    s32 sy = y / M_PI * 32768.0f;
    s32 sz = z / M_PI * 32768.0f;

    gMarioState->faceAngle[0] = sx < -32768 ? -32768 : sx > 32767 ? 32767 : sx;
    gMarioState->faceAngle[1] = sy < -32768 ? -32768 : sy > 32767 ? 32767 : sy;
    gMarioState->faceAngle[2] = sz < -32768 ? -32768 : sz > 32767 ? 32767 : sz;
}

void sm64_mario_set_animation(int32_t animationID) {
    if (!get_interpolation_should_update()) {
        return;
    }

    if (animationID == MARIO_ANIM_MOVE_ON_WIRE_NET_LEFT
        || animationID == MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT) {
        if (gMarioObject->header.gfx.animInfo.animID != MARIO_ANIM_HANG_ON_CEILING
            && !is_anim_at_end(gMarioState)) {
            return;
        }

        if (gMarioObject->header.gfx.animInfo.animID == MARIO_ANIM_MOVE_ON_WIRE_NET_LEFT)
            animationID = MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT;
        else
            animationID = MARIO_ANIM_MOVE_ON_WIRE_NET_LEFT;
    } else if (animationID == MARIO_ANIM_HANG_ON_CEILING) {
        if (gMarioObject->header.gfx.animInfo.animID == MARIO_ANIM_MOVE_ON_WIRE_NET_LEFT
            || gMarioObject->header.gfx.animInfo.animID == MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT) {
            return;
        }
    }

    u32 tmp = gMarioState->animation->locked;
    gMarioState->animation->locked = FALSE;
    set_mario_animation(gMarioState, animationID);
    gMarioState->animation->locked = tmp;

    if (animationID == MARIO_ANIM_STAR_DANCE) {
        gMarioState->marioBodyState->handState = gMarioObject->header.gfx.animInfo.animFrame >= 40
                                                     ? MARIO_HAND_PEACE_SIGN
                                                     : MARIO_HAND_FISTS;

        if (gMarioObject->header.gfx.animInfo.animFrame == 42) {
            play_sound(SOUND_MARIO_HERE_WE_GO, gMarioObject->header.gfx.cameraToObject);
        }
    } else {
        gMarioState->marioBodyState->handState = MARIO_HAND_FISTS;
    }

    gMarioState->marioBodyState->punchState = 0;
}

void sm64_mario_set_animation_lock(uint32_t locked) {
    gMarioState->animation->locked = locked;
}

void sm64_mario_toggle_wing_cap(void) {
    if (gMarioState->capTimer > 10) {
        gMarioState->capTimer = 10;
    } else if ((gMarioState->action & ACT_FLAG_IDLE) || gMarioState->action == ACT_WALKING) {
        gMarioState->flags &= ~MARIO_CAP_ON_HEAD & ~MARIO_CAP_IN_HAND;
        gMarioState->flags |= MARIO_WING_CAP;
        gMarioState->capTimer = ~0;
        gMarioState->flags |= MARIO_CAP_IN_HAND;
        set_mario_action(gMarioState, ACT_PUTTING_ON_CAP, 0);

        play_sound(SOUND_MENU_STAR_SOUND, gMarioState->marioObj->header.gfx.cameraToObject);
        play_sound(SOUND_MARIO_HERE_WE_GO, gMarioState->marioObj->header.gfx.cameraToObject);
    }
}

void sm64_mario_take_damage(void) {
    if (!(gMarioState->action & ACT_FLAG_INVULNERABLE) && gMarioState->action != ACT_LAVA_BOOST
        && gMarioState->action != ACT_LAVA_BOOST_LAND) {
        u32 action = ACT_FORWARD_GROUND_KB;

        if (gMarioState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER)) {
            action = ACT_FORWARD_WATER_KB;
        } else if (gMarioState->action & (ACT_FLAG_AIR | ACT_FLAG_ON_POLE | ACT_FLAG_HANGING)) {
            action = ACT_FORWARD_AIR_KB;
        }

        play_sound(SOUND_MARIO_ATTACKED, gMarioObject->header.gfx.cameraToObject);
        set_mario_action(gMarioState, action, 1);
    }
}

uint8_t sm64_mario_should_use_boost_collision(void) {
    return gMarioState->action != ACT_GROUND_POUND && gMarioState->action != ACT_GROUND_POUND_LAND
           && (gMarioState->action & (ACT_FLAG_ATTACKING | ACT_FLAG_DIVING)) != 0;
}

uint8_t sm64_mario_should_use_stomp_collision(void) {
    return gMarioState->action == ACT_GROUND_POUND || gMarioState->action == ACT_GROUND_POUND_LAND;
}

void sm64_mario_set_camera_to_object(float x, float y, float z) {
    vec3f_set(gMarioObject->header.gfx.cameraToObject, x, y, z);
}

extern void sm64_mario_set_external_control(uint8_t value) {
    gMarioState->externalControl = value;
}

uint8_t sm64_mario_is_lava_boost(void) {
    return gMarioState->action == ACT_LAVA_BOOST;
}

void sm64_mario_set_action(uint32_t action) {
    if (gMarioState->action != action
        && (action == ACT_WALKING || action == ACT_IDLE
            || (gMarioState->action & ACT_FLAG_AIR) != (action & ACT_FLAG_AIR))) {
        set_mario_action(gMarioState, action, 0);
    }
}

uint8_t sm64_mario_is_airborne(void) {
    return (gMarioState->action & ACT_FLAG_AIR) != 0;
}

uint8_t sm64_mario_pushing_wall(void) {
    if (!g_state) {
        return FALSE;
    }

    return (gMarioState->flags & MARIO_UNKNOWN_31) != 0;
}

uint32_t sm64_surface_object_create(const struct SM64SurfaceObject *surfaceObject) {
    uint32_t id = surfaces_load_object(surfaceObject);
    return id;
}

void sm64_surface_object_move(uint32_t objectId, const struct SM64ObjectTransform *transform) {
    surface_object_update_transform(objectId, transform);
}

void sm64_surface_object_delete(uint32_t objectId) {
    // A mario standing on the platform that is being destroyed will have a pointer to freed memory if
    // we don't clear it.
    for (int i = 0; i < s_mario_instance_pool.size; ++i) {
        if (s_mario_instance_pool.objects[i] == NULL)
            continue;

        struct GlobalState *state =
            ((struct MarioInstance *) s_mario_instance_pool.objects[i])->globalState;
        if (state->mgMarioObject->platform == surfaces_object_get_transform_ptr(objectId))
            state->mgMarioObject->platform = NULL;
    }

    surfaces_unload_object(objectId);
}
