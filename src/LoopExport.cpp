#include "LoopExport.h"

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

// A permanent location, because the DAW may keep referencing the file after the drop.
fs::path getExportFolder ()
{
#if defined(_WIN32)
    if (const wchar_t* localAppData = _wgetenv (L"LOCALAPPDATA"))
        return fs::path (localAppData) / "LoopTexturer" / "Exports";
#elif defined(__APPLE__)
    if (const char* home = std::getenv ("HOME"))
        return fs::path (home) / "Library" / "Application Support" / "LoopTexturer" / "Exports";
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
    const double sampleRate = bridge.getStatus ().sampleRate;
    const int64_t outputFrames = Ffi::looptexturer_render_frames (bridge.raw (), &settings);
    if (outputFrames <= 0 || sampleRate <= 0.0)
        return {};

    std::vector<float> interleaved (static_cast<size_t> (outputFrames) * 2);
    if (!Ffi::looptexturer_render (bridge.raw (), &settings, interleaved.data (), outputFrames))
        return {};

    const auto folder = getExportFolder ();
    std::error_code error;
    fs::create_directories (folder, error);
    if (error)
        return {};

    const auto path = folder / makeFileName ();
    return writeWav (path, interleaved, sampleRate) ? path.u8string () : std::string ();
}

} // namespace LoopTexturer
