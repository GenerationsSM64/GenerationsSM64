#ifndef LIB_SM64_H
#define LIB_SM64_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct SM64Surface
{
    int16_t type;
    int16_t force;
    uint16_t terrain;
    int16_t vertices[3][3];
};

struct SM64MarioInputs
{
    float camLookX, camLookZ;
    float stickX, stickY;
    uint8_t buttonA, buttonB, buttonZ;
};

struct SM64ObjectTransform
{
    float position[3];
    float eulerRotation[3];
};

struct SM64SurfaceObject
{
    struct SM64ObjectTransform transform;
    uint32_t surfaceCount;
    struct SM64Surface *surfaces;
};

struct SM64MarioState
{
    float position[3];
    float faceAngle;
    float interpolatedPosition[3];
    float interpolatedGfxPosition[3];
    float interpolatedVelocity[3];
    float interpolatedFaceAngle;
    uint8_t isUpdateFrame;
};

struct SM64MarioGeometryBuffer
{
    float *position;
    float *normal;
    float *color;
    float *uv;
    uint16_t numTrianglesUsed;
};

struct SM64MarioGeometryBuffers
{
    struct SM64MarioGeometryBuffer opaque;
    struct SM64MarioGeometryBuffer punchThrough;
};

typedef void (*SM64DebugPrintFunctionPtr)( const char * );

enum
{
    SM64_TEXTURE_WIDTH = 64 * 11,
    SM64_TEXTURE_HEIGHT = 64,
    SM64_GEO_MAX_TRIANGLES = 1024,
};

extern void sm64_global_init( uint8_t *rom, uint8_t *outTexture, SM64DebugPrintFunctionPtr debugPrintFunction );
extern void sm64_global_terminate( void );

extern void sm64_static_surfaces_load( const struct SM64Surface *surfaceArray, uint32_t numSurfaces );

extern int32_t sm64_mario_create( float x, float y, float z );
extern void sm64_mario_tick( int32_t marioId, const struct SM64MarioInputs *inputs, struct SM64MarioState *outState, struct SM64MarioGeometryBuffers *outBuffers );
extern void sm64_mario_delete( int32_t marioId );
extern void sm64_mario_set_position( int32_t marioId, float x, float y, float z, uint8_t overrideHistory );
extern void sm64_mario_set_velocity( int32_t marioId, float x, float y, float z, float forwardVel );
extern void sm64_mario_set_health( int32_t marioId, int16_t health );
extern void sm64_mario_set_face_angle(int32_t marioId, float x, float y, float z);
extern void sm64_mario_set_animation(int32_t marioId, int32_t animationID);
extern void sm64_mario_set_animation_lock(int32_t marioId, uint32_t locked);
extern void sm64_mario_toggle_wing_cap(int32_t marioId);
extern void sm64_mario_take_damage(int32_t marioId);
extern uint8_t sm64_mario_attacking(int32_t marioId);
extern uint8_t sm64_mario_diving(int32_t marioId);
extern void sm64_mario_set_external_control(int32_t marioId, uint8_t value);

extern uint32_t sm64_surface_object_create( const struct SM64SurfaceObject *surfaceObject );
extern void sm64_surface_object_move( uint32_t objectId, const struct SM64ObjectTransform *transform );
extern void sm64_surface_object_delete( uint32_t objectId );

#endif//LIB_SM64_H
