#include "LoopExport.h"

#include "dsp/Saturator.h"
#include "dsp/Smoother.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace LoopTexturer {

namespace {

constexpr int kPreRollFrames = 4096;

// A permanent location, because the DAW may keep referencing the file after the drop.
fs::path getExportFolder ()
{
#if defined(_WIN32)
    if (const wchar_t* localAppData = _wgetenv (L"LOCALAPPDATA"))
        return fs::path (localAppData) / "LoopTexturer" / "Exports";
#endif
    std::error_code error;
    return fs::temp_directory_path (error) / "LoopTexturer" / "Exports";
}

std::string makeFileName ()
{
    const auto now = std::chrono::system_clock::now ();
    const std::time_t seconds = std::chrono::system_clock::to_time_t (now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch ()).count () % 1000;

    std::tm local {};
#if defined(_WIN32)
    localtime_s (&local, &seconds);
#else
    localtime_r (&seconds, &local);
#endif

    char name[64];
    std::snprintf (name, sizeof (name), "Loop %04d%02d%02d-%02d%02d%02d-%03d.wav", local.tm_year + 1900, local.tm_mon + 1,
                   local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, static_cast<int> (millis));
    return name;
}

void appendU16 (std::vector<char>& out, uint16_t value)
{
    out.push_back (static_cast<char> (value & 0xFF));
    out.push_back (static_cast<char> (value >> 8));
}

void appendU32 (std::vector<char>& out, uint32_t value)
{
    appendU16 (out, static_cast<uint16_t> (value & 0xFFFF));
    appendU16 (out, static_cast<uint16_t> (value >> 16));
}

void appendTag (std::vector<char>& out, const char* tag)
{
    out.insert (out.end (), tag, tag + 4);
}

// A 32-bit float file, which keeps peaks above full scale intact.
bool writeWav (const fs::path& path, const std::vector<float>& interleaved, double sampleRate)
{
    constexpr uint16_t channels = 2;
    constexpr uint16_t bytesPerFrame = channels * 4;
    const auto rate = static_cast<uint32_t> (std::lround (sampleRate));
    const auto dataSize = static_cast<uint32_t> (interleaved.size () * sizeof (float));
    const auto frameCount = static_cast<uint32_t> (interleaved.size () / channels);

    std::vector<char> header;
    appendTag (header, "RIFF");
    appendU32 (header, 4 + (8 + 16) + (8 + 4) + 8 + dataSize);
    appendTag (header, "WAVE");

    appendTag (header, "fmt ");
    appendU32 (header, 16);
    appendU16 (header, 3); // IEEE float
    appendU16 (header, channels);
    appendU32 (header, rate);
    appendU32 (header, rate * bytesPerFrame);
    appendU16 (header, bytesPerFrame);
    appendU16 (header, 32);

    appendTag (header, "fact");
    appendU32 (header, 4);
    appendU32 (header, frameCount);

    appendTag (header, "data");
    appendU32 (header, dataSize);

    std::ofstream file (path, std::ios::binary | std::ios::trunc);
    file.write (header.data (), static_cast<std::streamsize> (header.size ()));
    file.write (reinterpret_cast<const char*> (interleaved.data ()), static_cast<std::streamsize> (dataSize));
    file.close ();
    return file.good ();
}

} // namespace

std::string exportLoop (const LooperBridge& bridge, const LoopExportSettings& settings)
{
    const auto& status = bridge.status;
    const auto tape = bridge.getActiveTape ();
    const int64_t loopFrames = status.loopFrames.load ();
    const double sampleRate = status.sampleRate.load ();
    if (!tape || loopFrames <= 0 || sampleRate <= 0.0 || !status.hasContent.load ())
        return {};

    // The number of frames the loop takes to play at its current speed.
    int64_t outputFrames = loopFrames;
    if (status.isSyncMode.load ())
    {
        const double tempo = status.tempo.load ();
        if (tempo > 0.0)
            outputFrames = std::llround (status.loopQuarters.load () * 60.0 / tempo * sampleRate);
    }
    else
    {
        outputFrames = std::llround (settings.freeLengthSeconds * sampleRate);
    }
    if (outputFrames <= 0)
        return {};

    const double speed = static_cast<double> (loopFrames) / static_cast<double> (outputFrames);
    const double width = std::max (0.0, speed - status.writeSpeed.load ());

    const auto readFrame = [&] (int64_t frame, float& left, float& right) {
        const int64_t wrapped = ((frame % outputFrames) + outputFrames) % outputFrames;
        const double head = static_cast<double> (wrapped) * speed;
        left = tape->read (0, head, loopFrames, width);
        right = tape->read (1, head, loopFrames, width);
    };

    std::vector<float> interleaved (static_cast<size_t> (outputFrames) * 2);

    if (settings.postSaturatorOn)
    {
        Saturator saturator;
        saturator.prepare (sampleRate, dbToGain (settings.postSaturatorDriveDb), static_cast<float> (settings.postSaturatorMix));
        saturator.setMode (static_cast<Saturator::Mode> (settings.postSaturatorMode));

        // Starting a little before the loop lets the saturator's filters settle on what plays just
        // before the loop's start, so that the file loops seamlessly. Its latency is skipped too.
        const int64_t preRoll = std::min<int64_t> (kPreRollFrames, outputFrames);
        const int64_t skipped = preRoll + Saturator::kLatency;
        const int64_t total = skipped + outputFrames;
        for (int64_t i = 0; i < total; ++i)
        {
            float left = 0.0f;
            float right = 0.0f;
            readFrame (i - preRoll, left, right);
            saturator.process (left, right);
            if (i >= skipped)
            {
                interleaved[static_cast<size_t> (i - skipped) * 2] = left;
                interleaved[static_cast<size_t> (i - skipped) * 2 + 1] = right;
            }
        }
    }
    else
    {
        for (int64_t i = 0; i < outputFrames; ++i)
            readFrame (i, interleaved[static_cast<size_t> (i) * 2], interleaved[static_cast<size_t> (i) * 2 + 1]);
    }

    const auto folder = getExportFolder ();
    std::error_code error;
    fs::create_directories (folder, error);
    if (error)
        return {};

    const auto path = folder / makeFileName ();
    return writeWav (path, interleaved, sampleRate) ? path.u8string () : std::string ();
}

} // namespace LoopTexturer
