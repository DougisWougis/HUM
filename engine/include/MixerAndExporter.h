#pragma once
#ifndef HUM_MIXER_AND_EXPORTER_H
#define HUM_MIXER_AND_EXPORTER_H

#include <string>
#include "DataTypes.h"

namespace HUM {
namespace MixerAndExporter {

    /**
     * @brief Configuration for the final mixdown and DSP processing.
     */ 
    struct MixSettings {
        float lead_volume = 1.0f;       // Volume multiplier for the synthesized lead (0.0 to 1.0+)
        float backing_volume = 0.3f;    // Volume multiplier for the accompaniment track
        float reverb_amount = 0.25f;    // Wet/Dry mix for the Freeverb room (0.0 = Dry, 1.0 = Washed out)
        bool enable_clipper = true;     // Safety limiter to prevent digital distortion
    };

    /**
     * @brief Mixes tracks, applies zero-bloat DSP, and writes a raw 16-bit PCM WAV file to disk.
     * * @param leadTrack    The generated 44.1kHz lead instrument buffer from Stage 5.
     * @param backingTrack The 44.1kHz accompaniment buffer (can be empty).
     * @param outputPath   The local file path to write the .wav file (e.g., "final_output.wav").
     * @param settings     The mix volumes and effect parameters.
     * @return true        If the RIFF/WAVE headers and PCM data were successfully written.
     * @return false       If file stream creation failed (e.g., locked file or bad path).
     */
    bool processAndExport(const AudioBuffer& leadTrack, 
                          const AudioBuffer& backingTrack, 
                          const std::string& outputPath,
                          const MixSettings& settings = MixSettings());

} // namespace MixerAndExporter
} // namespace HUM

#endif // HUM_MIXER_AND_EXPORTER_H