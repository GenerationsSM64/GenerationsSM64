#include "Mod.h"

float audioUpdateTimer;

HOOK(void, __stdcall, AppUpdate, 0xD6BE60, void* a1, float deltaTime)
{
    size_t target = (size_t)((1.0f / deltaTime) - 5.0f);
    const size_t remnant = target % 30;
    if (remnant > 0)
        target += 30 - remnant;

    if (get_interpolation_should_update())
        set_interpolation_interval(max(1, target / 30));

    audioUpdateTimer += deltaTime;
    if (audioUpdateTimer >= (1.0f / 30.0f))
    {
        audio_signal_game_loop_tick();
        audioUpdateTimer = 0.0f;
    }

    originalAppUpdate(a1, 1.0f / (float)target);
}

void initUpdate()
{
    INSTALL_HOOK(AppUpdate);
}