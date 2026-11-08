#pragma once

#include "PluginConfig.h"

#include "hadronyche/vst/RustController.h"

#include <memory>

namespace LoopTexturer {

class LooperBridge;

// The VST3 edit controller: its parameters and state come from the Rust side, it creates the editor, and it carries
// the looper's messages between the panel and the processor.
class Controller : public Hadronyche::RustController
{
public:
    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (LOOPTEXTURER_CONTROLLER_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller);
    }

    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // Null until the processor has introduced itself.
    std::shared_ptr<LooperBridge> getLooperBridge () const { return _looperBridge; }

    void sendLooperCommand (int command);

private:
    Steinberg::IPlugView* createEditor () override;

    std::shared_ptr<LooperBridge> _looperBridge;
};

} // namespace LoopTexturer
