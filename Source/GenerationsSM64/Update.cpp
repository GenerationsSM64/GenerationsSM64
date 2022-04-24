#include "Mod.h"

HOOK(void, __stdcall, CApplicationUpdateInternal, 0xD6BE60, void* application, float deltaTime)
{
    size_t target = (size_t)((1.0f / deltaTime) - 5.0f);
    const size_t remnant = target % 30;
    if (remnant > 0)
        target += 30 - remnant;

    if (get_interpolation_should_update())
        set_interpolation_interval(max(1, target / 30));

    originalCApplicationUpdateInternal(application, 1.0f / (float)target);
}

float audioUpdateTimer;

HOOK(void, __fastcall, CApplicationUpdate, 0xE7BED0, void* This, void* Edx, float deltaTime, bool flag)
{
    audioUpdateTimer += deltaTime;
    if (audioUpdateTimer >= (1.0f / 30.0f))
    {
        if (char* soundModuleManager = *(char**)0x1E77290)
            gGlobalVolume = *(float*)(soundModuleManager + 0x3C);
        else
            gGlobalVolume = 0.63f;

        audio_signal_game_loop_tick();
        audioUpdateTimer = 0.0f;
    }

    originalCApplicationUpdate(This, Edx, deltaTime, flag);
}

void initUpdate()
{
    INSTALL_HOOK(CApplicationUpdateInternal);
    INSTALL_HOOK(CApplicationUpdate);
}