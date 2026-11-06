cmake_minimum_required(VERSION 3.25)

project(LoopTexturer VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")

if(NOT EXISTS "${VST3_SDK_DIR}/CMakeLists.txt" OR NOT EXISTS "${VST3_SDK_DIR}/vstgui4/vstgui/CMakeLists.txt")
    message(FATAL_ERROR "SDK submodule is missing. Run: git submodule update --init --recursive")
endif()

# Can point at another checkout, to work on the library and a plug-in together.
set(HADRONYCHE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/hadronychecore" CACHE PATH "The HadronycheCore library")

if(NOT EXISTS "${HADRONYCHE_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "HadronycheCore submodule is missing. Run: git submodule update --init --recursive")
endif()

# --- VST3 SDK -----------------------------------------------------------------
# These are declared before the SDK so they stay overridable with -D on the command line.
option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Build the VST3 SDK example plug-ins" OFF)
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Build the VST3 SDK example hosts" OFF)
option(SMTG_ENABLE_VSTGUI_SUPPORT "Build VSTGUI, used for the editor UI" ON)
# Linking into the system VST3 folder needs admin rights or Windows Developer Mode.
option(SMTG_CREATE_PLUGIN_LINK "Symlink the built plug-in into the VST3 folder" OFF)
# Linking the C++ runtime statically means users don't need to install Microsoft's runtime separately.
option(SMTG_USE_STATIC_CRT "Link the C++ runtime statically" ON)

add_subdirectory("${VST3_SDK_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
smtg_enable_vst3_sdk()

add_subdirectory("${HADRONYCHE_CORE_DIR}" "${CMAKE_BINARY_DIR}/hadronychecore")

# --- Plug-in ------------------------------------------------------------------
smtg_add_vst3plugin(LoopTexturer
    PACKAGE_NAME "LoopTexturer"
    SOURCES_LIST
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

target_include_directories(LoopTexturer PRIVATE src)
target_compile_definitions(LoopTexturer PRIVATE
    LOOPTEXTURER_VERSION="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
target_link_libraries(LoopTexturer PRIVATE sdk vstgui_support hadronyche_dsp hadronyche_vst)

# --- Tests --------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
