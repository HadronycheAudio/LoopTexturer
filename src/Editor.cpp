#include "Editor.h"

#include "DragTestFile.h"
#include "Parameters.h"

#include "pluginterfaces/base/ustring.h"
#include "vstgui/lib/cdropsource.h"
#include "vstgui/vstgui.h"

#include <cmath>

using namespace VSTGUI;

namespace LoopTexturer {

namespace {

constexpr int kMargin = 8;
constexpr int kGap = 6;
constexpr int kColumnWidth = 66;
constexpr int kSectionPadding = 6;
constexpr int kSectionHeight = 148;
constexpr int kStripHeight = 84;
constexpr int kKnobSize = 44;

constexpr int kTitleRowOffset = 4;
constexpr int kMenuRowOffset = 26;
constexpr int kMenuRowHeight = 20;
constexpr int kKnobRowOffset = 52;
constexpr int kLabelHeight = 14;

constexpr int sectionWidth (int columns)
{
    return columns * kColumnWidth + 2 * kSectionPadding;
}

constexpr int kEditorWidth = 2 * kMargin + 5 * kGap + sectionWidth (1) + sectionWidth (3) + sectionWidth (4) + sectionWidth (2) + sectionWidth (2) + sectionWidth (1);
constexpr int kEditorHeight = 2 * kMargin + kGap + kSectionHeight + kStripHeight;

const CColor kPanelColor (28, 31, 36);
const CColor kSectionColor (42, 47, 55);
const CColor kFrameColor (70, 78, 90);
const CColor kAccentColor (232, 160, 70);
const CColor kTextColor (225, 228, 232);
const CColor kDimTextColor (160, 166, 175);

// A titled background box. Controls are added on top of it.
class SectionView : public CView
{
public:
    SectionView (const CRect& size, const char* title) : CView (size), _title (title)
    {
        setMouseEnabled (false);
    }

    void draw (CDrawContext* context) override
    {
        const auto bounds = getViewSize ();
        context->setFillColor (kSectionColor);
        context->setFrameColor (kFrameColor);
        context->setLineWidth (1.0);
        context->drawRect (bounds, kDrawFilledAndStroked);

        auto titleArea = bounds;
        titleArea.top += kTitleRowOffset;
        titleArea.setHeight (18);
        context->setFont (kNormalFont);
        context->setFontColor (kTextColor);
        context->drawString (_title.data (), titleArea);

        setDirty (false);
    }

private:
    UTF8String _title;
};

CTextLabel* createLabel (const CRect& area, const char* text, const CColor& color, CHoriTxtAlign align = kCenterText)
{
    auto* label = new CTextLabel (area, text);
    label->setFont (kNormalFontSmall);
    label->setFontColor (color);
    label->setHoriAlign (align);
    label->setStyle (CParamDisplay::kNoFrame);
    label->setTransparency (true);
    label->setMouseEnabled (false);
    return label;
}

// Drags a WAV file out of the plug-in as an ordinary operating system file drag. VST3 has no
// drag API of its own, so the host just sees a file being dropped on it.
class DragSourceView : public CView
{
public:
    explicit DragSourceView (const CRect& size) : CView (size) {}

    void draw (CDrawContext* context) override
    {
        const auto bounds = getViewSize ();
        context->setFillColor (_isHeld ? CColor (70, 110, 160) : kSectionColor);
        context->setFrameColor (kFrameColor);
        context->setLineWidth (1.0);
        context->drawRect (bounds, kDrawFilledAndStroked);

        context->setFont (kNormalFont);
        context->setFontColor (kTextColor);
        context->drawString (_status.data (), bounds);

        setDirty (false);
    }

