#pragma once

#define WIN32_LEAN_AND_MEAN

#include <BlueBlur.h>

#include <Windows.h>
#include <detours.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <INIReader.h>

#include <Helpers.h>

#include <LostCodeLoader.h>

extern "C"
{
#include <libsm64.h>
#include <decomp/global_state.h>
#include <decomp/audio/external.h>
#include <decomp/audio/load.h>
#include <decomp/engine/surface_collision.h>
#include <decomp/include/mario_animation_ids.h>
#include <decomp/include/sm64.h>
#include <decomp/include/surface_terrains.h>
}
#include <decomp/pc/audio/audio_wasapi.h>

#include "../../GenerationsParameterEditor/Include/DebugDrawText.h"