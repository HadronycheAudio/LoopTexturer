#pragma once

#include "Parameters.h"
#include "PluginConfig.h"
#include "dsp/Lfo.h"
#include "dsp/Saturator.h"
#include "dsp/Smoother.h"
#include "dsp/StateVariableFilter.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace LoopTexturer {

// The VST3 audio component, which runs the signal chain described in docs/signal-path.md.
class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (LOOPTEXTURER_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    double plain (ParamId id) const;
    void readParameterChanges (Steinberg::Vst::IParameterChanges* changes);
    void applyParameters ();
    void prepareDsp ();

    // Written from the host's threads and read on the audio thread.
    std::array<std::atomic<double>, kParamCount> _params;

    Smoother _gain;
    Smoother _volume;
    Smoother _pan;
    Smoother _panLfoAmount;
    Smoother _filterPitch; // log2 of the frequency, so that sweeps are even across the range.
    Smoother _filterLfoAmount;
    Smoother _filterQ;

    Lfo _panLfo { 0x9E3779B9u };
    Lfo _filterLfo { 0x85EBCA6Bu };
    StateVariableFilter _filter;
    Saturator _saturator;
    Saturator _postSaturator;
};

} // namespace LoopTexturer
