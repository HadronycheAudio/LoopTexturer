// The real editor in an off-screen window, driven with mouse messages on its real controls, while
// audio runs. It covers what the other tests can't: the buttons, and dragging a loop out to a WAV.
//
// It doesn't touch the real mouse, and exports go to a temporary folder. Set the environment variable
// LOOPTEXTURER_SCREENSHOTS to a folder to also save the panel as BMP files at each step.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Panel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;

namespace LoopTexturer {
namespace Tests {

namespace {

// Positions on the panel, which follow the layout in src/Editor.cpp.
constexpr int kButtonY = 198;
constexpr int kRecX = 253;
constexpr int kOverdubX = 327;
constexpr int kPlayX = 401;
constexpr int kStopX = 475;
constexpr int kClearX = 549;
constexpr int kWaveformX = 600;
constexpr int kWaveformY = 260;

fs::path gExportRoot;

struct Wav
{
    int format { 0 };
    int channels { 0 };
    int sampleRate { 0 };
    int bits { 0 };
    Samples left;
};

Wav readWav (const fs::path& path)
{
    std::ifstream file (path, std::ios::binary);
    const std::vector<char> data ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char> ());

    Wav wav;
    size_t position = 12;
    while (position + 8 <= data.size ())
    {
        uint32_t size = 0;
        std::memcpy (&size, data.data () + position + 4, 4);
        const char* body = data.data () + position + 8;

        if (std::strncmp (data.data () + position, "fmt ", 4) == 0)
        {
            uint16_t format = 0;
            uint16_t channels = 0;
            uint32_t sampleRate = 0;
            uint16_t bits = 0;
            std::memcpy (&format, body, 2);
            std::memcpy (&channels, body + 2, 2);
            std::memcpy (&sampleRate, body + 4, 4);
            std::memcpy (&bits, body + 14, 2);
            wav.format = format;
            wav.channels = channels;
            wav.sampleRate = static_cast<int> (sampleRate);
            wav.bits = bits;
        }
        else if (std::strncmp (data.data () + position, "data", 4) == 0 && wav.channels > 0)
        {
            const size_t frames = size / sizeof (float) / static_cast<size_t> (wav.channels);
            wav.left.resize (frames);
            for (size_t i = 0; i < frames; ++i)
                std::memcpy (&wav.left[i], body + i * static_cast<size_t> (wav.channels) * sizeof (float), sizeof (float));
        }
        position += 8 + size + (size & 1);
    }
    return wav;
}

std::vector<fs::path> listExports ()
{
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator (gExportRoot, error))
    {
        if (entry.path ().extension () == ".wav")
            files.push_back (entry.path ());
    }
    std::sort (files.begin (), files.end ());
    return files;
}

// One off-screen editor, with the audio running alongside it.
class Panel
{
public:
    explicit Panel (Plug& plug) : _plug (plug), _window (plug.controller, L"LoopTexturerPanelTest"), _mainThread (GetCurrentThreadId ())
    {
        isOpen = _window.open ();

        // DoDragDrop only re-checks the mouse when input arrives, so a drag needs some.
        _mouseInput = std::thread ([this] {
            while (!_stopMouseInput)
            {
                Sleep (100);
                PostThreadMessageW (_mainThread, WM_MOUSEMOVE, 0, 0);
            }
        });
    }

    ~Panel ()
    {
        _stopMouseInput = true;
        _mouseInput.join ();
    }

    void click (int x, int y) { _window.click (x, y); }

    void drag ()
    {
        SendMessageW (_window.editorWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (kWaveformX, kWaveformY));
        pump ();
        SendMessageW (_window.editorWindow, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (kWaveformX + 40, kWaveformY + 30));
        pump ();
    }

    // Runs audio a block at a time with the UI alive, and returns the left output.
    Samples runAudio (double seconds, float level, bool withEnvelope = false)
    {
        Samples output;
        Samples in (kBlockSize);
        Samples out;
        const int blocks = static_cast<int> (seconds * kSampleRate / kBlockSize);
        for (int block = 0; block < blocks; ++block)
        {
            for (auto& sample : in)
            {
                _tonePhase += 300.0 / kSampleRate;
                _envelopePhase += 2.3 / kSampleRate;
                const double envelope = withEnvelope ? 0.5 + 0.5 * std::sin (6.283185307 * _envelopePhase) : 1.0;
                sample = static_cast<float> (level * envelope * std::sin (6.283185307 * _tonePhase));
            }
            _plug.runMono (in, out);
            output.insert (output.end (), out.begin (), out.end ());
            pump ();
        }
        return output;
    }

    void saveScreenshot (const char* name) { _window.saveScreenshot ("LOOPTEXTURER_SCREENSHOTS", name); }

    static void pump () { Hadronyche::TestHost::pump (); }

    bool isOpen { false };

private:
    Plug& _plug;
    PanelWindow _window;
    DWORD _mainThread;
    std::thread _mouseInput;
    std::atomic<bool> _stopMouseInput { false };
    double _tonePhase { 0.0 };
    double _envelopePhase { 0.0 };
};

// Drags the loop out and returns the new WAV. Fails the check if there isn't exactly one.
Wav dragOutLoop (Panel& panel)
{
    const auto before = listExports ().size ();
    panel.drag ();
    panel.runAudio (0.1, 0.0f);

    const auto files = listExports ();
    check ("the drag wrote a new WAV", files.size () == before + 1);
    return files.empty () ? Wav {} : readWav (files.back ());
}

void checkExport (const Wav& wav, double seconds, double hz, const char* name)
{
    const double length = static_cast<double> (wav.left.size ()) / std::max (wav.sampleRate, 1);
    const double frequency = wav.left.empty () ? 0.0 : measureFrequency (wav.left, wav.sampleRate);
    std::printf ("  %s: %.4f s, %.1f Hz\n", name, length, frequency);
    check ("it's a 48 kHz, 32-bit float, stereo WAV", wav.format == 3 && wav.sampleRate == 48000 && wav.bits == 32 && wav.channels == 2);
    check ("its length matches the current speed", std::abs (length - seconds) < 0.001);
    check ("its pitch matches the current speed", std::abs (frequency - hz) < hz * 0.02);
}

} // namespace

