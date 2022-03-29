#include "Mod.h"

HOOK(void, __stdcall, AppUpdate, 0xD6BE60, void* a1, float deltaTime)
{
    size_t target = (size_t)((1.0f / deltaTime) - 5.0f);
    const size_t remnant = target % 30;
    if (remnant > 0)
        target += 30 - remnant;

    if (get_interpolation_should_update())
        set_interpolation_interval(max(1, target / 30));

    originalAppUpdate(a1, 1.0f / (float)target);
}

void initFPS()
{
    INSTALL_HOOK(AppUpdate);
}