// The LV2's looper, through its panel, the way a host runs them: the test loads the bundle, runs the plug-in in real
// time, and carries what the panel writes to the plug-in's ui_in port and what ui_out holds back to the panel, as a host
// does. It clicks the real buttons, and checks what the plug-in plays and what the panel draws.
//
// Usage: <program> <LV2 bundle folder>. It needs an X server, which CMake starts through xvfb-run.
//
// The panel has to work through the messages alone, because a host can run it in another process. So one part of the
// test passes the panel's commands to the plug-in but nothing back, and checks that the panel then never shows what the
// looper does.

#include <dlfcn.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <xcb/xcb.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Positions on the panel, from src/Editor.cpp's layout, the same as tests/PanelTest.cpp's.
constexpr int kButtonY = 198;
constexpr int kRecX = 253;
constexpr int kPlayX = 401;
constexpr int kStopX = 475;
constexpr int kClearX = 549;

// Inside the waveform's box, and its middle row, which an empty tape draws as a flat line, as the VST3's panel does.
constexpr int kWaveLeft = 220;
constexpr int kWaveRight = 960;
constexpr int kWaveTop = 216;
constexpr int kWaveBottom = 302;
constexpr int kWaveMiddle = 259;

// Colours from src/Editor.cpp, as red, green, blue.
constexpr uint32_t kRecordColor = 0xC42424;
constexpr uint32_t kPlayColor = 0x228046;
constexpr uint32_t kWaveColor = 0x78A0CD;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kPortBytes = 64 * 1024;

int gFailures = 0;
xcb_connection_t* gConnection = nullptr;
xcb_screen_t* gScreen = nullptr;
const LV2_Descriptor* gPlugin = nullptr;
const LV2UI_Descriptor* gUi = nullptr;
const LV2UI_Idle_Interface* gIdle = nullptr;

void check (bool isOk, const std::string& what)
{
    std::printf ("%s %s\n", isOk ? "ok  " : "FAIL", what.c_str ());
    std::fflush (stdout);
    if (!isOk)
        ++gFailures;
}

struct Port
{
    uint32_t index { 0 };
    float defaultValue { 0.0f };
    bool isControlInput { false };
};

std::map<std::string, Port> gPorts;

void readTurtle (const fs::path& file)
{
    std::ifstream stream (file);
    const std::string text ((std::istreambuf_iterator<char> (stream)), std::istreambuf_iterator<char> ());
    const std::regex block ("lv2:port \\[([^\\]]*)\\]");
    const std::regex index ("lv2:index ([0-9]+)");
    const std::regex symbol ("lv2:symbol \"([a-z_]+)\"");
    const std::regex defaultValue ("lv2:default ([-0-9.e]+)");
    for (auto it = std::sregex_iterator (text.begin (), text.end (), block); it != std::sregex_iterator (); ++it)
    {
        const std::string body = (*it)[1].str ();
        std::smatch match;
        Port port;
        if (!std::regex_search (body, match, index))
            continue;
        port.index = static_cast<uint32_t> (std::stoul (match[1].str ()));
        port.isControlInput = body.find ("lv2:InputPort , lv2:ControlPort") != std::string::npos;
        if (std::regex_search (body, match, defaultValue))
            port.defaultValue = std::stof (match[1].str ());
        if (std::regex_search (body, match, symbol))
            gPorts[match[1].str ()] = port;
    }
}

uint32_t portIndex (const char* symbol)
{
    return gPorts.at (symbol).index;
}

class Urids
{
public:
    Urids ()
    {
        _map.handle = this;
        _map.map = [] (LV2_URID_Map_Handle handle, const char* uri) {
            auto& known = static_cast<Urids*> (handle)->_known;
            const auto found = known.find (uri);
            if (found != known.end ())
                return found->second;
            const auto urid = static_cast<LV2_URID> (known.size () + 1);
            known[uri] = urid;
            return urid;
        };
    }

    LV2_URID_Map* map () { return &_map; }
    LV2_URID get (const char* uri) { return _map.map (_map.handle, uri); }

private:
    LV2_URID_Map _map {};
    std::map<std::string, LV2_URID> _known;
};

Urids gUrids;

// What a host keeps for a state.
struct SavedState
{
    std::map<uint32_t, std::pair<uint32_t, std::vector<uint8_t>>> values;
};