void useTemporaryExportFolder ()
{
    // Set before the plug-in loads, and in both places, because the plug-in's C runtime reads it.
    gExportRoot = fs::temp_directory_path () / ("LoopTexturerTests-" + std::to_string (GetCurrentProcessId ()));
    fs::create_directories (gExportRoot);
    SetEnvironmentVariableW (L"LOCALAPPDATA", gExportRoot.c_str ());
    _wputenv_s (L"LOCALAPPDATA", gExportRoot.c_str ());
}

void removeTemporaryExportFolder ()
{
    std::error_code error;
    fs::remove_all (gExportRoot, error);
}

void testPanel ()
{
    Plug plug;
    Panel panel (plug);
    check ("the editor opens", panel.isOpen);
    if (!panel.isOpen)
        return;

    // Tests run faster than real time, so this gives the panel's status a chance to catch up.
    panel.runAudio (0.2, 0.0f);

    const auto exportCount = listExports ().size ();
    panel.drag ();
    panel.runAudio (0.2, 0.0f);
    check ("dragging an empty loop writes no file", listExports ().size () == exportCount);
    panel.saveScreenshot ("1-empty");

    // Free running, at the default length of 2 seconds. Rec moves on to overdubbing by itself.
    panel.click (kRecX, kButtonY);
    panel.runAudio (0.6, 0.8f, true);
    panel.saveScreenshot ("2-recording");
    panel.runAudio (1.6, 0.8f, true);
    panel.saveScreenshot ("3-overdubbing");
    panel.click (kOverdubX, kButtonY);
    panel.runAudio (0.5, 0.0f);

    auto output = panel.runAudio (2.0, 0.0f);
    check ("the Rec button recorded a loop that plays back", rms (output) > 0.05);

    const auto plainExport = dragOutLoop (panel);
    checkExport (plainExport, 2.0, 300.0, "free running");

    const double seamStep = plainExport.left.empty () ? 1.0 : std::abs (plainExport.left.front () - plainExport.left.back ());
    double largestStep = 0.0;
    for (size_t i = 1; i < plainExport.left.size (); ++i)
        largestStep = std::max (largestStep, static_cast<double> (std::abs (plainExport.left[i] - plainExport.left[i - 1])));
    std::printf ("  step across the file's seam %.4f, largest step inside it %.4f\n", seamStep, largestStep);
    check ("the file loops without a click", seamStep <= largestStep);

    plug.set (kLooperFreeLength, 1.0);
    panel.runAudio (0.3, 0.0f);
    checkExport (dragOutLoop (panel), 1.0, 600.0, "at half the length");

    plug.set (kLooperFreeLength, 2.0);
    plug.set (kPostSatOn, 1);
    plug.set (kPostSatMode, 0);
    plug.set (kPostSatDrive, 24);
    panel.runAudio (0.3, 0.0f);
    const auto saturatedExport = dragOutLoop (panel);
    checkExport (saturatedExport, 2.0, 300.0, "through the post saturator");
    std::printf ("  level %.3f, compared with %.3f without the post saturator\n", rms (saturatedExport.left), rms (plainExport.left));
    check ("the post saturator is in the file", rms (saturatedExport.left) > rms (plainExport.left) * 1.2);

    plug.set (kPostSatOn, 0);
    plug.set (kPostSatDrive, 0);
    panel.runAudio (0.1, 0.0f);

    // Stop, Play and Clear.
    panel.click (kStopX, kButtonY);
    panel.runAudio (0.2, 0.0f);
    panel.saveScreenshot ("4-stopped");
    output = panel.runAudio (0.5, 0.0f);
    check ("the Stop button silences the loop", rms (output) < 1e-6);

    panel.click (kPlayX, kButtonY);
    panel.runAudio (0.1, 0.0f);
    output = panel.runAudio (1.0, 0.0f);
    check ("the Play button brings it back", rms (output) > 0.05);

    panel.click (kClearX, kButtonY);
    panel.runAudio (0.3, 0.0f);
    output = panel.runAudio (0.5, 0.0f);
    check ("the Clear button empties the loop", rms (output) < 1e-6);
    panel.saveScreenshot ("5-cleared");

    // Tempo sync, where one bar at 240 BPM is one second.
    useSyncMode (plug);
    Song song (plug, 240.0);
    song.locate (0);
    song.play (true);
    panel.runAudio (0.1, 0.0f);

    panel.click (kRecX, kButtonY);
    panel.runAudio (2.6, 0.8f, true);
    panel.click (kOverdubX, kButtonY);
    panel.runAudio (0.4, 0.0f);
    panel.saveScreenshot ("6-tempo-sync");
    checkExport (dragOutLoop (panel), 1.0, 300.0, "tempo sync at 240 BPM");

    // The same loop at half the tempo plays for twice as long, an octave down.
    song.context.tempo = 120.0;
    panel.runAudio (0.2, 0.0f);
    checkExport (dragOutLoop (panel), 2.0, 150.0, "tempo sync at 120 BPM");

    plug.context = nullptr;
}

} // namespace Tests
} // namespace LoopTexturer
