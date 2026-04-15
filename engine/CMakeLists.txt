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
    src/AccompanimentGenerator.cpp
    src/LeadSynthesizer.cpp
    src/MixerAndExporter.cpp
)

# 5. Create the main executable
add_executable(HUM_Engine ${SOURCE_FILES})

# ---------------------------------------------------------
# EXTERNAL LIBRARIES (Placeholders for our next steps)
# ---------------------------------------------------------
# As we build out the modules, we will use CMake's "find_package" 

# --- ONNX RUNTIME SETUP ---
# Point to the external folder we just created
set(ONNXRUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime)
# Tell the compiler where the ONNX headers are
include_directories(${ONNXRUNTIME_DIR}/include)
# Find the exact ONNX library file based on the compiler (MSVC vs GCC)
if(MSVC)
    set(ONNXRUNTIME_LIB ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib)
else()
    # If you are using MinGW/GCC on Windows
    set(ONNXRUNTIME_LIB ${ONNXRUNTIME_DIR}/lib/onnxruntime.dll.a)
endif()
# -------------------------------

# --- Find the FluidSynth Package ---
# vcpkg provides standard CMake config for fluidsynth
find_package(FluidSynth CONFIG REQUIRED)

# - LAME/FFmpeg (for Stages 6)

# --- LINKING AND DLL COPYING ---
# Link the ONNX Runtime library to our engine
target_link_libraries(HUM_Engine PRIVATE 
    ${ONNXRUNTIME_LIB} 
    FluidSynth::libfluidsynth
)
# The Windows "DLL Hell" Fix
# This automatically copies onnxruntime.dll right next to HUM_Engine.exe every time you build.
add_custom_command(TARGET HUM_Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    $<TARGET_FILE_DIR:HUM_Engine>
)