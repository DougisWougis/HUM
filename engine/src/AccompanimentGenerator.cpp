#include "AccompanimentGenerator.h"
#include "LeadSynthesizer.h" // Phase 4: Delegation
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

namespace {
    // --- PHASE 2: THE MUSICAL DICTIONARY ---
    // Helper function to translate a string like "F# Min" into stacked MIDI pitches
    std::vector<int> getChordPitches(const std::string& chordLabel) {
        std::vector<int> pitches;
        if (chordLabel.empty() || chordLabel == "None" || chordLabel == "N" || chordLabel == "N.C." || chordLabel == "NC") {
            return pitches; 
        }

        std::string root = chordLabel;
        std::string quality = "Maj";

        // Split "C Maj" into "C" and "Maj"
        size_t spaceIdx = chordLabel.find(' ');
        if (spaceIdx != std::string::npos) {
            root = chordLabel.substr(0, spaceIdx);
            quality = chordLabel.substr(spaceIdx + 1);
        }

        // Map root string to a base MIDI integer (Anchored at C3 = 48 for backing tracks)
        static const std::unordered_map<std::string, int> rootMap = {
            {"C", 48}, {"C#", 49}, {"Db", 49}, {"D", 50}, {"D#", 51}, {"Eb", 51},
            {"E", 52}, {"F", 53}, {"F#", 54}, {"Gb", 54}, {"G", 55}, {"G#", 56},
            {"Ab", 56}, {"A", 57}, {"A#", 58}, {"Bb", 58}, {"B", 59}
        };

        int basePitch = 48; // Default to C if parsing fails
        auto it = rootMap.find(root);
        if (it != rootMap.end()) {
            basePitch = it->second;
        } else {
            std::cerr << "    [WARNING] Accompaniment: Could not parse root '" << root << "'. Defaulting to C.\n";
        }

        // Apply harmonic intervals based on chord quality
        pitches.push_back(basePitch); // Root
        
        if (quality == "Min" || quality == "min" || quality == "m") {
            pitches.push_back(basePitch + 3); // Minor 3rd
            pitches.push_back(basePitch + 7); // Perfect 5th
        } else if (quality == "Dim" || quality == "dim") {
            pitches.push_back(basePitch + 3); // Minor 3rd
            pitches.push_back(basePitch + 6); // Diminished 5th (Tritone)
        } else if (quality == "Aug" || quality == "aug") {
            pitches.push_back(basePitch + 4); // Major 3rd
            pitches.push_back(basePitch + 8); // Augmented 5th
        } else { 
            // Default to Major (Catches "Maj", "7", etc.)
            pitches.push_back(basePitch + 4); // Major 3rd
            pitches.push_back(basePitch + 7); // Perfect 5th
        }

        return pitches;
    }
}

namespace HUM {
namespace AccompanimentGenerator {

    // --- PHASE 1: API CONTRACT & COMPILER SILENCING ---
    // Removed [[maybe_unused]] from genreText so we can actively read it
    AudioBuffer render(const std::string& genreText,
                       const ChromagramMatrix& progression,
                       const TempoMap& beatGrid,
                       [[maybe_unused]] const VocalPitchContour& pitchContour,
                       [[maybe_unused]] const SongStructure& structureData,
                       const std::string& soundfontPath) {

        std::cout << "    -> Accompaniment Generator Initialized. Style: " << genreText << "\n";
        
        if (progression.chord_labels.empty() || beatGrid.downbeat_timestamps.empty()) {
            std::cerr << "    [WARNING] Missing chord or beat data. Returning empty backing track.\n";
            return AudioBuffer(); // Return empty buffer
        }

        std::vector<MidiEvent> polyphonicChords;
        
        // Calculate a fallback duration for the very last measure (4 beats in standard 4/4 time)
        float defaultMeasureDuration = (beatGrid.bpm > 0) ? (240.0f / beatGrid.bpm) : 2.0f;

        // --- PHASE 3: THE TIME-GRID CONSTRUCTION LOOP ---
        std::cout << "    -> Building Polyphonic MIDI Grid...\n";
        
        // Ensure we don't read out of bounds if arrays are slightly mismatched in size
        size_t loopCount = std::min(progression.chord_labels.size(), beatGrid.downbeat_timestamps.size());

        for (size_t i = 0; i < loopCount; ++i) {
            float startTime = beatGrid.downbeat_timestamps[i];
            float duration = 0.0f;

            // Calculate duration to the next downbeat, or use default if it's the last chord
            if (i + 1 < loopCount) {
                duration = beatGrid.downbeat_timestamps[i + 1] - startTime;
            } else {
                duration = defaultMeasureDuration;
            }

            // Simulate human articulation: Lift hands slightly before the next chord
            float articulationDuration = duration * 0.95f; 

            // Get the MIDI integers for this specific chord
            std::vector<int> pitches = getChordPitches(progression.chord_labels[i]);

            // Create simultaneous MIDI events to form the chord block
            for (int pitch : pitches) {
                MidiEvent note;
                note.pitch = static_cast<float>(pitch);
                note.start_time = startTime;
                note.duration = articulationDuration; // Use the shortened duration
                
                polyphonicChords.push_back(note);
            }
        }

        std::cout << "    -> Generated " << polyphonicChords.size() << " backing MIDI events.\n";

        // --- PHASE 4: THE DELEGATION HANDOFF ---
        
        // CRITICAL FIX: Prevent LeadSynthesizer from crashing if the matrix was entirely "None"
        if (polyphonicChords.empty()) {
            std::cerr << "    [WARNING] Accompaniment: No valid chords generated. Returning empty buffer.\n";
            return AudioBuffer();
        }

        // We pass an empty dynamics array so FluidSynth uses maximum steady volume for the chords
        std::vector<float> emptyDynamics;
        
        // --- THE FIX: DYNAMIC INSTRUMENT ROUTING ---
        // Map the frontend "genre" string to specific MIDI patch names handled by LeadSynthesizer
        std::string backingInstrument;
        if (genreText == "pad") {
            backingInstrument = "synth";          // Maps to an analog synth pad
        } else if (genreText == "strings") {
            backingInstrument = "strings";        // Maps to a string ensemble
        } else {
            backingInstrument = "electric piano"; // Default fallback for "jazz"
        }
        
        std::cout << "    -> Routing backing track to instrument patch: [" << backingInstrument << "]\n";
        std::cout << "    -> Delegating synthesis to LeadSynthesizer engine...\n";
        
        return LeadSynthesizer::render(polyphonicChords, emptyDynamics, backingInstrument, soundfontPath);
    }

} // namespace AccompanimentGenerator
} // namespace HUM