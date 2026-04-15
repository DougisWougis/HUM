#include "MusicalAnalyzer.h"
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace {
    using namespace HUM;

    const std::array<std::string, 12> PITCH_CLASSES = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    // --- KRUMHANSL-SCHMUCKLER IDEAL PROFILES ---
    constexpr std::array<float, 12> KS_MAJOR = {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
    };
    constexpr std::array<float, 12> KS_MINOR = {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
    };

    // --- DATA STRUCTURES ---
    struct ChordTemplate {
        std::string name;
        std::array<float, 12> profile;
        float penalty_weight; 
        bool is_primary; 
        int root_bin; // <-- ADD THIS
    };

    // --- MATH HELPERS ---
    float cosineSimilarity(const std::array<float, 12>& a, const std::array<float, 12>& b) {
        float dot = 0.0f, magA = 0.0f, magB = 0.0f;
        for(int i = 0; i < 12; ++i) {
            dot += a[i] * b[i];
            magA += a[i] * a[i];
            magB += b[i] * b[i];
        }
        // FIX: Floating Point Equality Protection
        if (magA < 1e-6f || magB < 1e-6f) return 0.0f;
        return dot / (std::sqrt(magA) * std::sqrt(magB));
    }

    // Phase 1: Fujishima Pitch Class Profile (Duration-Weighted)
    std::array<float, 12> buildPCP(const std::vector<MidiEvent>& melody, float startSec, float endSec) {
        std::array<float, 12> pcp = {0.0f};
        for (const auto& note : melody) {
            float noteEnd = note.start_time + note.duration;
            if (noteEnd > startSec && note.start_time < endSec) {
                float overlapStart = std::max(startSec, note.start_time);
                float overlapEnd = std::min(endSec, noteEnd);
                float durationInWindow = overlapEnd - overlapStart;

                // FIX: Safe Modulo Arithmetic for Negative Pitches
                int pitchInt = static_cast<int>(std::round(note.pitch));
                int bin = ((pitchInt % 12) + 12) % 12; 
                
                pcp[bin] += durationInWindow;
            }
        }
        return pcp;
    }

    // Phase 2: Global Key Classifier
    void detectGlobalKey(const std::array<float, 12>& globalPcp, std::string& outKeyName, int& outRootBin, bool& outIsMinor) {
        float bestScore = -1.0f;
        
        // Test all 12 Major Keys
        for (int i = 0; i < 12; ++i) {
            std::array<float, 12> shiftedMajor;
            for (int j = 0; j < 12; ++j) shiftedMajor[(j + i) % 12] = KS_MAJOR[j];
            
            float score = cosineSimilarity(globalPcp, shiftedMajor);
            if (score > bestScore) {
                bestScore = score;
                outRootBin = i;
                outIsMinor = false;
                outKeyName = PITCH_CLASSES[i] + " Major";
            }
        }

        // Test all 12 Minor Keys
        for (int i = 0; i < 12; ++i) {
            std::array<float, 12> shiftedMinor;
            for (int j = 0; j < 12; ++j) shiftedMinor[(j + i) % 12] = KS_MINOR[j];
            
            float score = cosineSimilarity(globalPcp, shiftedMinor);
            if (score > bestScore) {
                bestScore = score;
                outRootBin = i;
                outIsMinor = true;
                outKeyName = PITCH_CLASSES[i] + " Minor";
            }
        }
    }

    // Phase 3: Dynamic Chord Dictionary Generator
    void addChordToDict(std::vector<ChordTemplate>& dict, const std::string& name, int root, const std::vector<int>& intervals, float penalty, bool isPrimary = false) {
        std::array<float, 12> profile = {0.0f};
        profile[root % 12] = 1.0f;
        for (int interval : intervals) {
            profile[(root + interval) % 12] = 1.0f;
        }
        // FIX: Add the modulo'd root to the struct initialization
        dict.push_back({name, profile, penalty, isPrimary, root % 12});
    }

    std::vector<ChordTemplate> buildExpandedDictionary(int rootBin, bool isMinor) {
        std::vector<ChordTemplate> dict;
        
        std::vector<int> maj = {4, 7};
        std::vector<int> min = {3, 7};
        std::vector<int> dim = {3, 6};
        std::vector<int> dom7 = {4, 7, 10};

        if (!isMinor) {
            // Core Diatonic (Primary chords flagged true)
            addChordToDict(dict, PITCH_CLASSES[rootBin] + " Maj", rootBin, maj, 1.0f, true);                 // I
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 2) % 12] + " Min", rootBin + 2, min, 1.0f);        // ii
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 4) % 12] + " Min", rootBin + 4, min, 1.0f);        // iii
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 5) % 12] + " Maj", rootBin + 5, maj, 1.0f, true);  // IV
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 7) % 12] + " Maj", rootBin + 7, maj, 1.0f, true);  // V
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 9) % 12] + " Min", rootBin + 9, min, 1.0f);        // vi
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 11) % 12] + " Dim", rootBin + 11, dim, 1.0f);      // vii°

            // Secondary Dominants
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 2) % 12] + " Maj", rootBin + 2, maj, 0.85f);       // V/V
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 4) % 12] + " Maj", rootBin + 4, maj, 0.85f);       // V/vi
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 9) % 12] + " Maj", rootBin + 9, maj, 0.85f);       // V/ii
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 11) % 12] + " Maj", rootBin + 11, maj, 0.85f);     // V/iii
            addChordToDict(dict, PITCH_CLASSES[rootBin] + " Dom7", rootBin, dom7, 0.85f);                    // V/IV
        } else {
            // Core Diatonic Minor (Primary chords flagged true)
            addChordToDict(dict, PITCH_CLASSES[rootBin] + " Min", rootBin, min, 1.0f, true);                 // i
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 2) % 12] + " Dim", rootBin + 2, dim, 1.0f);        // ii°
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 3) % 12] + " Maj", rootBin + 3, maj, 1.0f);        // III
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 5) % 12] + " Min", rootBin + 5, min, 1.0f, true);  // iv
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 7) % 12] + " Min", rootBin + 7, min, 1.0f, true);  // v (natural)
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 8) % 12] + " Maj", rootBin + 8, maj, 1.0f);        // VI
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 10) % 12] + " Maj", rootBin + 10, maj, 1.0f);      // VII
            
            // Harmonic Minor V chord
            addChordToDict(dict, PITCH_CLASSES[(rootBin + 7) % 12] + " Maj", rootBin + 7, maj, 0.95f, true); // V
        }

        return dict;
    }

} // end anonymous namespace

