# HUM

A high-performance C++ audio processing pipeline that translates raw vocal melodies (humming) into intelligent, polyphonic MIDI accompaniment, and synthesizes a fully mixed backing track.

## Overview

HUM Engine is designed to take a raw, unquantized audio recording of a human voice, extract the pitch and timing data, generate mathematically and musically sound chord progressions, and render a complete backing track using SoundFonts. 

It is built from the ground up in modern C++17 with an emphasis on modular architecture, digital signal processing (DSP), and offline rendering.

### The 6-Stage Pipeline
1. **Audio Ingestor:** Uses `miniaudio` to decode WAV files, apply a 100Hz High-Pass Filter (to remove AC hum), apply a Loudness Maximizer with hyperbolic tangent soft-clipping, and enforce a soft noise gate.
2. **Pitch Extractor:** Leverages the **Microsoft ONNX Runtime** to run a quantized CREPE neural network model, extracting highly accurate pitch contours from the isolated vocal track.
3. **Musical Analyzer:** Processes the raw pitch data to determine the tempo map, downbeats, key signature, and derives a functional chord progression (Chromagram Matrix).
4. **Accompaniment Generator:** Acts as a MIDI translation stub. It parses string-based chord labels (e.g., "C Maj", "F# Min", "N.C.") into stacked polyphonic MIDI notes, applying humanized articulation gaps for natural sounding synthesis.
5. **Lead Synthesizer:** An offline **FluidSynth** engine that maps the MIDI arrays to General MIDI (GM1) instruments using custom `.sf2` SoundFonts.
6. **Mixer & Exporter:** An internal DSP mixer that balances the lead melody against the backing chords, applies spatial studio reverb, prevents clipping, and exports the final mixed `.wav` file back to the disk.

## System Requirements

* **Compiler:** MSVC (Visual Studio 2022 x64) or any C++17 compatible compiler.
* **Build System:** CMake 3.15+
* **Package Manager:** vcpkg (for FluidSynth)
* **Architecture:** 64-bit (x64) required.

## External Dependencies & Asset Setup

To keep the repository lightweight, heavy neural network models, compiled binaries, and SoundFonts are **not** included in the source control. You must download them locally before building.

### 1. ONNX Runtime
HUM Engine requires the Microsoft ONNX Runtime C++ API to run the pitch extraction model.
1. Download the pre-compiled Windows x64 release from the [ONNX Runtime GitHub](https://github.com/microsoft/onnxruntime/releases) (v1.16.3 or newer).
2. Extract the `include/` and `lib/` folders.
3. Place them in your local directory exactly like this:
```text
   HUM/engine/external/onnxruntime/
   ├── include/
   └── lib/
       ├── onnxruntime.lib
       └── onnxruntime.dll
