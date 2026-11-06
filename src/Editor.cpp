#include "Editor.h"

#include "Controller.h"
#include "LoopExport.h"
#include "LooperBridge.h"
#include "Parameters.h"
#include "dsp/Looper.h"

#include "vstgui/lib/cdropsource.h"
#include "vstgui/vstgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

using namespace VSTGUI;

namespace LoopTexturer {

namespace {

#if defined(__linux__)
constexpr bool kCanDrag = false;
#else
constexpr bool kCanDrag = true;
#endif

constexpr Hadronyche::PanelLayout kLayout { 148, 52 };

constexpr int kMargin = kLayout.margin;
constexpr int kGap = kLayout.gap;
constexpr int kColumnWidth = kLayout.columnWidth;
constexpr int kSectionPadding = kLayout.sectionPadding;
constexpr int kLooperHeight = 148;

constexpr int kMenuRowOffset = 26;
constexpr int kMenuRowHeight = 20;
constexpr int kKnobRowOffset = kLayout.knobRowOffset;

constexpr auto kMessageDuration = std::chrono::seconds (3);

constexpr int kButtonWidth = 70;
constexpr int kButtonGap = 4;

// Tags for controls that aren't parameters.
constexpr int kTagRec = 1000;
constexpr int kTagOverdub = 1001;
constexpr int kTagPlay = 1002;
constexpr int kTagStop = 1003;
constexpr int kTagClear = 1004;
constexpr int kTagConfirmMode = 1005;
constexpr int kTagCancelMode = 1006;

constexpr int kEditorWidth = kLayout.rowWidth ({ 1, 3, 4, 2, 2, 1 });
constexpr int kEditorHeight = kLayout.rowHeight () + kGap + kLooperHeight;

const CColor kPanelColor (28, 31, 36);
const CColor kSectionColor (42, 47, 55);
const CColor kFrameColor (70, 78, 90);
const CColor kAccentColor (232, 160, 70);
const CColor kTextColor (225, 228, 232);
const CColor kDimTextColor (160, 166, 175);
const CColor kFaintTextColor (95, 100, 108);
const CColor kWaveColor (120, 160, 205);
const CColor kHeldColor (58, 72, 92);
const CColor kButtonColor (20, 22, 26);
const CColor kButtonPressedColor (58, 62, 70);
const CColor kButtonTextColor (255, 255, 255);
const CColor kRecordColor (196, 36, 36);
const CColor kOverdubColor (186, 146, 8);
const CColor kPlayColor (34, 128, 70);

const Hadronyche::PanelTheme kTheme { kPanelColor, kSectionColor, kFrameColor, kAccentColor, kTextColor, kDimTextColor };

std::string describeStatus (const LooperBridge::Status& status)
{
    if (status.isSyncMode.load ())
    {
        const bool isRunning = status.isTransportRunning.load ();
        if (status.isArmed.load ())
            return isRunning ? "Rec: waiting for the next bar" : "Rec: waiting for the transport";
        if (status.isClosing.load ())
            return "Recording: ends at the next bar";
        if (status.isRecording.load ())
            return "Recording";
        if (status.isPlayArmed.load ())
            return "Play: waiting for the next bar";
        if (status.transportJumped.load () && !status.hasContent.load ())
            return "Recording cancelled: the transport jumped";
        if (!status.hasContent.load ())
            return "Empty: press Rec";
        if (status.isOverdubbing.load ())
            return "Overdubbing";
        if (!isRunning)
            return "Waiting for the transport";
        return status.isPlaying.load () ? (kCanDrag ? "Playing: drag to the timeline" : "Playing") : "Stopped";
    }

    if (status.isRecording.load ())
        return "Recording";
    if (status.isOverdubbing.load ())
        return "Overdubbing";
    if (!status.hasContent.load ())
        return "Empty: press Rec";
    return status.isPlaying.load () ? (kCanDrag ? "Looping: drag to the timeline" : "Looping") : "Stopped: press Play";
}

} // namespace

// Draws the loop's waveform and playhead, and drags the loop out of the plug-in as an ordinary
// operating system file drag. VST3 has no drag API of its own, so the host just sees a file being
// dropped on it. On Linux, the waveform only draws, by the owner's decision: VSTGUI can't start a
// drag there, and a host can render or record the plug-in's output instead.
class WaveformView : public CView
{
public:
    WaveformView (const CRect& size, Controller& controller) : CView (size), _controller (controller) {}

