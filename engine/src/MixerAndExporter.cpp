#include "MixerAndExporter.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstdint> // Added for strict type definitions

namespace {
    // --- PHASE 2 COMPONENTS: IMPROVED REVERB ---
    
    class CombFilter {
        std::vector<float> buffer;
        size_t bufferIdx = 0;
        float feedback;
        float damp;        // FIX: High-frequency absorption
        float filterStore; // FIX: Memory for the low-pass filter
    public:
        CombFilter(size_t size, float fb, float dmp) : buffer(size, 0.0f), feedback(fb), damp(dmp), filterStore(0.0f) {}
        float process(float input) {
            float output = buffer[bufferIdx];
            
            // FIX: 1-pole lowpass filter to dampen metallic ringing
            filterStore = (output * (1.0f - damp)) + (filterStore * damp);
            
            buffer[bufferIdx] = input + (filterStore * feedback);
            if (++bufferIdx >= buffer.size()) bufferIdx = 0;
            return output;
        }
    };

    class AllPassFilter {
        std::vector<float> buffer;
        size_t bufferIdx = 0;
    public:
        AllPassFilter(size_t size) : buffer(size, 0.0f) {}
        float process(float input) {
            float bufOut = buffer[bufferIdx];
            float output = -input + bufOut;
            buffer[bufferIdx] = input + (bufOut * 0.5f); 
            if (++bufferIdx >= buffer.size()) bufferIdx = 0;
            return output;
        }
    };

    class SimpleReverb {
        std::vector<CombFilter> combs;
        std::vector<AllPassFilter> allpasses;
    public:
        SimpleReverb() {
            // Added damping factor (0.2f) to all comb filters
            combs.push_back(CombFilter(1557, 0.84f, 0.2f));
            combs.push_back(CombFilter(1617, 0.84f, 0.2f));
            combs.push_back(CombFilter(1491, 0.84f, 0.2f));
            combs.push_back(CombFilter(1422, 0.84f, 0.2f));
            
            allpasses.push_back(AllPassFilter(225));
            allpasses.push_back(AllPassFilter(556));
            allpasses.push_back(AllPassFilter(441));
            allpasses.push_back(AllPassFilter(341));
        }

        float process(float input) {
            float out = 0.0f;
            for (auto& comb : combs) out += comb.process(input);
            for (auto& ap : allpasses) out = ap.process(out);
            return out * 0.15f; 
        }
    };

    // --- PHASE 4 COMPONENT: WAV HEADER ---
#pragma pack(push, 1)
    struct WavHeader {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t chunkSize; 
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt[4] = {'f', 'm', 't', ' '};
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 1; 
        uint16_t numChannels = 1; 
        uint32_t sampleRate;      // FIX: Made dynamic
        uint32_t byteRate;        // FIX: Made dynamic
        uint16_t blockAlign = 2; 
        uint16_t bitsPerSample = 16;
        char data[4] = {'d', 'a', 't', 'a'};
        uint32_t dataSize; 
    };
#pragma pack(pop)
}

namespace HUM {
namespace MixerAndExporter {

    bool processAndExport(const AudioBuffer& leadTrack, 
                          const AudioBuffer& backingTrack, 
                          const std::string& outputPath,
                          const MixSettings& settings) {

        if (leadTrack.samples.empty()) {
            std::cerr << "Mixer: Lead track is empty. Nothing to export.\n";
            return false;
        }

        // FIX: Dynamically pull sample rate
        int targetSampleRate = leadTrack.sample_rate;

        std::cout << "    -> Phase 1: Summing Mixer...\n";
        
        size_t leadLen = leadTrack.samples.size();
        size_t backingLen = backingTrack.samples.size();
        size_t maxLen = std::max(leadLen, backingLen);

        // FIX: Add a 3-second tail for the reverb to decay into
        size_t reverbTailSamples = static_cast<size_t>(targetSampleRate * 3.0f);
        size_t totalRenderLen = maxLen + reverbTailSamples;

        std::vector<float> mixedAudio(totalRenderLen, 0.0f);

        for (size_t i = 0; i < maxLen; ++i) {
            float leadSample = (i < leadLen) ? leadTrack.samples[i] : 0.0f;
            float backingSample = (i < backingLen) ? backingTrack.samples[i] : 0.0f;
            
            mixedAudio[i] = (leadSample * settings.lead_volume) + 
                            (backingSample * settings.backing_volume);
        }

        std::cout << "    -> Phase 2: Processing Schroeder Reverb (Wet/Dry: " << (settings.reverb_amount * 100) << "%)...\n";
        
        if (settings.reverb_amount > 0.0f) {
            SimpleReverb room;
            // Notice this loop goes all the way to totalRenderLen, letting the tail ring out!
            for (size_t i = 0; i < totalRenderLen; ++i) {
                float dry = mixedAudio[i];
                float wet = room.process(dry);
                mixedAudio[i] = (dry * (1.0f - settings.reverb_amount)) + (wet * settings.reverb_amount);
            }
        }

        std::cout << "    -> Phase 3: Applying Mastering Soft-Clipper...\n";
        
        if (settings.enable_clipper) {
            for (size_t i = 0; i < totalRenderLen; ++i) {
                mixedAudio[i] = std::tanh(mixedAudio[i]);
            }
        }

        std::cout << "    -> Phase 4: Encoding 16-bit PCM WAV to disk...\n";

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            std::cerr << "Mixer: Failed to open output file " << outputPath << "\n";
            return false;
        }

        WavHeader header;
        header.sampleRate = targetSampleRate;
        header.numChannels = 1;
        header.bitsPerSample = 16;
        header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
        header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
        
        uint32_t bytesOfAudioData = static_cast<uint32_t>(mixedAudio.size() * (header.bitsPerSample / 8));
        header.dataSize = bytesOfAudioData;
        header.chunkSize = 36 + bytesOfAudioData;

        outFile.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));

        // FIX: The I/O Performance Upgrade. Convert floats to int16s in RAM first.
        std::vector<int16_t> pcmBuffer(mixedAudio.size());
        for (size_t i = 0; i < mixedAudio.size(); ++i) {
            float sample = std::max(-1.0f, std::min(1.0f, mixedAudio[i]));
            pcmBuffer[i] = static_cast<int16_t>(sample * 32767.0f);
        }

        // Blast the entire buffer to disk in one single OS operation
        outFile.write(reinterpret_cast<const char*>(pcmBuffer.data()), pcmBuffer.size() * sizeof(int16_t));

        outFile.close();
        std::cout << "    -> Export successful: " << outputPath << "\n";
        return true;
    }

} // namespace MixerAndExporter
} // namespace HUM