LV2_State_Status storeValue (LV2_State_Handle handle, uint32_t key, const void* value, size_t size, uint32_t type, uint32_t)
{
    const auto* bytes = static_cast<const uint8_t*> (value);
    static_cast<SavedState*> (handle)->values[key] = { type, std::vector<uint8_t> (bytes, bytes + size) };
    return LV2_STATE_SUCCESS;
}

const void* retrieveValue (LV2_State_Handle handle, uint32_t key, size_t* size, uint32_t* type, uint32_t* flags)
{
    const auto& values = static_cast<SavedState*> (handle)->values;
    const auto found = values.find (key);
    if (found == values.end ())
        return nullptr;
    *size = found->second.second.size ();
    *type = found->second.first;
    *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
    return found->second.second.data ();
}

class Panel;

// The plug-in, run as a host runs it, with the panel's messages queued for its next block.
class Plugin
{
public:
    Plugin ()
    {
        const LV2_Options_Option options[] = {
            { LV2_OPTIONS_INSTANCE, 0, gUrids.get (LV2_BUF_SIZE__maxBlockLength), sizeof (int32_t), gUrids.get (LV2_ATOM__Int), &_maxBlock },
            { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr },
        };
        const LV2_Feature mapFeature { LV2_URID__map, gUrids.map () };
        const LV2_Feature optionsFeature { LV2_OPTIONS__options, const_cast<LV2_Options_Option*> (options) };
        const LV2_Feature boundedFeature { LV2_BUF_SIZE__boundedBlockLength, nullptr };
        const LV2_Feature* const features[] = { &mapFeature, &optionsFeature, &boundedFeature, nullptr };
        _instance = gPlugin->instantiate (gPlugin, kSampleRate, "", features);
        if (!_instance)
            return;

        for (const auto& [symbol, port] : gPorts)
        {
            if (port.isControlInput)
            {
                _controls[port.index] = port.defaultValue;
                gPlugin->connect_port (_instance, port.index, &_controls[port.index]);
            }
        }
        for (auto& channel : _audio)
            channel.assign (kBlock, 0.0f);
        _events.assign (kPortBytes, 0);
        _input.assign (kPortBytes, 0);
        _output.assign (kPortBytes, 0);
        gPlugin->connect_port (_instance, portIndex ("in_left"), _audio[0].data ());
        gPlugin->connect_port (_instance, portIndex ("in_right"), _audio[1].data ());
        gPlugin->connect_port (_instance, portIndex ("out_left"), _audio[2].data ());
        gPlugin->connect_port (_instance, portIndex ("out_right"), _audio[3].data ());
        gPlugin->connect_port (_instance, portIndex ("events"), _events.data ());
        gPlugin->connect_port (_instance, portIndex ("latency"), &_latency);
        gPlugin->connect_port (_instance, portIndex ("ui_in"), _input.data ());
        gPlugin->connect_port (_instance, portIndex ("ui_out"), _output.data ());
        emptySequence (_events);
        gPlugin->activate (_instance);
    }

    ~Plugin ()
    {
        if (!_instance)
            return;
        gPlugin->deactivate (_instance);
        gPlugin->cleanup (_instance);
    }

    bool isValid () const { return _instance != nullptr; }

    // From the panel, or made up by the test. Atoms that aren't messages have to be ignored.
    void queueAtom (const void* atom, uint32_t size) { _pending.emplace_back (static_cast<const uint8_t*> (atom), static_cast<const uint8_t*> (atom) + size); }

    // One block in real time, with a sine at the input or silence. Returns the output's RMS.
    double play (bool withSine, Panel* panel);

    double playFor (double seconds, bool withSine, Panel* panel)
    {
        double sum = 0.0;
        const int blocks = static_cast<int> (seconds * kSampleRate / kBlock);
        for (int i = 0; i < blocks; ++i)
        {
            const double rms = play (withSine, panel);
            sum += rms * rms;
        }
        return std::sqrt (sum / std::max (1, blocks));
    }

    SavedState save ()
    {
        SavedState state;
        const auto* interface = static_cast<const LV2_State_Interface*> (gPlugin->extension_data (LV2_STATE__interface));
        interface->save (_instance, storeValue, &state, 0, nullptr);
        return state;
    }

    bool restore (SavedState& state)
    {
        const auto* interface = static_cast<const LV2_State_Interface*> (gPlugin->extension_data (LV2_STATE__interface));
        return interface->restore (_instance, retrieveValue, &state, 0, nullptr) == LV2_STATE_SUCCESS;
    }