    void setConfirming (bool isConfirming)
    {
        _isConfirming = isConfirming;
        invalid ();
    }

    void draw (CDrawContext* context) override
    {
        const auto bounds = getViewSize ();
        context->setFillColor (_isHeld ? kHeldColor : kPanelColor);
        context->setFrameColor (kFrameColor);
        context->setLineWidth (1.0);
        context->drawRect (bounds, kDrawFilledAndStroked);

        const auto bridge = _controller.getLooperBridge ();
        if (!bridge)
        {
            drawText (context, bounds, "The looper isn't connected to the processor");
            setDirty (false);
            return;
        }

        if (const auto tape = bridge->getActiveTape ())
            drawWaveform (context, bounds, *tape, bridge->status);

        drawText (context, bounds, statusText (bridge->status));
        setDirty (false);
    }

    void onMouseDownEvent (MouseDownEvent& event) override
    {
        if (!kCanDrag || !event.buttonState.isLeft () || _isConfirming)
            return;

        _message.clear ();
        _dragStart.init (event.mousePosition);
        _isHeld = true;
        invalid ();
        event.consumed = true;
    }

    void onMouseMoveEvent (MouseMoveEvent& event) override
    {
        if (!_isHeld || !event.buttonState.isLeft () || !_dragStart.shouldStartDrag (event.mousePosition))
            return;

        event.consumed = true;
        release ();
        startDrag ();
    }

    void onMouseUpEvent (MouseUpEvent& event) override
    {
        if (_isHeld)
            event.consumed = true;
        release ();
    }

    void onMouseCancelEvent (MouseCancelEvent& event) override
    {
        release ();
        event.consumed = true;
    }

private:
    std::string statusText (const LooperBridge::Status& status) const
    {
        if (_isConfirming)
            return "Switching mode clears the loop";

        const bool showsMessage = !_message.empty () && std::chrono::steady_clock::now () - _messageTime < kMessageDuration;
        return showsMessage ? _message : describeStatus (status);
    }

    static void drawText (CDrawContext* context, const CRect& bounds, const std::string& text)
    {
        auto area = bounds;
        area.setHeight (18);
        area.inset (6, 2);
        context->setFont (kNormalFontSmall);
        context->setFontColor (kTextColor);
        context->drawString (text.c_str (), area, kLeftText);
    }

    static void drawWaveform (CDrawContext* context, const CRect& bounds, const Tape& tape, const LooperBridge::Status& status)
    {
        const int64_t frames = status.displayFrames.load ();
        if (frames <= 0)
            return;

        const int64_t bucketFrames = tape.getBucketFrames ();
        const double middle = (bounds.top + bounds.bottom) / 2.0;
        const double scale = (bounds.getHeight () - 2.0) / 2.0;
        const int width = static_cast<int> (bounds.getWidth ());

        const auto columnBucket = [&] (int column) {
            const auto frame = static_cast<int64_t> (static_cast<double> (column) / width * static_cast<double> (frames));
            return std::min (frame / bucketFrames, Tape::kOverviewBuckets - 1);
        };

        context->setDrawMode (kAliasing);
        context->setLineStyle (kLineSolid);
        context->setLineWidth (1.0);
        context->setFrameColor (kWaveColor);
        for (int column = 0; column < width; ++column)
        {
            const int64_t first = columnBucket (column);
            const int64_t last = std::max (first, columnBucket (column + 1) - 1);
            float low = 0.0f;
            float high = 0.0f;
            for (int64_t bucket = first; bucket <= last; ++bucket)
            {
                low = std::min (low, tape.getOverviewMin (bucket));
                high = std::max (high, tape.getOverviewMax (bucket));
            }

            const double x = bounds.left + column + 0.5;
            const double top = middle - std::clamp (static_cast<double> (high), -1.0, 1.0) * scale;
            const double bottom = middle - std::clamp (static_cast<double> (low), -1.0, 1.0) * scale;
            context->drawLine (std::make_pair (CPoint (x, top), CPoint (x, std::max (bottom, top + 1.0))));
        }

        const double playhead = std::clamp (status.playhead.load () / static_cast<double> (frames), 0.0, 1.0);
        const double x = std::floor (bounds.left + playhead * width) + 0.5;
        context->setFrameColor (kAccentColor);
        context->drawLine (std::make_pair (CPoint (x, bounds.top + 1.0), CPoint (x, bounds.bottom - 1.0)));
    }

