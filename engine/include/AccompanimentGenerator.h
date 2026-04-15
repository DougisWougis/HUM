#pragma once
#ifndef HUM_ACCOMPANIMENT_GENERATOR_H
#define HUM_ACCOMPANIMENT_GENERATOR_H

#include <string>
#include "DataTypes.h"

namespace HUM {
namespace AccompanimentGenerator {

    /**
     * @brief Synthesizes the backing track. (Currently implemented as a FluidSynth Stub)
     * * This interface preserves the strict data contract required for future neural-network 
     * integration (e.g., Stable Audio / DDSP). For the MVP, it acts as a MIDI chord 
     * generator that maps Stage 3 harmonic data to an offline FluidSynth render.
     * * @param genreText       The desired style (e.g., "jazz", "synthwave").
     * @param progression     The extracted measure-by-measure chords from Stage 3.
     * @param beatGrid        The tempo map for temporal alignment from Stage 2.
     * @param pitchContour    The continuous vocal pitch from Stage 2.
     * @param structureData   The song sections from Stage 3.
     * @param soundfontPath   Path to the GM soundfont (Defaults to "assets/gm.sf2").
     * @return AudioBuffer    The rendered 44.1kHz backing track.
     */
    AudioBuffer render(const std::string& genreText,
                       const ChromagramMatrix& progression,
                       const TempoMap& beatGrid,
                       const VocalPitchContour& pitchContour,
                       const SongStructure& structureData,
                       const std::string& soundfontPath = "assets/gm.sf2");

} // namespace AccompanimentGenerator
} // namespace HUM

#endif // HUM_ACCOMPANIMENT_GENERATOR_H