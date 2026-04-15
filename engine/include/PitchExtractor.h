#pragma once
#ifndef PITCH_EXTRACTOR_H
#define PITCH_EXTRACTOR_H

#include <vector>
#include <stdexcept>

// Include our global structs
#include "DataTypes.h"

namespace HUM {
    
    // Add this inside your HUM namespace
    struct ExtractorConfig {
        std::wstring model_path = L"crepe_small.onnx"; // Allow dynamic injection
        bool enable_zcr = false;
        bool enable_flux = false;
        bool enable_pitch_delta = false;
        
        float target_bpm = 0.0f; // 0.0 means Auto-Detect
        int grid_resolution = 16; // Default to 16th notes
        float quantize_strength = 0.75f; // 75% snap to keep human groove
    };
    namespace PitchExtractor {

        /**
         * @brief Extracts MIDI, rhythm, and continuous pitch from a clean audio buffer.
         * * This function acts as the bridge between raw DSP and symbolic music. It uses 
         * ONNX Runtime to execute the SOME pitch extraction model and algorithmic 
         * processing to determine the tempo map and continuous pitch contour.
         * * @param cleanAudio  [IN] The sanitized 44.1kHz mono audio buffer from Stage 1.
         * @param outMelody   [OUT] Populated with discrete MIDI notes (pitch, start_time, duration).
         * @param outBeatGrid [OUT] Populated with the calculated BPM and downbeat timestamps.
         * @param outContour  [OUT] Populated with continuous pitch data (e.g., CQT bins).
         * * @throws std::runtime_error If ONNX inference fails or model files are missing.
         */
        void extract(const AudioBuffer& cleanAudio, 
                    const ExtractorConfig& config, 
                    std::vector<MidiEvent>& outMelody, 
                    TempoMap& outBeatGrid, 
                    VocalPitchContour& outContour);

    } // namespace PitchExtractor
} // namespace HUM

#endif // PITCH_EXTRACTOR_H