    void release ()
    {
        _isHeld = false;
        invalid ();
    }

    void showMessage (const char* message)
    {
        _message = message;
        _messageTime = std::chrono::steady_clock::now ();
        invalid ();
    }

    void startDrag ()
    {
        const auto bridge = _controller.getLooperBridge ();
        if (!bridge || !bridge->status.hasContent.load () || bridge->status.loopFrames.load () <= 0)
        {
            showMessage ("Nothing to drag: the loop is empty");
            return;
        }

        const auto plain = [this] (ParamId id) {
            return normalizedToPlain (getParamSpec (id), _controller.getParamNormalized (id));
        };

        LoopExportSettings settings {};
        settings.postSaturatorOn = plain (kPostSatOn) > 0.5;
        settings.postSaturatorMode = static_cast<int> (plain (kPostSatMode));
        settings.postSaturatorDriveDb = plain (kPostSatDrive);
        settings.postSaturatorMix = plain (kPostSatMix) / 100.0;
        settings.freeLengthSeconds = plain (kLooperFreeLength);

        const auto path = exportLoop (*bridge, settings);
        if (path.empty ())
        {
            showMessage ("Could not write the WAV file");
            return;
        }

        // The size includes the terminating null, which VSTGUI reads the path up to.
        const auto package = CDropSource::create (path.c_str (), static_cast<uint32_t> (path.size () + 1), IDataPackage::kFilePath);

        // Reports the result of the drag. It holds a reference so the view outlives the drag.
        auto callback = makeOwned<DragCallbackFunctions> ();
        callback->endedFunc = [self = SharedPointer<WaveformView> (this)] (IDraggingSession*, CPoint, DragOperation result) {
            self->showMessage (result == DragOperation::None ? "No drop: drag again" : "Dropped: the target accepted the file");
        };

        if (!doDrag (DragDescription (package), callback))
            showMessage ("Could not start the drag");
    }

