#include "Mod.h"

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_DSOUND

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

constexpr ma_backend backends[] = { ma_backend_dsound };

ma_context context;
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

HOOK(bool, __fastcall, LoadAudioConfig, 0xA5CAF0, struct AudioConfig* audioConfig)
{
    const bool result = originalLoadAudioConfig(audioConfig);
    if (!result)
        return false;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.sampleRate = 32000;
    deviceConfig.periodSizeInFrames = 528;
    deviceConfig.periods = 2;
    deviceConfig.dataCallback = audioCallback;
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = 2;

    bool valid = false;
    for (size_t i = 0; i < 16; i++)
        valid |= ((uint8_t*)audioConfig)[i] != 0xFF;

    if (valid)
        deviceConfig.playback.pDeviceID = (const ma_device_id*)audioConfig;

    ma_context_init(backends, _countof(backends), nullptr, &context);
    ma_device_init(&context, &deviceConfig, &device);
    ma_device_start(&device);

    return true;
}

void initAudio()
{
    INSTALL_HOOK(LoadAudioConfig);

    gSoundDataADSR = rom.get() + 0x57B720;
    gSoundDataRaw = rom.get() + 0x593560;
    gMusicData = rom.get() + 0x7B0860;
    gBankSetsData = rom.get() + 0x7CC620;

    audio_init();
    sound_init();
    sound_reset(0);
}