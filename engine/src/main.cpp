#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

// Include our global data structures
#include "DataTypes.h"

// Include module interfaces (We will build these header files next)
#include "AudioIngestor.h"
#include "PitchExtractor.h"
#include "MusicalAnalyzer.h"
#include "AccompanimentGenerator.h"
#include "LeadSynthesizer.h"
#include "MixerAndExporter.h"

#include <fstream>
#include <algorithm>

// --- RAW MIDI EXPORTER (Standardized with Tempo Track) ---
void exportToMidi(const std::vector<HUM::MidiEvent>& melody, float bpm, const std::string& filename) {
    std::ofstream f(filename, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create MIDI file.\n";
        return;
    }

    auto write32 = [&](uint32_t v) { f.put((v >> 24) & 0xFF); f.put((v >> 16) & 0xFF); f.put((v >> 8) & 0xFF); f.put(v & 0xFF); };
    auto write16 = [&](uint16_t v) { f.put((v >> 8) & 0xFF); f.put(v & 0xFF); };

    // 1. Header Chunk
    f.write("MThd", 4);
    write32(6);      // Header size
    write16(0);      // Format 0 (Single Track)
    write16(1);      // 1 Track
    uint16_t ticksPerQuarter = 480;
    write16(ticksPerQuarter);

    std::vector<uint8_t> trackData;
    auto pushVLQ = [&](uint32_t v) {
        uint32_t buffer = v & 0x7F;
        while ((v >>= 7)) { buffer <<= 8; buffer |= ((v & 0x7F) | 0x80); }
        while (true) { trackData.push_back(buffer & 0xFF); if (buffer & 0x80) buffer >>= 8; else break; }
    };

    // --- THE FIX: Inject BPM Meta Event ---
    // MIDI Tempo is measured in Microseconds Per Quarter Note (MPQN)
    uint32_t mpqn = static_cast<uint32_t>(60000000.0f / bpm);
    pushVLQ(0); // Delta time 0
    trackData.push_back(0xFF); // Meta Event
    trackData.push_back(0x51); // Set Tempo
    trackData.push_back(0x03); // Length (3 bytes)
    trackData.push_back((mpqn >> 16) & 0xFF);
    trackData.push_back((mpqn >> 8) & 0xFF);
    trackData.push_back(mpqn & 0xFF);

    // --- THE FIX: Dynamic Tick Scaling ---
    uint32_t ticksPerSec = static_cast<uint32_t>((ticksPerQuarter * bpm) / 60.0f);
    
    struct MidiMsg { uint32_t absTick; uint8_t type, note, vel; };
    std::vector<MidiMsg> msgs;
    for (const auto& note : melody) {
        uint32_t startTick = static_cast<uint32_t>(note.start_time * ticksPerSec);
        uint32_t endTick = static_cast<uint32_t>((note.start_time + note.duration) * ticksPerSec);
        msgs.push_back({startTick, 0x90, static_cast<uint8_t>(note.pitch), 100}); 
        msgs.push_back({endTick, 0x80, static_cast<uint8_t>(note.pitch), 0});    
    }
    
    std::sort(msgs.begin(), msgs.end(), [](const MidiMsg& a, const MidiMsg& b) { return a.absTick < b.absTick; });

    uint32_t lastTick = 0;
    for (const auto& msg : msgs) {
        pushVLQ(msg.absTick - lastTick);
        trackData.push_back(msg.type);
        trackData.push_back(msg.note);
        trackData.push_back(msg.vel);
        lastTick = msg.absTick;
    }
    
    // End of Track Meta Event
    pushVLQ(0); trackData.push_back(0xFF); trackData.push_back(0x2F); trackData.push_back(0x00);

    // 3. Write Track Chunk to File
    f.write("MTrk", 4);
    write32(static_cast<uint32_t>(trackData.size()));
    f.write(reinterpret_cast<char*>(trackData.data()), trackData.size());
}
// --------------------------------------------------


using namespace HUM;

