#pragma once

#include "hadronyche/gui/PanelEditor.h"

namespace LoopTexturer {

class Controller;
class WaveformView;

// The plug-in window: a single panel holding every control, in signal path order.
class Editor : public Hadronyche::PanelEditor
{
public:
    explicit Editor (Controller* controller);

    void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;

private:
    void buildPanel () override;
    void onSync () override;
    void onClose () override;

    void addRowLabel (const VSTGUI::CRect& area, const char* text);
    VSTGUI::CTextButton* addButton (const VSTGUI::CRect& area, int tag, const char* title);
    void addLooperSection (int top);
    static void setButtonFill (VSTGUI::CTextButton* button, VSTGUI::CGradient* fill);

    void setParameter (Steinberg::Vst::ParamID id, double normalized);
    void showModeConfirmation (bool show);

    Controller* _controller;

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

    VSTGUI::SharedPointer<VSTGUI::CGradient> _idleFill;
    VSTGUI::SharedPointer<VSTGUI::CGradient> _pressedFill;
    VSTGUI::SharedPointer<VSTGUI::CGradient> _recordFill;
    VSTGUI::SharedPointer<VSTGUI::CGradient> _overdubFill;
    VSTGUI::SharedPointer<VSTGUI::CGradient> _playFill;

    double _pendingMode { 0.0 };
};

} // namespace LoopTexturer
