#pragma once

#include "PluginConfig.h"

#include "hadronyche/vst/RustProcessor.h"

namespace LoopTexturer {

// The VST3 audio component. The signal chain described in docs/signal-path.md, the looper and its worker thread run
// on the Rust side, in rust/. This is its class ID, and the two messages between it and the controller: the looper's
// bridge, introduced by its ID, and the panel's buttons.
class Processor : public Hadronyche::RustProcessor
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (LOOPTEXTURER_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }

    Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
};

} // namespace LoopTexturer