namespace HUM {
namespace MusicalAnalyzer {

    void analyze(const std::vector<MidiEvent>& vocalMelody, 
                 const TempoMap& beatGrid, 
                 SongStructure& structureData, 
                 ChromagramMatrix& chordProgression) {
                     
        // FIX: Grid Boundary Crash Prevention
        if (vocalMelody.empty() || beatGrid.downbeat_timestamps.size() < 2) {
            throw std::runtime_error("MusicalAnalyzer: Beat grid too sparse to generate harmonic progression.");
        }

        std::cout << "    -> Building Global Pitch Class Profile...\n";
        float songEnd = vocalMelody.back().start_time + vocalMelody.back().duration;
        std::array<float, 12> globalPcp = buildPCP(vocalMelody, 0.0f, songEnd);

        std::cout << "    -> Running Krumhansl-Schmuckler Key Classification...\n";
        int rootBin = 0;
        bool isMinor = false;
        detectGlobalKey(globalPcp, structureData.key_and_mode, rootBin, isMinor);
        
        std::cout << "    -> [DETECTED KEY]: " << structureData.key_and_mode << "\n";

        std::cout << "    -> Generating Expanded Harmonic Dictionary...\n";
        std::vector<ChordTemplate> dictionary = buildExpandedDictionary(rootBin, isMinor);

        std::cout << "    -> Slicing Timeline and Assigning Chords...\n";
        
        // --- ADD MVP MACRO STRUCTURE ---
        structureData.phrase_segmentation = "A";
        structureData.section_labels = {{"Verse", 0.0f}};

        chordProgression.matrix.assign(12, std::vector<float>(beatGrid.downbeat_timestamps.size() - 1, 0.0f));
        chordProgression.chord_labels.clear();
        chordProgression.bass_root_bins.clear(); // Initialize our new array

        for (size_t i = 0; i < beatGrid.downbeat_timestamps.size() - 1; ++i) {
            float measureStart = beatGrid.downbeat_timestamps[i];
            float measureEnd = beatGrid.downbeat_timestamps[i + 1];

            std::array<float, 12> localPcp = buildPCP(vocalMelody, measureStart, measureEnd);
            
            float totalWeight = std::accumulate(localPcp.begin(), localPcp.end(), 0.0f);
            if (totalWeight < 0.05f) {
                chordProgression.chord_labels.push_back("N.C.");
                continue; 
            }

            float bestScore = -1.0f;
            const ChordTemplate* bestChord = nullptr;

            for (const auto& tmpl : dictionary) {
                float score = cosineSimilarity(localPcp, tmpl.profile) * tmpl.penalty_weight;
                
                // FIX: Tie-Breaker Bias Implementation
                if (score > bestScore + 0.01f) {
                    // Clear winner
                    bestScore = score;
                    bestChord = &tmpl;
                } else if (bestChord && std::abs(score - bestScore) <= 0.01f) {
                    // Margin is razor thin; defer to primary harmonic function
                    if (tmpl.is_primary && !bestChord->is_primary) {
                        bestScore = score;
                        bestChord = &tmpl;
                    }
                }
            }

            // FIX: Prevent Name Erasure
            if (bestChord) {
                for (int row = 0; row < 12; ++row) {
                    chordProgression.matrix[row][i] = bestChord->profile[row];
                }
                chordProgression.chord_labels.push_back(bestChord->name);
                chordProgression.bass_root_bins.push_back(bestChord->root_bin); // Push the root!
            } else {
                chordProgression.chord_labels.push_back("N.C.");
                chordProgression.bass_root_bins.push_back(-1); // -1 signals the synth to rest
            }
        }
        
        std::cout << "    -> Matrix Populated successfully.\n";
    }

}
}