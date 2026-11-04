#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/vstguibase.h"

#include <vector>

namespace VSTGUI {
class CControl;
class CTextButton;
class CTextLabel;
class CView;
class CVSTGUITimer;
} // namespace VSTGUI

namespace LoopTexturer {

class Controller;
class WaveformView;

// The plug-in window: a single panel holding every control, in signal path order.
class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    explicit Editor (Controller* controller);

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

    using ViewGroup = std::vector<VSTGUI::CView*>;

    void addSection (const VSTGUI::CRect& area, const char* title);
    ViewGroup addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar = false);
    VSTGUI::CControl* addMenu (const VSTGUI::CRect& area, Steinberg::Vst::ParamID id);
    void addToggle (const VSTGUI::CRect& area, Steinberg::Vst::ParamID id, const char* title);
    void addRowLabel (const VSTGUI::CRect& area, const char* text);
    VSTGUI::CTextButton* addButton (const VSTGUI::CRect& area, int tag, const char* title);
    void addLooperSection (int top);

    void syncControls ();
    void syncLooper ();
    void setParameter (Steinberg::Vst::ParamID id, double normalized);
    void showModeConfirmation (bool show);

    Controller* _controller;
    std::vector<Binding> _bindings;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> _syncTimer;

    // Looper controls that are only shown in one mode. All of these belong to the frame.
    ViewGroup _freeOnlyViews;
    ViewGroup _customBarsViews;
    VSTGUI::CControl* _barsMenu { nullptr };
    VSTGUI::CTextButton* _recButton { nullptr };
    VSTGUI::CTextButton* _overdubButton { nullptr };
    VSTGUI::CTextButton* _playButton { nullptr };
    VSTGUI::CTextButton* _stopButton { nullptr };
    VSTGUI::CTextButton* _confirmButton { nullptr };
    VSTGUI::CTextButton* _cancelButton { nullptr };
    WaveformView* _waveform { nullptr };

    double _pendingMode { 0.0 };
};

} // namespace LoopTexturer
