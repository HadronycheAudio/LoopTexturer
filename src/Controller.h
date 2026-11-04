#pragma once

#include "PluginConfig.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>

namespace LoopTexturer {

class LooperBridge;

// The VST3 edit controller: registers the parameters and creates the editor.
class Controller : public Steinberg::Vst::EditController
{
public:
    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (LOOPTEXTURER_CONTROLLER_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller);
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // Null until the processor has introduced itself.
    std::shared_ptr<LooperBridge> getLooperBridge () const { return _looperBridge; }

    void sendLooperCommand (int command);

private:
    std::shared_ptr<LooperBridge> _looperBridge;
};

} // namespace LoopTexturer
