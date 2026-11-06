cmake_minimum_required(VERSION 3.25)

# The oldest macOS it runs on. This has to come before project (), or it defaults to the build machine's own.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "The oldest macOS supported")

project(LoopTexturer VERSION 1.1.2 LANGUAGES CXX)

# Can point at another checkout, to work on the library and a plug-in together.
set(HADRONYCHE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/hadronychecore" CACHE PATH "The HadronycheCore library")

if(NOT EXISTS "${HADRONYCHE_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "HadronycheCore submodule is missing. Run: git submodule update --init --recursive")
endif()

include("${HADRONYCHE_CORE_DIR}/cmake/HadronychePlugin.cmake")

hadronyche_use_vst3_sdk()

# --- Plug-in ------------------------------------------------------------------
hadronyche_add_plugin(LoopTexturer AU_TYPE aufx AU_SUBTYPE LpTx SOURCES
    src/PluginConfig.h
    src/PluginEntry.cpp
    src/Messages.h
    src/LooperBridge.h
    src/LooperBridge.cpp
    src/LoopExport.h
    src/LoopExport.cpp
    src/Parameters.h
    src/Parameters.cpp
    src/Processor.h
    src/Processor.cpp
    src/Controller.h
    src/Controller.cpp
    src/Editor.h
    src/Editor.cpp
    src/dsp/FloatGain.h
    src/dsp/Lfo.h
    src/dsp/Looper.h
    src/dsp/Looper.cpp
    src/dsp/Oversampler.h
    src/dsp/Saturator.h
    src/dsp/StateVariableFilter.h
    src/dsp/Tape.h
)

# --- Tests --------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
