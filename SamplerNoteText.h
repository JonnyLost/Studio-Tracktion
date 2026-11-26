// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <optional>

namespace studio::sampler
{
inline juce::String musicalNoteText(int note)
{
    return juce::MidiMessage::getMidiNoteName(note,true,true,4)+" ("+juce::String(note)+")";
}
inline std::optional<int> parseMusicalNote(juce::String text)
{
    text=text.trim();
    if(text.isNotEmpty()&&text.length()<=3&&text.containsOnly("0123456789"))
    {
        const auto n=text.getIntValue();if(n<=127)return n;return {};
    }
    for(int note=0;note<128;++note)
    {
        const auto sharp=juce::MidiMessage::getMidiNoteName(note,true,true,4);
        const auto flat=juce::MidiMessage::getMidiNoteName(note,false,true,4);
        const auto suffix=" ("+juce::String(note)+")";
        if(text.equalsIgnoreCase(sharp)||text.equalsIgnoreCase(flat)||
           text.equalsIgnoreCase(sharp+suffix)||text.equalsIgnoreCase(flat+suffix))return note;
    }
    return {};
}
inline void useMusicalNotes(juce::Slider& slider)
{
    slider.textFromValueFunction=[](double v){return musicalNoteText(juce::roundToInt(v));};
    slider.valueFromTextFunction=[&slider](const juce::String& s)
    {return static_cast<double>(parseMusicalNote(s).value_or(juce::roundToInt(slider.getValue())));};
    slider.updateText();
}
}
