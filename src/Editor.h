#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/vstguibase.h"

#include <vector>

namespace VSTGUI {
class CControl;
class CTextLabel;
class CVSTGUITimer;
} // namespace VSTGUI

namespace LoopTexturer {

// The plug-in window: a single panel holding every control, in signal path order.
class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    explicit Editor (void* controller);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close () SMTG_OVERRIDE;

    void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

private:
    struct Binding
    {
        VSTGUI::CControl* control;
        VSTGUI::CTextLabel* valueLabel; // Null for controls that show their value themselves.
    };

    void addSection (const VSTGUI::CRect& area, const char* title);
    void addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar = false);
    void addMenu (const VSTGUI::CRect& area, Steinberg::Vst::ParamID id);
    void addToggle (const VSTGUI::CRect& area, Steinberg::Vst::ParamID id, const char* title);
    void addRowLabel (const VSTGUI::CRect& area, const char* text);
    void syncControls ();

    std::vector<Binding> _bindings;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> _syncTimer;
};

} // namespace LoopTexturer
