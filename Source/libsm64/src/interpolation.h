#ifndef INTERPOLATION_H
#define INTERPOLATION_H

#include "decomp/include/types.h"
#include "decomp/include/PR/ultratypes.h"

void set_interpolation_interval(u32 interval);
f32 get_interpolation_delta_time(void);
void increment_interpolation_frame(void);
u16 get_interpolation_area_update_counter(void);
u8 get_interpolation_should_update(void);
u8 get_interpolation_gonna_update(void);

f32 f32_interpolate(f32 a, f32 b);
void vec3f_interpolate(Vec3f dest, Vec3f a, Vec3f b);

s16 s16_angle_interpolate(s16 a, s16 b);
void vec3s_angle_interpolate(Vec3s dest, Vec3s a, Vec3s b);

void mtxf_interpolate(Mat4 dest, Mat4 a, Mat4 b);

#endif