    bool forwardsToPanel { true };
    bool dropsCopies { false }; // Passes everything back except src/lv2/LooperLink.cpp's copies.
    long messagesOut { 0 };
    long badOutputs { 0 };

private:
    static void emptySequence (std::vector<uint8_t>& buffer)
    {
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*> (buffer.data ());
        sequence->atom.type = gUrids.get (LV2_ATOM__Sequence);
        sequence->atom.size = sizeof (LV2_Atom_Sequence_Body);
        sequence->body.unit = 0;
        sequence->body.pad = 0;
    }

    void fillInput ()
    {
        emptySequence (_input);
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*> (_input.data ());
        for (const auto& atom : _pending)
        {
            const uint32_t eventSize = static_cast<uint32_t> (sizeof (LV2_Atom_Event)) + lv2_atom_pad_size (static_cast<uint32_t> (atom.size () - sizeof (LV2_Atom)));
            if (sizeof (LV2_Atom) + sequence->atom.size + eventSize > _input.size ())
                break;
            auto* event = reinterpret_cast<LV2_Atom_Event*> (reinterpret_cast<uint8_t*> (&sequence->body) + sequence->atom.size);
            event->time.frames = 0;
            std::memcpy (&event->body, atom.data (), atom.size ());
            sequence->atom.size += eventSize;
        }
        _pending.clear ();
    }

    LV2_Handle _instance { nullptr };
    int32_t _maxBlock { kBlock };
    std::map<uint32_t, float> _controls;
    std::vector<float> _audio[4];
    std::vector<uint8_t> _events;
    std::vector<uint8_t> _input;
    std::vector<uint8_t> _output;
    float _latency { 0.0f };
    std::vector<std::vector<uint8_t>> _pending;
    double _phase { 0.0 };
};

// What a host records from the panel.
struct UiHost
{
    Plugin* plugin { nullptr };
    bool forwardsToPlugin { true };
    long messagesIn { 0 };
    long badWrites { 0 };
};

void writePort (LV2UI_Controller controller, uint32_t port, uint32_t size, uint32_t protocol, const void* buffer)
{
    auto* host = static_cast<UiHost*> (controller);
    if (port != portIndex ("ui_in") || protocol != gUrids.get (LV2_ATOM__eventTransfer) || size < sizeof (LV2_Atom)
        || static_cast<const LV2_Atom*> (buffer)->size + sizeof (LV2_Atom) != size)
    {
        ++host->badWrites;
        return;
    }
    ++host->messagesIn;
    if (host->forwardsToPlugin && host->plugin)
        host->plugin->queueAtom (buffer, size);
}

std::vector<xcb_window_t> childrenOf (xcb_window_t window)
{
    std::vector<xcb_window_t> result;
    auto* reply = xcb_query_tree_reply (gConnection, xcb_query_tree (gConnection, window), nullptr);
    if (reply)
    {
        const auto* children = xcb_query_tree_children (reply);
        result.assign (children, children + xcb_query_tree_children_length (reply));
    }
    std::free (reply);
    return result;
}

// Where open panels are, side by side, because reading the pixels of a covered window gets whatever covers it.
bool gIsSlotUsed[2] {};

class Panel
{
public:
    Panel (Plugin* plugin, bool withMap) : _window (xcb_generate_id (gConnection))
    {
        host.plugin = plugin;
        _slot = gIsSlotUsed[0] ? 1 : 0;
        gIsSlotUsed[_slot] = true;
        xcb_create_window (gConnection, XCB_COPY_FROM_PARENT, _window, gScreen->root, static_cast<int16_t> (1000 * _slot), 0, 1000, 400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                           gScreen->root_visual, 0, nullptr);
        xcb_map_window (gConnection, _window);
        std::free (xcb_get_input_focus_reply (gConnection, xcb_get_input_focus (gConnection), nullptr));

        const LV2_Feature parentFeature { LV2_UI__parent, reinterpret_cast<void*> (static_cast<uintptr_t> (_window)) };
        const LV2_Feature mapFeature { LV2_URID__map, gUrids.map () };
        const LV2_Feature* const withMapFeatures[] = { &parentFeature, &mapFeature, nullptr };
        const LV2_Feature* const withoutMapFeatures[] = { &parentFeature, nullptr };
        _handle = gUi->instantiate (gUi, "", "", writePort, &host, &_widget, withMap ? withMapFeatures : withoutMapFeatures);
        if (_handle)
        {
            const auto frames = childrenOf (static_cast<xcb_window_t> (reinterpret_cast<uintptr_t> (_widget)));
            _frame = frames.empty () ? 0 : frames.front ();
        }
    }

