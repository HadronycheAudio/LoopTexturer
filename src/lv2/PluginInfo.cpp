// What the LV2 wrapper and its Turtle need to know about this plug-in. The URI and the symbols are saved in hosts'
// projects, so never change them, and a new parameter adds a symbol at the end.

#include "Parameters.h"

#include "hadronyche/lv2/PluginInfo.h"

namespace {

const char* const kSymbols[] = {
    "gain",
    "pan",
    "pan_lfo_rate",
    "pan_lfo_amount",
    "pan_lfo_wave",
    "filter_mode",
    "filter_frequency",
    "filter_resonance",
    "filter_lfo_rate",
    "filter_lfo_amount",
    "filter_lfo_wave",
    "saturator_on",
    "saturator_mode",
    "saturator_drive",
    "saturator_mix",
    "post_saturator_on",
    "post_saturator_mode",
    "post_saturator_drive",
    "post_saturator_mix",
    "volume",
    "looper_mode",
    "looper_length",
    "looper_bars",
    "looper_custom_bars",
    "looper_feedback",
    "looper_dry_wet",
};

static_assert (sizeof (kSymbols) / sizeof (kSymbols[0]) == LoopTexturer::kParamCount, "One symbol for each parameter");

} // namespace

const Hadronyche::Lv2::PluginInfo& Hadronyche::Lv2::getPluginInfo ()
{
    static const PluginInfo info {
        "https://github.com/HadronycheAudio/LoopTexturer",
        "LoopTexturer",
        "DelayPlugin",
        LoopTexturer::getParamSpec,
        LoopTexturer::kParamCount,
        kSymbols,
        2, // LV2 treats an odd minor version as a development build.
        0,
        true, // Audio input
        false, // MIDI
        true, // Transport
        true, // Latency
        true, // Message ports, for the looper's buttons, status and waveform. See src/lv2/LooperLink.cpp.
    };
    return info;
}
