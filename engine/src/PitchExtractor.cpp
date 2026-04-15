#include "PitchExtractor.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <array>
#include <cstring> // Required for std::memcpy

#include <onnxruntime_cxx_api.h>

namespace {

    using namespace HUM;

    // --- ENGINE CONSTANTS ---
    const char* INPUT_TENSOR_NAME = "frames";   
    const char* OUTPUT_TENSOR_NAME = "probabilities"; 

    constexpr int SAMPLE_RATE = 16000;         
    constexpr int CREPE_WINDOW_SIZE = 1024;
    constexpr int FRAME_STEP_SAMPLES = 160;    
    constexpr float FRAME_STEP_SEC = 0.01f;

    // --- DSP PRE-COMPUTATION HELPER (Running Sum O(N) Refactor) ---
    struct DspMetrics {
        std::vector<float> rms;
        std::vector<float> flux; 
        std::vector<float> zcr;  
    };

    DspMetrics calculateDspMetrics(const std::vector<float>& audio) {
        size_t numFrames = 1 + (audio.size() - CREPE_WINDOW_SIZE) / FRAME_STEP_SAMPLES;
        DspMetrics metrics;
        metrics.rms.resize(numFrames, 0.0f);
        metrics.flux.resize(numFrames, 0.0f);
        metrics.zcr.resize(numFrames, 0.0f);

        // 1. Initialize the first window (Frame 0)
        float sumSquares = 0.0f;
        int zeroCrossings = 0;
        
        for (size_t j = 0; j < CREPE_WINDOW_SIZE; ++j) {
            float sample = audio[j];
            sumSquares += sample * sample;
            if (j > 0) {
                float prevSample = audio[j - 1];
                if ((sample > 0.0f && prevSample <= 0.0f) || (sample < 0.0f && prevSample >= 0.0f)) {
                    zeroCrossings++;
                }
            }
        }

        metrics.rms[0] = std::sqrt(std::max(0.0f, sumSquares) / CREPE_WINDOW_SIZE);
        metrics.flux[0] = 0.0f;
        metrics.zcr[0] = static_cast<float>(zeroCrossings) / CREPE_WINDOW_SIZE;
        
        float prevRms = metrics.rms[0];

        // 2. Sliding Window (Running Sum O(N)) for remaining frames
        for (size_t i = 1; i < numFrames; ++i) {
            size_t startIdx = i * FRAME_STEP_SAMPLES;

            // --- RMS SLIDING WINDOW ---
            for (size_t j = 0; j < FRAME_STEP_SAMPLES; ++j) {
                float old_sample = audio[startIdx - FRAME_STEP_SAMPLES + j];
                sumSquares -= old_sample * old_sample;
            }
            for (size_t j = 0; j < FRAME_STEP_SAMPLES; ++j) {
                float new_sample = audio[startIdx + CREPE_WINDOW_SIZE - FRAME_STEP_SAMPLES + j];
                sumSquares += new_sample * new_sample;
            }

            metrics.rms[i] = std::sqrt(std::max(0.0f, sumSquares) / CREPE_WINDOW_SIZE);
            float delta = metrics.rms[i] - prevRms;
            metrics.flux[i] = (delta > 0.0f) ? delta : 0.0f; 
            prevRms = metrics.rms[i];

            // --- ZCR SLIDING WINDOW ---
            // Drop old crossings that slid out the back
            for (size_t j = 1; j <= FRAME_STEP_SAMPLES; ++j) {
                size_t k = startIdx - FRAME_STEP_SAMPLES + j;
                float sample = audio[k];
                float pSample = audio[k - 1];
                if ((sample > 0.0f && pSample <= 0.0f) || (sample < 0.0f && pSample >= 0.0f)) zeroCrossings--;
            }
            // Add new crossings that slid in the front
            for (size_t j = 0; j < FRAME_STEP_SAMPLES; ++j) {
                size_t k = startIdx + CREPE_WINDOW_SIZE - FRAME_STEP_SAMPLES + j;
                float sample = audio[k];
                float pSample = audio[k - 1];
                if ((sample > 0.0f && pSample <= 0.0f) || (sample < 0.0f && pSample >= 0.0f)) zeroCrossings++;
            }
            
            metrics.zcr[i] = static_cast<float>(zeroCrossings) / CREPE_WINDOW_SIZE;
        }
        
        return metrics;
    }