    ~Panel ()
    {
        if (_handle)
            gUi->cleanup (_handle);
        xcb_destroy_window (gConnection, _window);
        xcb_flush (gConnection);
        gIsSlotUsed[_slot] = false;
    }

    bool isValid () const { return _handle != nullptr && _frame != 0; }

    void portEvent (uint32_t port, uint32_t size, uint32_t format, const void* buffer) { gUi->port_event (_handle, port, size, format, buffer); }
    void idle () { gIdle->idle (_handle); }

    void click (int x, int y)
    {
        sendMouse (XCB_BUTTON_PRESS, x, y, 1, 0);
        sendMouse (XCB_BUTTON_RELEASE, x, y, 1, XCB_BUTTON_MASK_1);
    }

    // The panel's pixels as red, green, blue.
    std::vector<uint32_t> grab ()
    {
        auto* geometry = xcb_get_geometry_reply (gConnection, xcb_get_geometry (gConnection, _frame), nullptr);
        const int kWidth = geometry ? geometry->width : 0;
        const int kHeight = geometry ? geometry->height : 0;
        std::free (geometry);
        auto* reply = xcb_get_image_reply (gConnection,
                                           xcb_get_image (gConnection, XCB_IMAGE_FORMAT_Z_PIXMAP, _frame, 0, 0, static_cast<uint16_t> (kWidth), static_cast<uint16_t> (kHeight), 0xFFFFFFFF), nullptr);
        std::vector<uint32_t> pixels;
        if (reply && xcb_get_image_data_length (reply) >= kWidth * kHeight * 4)
        {
            const uint8_t* data = xcb_get_image_data (reply);
            pixels.resize (kWidth * kHeight);
            for (int i = 0; i < kWidth * kHeight; ++i)
                pixels[static_cast<size_t> (i)] = (data[4 * i + 2] << 16) | (data[4 * i + 1] << 8) | data[4 * i];
        }
        std::free (reply);
        _grabWidth = kWidth;
        return pixels;
    }

    // How many pixels in the area have the colour, leaving out the rows from skipTop to skipBottom.
    int count (uint32_t color, int left, int top, int right, int bottom, int skipTop = 0, int skipBottom = -1)
    {
        const auto pixels = grab ();
        int found = 0;
        const int height = static_cast<int> (pixels.size ()) / std::max (1, _grabWidth);
        right = std::min (right, _grabWidth);
        bottom = std::min (bottom, height);
        for (int y = top; y < bottom; ++y)
        {
            if (y >= skipTop && y <= skipBottom)
                continue;
            for (int x = left; x < right; ++x)
                found += pixels[static_cast<size_t> (y * _grabWidth + x)] == color ? 1 : 0;
        }
        return found;
    }

    // Saves the panel as a PPM file in the folder that LOOPTEXTURER_SCREENSHOTS names, if it's set.
    void save (const char* name)
    {
        const char* folder = std::getenv ("LOOPTEXTURER_SCREENSHOTS");
        if (!folder)
            return;
        const auto pixels = grab ();
        std::ofstream file (fs::path (folder) / (std::string (name) + ".ppm"), std::ios::binary);
        file << "P6\n" << _grabWidth << " " << pixels.size () / static_cast<size_t> (std::max (1, _grabWidth)) << "\n255\n";
        for (const uint32_t pixel : pixels)
        {
            const char rgb[] = { static_cast<char> (pixel >> 16), static_cast<char> (pixel >> 8), static_cast<char> (pixel) };
            file.write (rgb, 3);
        }
    }

    int countOnButton (uint32_t color, int x) { return count (color, x - 34, kButtonY - 9, x + 34, kButtonY + 9); }
    // The waveform's pixels away from its middle line, so a flat line doesn't count.
    int countWave () { return count (kWaveColor, kWaveLeft, kWaveTop, kWaveRight, kWaveBottom, kWaveMiddle - 2, kWaveMiddle + 2); }

