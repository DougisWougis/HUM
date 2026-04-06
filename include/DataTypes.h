#pragma once
#ifndef DATATYPES_H
#define DATATYPES_H

#include <vector>
#include <string>
#include <utility> // For std::pair

namespace HUM {

    // ---------------------------------------------------------
    // 1. Audio Data
    // ---------------------------------------------------------
    
    // Represents a standardized audio buffer (strictly 44.1kHz mono for internal routing)
    struct AudioBuffer {
        std::vector<float> samples;
    };

    // ---------------------------------------------------------
    // 2. Symbolic & Rhythmic Data
    // ---------------------------------------------------------

    // Represents a single discrete musical note
    struct MidiEvent {
        float pitch;       // MIDI note number (e.g., 60.0 for Middle C)
        float start_time;  // In seconds
        float duration;    // In seconds
    };

    // Represents the explicit beat grid derived from the audio
    struct TempoMap {
        float bpm;
        std::vector<float> downbeat_timestamps; // In seconds
    };

    // ---------------------------------------------------------
    // 3. Continuous & Structural Metadata
    // ---------------------------------------------------------

    // Represents the continuous pitch nuances (slides, vibrato) 
    // extracted via Constant-Q Transform (top-4 bins)
    struct VocalPitchContour {
        std::vector<std::vector<float>> cqt_top4_bins; 
    };

    // Represents the high-level musical roadmap
    struct SongStructure {
        std::string key_and_mode; // e.g., "G Major"
        std::string phrase_segmentation; // e.g., "A8A8B8B8"
        std::vector<std::pair<std::string, float>> section_labels; // e.g., {"Verse", 0.0}, {"Chorus", 32.5}
    };

    // ---------------------------------------------------------
    // 4. Harmonic Data
    // ---------------------------------------------------------

    // Represents the explicit 12-bin chord progression over time
    // Rows = 12 pitch classes, Columns = Time frames
    struct ChromagramMatrix {
        std::vector<std::vector<float>> matrix;
    };

} // namespace AIStudio

#endif // DATATYPES_H