    void onMouseDownEvent (MouseDownEvent& event) override
    {
        if (!event.buttonState.isLeft ())
            return;

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
    void release ()
    {
        _isHeld = false;
        invalid ();
    }

    void setStatus (UTF8StringPtr status)
    {
        _status = status;
        invalid ();
    }

    void startDrag ()
    {
        // Checked on every drag, because the drop target may have moved the file away.
        const auto path = ensureDragTestFile ();
        if (path.empty ())
        {
            setStatus ("Could not write the test WAV");
            return;
        }

        // The size includes the terminating null, which VSTGUI reads the path up to.
        const auto package = CDropSource::create (path.c_str (), static_cast<uint32_t> (path.size () + 1), IDataPackage::kFilePath);

        // Reports the result of the drag. It holds a reference so the view outlives the drag.
        auto callback = makeOwned<DragCallbackFunctions> ();
        callback->endedFunc = [self = SharedPointer<DragSourceView> (this)] (IDraggingSession*, CPoint, DragOperation result) {
            self->setStatus (result == DragOperation::None ? "No drop: drag again" : "Dropped: target accepted the file");
        };

        if (!doDrag (DragDescription (package), callback))
            setStatus ("Could not start the drag");
    }

    DragStartMouseObserver _dragStart;
    bool _isHeld { false };
    UTF8String _status { "Looper goes here. For now: drag from here to the Cubase timeline" };
};

} // namespace

Editor::Editor (void* controller) : VSTGUIEditor (controller)
{
    Steinberg::ViewRect size (0, 0, kEditorWidth, kEditorHeight);
    setRect (size);
}

void Editor::addSection (const CRect& area, const char* title)
{
    frame->addView (new SectionView (area, title));
}

void Editor::addRowLabel (const CRect& area, const char* text)
{
    frame->addView (createLabel (area, text, kDimTextColor, kRightText));
}

void Editor::addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar)
{
    frame->addView (createLabel (CRect (x, y, x + kColumnWidth, y + kLabelHeight), title, kDimTextColor));

    const int knobX = x + (kColumnWidth - kKnobSize) / 2;
    const int knobY = y + kLabelHeight + 2;
    const CRect knobArea (knobX, knobY, knobX + kKnobSize, knobY + kKnobSize);

    int32_t style = CKnob::kCoronaDrawing | CKnob::kCoronaOutline;
    if (bipolar)
        style |= CKnob::kCoronaFromCenter;

    auto* knob = new CKnob (knobArea, this, static_cast<int32_t> (id), nullptr, nullptr, CPoint (0, 0), style);
    knob->setCoronaColor (kAccentColor);
    knob->setColorHandle (kTextColor);
    knob->setColorShadowHandle (kFrameColor);
    knob->setHandleLineWidth (2.0);
    knob->setCoronaInset (2.0);

    const auto& spec = getParamSpec (id);
    knob->setDefaultValue (static_cast<float> (plainToNormalized (spec, spec.defaultValue)));
    frame->addView (knob);

    const int valueY = knobY + kKnobSize + 2;
    auto* value = createLabel (CRect (x, valueY, x + kColumnWidth, valueY + kLabelHeight), "", kTextColor);
    frame->addView (value);

    _bindings.push_back ({ knob, value });
}

void Editor::addMenu (const CRect& area, Steinberg::Vst::ParamID id)
{
    auto* menu = new COptionMenu (area, this, static_cast<int32_t> (id));
    const auto& spec = getParamSpec (id);
    for (int i = 0; i < spec.labelCount; ++i)
        menu->addEntry (spec.labels[i]);
    menu->setCurrent (0); // A new menu has no selection, and would stay blank at a value of 0.

    menu->setFont (kNormalFontSmall);
    menu->setFontColor (kTextColor);
    menu->setBackColor (kPanelColor);
    menu->setFrameColor (kFrameColor);
    menu->setHoriAlign (kCenterText);
    frame->addView (menu);

    _bindings.push_back ({ menu, nullptr });
}

void Editor::addToggle (const CRect& area, Steinberg::Vst::ParamID id, const char* title)
{
    auto* toggle = new CCheckBox (area, this, static_cast<int32_t> (id), title);
    toggle->setFont (kNormalFontSmall);
    toggle->setFontColor (kTextColor);
    toggle->setBoxFillColor (kPanelColor);
    toggle->setBoxFrameColor (kFrameColor);
    toggle->setCheckMarkColor (kAccentColor);
    frame->addView (toggle);

    _bindings.push_back ({ toggle, nullptr });
}

bool PLUGIN_API Editor::open (void* parent, const PlatformType& platformType)
{
    const CRect bounds (0, 0, kEditorWidth, kEditorHeight);

    frame = new CFrame (bounds, this);
    frame->setBackgroundColor (kPanelColor);

    const int top = kMargin;
    const int menuY = top + kMenuRowOffset;
    const int knobY = top + kKnobRowOffset;
    int x = kMargin;

    const auto section = [&] (int columns, const char* title) {
        const int left = x;
        addSection (CRect (left, top, left + sectionWidth (columns), top + kSectionHeight), title);
        x += sectionWidth (columns) + kGap;
        return left + kSectionPadding;
    };
    const auto menuRow = [&] (int left, int width) {
        return CRect (left, menuY, left + width, menuY + kMenuRowHeight);
    };

    {
        const int left = section (1, "Gain");
        addKnob (left, knobY, kGain, "Gain", true);
    }
    {
        const int left = section (3, "Pan");
        addRowLabel (menuRow (left, kColumnWidth), "LFO wave");
        addMenu (menuRow (left + kColumnWidth + 4, 2 * kColumnWidth - 4), kPanLfoWave);
        addKnob (left, knobY, kPan, "Pan", true);
        addKnob (left + kColumnWidth, knobY, kPanLfoRate, "LFO rate");
        addKnob (left + 2 * kColumnWidth, knobY, kPanLfoAmount, "LFO amount");
    }
    {
        const int left = section (4, "Filter");
        addMenu (menuRow (left, 2 * kColumnWidth - 4), kFilterMode);
        addMenu (menuRow (left + 2 * kColumnWidth + 4, 2 * kColumnWidth - 4), kFilterLfoWave);
        addKnob (left, knobY, kFilterFreq, "Frequency");
        addKnob (left + kColumnWidth, knobY, kFilterResonance, "Resonance");
        addKnob (left + 2 * kColumnWidth, knobY, kFilterLfoRate, "LFO rate");
        addKnob (left + 3 * kColumnWidth, knobY, kFilterLfoAmount, "LFO amount");
    }
    {
        const int left = section (2, "Saturator");
        addToggle (menuRow (left, 36), kSatOn, "On");
        addMenu (menuRow (left + 38, 2 * kColumnWidth - 38), kSatMode);
        addKnob (left, knobY, kSatDrive, "Drive");
        addKnob (left + kColumnWidth, knobY, kSatMix, "Mix");
    }
    {
        const int left = section (2, "Post saturator");
        addToggle (menuRow (left, 36), kPostSatOn, "On");
        addMenu (menuRow (left + 38, 2 * kColumnWidth - 38), kPostSatMode);
        addKnob (left, knobY, kPostSatDrive, "Drive");
        addKnob (left + kColumnWidth, knobY, kPostSatMix, "Mix");
    }
    {
        const int left = section (1, "Volume");
        addKnob (left, knobY, kVolume, "Volume");
    }

    const int stripTop = top + kSectionHeight + kGap;
    frame->addView (new DragSourceView (CRect (kMargin, stripTop, kEditorWidth - kMargin, stripTop + kStripHeight)));

    if (!frame->open (parent, platformType))
    {
        _bindings.clear ();
        frame->forget ();
        frame = nullptr;
        return false;
    }

    syncControls ();

    // Keeps the panel up to date with automation and state changes from the host.
    _syncTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { syncControls (); }, 33);
    return true;
}