    UiHost host;

private:
    void sendMouse (uint8_t type, int x, int y, uint8_t button, uint16_t state)
    {
        xcb_button_press_event_t event {};
        event.response_type = type;
        event.detail = button;
        event.time = XCB_CURRENT_TIME;
        event.root = gScreen->root;
        event.event = _frame;
        event.event_x = static_cast<int16_t> (x);
        event.event_y = static_cast<int16_t> (y);
        event.state = state;
        event.same_screen = 1;
        xcb_send_event (gConnection, 0, _frame, 0, reinterpret_cast<const char*> (&event));
        xcb_flush (gConnection);
    }

    xcb_window_t _window;
    int _slot { 0 };
    xcb_window_t _frame { 0 };
    LV2UI_Handle _handle { nullptr };
    LV2UI_Widget _widget { nullptr };
    int _grabWidth { 0 };
};

double Plugin::play (bool withSine, Panel* panel)
{
    if (panel && !panel->isValid ())
        panel = nullptr;

    for (int i = 0; i < kBlock; ++i)
    {
        const float value = withSine ? static_cast<float> (0.5 * std::sin (_phase)) : 0.0f;
        _phase += 2.0 * 3.14159265358979 * 220.0 / kSampleRate;
        _audio[0][static_cast<size_t> (i)] = value;
        _audio[1][static_cast<size_t> (i)] = value;
    }

    fillInput ();
    auto* output = reinterpret_cast<LV2_Atom_Sequence*> (_output.data ());
    output->atom.type = gUrids.get (LV2_ATOM__Chunk);
    output->atom.size = static_cast<uint32_t> (_output.size () - sizeof (LV2_Atom));
    gPlugin->run (_instance, kBlock);

    if (output->atom.type != gUrids.get (LV2_ATOM__Sequence) || output->atom.size > _output.size () - sizeof (LV2_Atom))
        ++badOutputs;
    else
    {
        LV2_ATOM_SEQUENCE_FOREACH (output, event)
        {
            ++messagesOut;
            const auto* bytes = reinterpret_cast<const char*> (&event->body);
            const std::string body (bytes, sizeof (LV2_Atom) + event->body.size);
            const bool isCopy = body.find ("LooperLv2") != std::string::npos;
            if (panel && forwardsToPanel && !(dropsCopies && isCopy))
                panel->portEvent (portIndex ("ui_out"), static_cast<uint32_t> (sizeof (LV2_Atom)) + event->body.size,
                                  gUrids.get (LV2_ATOM__eventTransfer), &event->body);
        }
    }
    if (panel)
        panel->idle ();

    double sum = 0.0;
    for (int i = 0; i < kBlock; ++i)
        sum += _audio[2][static_cast<size_t> (i)] * _audio[2][static_cast<size_t> (i)];

    // About real time, because the plug-in's message thread and the panel's timers run on the clock.
    std::this_thread::sleep_for (std::chrono::microseconds (static_cast<int> (1e6 * kBlock / kSampleRate)));
    return std::sqrt (sum / kBlock);
}

// Free running at its default length of 2 seconds, where Rec moves on to overdubbing by itself.
double recordLoop (Plugin& plugin, Panel& panel)
{
    panel.click (kRecX, kButtonY);
    plugin.playFor (2.6, true, &panel);
    return plugin.playFor (0.5, false, &panel);
}