    // --- STAGE 1: CREPE INFERENCE (Batched & Safely Managed) ---
    std::vector<std::vector<float>> runCrepeInference(const std::vector<float>& audio16k, const std::wstring& modelPath) {
        try {
            // FIX: Env must be static to prevent massive thread creation overhead on every call
            static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "HUM_CrepeExtractor");
            
            Ort::SessionOptions sessionOptions;
            sessionOptions.SetIntraOpNumThreads(1); 
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            Ort::Session session(env, modelPath.c_str(), sessionOptions);
            
            if (audio16k.size() < CREPE_WINDOW_SIZE) {
                throw std::runtime_error("Audio too short for a single CREPE frame.");
            }

            size_t numFrames = 1 + (audio16k.size() - CREPE_WINDOW_SIZE) / FRAME_STEP_SAMPLES;
            
            // Memory Optimization: Raw memcpy blast avoids vector reallocation overhead
            std::vector<float> batchedInput(numFrames * CREPE_WINDOW_SIZE, 0.0f);
            
            for (size_t i = 0; i < numFrames; ++i) {
                size_t srcOffset = i * FRAME_STEP_SAMPLES;
                size_t destOffset = i * CREPE_WINDOW_SIZE;
                std::memcpy(&batchedInput[destOffset], 
                            &audio16k[srcOffset], 
                            CREPE_WINDOW_SIZE * sizeof(float));
            }

            std::vector<int64_t> inputShape = {static_cast<int64_t>(numFrames), CREPE_WINDOW_SIZE};
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo, batchedInput.data(), batchedInput.size(), inputShape.data(), inputShape.size()
            );

            const char* inputNames[] = {INPUT_TENSOR_NAME};
            const char* outputNames[] = {OUTPUT_TENSOR_NAME};

            auto outputTensors = session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
            float* probData = outputTensors[0].GetTensorMutableData<float>();
            
            std::vector<std::vector<float>> probabilityMatrix(numFrames, std::vector<float>(360, 0.0f));
            for (size_t i = 0; i < numFrames; ++i) {
                probabilityMatrix[i].assign(probData + (i * 360), probData + ((i + 1) * 360));
            }