void PLUGIN_API Editor::close ()
{
    _syncTimer = nullptr;
    _bindings.clear ();

    if (frame)
    {
        frame->close (); // This also releases the frame.
        frame = nullptr;
    }
}

void Editor::syncControls ()
{
    auto* editController = getController ();
    if (!editController)
        return;

    for (const auto& binding : _bindings)
    {
        const auto id = static_cast<Steinberg::Vst::ParamID> (binding.control->getTag ());
        const auto value = editController->getParamNormalized (id);

        if (std::abs (binding.control->getValueNormalized () - static_cast<float> (value)) > 1e-5f)
        {
            binding.control->setValueNormalized (static_cast<float> (value));
            binding.control->invalid ();
        }

        if (binding.valueLabel)
        {
            Steinberg::Vst::String128 text {};
            editController->getParamStringByValue (id, value, text);

            char ascii[128] {};
            Steinberg::UString (text, str16BufferSize (Steinberg::Vst::String128)).toAscii (ascii, sizeof (ascii));

            UTF8String display (ascii);
            const auto& spec = getParamSpec (id);
            if (spec.units[0] != '\0')
            {
                display += " ";
                display += spec.units;
            }
            binding.valueLabel->setText (display);
        }
    }
}

void Editor::valueChanged (CControl* control)
{
    auto* editController = getController ();
    if (!editController)
        return;

    const auto id = static_cast<Steinberg::Vst::ParamID> (control->getTag ());
    const double value = control->getValueNormalized ();
    editController->setParamNormalized (id, value);
    editController->performEdit (id, value);
}

void Editor::controlBeginEdit (CControl* control)
{
    beginEdit (control->getTag ());
}

void Editor::controlEndEdit (CControl* control)
{
    endEdit (control->getTag ());
}

} // namespace LoopTexturer