void testFullHost ()
{
    Plugin plugin;
    Panel panel (&plugin, true);
    check (plugin.isValid () && panel.isValid (), "the plug-in and its panel open");
    if (!plugin.isValid () || !panel.isValid ())
        return;

    plugin.playFor (0.5, false, &panel);
    check (panel.host.messagesIn > 0, "the panel says hello to the plug-in as it opens");
    check (plugin.messagesOut > 0, "the plug-in sends the panel messages");
    check (panel.countOnButton (kRecordColor, kRecX) == 0, "Rec starts unlit");

    panel.click (kRecX, kButtonY);
    plugin.playFor (0.5, true, &panel);
    panel.save ("recording");
    check (panel.countOnButton (kRecordColor, kRecX) > 0, "Rec lights up while recording, from the plug-in's status");
    plugin.playFor (2.1, true, &panel);

    const double loop = plugin.playFor (0.5, false, &panel);
    check (loop > 0.05, "the Rec button recorded a loop that plays back: RMS " + std::to_string (loop));
    panel.save ("recorded");
    const int wave = panel.countWave ();
    check (wave > 5000, "the panel draws the loop's waveform: " + std::to_string (wave) + " pixels");

    // Another panel, or one after a restore, has to have the whole of it, give or take the playhead.
    const auto isWhole = [wave] (int pixels) { return pixels > wave * 95 / 100; };

    panel.click (kStopX, kButtonY);
    plugin.playFor (0.3, false, &panel);
    const double stopped = plugin.playFor (0.3, false, &panel);
    check (stopped < 1e-6, "the Stop button silences the loop: RMS " + std::to_string (stopped));
    check (panel.countOnButton (kPlayColor, kPlayX) == 0, "  and Play isn't lit");

    panel.click (kPlayX, kButtonY);
    plugin.playFor (0.3, false, &panel);
    const double playing = plugin.playFor (0.3, false, &panel);
    check (playing > 0.05, "the Play button brings it back: RMS " + std::to_string (playing));
    check (panel.countOnButton (kPlayColor, kPlayX) > 0, "  and Play lights up");
    check (plugin.badOutputs == 0 && panel.host.badWrites == 0, "every message is a whole atom in a sequence");

    // A panel opened later gets the whole waveform at once.
    {
        Panel second (&plugin, true);
        plugin.playFor (1.0, false, &second);
        second.save ("second");
        check (isWhole (second.countWave ()), "a panel opened onto a playing loop draws all its waveform: " + std::to_string (second.countWave ()) + " pixels");
    }

    // Damaged messages, both ways, change nothing.
    {
        std::mt19937 random (7);
        for (int i = 0; i < 200; ++i)
        {
            std::vector<uint8_t> junk (8 + random () % 300);
            for (auto& byte : junk)
                byte = static_cast<uint8_t> (random ());
            auto* atom = reinterpret_cast<LV2_Atom*> (junk.data ());
            atom->size = static_cast<uint32_t> (junk.size () - sizeof (LV2_Atom));
            if (i % 2 == 0)
            {
                // An object of the plug-in's own type, with junk inside.
                atom->type = gUrids.get (LV2_ATOM__Object);
                if (junk.size () >= sizeof (LV2_Atom_Object))
                    reinterpret_cast<LV2_Atom_Object*> (atom)->body.otype = gUrids.get ("https://github.com/HadronycheAudio/LoopTexturer#message");
                if (i % 4 == 0 && junk.size () >= sizeof (LV2_Atom_Object) + sizeof (LV2_Atom_Property_Body))
                {
                    auto* property = reinterpret_cast<LV2_Atom_Property_Body*> (reinterpret_cast<LV2_Atom_Object*> (atom) + 1);
                    property->key = gUrids.get ("https://github.com/HadronycheAudio/LoopTexturer#bytes");
                    property->value.type = gUrids.get (LV2_ATOM__Chunk);
                    property->value.size = static_cast<uint32_t> (junk.size () - sizeof (LV2_Atom_Object) - sizeof (LV2_Atom_Property_Body)) - (i % 8 == 0 ? 0 : 1);
                }
            }
            plugin.queueAtom (junk.data (), static_cast<uint32_t> (junk.size ()));
            panel.portEvent (portIndex ("ui_out"), static_cast<uint32_t> (junk.size ()), gUrids.get (LV2_ATOM__eventTransfer), junk.data ());
            panel.portEvent (portIndex ("ui_out"), static_cast<uint32_t> (junk.size () / 2), gUrids.get (LV2_ATOM__eventTransfer), junk.data ());
        }
        const double after = plugin.playFor (0.5, false, &panel);
        check (after > 0.05, "after 200 damaged messages each way, the loop still plays: RMS " + std::to_string (after));
        check (isWhole (panel.countWave ()), "  and the panel still draws it");
    }

    // A state saved with the loop brings it back in another instance, and to its panel.
    SavedState state = plugin.save ();
    {
        Plugin restored;
        check (restored.restore (state), "a state with the loop restores");
        Panel restoredPanel (&restored, true);
        restored.playFor (1.0, false, &restoredPanel);
        restoredPanel.save ("restored");
        check (isWhole (restoredPanel.countWave ()), "  and its panel draws all the restored loop: " + std::to_string (restoredPanel.countWave ()) + " pixels");
        restoredPanel.click (kPlayX, kButtonY);
        restored.playFor (0.3, false, &restoredPanel);
        const double played = restored.playFor (0.3, false, &restoredPanel);
        check (played > 0.05, "  which plays: RMS " + std::to_string (played));
    }

    panel.click (kClearX, kButtonY);
    plugin.playFor (0.5, false, &panel);
    panel.save ("cleared");
    check (panel.countWave () == 0, "the Clear button empties the waveform");
}

