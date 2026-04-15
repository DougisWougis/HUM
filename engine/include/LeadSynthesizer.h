#pragma once
#ifndef HUM_LEAD_SYNTHESIZER_H
#define HUM_LEAD_SYNTHESIZER_H

#include <vector>
#include <string>

// Include the global data structures required for the pipeline handoff
#include "DataTypes.h"

namespace HUM {
namespace LeadSynthesizer {

    /**
     * @brief Synthesizes a discrete MIDI melody into a continuous, expressive audio waveform.
     * * This module utilizes the FluidSynth C API in offline-render mode. It converts 
     * quantized MIDI notes into standard Note-On/Note-Off events, and maps the continuous 
     * RMS loudness data to MIDI CC 11 (Expression) to simulate realistic human breath dynamics.
     * * @param vocalMelody    The quantized vector of MIDI events from Stage 2/3.
     * @param loudness_rms   The continuous volume data extracted in Stage 2 for dynamic expression.
     * @param instrumentName A natural language string (e.g., "saxophone", "piano").
     * @param soundfontPath  The local path to the .sf2 General MIDI file.
     * @return AudioBuffer   The rendered 44.1kHz mono audio float array.
     * * @throws std::runtime_error if FluidSynth fails to initialize or the SoundFont is missing.
     */
    AudioBuffer render(const std::vector<MidiEvent>& vocalMelody, 
                       const std::vector<float>& loudness_rms,
                       const std::string& instrumentName,
                       const std::string& soundfontPath = "assets/gm.sf2");

} // namespace LeadSynthesizer
} // namespace HUM

#endif // HUM_LEAD_SYNTHESIZER_H