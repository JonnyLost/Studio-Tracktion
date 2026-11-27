// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <optional>

namespace studio::sampler
{
inline constexpr auto audioExtensions = "wav;wave;aif;aiff;aifc;flac;caf";

inline std::unique_ptr<juce::AudioFormatReader> openSampleReader (const juce::File& file)
{
    using namespace juce;
    AudioFormatManager formats;
    formats.registerFormat (new WavAudioFormat(), true);
    formats.registerFormat (new AiffAudioFormat(), false);
   #if JUCE_USE_FLAC
    formats.registerFormat (new FlacAudioFormat(), false);
   #endif
   #if JUCE_MAC
    if (file.hasFileExtension ("caf"))
        formats.registerFormat (new CoreAudioFormat (CoreAudioFormat::StreamKind::kCaf), false);
   #endif
    return std::unique_ptr<AudioFormatReader> (formats.createReaderFor (file));
}

struct SoundFontSample
{
    juce::int64 offset = 0; // 16-bit, little-endian mono PCM within the bank's smpl chunk
    int frames = 0, rate = 0;
    bool operator== (const SoundFontSample&) const = default;
};

struct SoundFontChunk
{
    juce::int64 offset = 0, size = 0;
    explicit operator bool() const { return offset > 0; }
};

// Inspect a bounded RIFF list without loading the sample data.
inline SoundFontChunk findRiffChunk (juce::InputStream& stream, juce::int64 start, juce::int64 end,
                                     const char* id, const char* listType = nullptr)
{
    for (auto cursor = start; cursor <= end - 8;)
    {
        char name[4] {};
        if (! stream.setPosition (cursor) || stream.read (name, 4) != 4) break;
        const auto length = static_cast<juce::int64> (static_cast<std::uint32_t> (stream.readInt()));
        const auto data = cursor + 8;
        if (length > end - data) return {};
        if (std::memcmp (name, id, 4) == 0)
        {
            if (listType == nullptr) return { data, length };
            if (length >= 4 && stream.read (name, 4) == 4 && std::memcmp (name, listType, 4) == 0)
                return { data + 4, length - 4 };
        }
        cursor = data + length + (length & 1);
    }
    return {};
}

inline juce::int64 soundFontEnd (juce::InputStream& stream)
{
    char id[4] {};
    if (! stream.setPosition (0) || stream.read (id, 4) != 4 || std::memcmp (id, "RIFF", 4) != 0) return 0;
    const auto end = 8 + static_cast<juce::int64> (static_cast<std::uint32_t> (stream.readInt()));
    if (end < 12 || end > stream.getTotalLength() || stream.read (id, 4) != 4 || std::memcmp (id, "sfbk", 4) != 0)
        return 0;
    return end;
}

inline SoundFontChunk soundFontPCM (juce::InputStream& stream, juce::int64 end)
{
    const auto list = findRiffChunk (stream, 12, end, "LIST", "sdta");
    return list ? findRiffChunk (stream, list.offset, list.offset + list.size, "smpl") : SoundFontChunk {};
}
}
