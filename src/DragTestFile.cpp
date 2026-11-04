#include "DragTestFile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace LoopTexturer {

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kChannels = 2;
constexpr uint16_t kBitsPerSample = 16;
constexpr uint32_t kFrames = kSampleRate * 2;
constexpr uint32_t kFadeFrames = kSampleRate / 100; // Avoids clicks at the start and end.

// A permanent location, because the DAW may keep referencing the file after the drop.
fs::path getOutputFolder ()
{
#if defined(_WIN32)
    if (const wchar_t* localAppData = _wgetenv (L"LOCALAPPDATA"))
        return fs::path (localAppData) / "LoopTexturer";
#endif
    std::error_code error;
    return fs::temp_directory_path (error) / "LoopTexturer";
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

std::vector<char> createWavData ()
{
    constexpr uint16_t blockAlign = kChannels * kBitsPerSample / 8;
    constexpr uint32_t dataSize = kFrames * blockAlign;

    std::vector<char> wav;
    wav.reserve (44 + dataSize);

    appendTag (wav, "RIFF");
    appendU32 (wav, 36 + dataSize);
    appendTag (wav, "WAVE");

    appendTag (wav, "fmt ");
    appendU32 (wav, 16);
    appendU16 (wav, 1); // PCM
    appendU16 (wav, kChannels);
    appendU32 (wav, kSampleRate);
    appendU32 (wav, kSampleRate * blockAlign);
    appendU16 (wav, blockAlign);
    appendU16 (wav, kBitsPerSample);

    appendTag (wav, "data");
    appendU32 (wav, dataSize);

    constexpr double twoPi = 6.283185307179586;
    for (uint32_t frame = 0; frame < kFrames; ++frame)
    {
        const auto distanceFromEdge = std::min (frame, kFrames - 1 - frame);
        const double fade = std::min (1.0, static_cast<double> (distanceFromEdge) / kFadeFrames);
        const double sample = 0.5 * fade * std::sin (twoPi * 440.0 * frame / kSampleRate);
        const auto value = static_cast<int16_t> (std::lround (sample * 32767.0));

        for (uint16_t channel = 0; channel < kChannels; ++channel)
            appendU16 (wav, static_cast<uint16_t> (value));
    }

    return wav;
}

} // namespace

std::string ensureDragTestFile ()
{
    const auto folder = getOutputFolder ();
    const auto path = folder / "drag-test.wav";

    std::error_code error;
    if (fs::exists (path, error))
        return path.u8string ();

    fs::create_directories (folder, error);
    if (error)
        return {};

    // Written under a temporary name so that a failed write never leaves a partial WAV behind.
    auto temporaryPath = path;
    temporaryPath += ".tmp";
    {
        const auto wav = createWavData ();
        std::ofstream file (temporaryPath, std::ios::binary | std::ios::trunc);
        file.write (wav.data (), static_cast<std::streamsize> (wav.size ()));
        file.close ();
        if (!file)
        {
            fs::remove (temporaryPath, error);
            return {};
        }
    }

    fs::rename (temporaryPath, path, error);
    if (error)
    {
        fs::remove (temporaryPath, error);
        return {};
    }
    return path.u8string ();
}

} // namespace LoopTexturer