            return probabilityMatrix;

        } catch (const Ort::Exception& e) {
            // FIX: Safely catch neural network crashes and pass them up the chain
            throw std::runtime_error(std::string("ONNX Runtime Error: ") + e.what());
        }
    }

    // --- STAGE 2: MATRIX DECODING & SMOOTHING (Linear Math Refactor) ---
    std::vector<float> decodeCrepeToMidi(const std::vector<std::vector<float>>& probabilityMatrix) {
        std::vector<float> rawContour(probabilityMatrix.size(), 0.0f);

        for (size_t i = 0; i < probabilityMatrix.size(); ++i) {
            int bestBin = 0;
            float maxConfidence = 0.0f;
            
            for (int j = 0; j < 360; ++j) {
                if (probabilityMatrix[i][j] > maxConfidence) {
                    maxConfidence = probabilityMatrix[i][j];
                    bestBin = j;
                }
            }

            // FIX: Logarithmic double-tax mathematically reduced to a single linear equation
            if (maxConfidence >= 0.40f) {
                rawContour[i] = 23.4606f + (0.2f * bestBin); 
            }
        }

        std::vector<float> smoothContour = rawContour;
        for (size_t i = 2; i < rawContour.size() - 2; ++i) {
            if (rawContour[i] > 0.0f) {
                std::array<float, 5> window = {rawContour[i-2], rawContour[i-1], rawContour[i], rawContour[i+1], rawContour[i+2]};
                auto medianIt = window.begin() + 2;
                std::nth_element(window.begin(), medianIt, window.end());
                smoothContour[i] = *medianIt; 
            }
        }

        return smoothContour;
    }

    // --- STAGE 3: CONFIGURABLE STATE MACHINE ---
    void segmentNotes(const std::vector<float>& pitchContour, 
                      const DspMetrics& metrics, 
                      const ExtractorConfig& config,
                      std::vector<MidiEvent>& outMelody) {
        
        enum State { SILENCE, NOTE_ON };
        State currentState = SILENCE;
        
        float currentNoteStart = 0.0f;
        std::vector<float> currentNotePitches;

        // --- BRUTE FORCE THRESHOLDS ---
        constexpr float RMS_GATE_ON = 0.015f;  
        constexpr float RMS_GATE_OFF = 0.005f; 
        constexpr float ZCR_THRESHOLD = 0.05f;         
        constexpr float FLUX_THRESHOLD = 0.012f;      
        constexpr float PITCH_DELTA_TOLERANCE = 1.5f; 
        
        constexpr float MIN_HUMAN_PITCH = 40.0f;       
        constexpr float MAX_HUMAN_PITCH = 80.0f;       

        for (size_t i = 0; i < pitchContour.size(); ++i) {
            float pitch = pitchContour[i];
            float rms = metrics.rms[i];
            float time = i * FRAME_STEP_SEC;

            if (config.enable_zcr && metrics.zcr[i] > ZCR_THRESHOLD) {
                pitch = 0.0f; 
            }
            
            if (pitch > 0.0f && (pitch < MIN_HUMAN_PITCH || pitch > MAX_HUMAN_PITCH)) {
                pitch = 0.0f;
            }

            if (currentState == SILENCE) {
                if (rms > RMS_GATE_ON && pitch > 20.0f) {
                    currentState = NOTE_ON;
                    currentNoteStart = time;
                    currentNotePitches.clear();
                    currentNotePitches.push_back(pitch);
                }
            } 
            else if (currentState == NOTE_ON) {
                float avgCurrentPitch = std::accumulate(currentNotePitches.begin(), currentNotePitches.end(), 0.0f) / currentNotePitches.size();
                
                bool volumeDropped = (rms < RMS_GATE_OFF || pitch < 20.0f);
                bool pitchJumped = (config.enable_pitch_delta && std::abs(pitch - avgCurrentPitch) > PITCH_DELTA_TOLERANCE);
                bool fluxSpiked = (config.enable_flux && metrics.flux[i] > FLUX_THRESHOLD);

                if (volumeDropped || pitchJumped || fluxSpiked) {
                    float duration = time - currentNoteStart;
                    
                    if (duration > 0.05f && !currentNotePitches.empty()) {
                        
                        size_t trimSize = (currentNotePitches.size() > 6) ? 2 : 0;
                        auto startIt = currentNotePitches.begin() + trimSize;
                        auto endIt = currentNotePitches.end() - trimSize;

                        auto medianIt = startIt + std::distance(startIt, endIt) / 2;
                        std::nth_element(startIt, medianIt, endIt);
                        float medianFinalPitch = *medianIt;

                        MidiEvent note;
                        note.start_time = currentNoteStart;
                        note.duration = duration;
                        note.pitch = std::round(medianFinalPitch);
                        outMelody.push_back(note);
                    }

                    if (volumeDropped) {
                        currentState = SILENCE;
                    } else {
                        currentNoteStart = time;
                        currentNotePitches.clear();
                        currentNotePitches.push_back(pitch);
                    }
                } else {
                    currentNotePitches.push_back(pitch);
                }
            }
        }
    }

    // --- STAGE 4: MIDI POST-PROCESSING SIEVE (Infinite Breath Fix) ---
    void runPostProcessingSieve(std::vector<MidiEvent>& melody) {
        if (melody.size() < 3) return; 

        bool deletedAnyArtifacts;
        
        do {
            deletedAnyArtifacts = false;
            std::vector<MidiEvent> cleaned;
            cleaned.push_back(melody[0]); 

            for (size_t i = 1; i < melody.size() - 1; ++i) {
                MidiEvent& prevNote = cleaned.back();
                MidiEvent currNote = melody[i];
                MidiEvent nextNote = melody[i + 1];

                constexpr float SUSPICIOUS_LENGTH = 0.25f;  
                constexpr float MICRO_NOTE_LENGTH = 0.15f;  
                constexpr float DISTANCE_TOLERANCE = 1.0f;  

                bool isShort = currNote.duration < SUSPICIOUS_LENGTH; 
                
                float distPrev = std::abs(currNote.pitch - prevNote.pitch);
                float distNext = std::abs(currNote.pitch - nextNote.pitch);

                if (isShort) {
                    float avgNeighborPitch = (prevNote.pitch + nextNote.pitch) / 2.0f;
                    float distToAvg = currNote.pitch - avgNeighborPitch;
                    
                    if (std::abs(distToAvg) >= 10.0f && std::abs(distToAvg) <= 14.0f) {
                        currNote.pitch += (distToAvg > 0) ? -12.0f : 12.0f;
                        distPrev = std::abs(currNote.pitch - prevNote.pitch); 
                        distNext = std::abs(currNote.pitch - nextNote.pitch);
                    }
                }

                bool isOutlier = false;

                if (currNote.duration < MICRO_NOTE_LENGTH) {
                    isOutlier = true; 
                } else if (isShort) {
                    if (distPrev > DISTANCE_TOLERANCE && distNext > DISTANCE_TOLERANCE) {
                        isOutlier = true;
                    }
                }

                if (isOutlier) {
                    // FIX: Maximum Stretch Distance prevents stretching across rests/breaths
                    float gapToPrev = currNote.start_time - (prevNote.start_time + prevNote.duration);
                    if (gapToPrev < 0.10f) { // 100ms threshold
                        float newEndTime = currNote.start_time + currNote.duration;
                        prevNote.duration = newEndTime - prevNote.start_time;
                    }
                    deletedAnyArtifacts = true; 
                } else {
                    cleaned.push_back(currNote);
                }
            }
            
            cleaned.push_back(melody.back());
            melody = cleaned;

        } while (deletedAnyArtifacts && melody.size() >= 3);
    }

    // --- STAGE 6A: SMART BPM DETECTION ---
    float calculateSmartBPM(const std::vector<MidiEvent>& melody) {
        if (melody.size() < 4) return 120.0f; 

        std::vector<float> intervals;
        for (size_t i = 1; i < melody.size(); ++i) {
            float gap = melody[i].start_time - melody[i-1].start_time;
            if (gap > 0.15f && gap < 1.5f) {
                intervals.push_back(gap);
            }
        }

        if (intervals.empty()) return 120.0f;

        std::sort(intervals.begin(), intervals.end());
        float medianPulse = intervals[intervals.size() / 2];

        float guessedBPM = 60.0f / (medianPulse * 2.0f); 
        
        while (guessedBPM < 60.0f) guessedBPM *= 2.0f;
        while (guessedBPM > 180.0f) guessedBPM /= 2.0f;

        return guessedBPM;
    }

    // --- STAGE 6B: PERCENTAGE QUANTIZATION (Iterator Refactor) ---
    void quantizeMelody(std::vector<MidiEvent>& melody, float bpm, int gridResolution, float strength) {
        if (melody.empty() || strength <= 0.0f) return;

        float quarterNoteDuration = 60.0f / bpm;
        float gridStep = quarterNoteDuration * (4.0f / static_cast<float>(gridResolution));

        for (auto& note : melody) {
            float nearestStartLine = std::round(note.start_time / gridStep) * gridStep;
            float startOffset = (nearestStartLine - note.start_time) * strength;
            note.start_time += startOffset;

            float originalEndTime = note.start_time + note.duration;
            float nearestEndLine = std::round(originalEndTime / gridStep) * gridStep;
            
            float newDuration = std::max(nearestEndLine - note.start_time, gridStep);
            note.duration = newDuration;
        }

        // FIX: Replaced index access with standard iterator range-loop
        for (auto it = melody.begin(); it != melody.end() - 1; ++it) {
            auto& currNote = *it;
            auto& nextNote = *(it + 1);

            if (currNote.start_time >= nextNote.start_time) {
                currNote.start_time = std::max(0.0f, nextNote.start_time - 0.05f); 
            }

            float noteEnd = currNote.start_time + currNote.duration;
            if (noteEnd > nextNote.start_time) {
                float availableSpace = nextNote.start_time - currNote.start_time;
                currNote.duration = std::max(0.02f, availableSpace); 
            }
        }
    }

} // end anonymous namespace

