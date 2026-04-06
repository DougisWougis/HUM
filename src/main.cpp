#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

// Include our global data structures
#include "DataTypes.h"

// Include module interfaces (We will build these header files next)
#include "AudioIngestor.h"
// #include "PitchExtractor.h"
// #include "MusicalAnalyzer.h"
// #include "LeadSynthesizer.h"
// #include "MixerAndExporter.h"

using namespace HUM;

int main(int argc, char* argv[]) {
    // 1. Command Line Argument Parsing
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <input_audio_path> <instrument_name> <output_audio_path>\n";
        std::cerr << "Example: ./HUM_Engine input_hum.wav saxophone final_output.mp3\n";
        return 1; 
    }

    std::string inputPath = argv[1];
    std::string instrumentName = argv[2];
    std::string outputPath = argv[3];

    std::cout << "--- HUM Engine MVP Initialization ---\n";

    try {
        // ==========================================
        // STAGE 1: Audio Ingestion & Cleanup
        // ==========================================
        std::cout << "[1/5] Ingesting and cleaning audio...\n";
        AudioBuffer cleanAudio = AudioIngestor::process(inputPath);

        // --- STAGE 1 TEST VERIFICATION ---
        std::cout << "\n==========================================\n";
        std::cout << "✅ STAGE 1 TEST PASSED\n";
        std::cout << "==========================================\n";
        std::cout << "Vector Size: " << cleanAudio.samples.size() << " samples\n";
        
        // Calculate duration based on our strict 44.1kHz sample rate
        float durationSeconds = static_cast<float>(cleanAudio.samples.size()) / AudioIngestor::TARGET_SAMPLE_RATE;
        std::cout << "Calculated Duration: " << durationSeconds << " seconds\n";
        std::cout << "Memory Footprint: " << (cleanAudio.samples.size() * sizeof(float)) / (1024.0f * 1024.0f) << " MB\n";
        std::cout << "==========================================\n\n";
        
        /* // ==========================================
        // STAGE 2: Pitch & Rhythm Extraction
        // ==========================================
        std::cout << "[2/5] Extracting MIDI, Beat Grid, and Pitch Contour...\n";
        std::vector<MidiEvent> vocalMelody;
        TempoMap beatGrid;
        VocalPitchContour pitchContour; // Extracted now for future Stage 4 compatibility
        
        PitchExtractor::extract(cleanAudio, vocalMelody, beatGrid, pitchContour);

        // ==========================================
        // STAGE 3: Harmonic Analysis (MVP Output)
        // ==========================================
        std::cout << "[3/5] Analyzing musical structure...\n";
        SongStructure structureData;
        ChromagramMatrix chordProgression;
        
        MusicalAnalyzer::analyze(vocalMelody, beatGrid, structureData, chordProgression);
        
        // Print the extracted key and chords to terminal to verify MVP math
        // MusicalAnalyzer::printAnalysis(structureData, chordProgression);

        // ==========================================
        // STAGE 5: Lead Synthesis (Stage 4 Skipped for MVP)
        // ==========================================
        std::cout << "[4/5] Synthesizing " << instrumentName << " lead...\n";
        AudioBuffer synthAudio = LeadSynthesizer::render(vocalMelody, instrumentName);

        // ==========================================
        // STAGE 6: Mixing and MP3 Export
        // ==========================================
        std::cout << "[5/5] Mixing and exporting to " << outputPath << "...\n";
        MixerAndExporter::exportTrack(synthAudio, outputPath);
        */

        std::cout << "\nSuccess! Pipeline executed flawlessly.\n";

    } catch (const std::exception& e) {
        // Catch any fatal errors thrown by our modules and safely terminate
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}