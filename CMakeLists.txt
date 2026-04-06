# 1. Define the minimum CMake version and Project Name
cmake_minimum_required(VERSION 3.15)
project(HUM VERSION 1.0 LANGUAGES CXX)

# 2. Enforce modern C++ standards (C++17 is great for our structs and standard libraries)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 3. Define where our header files (.h) live
# This tells the compiler where to look when we type #include "DataTypes.h"
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 4. Gather all our source files (.cpp)
# Right now we are explicitly listing the files we mapped out for the MVP
set(SOURCE_FILES
    src/main.cpp
    src/AudioIngestor.cpp
    src/PitchExtractor.cpp
    src/MusicalAnalyzer.cpp
    src/LeadSynthesizer.cpp
    src/MixerAndExporter.cpp
)

# 5. Create the main executable
add_executable(HUM_Engine ${SOURCE_FILES})

# ---------------------------------------------------------
# EXTERNAL LIBRARIES (Placeholders for our next steps)
# ---------------------------------------------------------
# As we build out the modules, we will use CMake's "find_package" 
# or "add_subdirectory" here to pull in:
# - ONNX Runtime (for Stage 2)
# - FluidSynth (for Stage 5)
# - RNNoise & LAME/FFmpeg (for Stages 1 & 6)

# Example of how we will link them later:
# target_link_libraries(AIBand_Engine PRIVATE ONNXRuntime::ONNXRuntime FluidSynth::FluidSynth)