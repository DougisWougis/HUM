#pragma once
#ifndef HUM_MUSICAL_ANALYZER_H
#define HUM_MUSICAL_ANALYZER_H

#include <vector>

// Include the global data structures required for the pipeline handoff
#include "DataTypes.h"

namespace HUM {
namespace MusicalAnalyzer {

    /** 
     * @brief Analyzes a quantized monophonic melody to determine its global key and local chord progression.
     * * This module utilizes a Fujishima Pitch Class Profile (PCP) generator paired with a 
     * Krumhansl-Schmuckler Key Classifier to determine the global key. It then uses Cosine 
     * Similarity template matching (with an expanded dictionary including secondary dominants) 
     * to populate a continuous chord matrix over the provided temporal beat grid.
     * * @param vocalMelody       The extracted and quantized vector of MIDI events from Stage 2.
     * @param beatGrid          The TempoMap detailing the downbeat timestamps.
     * @param structureData     [OUT] The struct to be populated with the detected global key and mode.
     * @param chordProgression  [OUT] The matrix and string labels to be populated with the harmonic progression.
     * * @throws std::runtime_error if vocalMelody is empty or beatGrid is too sparse to form a progression.
     */
    void analyze(const std::vector<MidiEvent>& vocalMelody, 
                 const TempoMap& beatGrid, 
                 SongStructure& structureData, 
                 ChromagramMatrix& chordProgression);

} // namespace MusicalAnalyzer
} // namespace HUM

#endif // HUM_MUSICAL_ANALYZER_H