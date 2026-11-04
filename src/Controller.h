#pragma once

#include "PluginConfig.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace LoopTexturer {

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
};

} // namespace LoopTexturer
