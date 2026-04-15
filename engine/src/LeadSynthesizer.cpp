#include "LeadSynthesizer.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cctype>
#include <stdexcept>
#include <cmath>
#include <memory>

// FluidSynth C API
#include <fluidsynth.h>

namespace {

    using namespace HUM;

    constexpr int TARGET_SAMPLE_RATE = 44100;
    constexpr int FLUID_RENDER_BLOCK_SIZE = 64;
    constexpr float FRAME_STEP_SEC = 0.01f;

    enum ActionType {
        CC_EXPRESSION,
        NOTE_OFF,
        NOTE_ON
    };

    struct SynthAction {
        size_t target_sample;
        ActionType type;
        int param1; 
        int param2; 

        bool operator<(const SynthAction& other) const {
            if (target_sample != other.target_sample) return target_sample < other.target_sample;
            return type < other.type; 
        }
    };

    // FIX: RAII Deleters for FluidSynth C-Pointers
    struct FluidSettingsDeleter { void operator()(fluid_settings_t* p) const { delete_fluid_settings(p); } };
    struct FluidSynthDeleter { void operator()(fluid_synth_t* p) const { delete_fluid_synth(p); } };

    // FIX: Static initialization prevents rebuilding the dictionary on every call
    int getGeneralMidiProgram(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });

        static const std::unordered_map<std::string, int> gmMap = {
            {"piano", 0}, {"electric piano", 4}, {"harpsichord", 7}, {"organ", 16},
            {"acoustic guitar", 24}, {"electric guitar", 27}, {"bass", 33}, {"fretless bass", 35},
            {"violin", 40}, {"cello", 42}, {"strings", 48}, {"choir", 52},
            {"trumpet", 56}, {"trombone", 57}, {"french horn", 60}, {"brass", 61},
            {"saxophone", 65}, {"oboe", 68}, {"flute", 73}, {"pan flute", 75},
            {"synth lead", 80}, {"saw lead", 81}, {"pad", 88}, {"halo pad", 94}
        };

        auto it = gmMap.find(name);
        if (it != gmMap.end()) return it->second;
        
        std::cout << "    [WARNING] Instrument '" << name << "' not found. Defaulting to Piano (0).\n";
        return 0; 
    }
}

namespace HUM {
namespace LeadSynthesizer {

