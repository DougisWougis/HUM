#pragma once
#ifndef AUDIO_INGESTOR_H
#define AUDIO_INGESTOR_H

#include <string>
#include <stdexcept>

// Include our global structs
#include "DataTypes.h"

namespace HUM {
    
    // Instead of a class, this is a dedicated utility namespace
    namespace AudioIngestor {

        // Explicitly define the pipeline's strict audio requirements
        constexpr int TARGET_SAMPLE_RATE = 16000;
        constexpr int TARGET_CHANNELS = 1; // Mono

        /**
         * @brief Ingests raw audio, standardizes format, and removes background noise.
         * * @param filePath The absolute or relative path to the input audio file.
         * @return AudioBuffer The sanitized, ready-for-inference audio array.
         * @throws std::runtime_error If the file does not exist or decoding fails.
         */
        AudioBuffer process(const std::string& filePath);

        // NOTE: Internal processing helpers (decodeAudio, applyNoiseGate) have been 
        // stripped from the header. They will be strictly contained within the .cpp file.

    } // namespace AudioIngestor
} // namespace HUM

#endif // AUDIO_INGESTOR_H