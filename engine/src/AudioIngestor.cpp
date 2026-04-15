#include "AudioIngestor.h"

#include <iostream>
#include <cmath>
#include <numeric>
#include <stdexcept>

// Inject the miniaudio C library implementation right into this file
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// The M_PI constant isn't always guaranteed by <cmath> on Windows MSVC
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------
// ANONYMOUS NAMESPACE: Internal logic hidden from main.cpp
// ---------------------------------------------------------
namespace {

    using namespace HUM;

    // RAII Wrapper for miniaudio decoder. 
    // This guarantees the C-library memory is freed even if a C++ exception is thrown.
    struct DecoderGuard {
        ma_decoder decoder;
        bool initialized = false;

        ~DecoderGuard() {
            if (initialized) {
                ma_decoder_uninit(&decoder);
            }
        }
    };

    // Phase 1: Decoding with Optimized Memory Allocation
    AudioBuffer decodeWithMiniaudio(const std::string& filePath) {
        ma_decoder_config config = ma_decoder_config_init(
            ma_format_f32, 
            AudioIngestor::TARGET_CHANNELS, 
            AudioIngestor::TARGET_SAMPLE_RATE
        );
        
        DecoderGuard guard;
        ma_result result = ma_decoder_init_file(filePath.c_str(), &config, &guard.decoder);
        
        if (result != MA_SUCCESS) {
            throw std::runtime_error("Failed to open or decode audio file: " + filePath);
        }
        guard.initialized = true;

        AudioBuffer buffer;

        // OPTIMIZATION: Query the file length to pre-allocate RAM.
        // This prevents the std::vector from constantly destroying and rebuilding 
        // itself in memory as it grows during the while loop.
        ma_uint64 totalFrames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&guard.decoder, &totalFrames) == MA_SUCCESS && totalFrames > 0) {
            buffer.samples.reserve(totalFrames);
        }

        constexpr ma_uint32 CHUNK_SIZE = 4096;
        std::vector<float> chunk(CHUNK_SIZE);
        
        while (true) {
            ma_uint64 framesRead = 0;
            
            ma_result readResult = ma_decoder_read_pcm_frames(&guard.decoder, chunk.data(), CHUNK_SIZE, &framesRead);
            // Break if we hit an error OR if the file is completely finished reading
            if (readResult != MA_SUCCESS || framesRead == 0) {
                break; 
            }
            
            buffer.samples.insert(buffer.samples.end(), chunk.begin(), chunk.begin() + framesRead);
        }

        return buffer;
    }

    // Phases 2 & 3: The 2-Pass DSP Pipeline
    void applyOptimizedDSP(AudioBuffer& buffer) {
        if (buffer.samples.empty()) return;

        size_t numSamples = buffer.samples.size();

        // ---------------------------------------------------------
        // PASS 1: 100Hz High-Pass Filter & Max Peak Detection
        // ---------------------------------------------------------
        constexpr float cutoffFreq = 100.0f;
        constexpr float dt = 1.0f / static_cast<float>(AudioIngestor::TARGET_SAMPLE_RATE);
        constexpr float RC = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoffFreq);
        constexpr float alpha = RC / (RC + dt);

        float prev_x = buffer.samples[0];
        float prev_y = buffer.samples[0];
        float max_peak = std::abs(prev_y);

        for (size_t i = 1; i < numSamples; ++i) {
            float current_x = buffer.samples[i];
            
            // Apply 1-pole IIR HPF to mathematically eliminate AC Hum (and DC Offset)
            float current_y = alpha * (prev_y + current_x - prev_x);
            buffer.samples[i] = current_y; 
            
            // Simultaneously track the peak volume for the next pass
            float abs_val = std::abs(current_y);
            if (abs_val > max_peak) {
                max_peak = abs_val;
            }
            
            prev_x = current_x;
            prev_y = current_y;
        }

        if (max_peak < 0.0001f) max_peak = 1.0f; // Prevent divide-by-zero on silent files

        // ---------------------------------------------------------
        // PASS 2: Loudness Maximizer and "Soft" Noise Gate
        // ---------------------------------------------------------
        
        // 1. Massive Pre-Gain: We boost the normalized signal by 500% to pull up quiet laptop mics
        constexpr float PRE_GAIN_BOOST = 5.0f; 
        float norm_factor = (1.0f / max_peak) * PRE_GAIN_BOOST;

        // Smooth envelope state for the gate
        float current_gain = 0.0f; 
        
        // 2. Forgiving Gate: Lowered threshold so we don't accidentally cut off quiet tail ends of notes
        constexpr float GATE_THRESHOLD = 0.008f; 
        
        constexpr float ATTACK_COEF = 0.01f;  // Fast open when speaking/humming
        constexpr float RELEASE_COEF = 0.002f; // Smooth fade out when silent

        for (size_t i = 0; i < numSamples; ++i) {
            // Apply the massive volume boost
            float sample = buffer.samples[i] * norm_factor;
            
            // Apply Hyperbolic Tangent Soft-Clipper
            // This safely maps any massive volume spikes (from -inf to +inf) gracefully into (-1.0 to 1.0)
            sample = std::tanh(sample);
            
            // Scale it down just slightly so the absolute peaks sit at a safe 0.9f (0dBFS headroom)
            sample *= 0.9f;
            
            // Determine if the gate should be open (1.0) or closed (0.0)
            float target_gain = (std::abs(sample) > GATE_THRESHOLD) ? 1.0f : 0.0f;
            
            // Smoothly transition the current gain towards the target
            if (target_gain > current_gain) {
                current_gain += (target_gain - current_gain) * ATTACK_COEF;
            } else {
                current_gain += (target_gain - current_gain) * RELEASE_COEF;
            }
            
            // Apply the smoothed gate multiplier
            buffer.samples[i] = sample * current_gain;
        }
    }

} // end anonymous namespace


// ---------------------------------------------------------
// PUBLIC EXPOSED LOGIC
// ---------------------------------------------------------
namespace HUM {
namespace AudioIngestor {

    AudioBuffer process(const std::string& filePath) {
        std::cout << "    -> Decoding file: " << filePath << "\n";
        AudioBuffer buffer = decodeWithMiniaudio(filePath);
        
        std::cout << "    -> Applying 100Hz HPF, Normalization, and Smooth Gate...\n";
        applyOptimizedDSP(buffer);
        
        return buffer;
    }

} // namespace AudioIngestor
} // namespace HUM