    Controller& _controller;
    DragStartMouseObserver _dragStart;
    bool _isHeld { false };
    bool _isConfirming { false };
    std::string _message;
    std::chrono::steady_clock::time_point _messageTime;
};

Editor::Editor (Controller* controller)
: PanelEditor (controller, kEditorWidth, kEditorHeight, getParamSpec, kParamCount, kTheme, kLayout), _controller (controller)
{
}

void Editor::addRowLabel (const CRect& area, const char* text)
{
    frame->addView (createLabel (area, text, kDimTextColor, kRightText));
}

CTextButton* Editor::addButton (const CRect& area, int tag, const char* title)
{
    auto* button = new CTextButton (area, this, tag, title);
    button->setFont (kNormalFontSmall);
    button->setRoundRadius (3.0);
    button->setFrameColor (kFrameColor);
    button->setFrameColorHighlighted (kFrameColor);
    button->setTextColor (kButtonTextColor);
    button->setTextColorHighlighted (kButtonTextColor);
    button->setGradient (_idleFill);
    button->setGradientHighlighted (_pressedFill);
    frame->addView (button);
    return button;
}

void Editor::setButtonFill (CTextButton* button, CGradient* fill)
{
    if (button->getGradient () != fill)
    {
        button->setGradient (fill);
        button->invalid ();
    }
}

void Editor::addLooperSection (int top)
{
    const auto solidFill = [] (const CColor& color) { return owned (CGradient::create (0.0, 1.0, color, color)); };
    _idleFill = solidFill (kButtonColor);
    _pressedFill = solidFill (kButtonPressedColor);
    _recordFill = solidFill (kRecordColor);
    _overdubFill = solidFill (kOverdubColor);
    _playFill = solidFill (kPlayColor);

    const CRect section (kMargin, top, kEditorWidth - kMargin, top + kLooperHeight);
    addSection (section, "Looper");

    const int left = kMargin + kSectionPadding;
    const int menuY = top + kMenuRowOffset;
    const int knobY = top + kKnobRowOffset;

    addMenu (CRect (left, menuY, left + kColumnWidth + 30, menuY + kMenuRowHeight), kLooperMode);
    _barsMenu = addMenu (CRect (left + kColumnWidth + 34, menuY, left + 3 * kColumnWidth, menuY + kMenuRowHeight), kLooperBars);

    // These two share a column, because only one of them applies at a time.
    _freeOnlyViews = addKnob (left, knobY, kLooperFreeLength, "Length");
    _customBarsViews = addKnob (left, knobY, kLooperCustomBars, "Custom bars");
    addKnob (left + kColumnWidth, knobY, kLooperFeedback, "Feedback");
    addKnob (left + 2 * kColumnWidth, knobY, kLooperDryWet, "Dry/wet");

    const int rightLeft = left + 3 * kColumnWidth + kGap;
    const int rightEnd = kEditorWidth - kMargin - kSectionPadding;

    const auto buttonArea = [&] (int index) {
        const int x = rightLeft + index * (kButtonWidth + kButtonGap);
        return CRect (x, menuY, x + kButtonWidth, menuY + kMenuRowHeight);
    };
    _recButton = addButton (buttonArea (0), kTagRec, "Rec");
    _overdubButton = addButton (buttonArea (1), kTagOverdub, "Overdub");
    _playButton = addButton (buttonArea (2), kTagPlay, "Play");
    _stopButton = addButton (buttonArea (3), kTagStop, "Stop");
    addButton (buttonArea (4), kTagClear, "Clear");

    const CRect waveformArea (rightLeft, knobY, rightEnd, top + kLooperHeight - kSectionPadding);
    _waveform = new WaveformView (waveformArea, *_controller);
    frame->addView (_waveform);

    // Shown over the waveform when a mode change needs confirming.
    const int confirmY = knobY + 26;
    _confirmButton = addButton (CRect (rightLeft + 6, confirmY, rightLeft + 126, confirmY + kMenuRowHeight), kTagConfirmMode, "Clear and switch");
    _cancelButton = addButton (CRect (rightLeft + 132, confirmY, rightLeft + 212, confirmY + kMenuRowHeight), kTagCancelMode, "Cancel");
    showModeConfirmation (false);
}

void Editor::buildPanel ()
{
    const int top = kMargin;
    const int menuY = top + kMenuRowOffset;
    const int knobY = top + kKnobRowOffset;

    const auto menuRow = [&] (int left, int width) {
        return CRect (left, menuY, left + width, menuY + kMenuRowHeight);
    };

    {
        const int left = addColumnSection (1, "Gain");
        addKnob (left, knobY, kGain, "Gain", true);
    }
    {
        const int left = addColumnSection (3, "Pan");
        addRowLabel (menuRow (left, kColumnWidth), "LFO wave");
        addMenu (menuRow (left + kColumnWidth + 4, 2 * kColumnWidth - 4), kPanLfoWave);
        addKnob (left, knobY, kPan, "Pan", true);
        addKnob (left + kColumnWidth, knobY, kPanLfoRate, "LFO rate");
        addKnob (left + 2 * kColumnWidth, knobY, kPanLfoAmount, "LFO amount");
    }
    {
        const int left = addColumnSection (4, "Filter");
        addMenu (menuRow (left, 2 * kColumnWidth - 4), kFilterMode);
        addMenu (menuRow (left + 2 * kColumnWidth + 4, 2 * kColumnWidth - 4), kFilterLfoWave);
        addKnob (left, knobY, kFilterFreq, "Frequency");
        addKnob (left + kColumnWidth, knobY, kFilterResonance, "Resonance");
        addKnob (left + 2 * kColumnWidth, knobY, kFilterLfoRate, "LFO rate");
        addKnob (left + 3 * kColumnWidth, knobY, kFilterLfoAmount, "LFO amount");
    }
    {
        const int left = addColumnSection (2, "Saturator");
        addToggle (menuRow (left, 36), kSatOn, "On");
        addMenu (menuRow (left + 38, 2 * kColumnWidth - 38), kSatMode);
        addKnob (left, knobY, kSatDrive, "Drive");
        addKnob (left + kColumnWidth, knobY, kSatMix, "Mix");
    }
    {
        const int left = addColumnSection (2, "Post saturator");
        addToggle (menuRow (left, 36), kPostSatOn, "On");
        addMenu (menuRow (left + 38, 2 * kColumnWidth - 38), kPostSatMode);
        addKnob (left, knobY, kPostSatDrive, "Drive");
        addKnob (left + kColumnWidth, knobY, kPostSatMix, "Mix");
    }
    {
        const int left = addColumnSection (1, "Volume");
        addKnob (left, knobY, kVolume, "Volume");
    }

    addLooperSection (top + kLayout.sectionHeight + kGap);

}

void Editor::onClose ()
{
    _freeOnlyViews.clear ();
    _customBarsViews.clear ();
    _barsMenu = nullptr;
    _recButton = nullptr;
    _overdubButton = nullptr;
    _playButton = nullptr;
    _stopButton = nullptr;
    _confirmButton = nullptr;
    _cancelButton = nullptr;
    _waveform = nullptr;
}

void Editor::onSync ()
{
    const auto plain = [this] (ParamId id) {
        return normalizedToPlain (getParamSpec (id), _controller->getParamNormalized (id));
    };

    const bool isSync = plain (kLooperMode) > 0.5;
    const bool showsCustomBars = isSync && static_cast<int> (plain (kLooperBars)) == kBarsCustom;

    for (auto* view : _freeOnlyViews)
        view->setVisible (!isSync);
    for (auto* view : _customBarsViews)
        view->setVisible (showsCustomBars);
    _barsMenu->setVisible (isSync);

    if (const auto bridge = _controller->getLooperBridge ())
    {
        const auto& status = bridge->status;
        const bool isRecording = status.isRecording.load () || status.isArmed.load () || status.isClosing.load ();
        const bool isPlaying = status.hasContent.load () && (status.isPlaying.load () || status.isPlayArmed.load ());
        setButtonFill (_recButton, isRecording ? _recordFill : _idleFill);
        setButtonFill (_overdubButton, status.isOverdubbing.load () ? _overdubFill : _idleFill);
        setButtonFill (_playButton, isPlaying ? _playFill : _idleFill);
    }

    _waveform->invalid ();
}

void Editor::setParameter (Steinberg::Vst::ParamID id, double normalized)
{
    _controller->beginEdit (id);
    _controller->setParamNormalized (id, normalized);
    _controller->performEdit (id, normalized);
    _controller->endEdit (id);
}

void Editor::showModeConfirmation (bool show)
{
    _confirmButton->setVisible (show);
    _cancelButton->setVisible (show);
    _waveform->setConfirming (show);
}

void Editor::valueChanged (CControl* control)
{
    const int tag = control->getTag ();

    if (tag >= kTagRec && tag <= kTagCancelMode)
    {
        // Kick buttons report being pressed and then released.
        if (control->getValue () < 0.5f)
            return;

        switch (tag)
        {
            case kTagRec: _controller->sendLooperCommand (Looper::kCommandRec); break;
            case kTagOverdub: _controller->sendLooperCommand (Looper::kCommandOverdub); break;
            case kTagPlay: _controller->sendLooperCommand (Looper::kCommandPlay); break;
            case kTagStop: _controller->sendLooperCommand (Looper::kCommandStop); break;
            case kTagClear: _controller->sendLooperCommand (Looper::kCommandClear); break;
            case kTagConfirmMode:
                setParameter (kLooperMode, _pendingMode);
                showModeConfirmation (false);
                break;
            case kTagCancelMode: showModeConfirmation (false); break;
        }
        return;
    }

    const auto id = static_cast<Steinberg::Vst::ParamID> (tag);
    const double value = control->getValueNormalized ();

    if (id == kLooperMode)
    {
        const auto bridge = _controller->getLooperBridge ();
        if (bridge && bridge->status.hasContent.load ())
        {
            // The menu goes back to the current mode by itself, until this is confirmed.
            _pendingMode = value;
            showModeConfirmation (true);
            return;
        }
    }

    PanelEditor::valueChanged (control);
}

} // namespace LoopTexturer
