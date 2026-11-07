#pragma once

#include "LooperBridge.h"
#include "Parameters.h"
#include "PluginConfig.h"
#include "dsp/Lfo.h"
#include "dsp/Looper.h"
#include "dsp/Saturator.h"
#include "dsp/StateVariableFilter.h"

#include "hadronyche/dsp/Smoother.h"
#include "hadronyche/vst/StereoProcessor.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace LoopTexturer {

// The VST3 audio component, which runs the signal chain described in docs/signal-path.md.
class Processor : public Hadronyche::StereoProcessor<kParamCount>
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (LOOPTEXTURER_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

private:
    void prepareDsp () override;
    void renderBlock (Steinberg::Vst::ProcessData& data, Steinberg::Vst::AudioBusBuffers& output) override;

    double plain (ParamId id) const;
    void applyParameters ();
    Looper::Settings getLooperSettings () const;

    bool writeLoopState (Steinberg::IBStream* state);
    void readLoopState (Steinberg::IBStream* state);
    void runWorker ();

    using Smoother = Hadronyche::Smoother;


    Smoother _gain;
    Smoother _volume;
    Smoother _pan;
    Smoother _panLfoAmount;
    Smoother _filterPitch; // log2 of the frequency, so that sweeps are even across the range.
    Smoother _filterLfoAmount;
    Smoother _filterQ;
    Smoother _dryGain;
    Smoother _wetGain;

    Lfo _panLfo { 0x9E3779B9u };
    Lfo _filterLfo { 0x85EBCA6Bu };
    StateVariableFilter _filter;
    Saturator _saturator;
    Saturator _postSaturator;

    std::shared_ptr<LooperBridge> _bridge;
    Looper _looper;

    // Keeps the dry signal lined up with the post saturator's output.
    std::array<std::array<float, Saturator::kLatency>, 2> _dryDelay {};
    int _dryDelayPosition { 0 };

    // Allocates large tapes and frees old ones, neither of which can happen on the audio thread.
    std::thread _worker;
    std::mutex _workerMutex;
    std::condition_variable _workerWake;
    bool _stopWorker { false };

    // Held while tapes are being added, so the worker doesn't release one before it's in use.
    std::mutex _tapeChangeMutex;

    // A restored loop that the audio thread hasn't picked up yet. Saved as is, if the host asks.
    std::vector<std::unique_ptr<Looper::LoadedLoop>> _loads;
    std::shared_ptr<Tape> _lastLoadedTape;
    Looper::LoadedLoop _lastLoad {};
};

} // namespace LoopTexturer
