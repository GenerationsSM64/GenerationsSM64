#include "Mod.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

ma_device device;

void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    for (size_t i = 0; i < frameCount; i += DEFAULT_LEN_2CH)
    {
        const size_t count = min(i + DEFAULT_LEN_2CH, frameCount) - i;
        create_next_audio_buffer((s16*)pOutput + i, count);
    }

    (void)pInput;
}

void initAudio()
{
    gSoundDataADSR = rom.get() + 0x57B720;
    gSoundDataRaw = rom.get() + 0x593560;
    gMusicData = rom.get() + 0x7B0860;
    gBankSetsData = rom.get() + 0x7CC620;

    audio_init();
    sound_init();
    sound_reset(0);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.sampleRate = 32000;
    deviceConfig.periodSizeInFrames = 528;
    deviceConfig.periods = 2;
    deviceConfig.dataCallback = audioCallback;
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = 2;

    ma_device_init(nullptr, &deviceConfig, &device);
    ma_device_start(&device);
}