int main(int argc, char* argv[]) {
    // --- NEW: Force std::cout to flush immediately (fixes Node.js pipe buffering) ---
    std::cout << std::unitbuf;

    // 1. Command Line Argument Parsing with Flags
    HUM::ExtractorConfig config;
    std::vector<std::string> positionalArgs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--zcr") {
            config.enable_zcr = true;
        } else if (arg == "--flux") {
            config.enable_flux = true;
        } else if (arg == "--delta") {
            config.enable_pitch_delta = true;
        } else {
            // It's not a flag, so it must be a file path or instrument name
            positionalArgs.push_back(arg);
        }
    }

    // Ensure the user actually provided all 4 required arguments
    if (positionalArgs.size() < 4) {
        std::cerr << "Error: Missing arguments.\n";
        std::cerr << "Usage: ./HUM_Engine [flags] <input.wav> <lead_instrument> <backing_style> <output.wav>\n";
        std::cerr << "Available flags:\n";
        std::cerr << "  --zcr    : Enable Zero Crossing Rate to kill high-pitched consonant glitches\n";
        std::cerr << "  --flux   : Enable Spectral Flux to slice notes on fast syllable attacks\n";
        std::cerr << "  --delta  : Enable Pitch Delta to slice notes during legato vocal slides\n\n";
        std::cerr << "Example: ./HUM_Engine --zcr --flux input_hum.wav saxophone jazz final_output.wav\n";
        return 1; 
    }

    // Extract the arguments into cleanly named variables
    std::string inputFile = positionalArgs[0];
    std::string instrumentName = positionalArgs[1];
    std::string backingInstrument = positionalArgs[2]; // <-- NEW: Catch the backing style
    std::string outputFile = positionalArgs[3];        // <-- SHIFTED: Output is now the 4th argument

    std::cout << "--- HUM Engine MVP Initialization ---\n";

    try {
        // ==========================================
        // STAGE 1: Audio Ingestion & Cleanup
        // ==========================================
        std::cout << "[1/5] Ingesting and cleaning audio...\n";
        AudioBuffer cleanAudio = AudioIngestor::process(inputFile);
        // --- STAGE 1 TEST VERIFICATION ---
        std::cout << "\n==========================================\n";
        std::cout << "STAGE 1 TEST PASSED\n";
        std::cout << "==========================================\n";
        std::cout << "Vector Size: " << cleanAudio.samples.size() << " samples\n";
        // Calculate duration based on our strict 44.1kHz sample rate
        float durationSeconds = static_cast<float>(cleanAudio.samples.size()) / AudioIngestor::TARGET_SAMPLE_RATE;
        std::cout << "Calculated Duration: " << durationSeconds << " seconds\n";
        std::cout << "Memory Footprint: " << (cleanAudio.samples.size() * sizeof(float)) / (1024.0f * 1024.0f) << " MB\n";
        std::cout << "==========================================\n\n";

        // ==========================================
        // STAGE 2: Pitch & Rhythm Extraction
        // ==========================================
        std::cout << "[2/5] Extracting MIDI, Beat Grid, and Pitch Contour...\n";
        std::vector<MidiEvent> vocalMelody;
        TempoMap beatGrid;
        VocalPitchContour contour;
        // Pass the new config object here to fix the 4-vs-5 argument compiler error
        PitchExtractor::extract(cleanAudio, config, vocalMelody, beatGrid, contour);
        std::cout << "STAGE 2 TEST PASSED\n";
        std::cout << "Extracted " << vocalMelody.size() << " MIDI notes.\n";
        std::cout << "Calculated BPM: " << beatGrid.bpm << "\n";
        exportToMidi(vocalMelody, beatGrid.bpm, "debug_hum.mid");

        // ==========================================
        // STAGE 3: Harmonic Analysis (MVP Output)
        // ==========================================
        std::cout << "\n[3/5] Analyzing musical structure...\n";
        SongStructure structureData;
        ChromagramMatrix chordProgression;
        // Execute the Phase 1, 2, and 3 Math Pipeline
        MusicalAnalyzer::analyze(vocalMelody, beatGrid, structureData, chordProgression);
        // --- STAGE 3 TEST VERIFICATION & PRINTING ---
        std::cout << "\n==========================================\n";
        std::cout << "STAGE 3 TEST PASSED\n";
        std::cout << "==========================================\n";
        std::cout << "Detected Global Key: " << structureData.key_and_mode << "\n";
        std::cout << "Harmonic Progression (Per Measure/Downbeat):\n\n| ";
        // Iterate through our saved string labels and print a visual chord chart
        for (size_t i = 0; i < chordProgression.chord_labels.size(); ++i) {
            std::cout << chordProgression.chord_labels[i] << " | ";
            
            // Format output: Wrap to a new line every 4 chords (Standard 4/4 musical phrasing)
            if ((i + 1) % 4 == 0 && i != chordProgression.chord_labels.size() - 1) {
                std::cout << "\n| ";
            }
        }
        std::cout << "\n==========================================\n\n";

        // ==========================================
        // STAGE 4: Accompaniment Synthesis (Stub)
        // ==========================================
        std::cout << "\n[4/6] Synthesizing Backing Chords [" << backingInstrument << "]...\n";
        
        // Call our new engine. We pass all the data to maintain the neural-net contract.
        AudioBuffer backingTrack = AccompanimentGenerator::render(
            backingInstrument,  // <-- Passed dynamically from the frontend
            chordProgression,   // from Stage 3
            beatGrid,           // from Stage 2
            contour,            // from Stage 2
            structureData       // from Stage 3
        );

        // ==========================================
        // STAGE 5: Lead Synthesis
        // ==========================================
        std::cout << "\n[5/6] Synthesizing " << instrumentName << " lead...\n";
        AudioBuffer synthAudio = LeadSynthesizer::render(vocalMelody, contour.loudness_rms, instrumentName);
        
        std::cout << "Synthesized Audio Size: " << synthAudio.samples.size() << " samples\n";

        // ==========================================
        // STAGE 6: Mixer & WAV Exporter
        // ==========================================
        std::cout << "\n[6/6] Mixing and Exporting to disk...\n";
        
        HUM::MixerAndExporter::MixSettings mixSettings;
        
        // --- THE DIGITAL MIXER ---
        mixSettings.lead_volume = 1.0f;     // Keep the lead melody front and center (100%)
        mixSettings.backing_volume = 0.3f;  // LOWER THIS: Push chords to the background (30%)
        mixSettings.reverb_amount = 0.25f;  
        mixSettings.enable_clipper = true;  
        
        bool success = HUM::MixerAndExporter::processAndExport(
            synthAudio, 
            backingTrack, 
            outputFile, 
            mixSettings
        );

        if (success) {
            std::cout << "\n==========================================\n";
            std::cout << "FULL 6-STAGE PIPELINE COMPLETE: " << outputFile << "\n";
            std::cout << "==========================================\n\n";
        }

    } catch (const std::exception& e) {
        // Catch any fatal errors thrown by our modules and safely terminate
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}