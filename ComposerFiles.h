// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <filesystem>

namespace studio
{
inline juce::Result publishNewFile (const juce::File& staged, const juce::File& target)
{
    std::error_code error;
    std::filesystem::create_hard_link (std::filesystem::u8path (staged.getFullPathName().toRawUTF8()),
                                       std::filesystem::u8path (target.getFullPathName().toRawUTF8()), error);
    if (! error)
        return juce::Result::ok();
    return juce::Result::fail (target.getFileName() + ": " + juce::String (error.message()));
}
}