// Commands reach the plug-in, and everything but the copies of the looper's state comes back. The panel mustn't show the
// looper's state, which proves that it doesn't read the plug-in's memory, and that the processor's own introduction,
// which would let it, never reaches it. The panel opens before the plug-in's first block, which sends that.
void testPanelOnlyKnowsMessages ()
{
    Plugin plugin;
    plugin.dropsCopies = true;
    Panel panel (&plugin, true);
    if (!plugin.isValid () || !panel.isValid ())
    {
        check (false, "a panel the plug-in can't reach opens");
        return;
    }

    plugin.playFor (0.3, false, &panel);

    // A panel that can't send at all is the VST3's own panel with no processor, which lv2-panel compares with it.
    {
        Plugin other;
        Panel unconnected (&other, false);
        other.playFor (0.3, false, &unconnected);
        check (unconnected.isValid () && panel.grab () == unconnected.grab (), "without the copies, the panel looks like one with no processor");
    }

    panel.click (kRecX, kButtonY);
    plugin.playFor (0.5, true, &panel);
    check (panel.countOnButton (kRecordColor, kRecX) == 0, "  and Rec stays unlit while the plug-in records");
    check (plugin.messagesOut > 0, "  although the plug-in sends messages");
    plugin.playFor (2.1, true, &panel);
    const double loop = plugin.playFor (0.5, false, &panel);
    check (loop > 0.05, "  although the plug-in recorded the loop: RMS " + std::to_string (loop));
    check (panel.countWave () == 0, "  and the panel draws no waveform");
}

// Without a URID map, a panel can't make atoms. It opens, and its buttons send nothing.
void testPanelWithoutMap ()
{
    Plugin plugin;
    Panel panel (&plugin, false);
    check (panel.isValid (), "a panel opens for a host that gives it no URID map");
    if (!panel.isValid ())
        return;

    panel.click (kRecX, kButtonY);
    plugin.playFor (2.0, true, &panel);
    plugin.playFor (1.0, false, &panel); // The chain's tail.
    const double after = plugin.playFor (0.5, false, &panel);
    check (panel.host.messagesIn == 0 && panel.host.badWrites == 0, "  and writes nothing when Rec is clicked");
    check (after < 1e-6, "  so nothing records: RMS " + std::to_string (after));
}

} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "Usage: %s <LV2 bundle folder>\n", argv[0]);
        return 2;
    }

    const fs::path bundle = argv[1];
    readTurtle (bundle / "LoopTexturer.ttl");
    for (const char* symbol : { "in_left", "in_right", "out_left", "out_right", "events", "latency", "ui_in", "ui_out" })
    {
        if (gPorts.find (symbol) == gPorts.end ())
        {
            std::fprintf (stderr, "No port %s in the Turtle\n", symbol);
            return 1;
        }
    }

    void* library = dlopen ((bundle / "LoopTexturer.so").c_str (), RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf (stderr, "%s\n", dlerror ());
        return 1;
    }
    const auto pluginEntry = reinterpret_cast<const LV2_Descriptor* (*) (uint32_t)> (dlsym (library, "lv2_descriptor"));
    const auto uiEntry = reinterpret_cast<const LV2UI_Descriptor* (*) (uint32_t)> (dlsym (library, "lv2ui_descriptor"));
    gPlugin = pluginEntry ? pluginEntry (0) : nullptr;
    gUi = uiEntry ? uiEntry (0) : nullptr;
    gIdle = gUi ? static_cast<const LV2UI_Idle_Interface*> (gUi->extension_data (LV2_UI__idleInterface)) : nullptr;
    gConnection = xcb_connect (nullptr, nullptr);
    if (!gPlugin || !gUi || !gIdle || xcb_connection_has_error (gConnection))
    {
        std::fprintf (stderr, "Couldn't load the plug-in, or reach the X server\n");
        return 1;
    }
    gScreen = xcb_setup_roots_iterator (xcb_get_setup (gConnection)).data;

    testFullHost ();
    testPanelOnlyKnowsMessages ();
    testPanelWithoutMap ();

    xcb_disconnect (gConnection);
    std::printf ("%d failure(s)\n", gFailures);
    // Not unloading, as a host often doesn't: VSTGUI's statics outlive this point.
    return gFailures == 0 ? 0 : 1;
}