namespace HUM {
namespace PitchExtractor {
    
    void extract(const AudioBuffer& cleanAudio, 
                 const ExtractorConfig& config, 
                 std::vector<MidiEvent>& outMelody, 
                 TempoMap& outBeatGrid, 
                 VocalPitchContour& outContour) {
                     
        if (cleanAudio.samples.empty()) throw std::runtime_error("Empty audio buffer.");

        std::cout << "    -> Stage 1: Running CREPE Acoustic Model (Safely Batched)...\n";
        auto probabilities = runCrepeInference(cleanAudio.samples, config.model_path);

        std::cout << "    -> Stage 2: Decoding Matrix to Continuous MIDI...\n";
        std::vector<float> smoothContour = decodeCrepeToMidi(probabilities);
        outContour.cqt_top4_bins = {smoothContour}; 

        std::cout << "    -> Stage 3: Running Pre-Computation DSP Physics...\n";
        DspMetrics metrics = calculateDspMetrics(cleanAudio.samples);
        outContour.loudness_rms = metrics.rms;

        std::cout << "    -> Stage 4: Configurable State Machine Segmentation...\n";
        std::cout << "       [ZCR: " << (config.enable_zcr ? "ON" : "OFF") 
                  << " | FLUX: " << (config.enable_flux ? "ON" : "OFF") 
                  << " | DELTA: " << (config.enable_pitch_delta ? "ON" : "OFF") << "]\n";
        segmentNotes(smoothContour, metrics, config, outMelody);

        std::cout << "    -> Stage 5: Running Sieve (Artifact Cleaning & Healing)...\n";
        runPostProcessingSieve(outMelody);

        // ===============================================
        // --- STAGE 6: TEMPO & QUANTIZATION ---
        std::cout << "    -> Stage 6: Calculating BPM and Quantizing Grid...\n";
        
        // 1. Calculate the BPM (Restored Code)
        float finalBPM = (config.target_bpm > 0.0f) ? config.target_bpm : calculateSmartBPM(outMelody);
        int gridRes = (config.grid_resolution > 0) ? config.grid_resolution : 16;
        float quantStrength = std::max(0.0f, std::min(1.0f, config.quantize_strength));

        std::cout << "       [BPM: " << finalBPM << " | Grid: 1/" << gridRes 
                  << " | Snap: " << (quantStrength * 100.0f) << "%]\n";
                  
        // 2. Quantize the notes to the grid (Restored Code)
        quantizeMelody(outMelody, finalBPM, gridRes, quantStrength);
        
        outBeatGrid.bpm = finalBPM; 
        
        // 3. Generate the Downbeat Grid (Audio-Duration Bound)
        outBeatGrid.downbeat_timestamps.clear();
        
        float quarterNoteSec = 60.0f / finalBPM;
        float measureSec = quarterNoteSec * 4.0f; 
        
        // Grab the absolute length of the audio file in seconds
        float totalAudioSeconds = static_cast<float>(cleanAudio.samples.size()) / SAMPLE_RATE;
        
        // Stamp a downbeat every 'measureSec' covering the ENTIRE file length
        for (float t = 0.0f; t <= totalAudioSeconds; t += measureSec) {
            outBeatGrid.downbeat_timestamps.push_back(t);
        }
        
        // Ensure there is a closing boundary for the final slice
        if (!outBeatGrid.downbeat_timestamps.empty() && outBeatGrid.downbeat_timestamps.back() < totalAudioSeconds) {
            outBeatGrid.downbeat_timestamps.push_back(outBeatGrid.downbeat_timestamps.back() + measureSec);
        }
        // ===============================================
    }

}
}