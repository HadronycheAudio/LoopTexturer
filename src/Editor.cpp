#include "Editor.h"

#include "DragTestFile.h"

#include "vstgui/lib/cdropsource.h"
#include "vstgui/vstgui.h"

using namespace VSTGUI;

namespace LoopTexturer {

namespace {

constexpr int kEditorWidth = 360;
constexpr int kEditorHeight = 160;

// Drags a WAV file out of the plug-in as an ordinary operating system file drag. VST3 has no
// drag API of its own, so the host just sees a file being dropped on it.
class DragSourceView : public CView
{
public:
    explicit DragSourceView (const CRect& size) : CView (size) {}

    void draw (CDrawContext* context) override
    {
        const auto bounds = getViewSize ();
        context->setFillColor (_isHeld ? CColor (70, 110, 160) : CColor (50, 60, 75));
        context->drawRect (bounds, kDrawFilled);

        context->setFont (kNormalFont);
        context->setFontColor (kWhiteCColor);
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
    UTF8String _status { "Drag from here to the Cubase timeline" };
};

} // namespace

Editor::Editor (void* controller) : VSTGUIEditor (controller)
{
    Steinberg::ViewRect size (0, 0, kEditorWidth, kEditorHeight);
    setRect (size);
}

bool PLUGIN_API Editor::open (void* parent, const PlatformType& platformType)
{
    const CRect bounds (0, 0, kEditorWidth, kEditorHeight);

    frame = new CFrame (bounds, this);
    frame->setBackgroundColor (kBlackCColor);
    frame->addView (new DragSourceView (bounds));

    if (!frame->open (parent, platformType))
    {
        frame->forget ();
        frame = nullptr;
        return false;
    }
    return true;
}

void PLUGIN_API Editor::close ()
{
    if (frame)
    {
        frame->close (); // This also releases the frame.
        frame = nullptr;
    }
}

} // namespace LoopTexturer
