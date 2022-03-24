#include "Mod.h"
#include "Util.h"

#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528

std::thread audioThread;

extern "C" void create_next_audio_buffer(s16 * samples, u32 num_samples);

void audioCallback()
{
    constexpr auto interval = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::duration<double>(1.0 / 30.0));

    s16 audioBuffer[SAMPLES_HIGH * 2 * 2];
    auto next = std::chrono::high_resolution_clock::now();

    while (!*(int*)0x1E5E2E8) // Application exit flag
    {
        auto current = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for((next - current) / 2);

        current = std::chrono::high_resolution_clock::now();
        if (current < next)
            continue;

        audio_signal_game_loop_tick();

        const int samplesLeft = audio_wasapi.buffered();
        const u32 numAudioSamples = samplesLeft < audio_wasapi.get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;

        for (int i = 0; i < 2; i++)
            create_next_audio_buffer(audioBuffer + i * (numAudioSamples * 2), numAudioSamples);

        audio_wasapi.play((u8*)audioBuffer, 2 * numAudioSamples * 4);

        next = current + interval;
    }
}

void initAudio(const std::string& directoryPath)
{
    gSoundDataADSR = rom.get() + 0x57B720;
    gSoundDataRaw = rom.get() + 0x593560;
    gMusicData = rom.get() + 0x7B0860;
    gBankSetsData = rom.get() + 0x7CC620;

    audio_wasapi.init();
    audio_init();
    sound_init();
    sound_reset(0);

    audioThread = std::thread(audioCallback);
}