    AudioBuffer render(const std::vector<MidiEvent>& vocalMelody, 
                       const std::vector<float>& loudness_rms,
                       const std::string& instrumentName, 
                       const std::string& soundfontPath) {
                           
        if (vocalMelody.empty()) throw std::runtime_error("LeadSynthesizer: Cannot synthesize an empty melody.");

        std::cout << "    -> Booting FluidSynth Engine (Offline Render Mode)...\n";

        // --- THE FIX: MUTE THE INTERNAL LOGGER ---
        // Silence the massive wall of text, but leave FLUID_ERR active so we still see fatal crashes
        fluid_set_log_function(FLUID_DBG, nullptr, nullptr);
        fluid_set_log_function(FLUID_INFO, nullptr, nullptr);
        fluid_set_log_function(FLUID_WARN, nullptr, nullptr);

        // FIX: Safe RAII Initialization. These will auto-delete even if an exception is thrown.
        std::unique_ptr<fluid_settings_t, FluidSettingsDeleter> settings(new_fluid_settings());
        if (!settings) throw std::runtime_error("Failed to create FluidSynth settings.");
        
        fluid_settings_setnum(settings.get(), "synth.sample-rate", TARGET_SAMPLE_RATE);
        fluid_settings_setint(settings.get(), "synth.audio-channels", 1); 

        std::unique_ptr<fluid_synth_t, FluidSynthDeleter> synth(new_fluid_synth(settings.get()));
        if (!synth) throw std::runtime_error("Failed to create FluidSynth instance.");

        int sf_id = fluid_synth_sfload(synth.get(), soundfontPath.c_str(), 1);
        if (sf_id == FLUID_FAILED) {
            throw std::runtime_error("LeadSynthesizer: Failed to load SoundFont at " + soundfontPath);
        }

        int programNum = getGeneralMidiProgram(instrumentName);
        fluid_synth_program_change(synth.get(), 0, programNum); 
        
        std::cout << "    -> Building Expression Dynamics Queue...\n";
        std::vector<SynthAction> actions;
        
        // FIX: Pre-allocate vector memory to prevent O(N) reallocation overhead
        actions.reserve(loudness_rms.size() + (vocalMelody.size() * 2));

        if (!loudness_rms.empty()) {
            // FIX: Fast max_element iterator
            float maxRms = *std::max_element(loudness_rms.begin(), loudness_rms.end());
            if (maxRms < 0.0001f) maxRms = 0.0001f;

            for (size_t i = 0; i < loudness_rms.size(); ++i) {
                float normRms = loudness_rms[i] / maxRms;
                int ccVal = static_cast<int>(std::pow(normRms, 0.5f) * 127.0f);
                size_t targetSample = static_cast<size_t>(i * FRAME_STEP_SEC * TARGET_SAMPLE_RATE);
                
                actions.push_back({targetSample, CC_EXPRESSION, 11, std::max(0, std::min(127, ccVal))});
            }
        }

        for (const auto& note : vocalMelody) {
            size_t startSample = static_cast<size_t>(note.start_time * TARGET_SAMPLE_RATE);
            size_t endSample = static_cast<size_t>((note.start_time + note.duration) * TARGET_SAMPLE_RATE);
            
            actions.push_back({startSample, NOTE_ON, static_cast<int>(note.pitch), 100});
            actions.push_back({endSample, NOTE_OFF, static_cast<int>(note.pitch), 0});
        }
        
        std::sort(actions.begin(), actions.end());

        size_t totalSamples = actions.back().target_sample + TARGET_SAMPLE_RATE;
        std::vector<float> outputBuffer(totalSamples, 0.0f);

        std::cout << "    -> Synthesizing " << totalSamples / TARGET_SAMPLE_RATE << " seconds of " << instrumentName << "...\n";

        size_t actionIndex = 0;
        std::vector<float> leftBlock(FLUID_RENDER_BLOCK_SIZE, 0.0f);
        std::vector<float> rightBlock(FLUID_RENDER_BLOCK_SIZE, 0.0f);

        for (size_t currentSample = 0; currentSample < totalSamples; currentSample += FLUID_RENDER_BLOCK_SIZE) {
            
            while (actionIndex < actions.size() && actions[actionIndex].target_sample <= currentSample) {
                const auto& action = actions[actionIndex];
                
                if (action.type == NOTE_ON) {
                    fluid_synth_noteon(synth.get(), 0, action.param1, action.param2);
                } else if (action.type == NOTE_OFF) {
                    fluid_synth_noteoff(synth.get(), 0, action.param1);
                } else if (action.type == CC_EXPRESSION) {
                    fluid_synth_cc(synth.get(), 0, action.param1, action.param2);
                }
                actionIndex++;
            }

            int framesToRender = static_cast<int>(std::min<size_t>(FLUID_RENDER_BLOCK_SIZE, totalSamples - currentSample));
            fluid_synth_write_float(synth.get(), framesToRender, leftBlock.data(), 0, 1, rightBlock.data(), 0, 1);

            for (int i = 0; i < framesToRender; ++i) {
                outputBuffer[currentSample + i] = (leftBlock[i] + rightBlock[i]) * 0.5f;
            }
        }

        // NO MANUAL CLEANUP NEEDED: std::unique_ptr handles synth and settings automatically here.

        AudioBuffer finalOutput;
        finalOutput.samples = std::move(outputBuffer);
        finalOutput.sample_rate = TARGET_SAMPLE_RATE;
        
        std::cout << "    -> Synthesis complete.\n";
        return finalOutput;
    }

} 
}