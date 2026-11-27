// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "StudioKeyboardPerformanceState.h"
#include <array>
namespace studio
{
// Existing persisted Inspector values, shared by every audition surface.
// Sustain is never persisted. Runtime MIDI observations do not dirty the edit.
inline constexpr std::array<int,8> sharedCCNumbers { 1,2,7,11,64,71,74,91 };
inline constexpr std::array<int,8> sharedCCDefaults { 0,0,100,127,0,64,64,40 };
inline std::array<int,8> readSharedCC (const juce::ValueTree& state)
{
    auto values = sharedCCDefaults;
    auto tokens = juce::StringArray::fromTokens (state.getProperty ("studioAtelierCCState").toString(), ",", "");
    if (tokens.size() == 8)
        for (size_t i=0; i<values.size(); ++i)
            values[i] = i==4 ? 0 : juce::jlimit (0,127,tokens[(int)i].getIntValue());
    return values;
}
inline void writeSharedCC (juce::ValueTree state, int cc, int value)
{
    auto values = readSharedCC (state);
    for (size_t i=0; i<values.size(); ++i)
        if (sharedCCNumbers[i]==cc && cc!=64)
        {
            values[i]=juce::jlimit (0,127,value);
            juce::StringArray tokens;
            for (auto v:values) tokens.add (juce::String(v));
            state.setProperty ("studioAtelierCCState",tokens.joinIntoString(","),nullptr);
            return;
        }
}
inline KeyboardPerformanceValues sharedKeyboardValues (const juce::ValueTree& state)
{
    const auto v=readSharedCC(state);
    KeyboardPerformanceValues p;
    p.mod=v[0]; p.vol=v[2]; p.expr=v[3]; p.tone=v[6]; p.reverb=v[7];
    p.changed=((1u<<6)-1u) & ~KeyboardPerformanceValues::bit(KeyboardPerformanceValues::pitch);
    return p;
}
}
