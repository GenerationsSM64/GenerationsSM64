using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using CsbBuilder.Serialization;
using NAudio.Utils;
using NAudio.Wave;
using NAudio.Wave.SampleProviders;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using SonicAudioLib.Archives;
using SonicAudioLib.CriMw;
using SonicAudioLib.CriMw.Serialization;
using SoundBankConverter.Properties;
using VGAudio.Containers;
using VGAudio.Containers.Adx;
using VGAudio.Containers.Wave;
using VGAudio.Formats;
using VGAudio.Formats.Pcm16;

namespace SoundBankConverter
{
    internal class Program
    {
        static void Main(string[] args)
        {
            // We're only interested in sequences that have a .s file associated with it.
            var sequenceNames = Directory.EnumerateFiles(Path.Combine(args[0], "sequences"), "*.s")
                .Select(Path.GetFileNameWithoutExtension).ToList();

            // Load the sequences JSON.
            JObject sequences;

            using (var streamReader = new StreamReader(Path.Combine(args[0], "sequences.json")))
            using (var jsonReader = new JsonTextReader(streamReader))
                sequences = JObject.Load(jsonReader);

            // Prepare cue sheet data.
            var cueList = new List<SerializationCueTable>();
            var synthMap = new Dictionary<long, SerializationSynthTable>();
            var synthList = new List<SerializationSynthTable>();
            var loopedSynths = new HashSet<SerializationSynthTable>();
            var soundElementList = new List<SerializationSoundElementTable>();
            var voiceLimitGroups = new List<SerializationVoiceLimitGroupTable>();

            // Create default voice limit group for Mario's voice
            voiceLimitGroups.Add(new SerializationVoiceLimitGroupTable
            {
                VoiceLimitGroupName = "mario",
                VoiceLimitGroupNum = 1
            });

            foreach (var sequenceName in sequenceNames)
            {
                var soundBanks = sequences[sequenceName];
                long bankIndex = 0;

                foreach (var soundBankName in soundBanks)
                {
                    // Load the sound bank JSON.
                    JObject soundBank;

                    using (var streamReader = new StreamReader(Path.Combine(args[0], "sound_banks", soundBankName + ".json")))
                    using (var jsonReader = new JsonTextReader(streamReader))
                        soundBank = JObject.Load(jsonReader);

                    string sampleBank = $"{soundBank["sample_bank"]}";
                    long instrIndex = 0;
                    
                    // Load "instruments".
                    foreach (var instrumentName in soundBank["instrument_list"])
                    {
                        var instrument = soundBank["instruments"][$"{instrumentName}"];
                        if (instrument == null || !instrument.HasValues)
                        {
                            ++instrIndex;
                            continue;
                        }

                        var sound = instrument["sound"];
                        string soundName = $"{(sound.HasValues ? sound["else"] : sound)}";

                        string synthSoundElementPath = $"Synth/{soundBankName}/{instrumentName}/{instrumentName}";
                        string synthTrackPath = $"Synth/{soundBankName}/{instrumentName}";
                        string soundElementPath = $"Synth/{soundBankName}/{soundName}_aif";

                        // Create cue sheet data.
                        var synthSoundElement = new SerializationSynthTable
                        {
                            SynthName = synthSoundElementPath,
                            LinkName = soundElementPath,
                            F2CofHighOffset = 1000,
                            Pan3dDistanceOffset = 1000,
                            Pan3dVolumeOffset = 1000,
                        };


                        if (soundName.Contains("mario")) 
                            synthSoundElement.VoiceLimitGroupName = "mario";

                        var synthTrack = new SerializationSynthTable
                        {
                            SynthName = synthTrackPath,
                            LinkName = synthSoundElementPath + (char)0x0A,
                            SynthType = 1,
                            Repeat = 1,
                            Probability = 0,
                            AisacSetName = "uw_switch::Globals/Aisac/uw_switch1" + (char)0x0A,
                        };

                        synthMap[bankIndex << 32 | instrIndex] = synthTrack;
                        ++instrIndex;

                        synthList.Add(synthSoundElement);
                        synthList.Add(synthTrack);

                        if (soundElementList.Any(x => x.Name.Equals(soundElementPath)))
                            continue;

                        // Load sample AIFF.
                        var bytes = File.ReadAllBytes(Path.Combine(args[0], "samples", sampleBank,
                            soundName + ".aiff"));

                        int ReadInt32BE(int offset)
                        {
                            return bytes[offset] << 24 | bytes[offset + 1] << 16 | bytes[offset + 2] << 8 |
                                   bytes[offset + 3];
                        }

                        // Extract loop points, if they exist.
                        int loopStart = 0;
                        int loopEnd = 0;

                        if (ReadInt32BE(0x26) == 0x4D41524B) // MARK
                        {
                            loopStart = ReadInt32BE(0x32);
                            loopEnd = ReadInt32BE(0x3E);
                        }

                        const int targetSampleRate = 48000;

                        // Read AIFF data.
                        List<short>[] channelList;

                        using (var reader = new AiffFileReader(new MemoryStream(bytes)))
                        using (var resampler = new MediaFoundationResampler(reader, targetSampleRate) {ResamplerQuality = 60})
                        {
                            // Resize loop start/end points.
                            void Resize(ref int value)
                            {
                                value = value * resampler.WaveFormat.SampleRate / reader.WaveFormat.SampleRate;
                            }

                            Resize(ref loopStart);
                            Resize(ref loopEnd);

                            channelList = new List<short>[resampler.WaveFormat.Channels];

                            for (int i = 0; i < channelList.Length; i++)
                                channelList[i] = new List<short>();

                            var samples = new byte[resampler.WaveFormat.AverageBytesPerSecond];

                            while (true)
                            {
                                int length = resampler.Read(samples, 0, samples.Length);

                                if (length == 0)
                                    break;

                                // Load into VGAudio compatible channel data.
                                for (int i = 0; i < length; i += sizeof(short) * channelList.Length)
                                {
                                    for (int j = 0; j < channelList.Length; j++)
                                        channelList[j].Add((short)(samples[i + j * sizeof(short) + 0] |
                                                                   samples[i + j * sizeof(short) + 1] << 8));
                                }
                            }
                        }

                        byte[] EncodeAudio(int start, int end)
                        {
                            if (end == 0)
                                end = channelList[0].Count;

                            var channelArray = new short[channelList.Length][];
                            int count = end - start; // TODO: Inclusive or exclusive?

                            for (int i = 0; i < channelList.Length; i++)
                            {
                                channelArray[i] = new short[count];
                                channelList[i].CopyTo(start, channelArray[i], 0, count);
                            }

                            var audioData = new AudioData(new Pcm16Format(channelArray, targetSampleRate));
                            var adxWriter = new AdxWriter();

                            using (var memoryStream = new MemoryStream())
                            {
                                adxWriter.WriteToStream(audioData, memoryStream);
                                return memoryStream.ToArray();
                            }
                        }

                        // Create AAX.
                        var aaxArchive = new CriAaxArchive
                        {
                            Mode = CriAaxArchiveMode.Adx
                        };

                        // Add intro.
                        if (loopStart > 0 || loopEnd == 0)
                        {
                            aaxArchive.Add(new CriAaxEntry
                                { Flag = CriAaxEntryFlag.Intro, Data = EncodeAudio(0, loopStart) });
                        }

                        // Add loop.
                        if (loopEnd > 0)
                        {
                            aaxArchive.Add(new CriAaxEntry
                                { Flag = CriAaxEntryFlag.Loop, Data = EncodeAudio(loopStart, loopEnd) });

                            loopedSynths.Add(synthTrack);
                        }

                        // Save AAX and add sound element.
                        soundElementList.Add(new SerializationSoundElementTable
                        {
                            Name = soundElementPath,
                            SoundFrequency = targetSampleRate,
                            NumberChannels = (byte)channelList.Length,
                            NumberSamples = (uint)channelList[0].Count,
                            Data = aaxArchive.Save(),
                            FormatType = (byte)aaxArchive.Mode,
                        });
                    }

                    ++bankIndex;
                }

                // Create cues from the sequence file and link them to the synth elements.
                var source = File.ReadAllLines(Path.Combine(args[0], "sequences", sequenceName + ".s")).Select(x => x.Trim()).ToList();

                foreach (var channelLine in source.Where(x => x.StartsWith("seq_startchannel")))
                {
                    int spaceIndex = channelLine.IndexOf(' ');
                    int commaIndex = channelLine.IndexOf(',');
                    int channelIndex = int.Parse(channelLine.Substring(spaceIndex + 1, commaIndex - spaceIndex - 1));
                    string channelName = channelLine.Substring(commaIndex + 2);

                    // Read down until there are no more sound references.
                    int soundIndex = 0;

                    for (int i = source.IndexOf($"{channelName}_table:") + 1; i < source.Count; i++)
                    {
                        if (string.IsNullOrEmpty(source[i]) || source[i].StartsWith("//"))
                            continue;

                        // Every #ifdef checks for VERSION_JP with an #else pair, so we can
                        // safely skip to the #else statement if we run into one.
                        if (source[i].StartsWith("#ifdef"))
                        {
                            while (++i < source.Count && !source[i].StartsWith("#"))
                                ;
                        }

                        // Skip any other preprocessor directive.
                        if (source[i].StartsWith("#"))
                            continue;

                        // We reached the end.
                        if (!source[i].StartsWith("sound_ref"))
                            break;

                        // Recursively find the sound label.
                        int currentLine = i;
                        int soundLineIndex = -1;
                        int lineCount = -1;

                        while (currentLine >= 0)
                        {
                            // Extract sound name.
                            spaceIndex = source[currentLine].IndexOf(' ');
                            string soundName = source[currentLine].Substring(spaceIndex + 1);

                            // Find the sound label.
                            string label = $"{soundName}:";
                            soundLineIndex = source.FindIndex(x => x.StartsWith(label)) + 1;

                            // Find the next label to limit our search range.
                            int nextLabelIndex = source.FindIndex(soundLineIndex, x => x.Contains(':'));
                            lineCount = nextLabelIndex - soundLineIndex + 1;

                            // Check if this sound reuses another.
                            currentLine = source.FindIndex(soundLineIndex, lineCount, x => x.StartsWith("chan_jump"));
                        }

                        // Find bank/instrument calls.
                        int bankLineIndex = source.FindIndex(soundLineIndex, lineCount, x => x.StartsWith("chan_setbank"));
                        int instrLineIndex = source.FindIndex(soundLineIndex, lineCount, x => x.StartsWith("chan_setinstr"));

                        // Find layers. These can set the bank/instrument as well.
                        int layerLineIndex = source.FindIndex(soundLineIndex, lineCount, x => x.StartsWith("chan_setlayer"));
                        if (layerLineIndex >= 0)
                        {
                            string setLayer = source[layerLineIndex];
                            string layerName = setLayer.Substring(setLayer.IndexOf(',') + 2);

                            string label = $"{layerName}:";
                            layerLineIndex = source.FindIndex(x => x.StartsWith(label)) + 1;

                            int nextLabelIndex = source.FindIndex(layerLineIndex, x => x.Contains(':'));
                            lineCount = nextLabelIndex - layerLineIndex + 1;

                            int layerBankLineIndex = source.FindIndex(layerLineIndex, lineCount, x => x.StartsWith("layer_setbank"));
                            int layerInstrLineIndex = source.FindIndex(layerLineIndex, lineCount, x => x.StartsWith("layer_setinstr"));

                            if (layerBankLineIndex >= 0) bankLineIndex = layerBankLineIndex;
                            if (layerInstrLineIndex >= 0) instrLineIndex = layerInstrLineIndex;
                        }

                        // Skip if we couldn't find any banks or instruments.
                        if (bankLineIndex == -1 || instrLineIndex == -1)
                        {
                            ++soundIndex;
                            continue;
                        }

                        string setBank = source[bankLineIndex];
                        string setInstr = source[instrLineIndex];

                        // Extract indices.
                        bankIndex = long.Parse(setBank.Substring(setBank.IndexOf(' ') + 1));
                        long instrIndex = long.Parse(setInstr.Substring(setInstr.IndexOf(' ') + 1));
                        
                        // See if we have a synth at these indices.
                        if (!synthMap.TryGetValue(bankIndex << 32 | instrIndex, out var synth))
                        {
                            ++soundIndex;
                            continue;
                        }

                        // Create cue using this data.
                        cueList.Add(new SerializationCueTable
                        {
                            Id = (uint)((channelIndex << 28) | (soundIndex << 16)),
                            Name = $"mario{cueList.Count}",
                            SynthPath = synth.SynthName,
                            Flags = (byte)(loopedSynths.Contains(synth) ? 1 : 0)
                        });

                        ++soundIndex;
                    }
                }
            }

            // Create cue sheet binary file.
            var cueSheetList = new List<SerializationCueSheetTable>
            {
                new SerializationCueSheetTable
                {
                    TableData = CriTableSerializer.Serialize(
                        new List<SerializationVersionInfoTable> { new SerializationVersionInfoTable() },
                        CriTableWriterSettings.AdxSettings),
                    Name = "INFO",
                    TableType = 7
                },
                new SerializationCueSheetTable
                {
                    TableData = CriTableSerializer.Serialize(cueList, CriTableWriterSettings.AdxSettings),
                    Name = "CUE",
                    TableType = 1
                },
                new SerializationCueSheetTable
                {
                    TableData = CriTableSerializer.Serialize(synthList, CriTableWriterSettings.AdxSettings),
                    Name = "SYNTH",
                    TableType = 2,
                },
                new SerializationCueSheetTable
                {
                    TableData = CriTableSerializer.Serialize(soundElementList, CriTableWriterSettings.AdxSettings),
                    Name = "SOUND_ELEMENT",
                    TableType = 4,
                },
                new SerializationCueSheetTable
                {
                    TableData = Resources.Aisac,
                    Name = "ISAAC",
                    TableType = 5,
                },
                new SerializationCueSheetTable
                {
                    TableData = CriTableSerializer.Serialize(voiceLimitGroups, CriTableWriterSettings.AdxSettings),
                    Name = "VOICE_LIMIT_GROUP",
                    TableType = 6,
                }
            };

            CriTableSerializer.Serialize("Mario.csb", cueSheetList, CriTableWriterSettings.AdxSettings);
        }
    }
}
