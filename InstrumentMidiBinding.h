// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentProfile.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <optional>

namespace studio::designer
{
// A one-shot, nonblocking mailbox: audio/MIDI producers never allocate,
// lock, or touch the profile/UI. Only the message thread decodes a result.
class LearnMailbox
{
public:
    void arm() noexcept { state.store(armed, std::memory_order_release); }
    void cancel() noexcept { state.store(0, std::memory_order_release); }
    void capture(const juce::MidiMessage& m) noexcept
    {
        if(state.load(std::memory_order_relaxed)!=armed)return;
        unsigned kind=0, number=0, value=0;
        if(m.isNoteOn()){kind=1;number=(unsigned)m.getNoteNumber();value=(unsigned)m.getVelocity();}
        else if(m.isController() && m.getControllerNumber()<120){kind=2;number=(unsigned)m.getControllerNumber();value=(unsigned)m.getControllerValue();}
        else if(m.isChannelPressure()){kind=3;value=(unsigned)m.getChannelPressureValue();}
        else if(m.isPitchWheel()){kind=4;value=(unsigned)m.getPitchWheelValue();}
        else return;
        const auto channel=m.getChannel();
        if(channel<1||channel>16)return;
        uint32_t expected=armed;
        const uint32_t packed=(kind<<26)|(uint32_t(channel)<<21)|(number<<14)|value;
        state.compare_exchange_strong(expected,packed,std::memory_order_release,std::memory_order_relaxed);
    }
    std::optional<Binding> consume()
    {
        auto x=state.load(std::memory_order_acquire);
        if(x==0||x==armed)return {};
        if(!state.compare_exchange_strong(x,0,std::memory_order_acq_rel))return {};
        Binding b;const auto k=x>>26;
        b.type=k==1?"note":k==2?"cc":k==3?"pressure":"bend";
        b.channel=int((x>>21)&31);b.number=int((x>>14)&127);b.value=int(x&16383);
        return b.valid()?std::optional<Binding>(b):std::nullopt;
    }
private:
    static constexpr uint32_t armed=1;
    std::atomic<uint32_t> state{0};
};

inline std::optional<juce::MidiMessage> auditionMessage(const Binding& b,int channel)
{
    if(!b.valid()||channel<1||channel>16)return {};
    const int ch=b.resolvedChannel(channel);
    if(b.type=="note")return juce::MidiMessage::noteOn(ch,b.number,(juce::uint8)b.value);
    if(b.type=="cc" && b.number<120)return juce::MidiMessage::controllerEvent(ch,b.number,b.value);
    if(b.type=="pressure")return juce::MidiMessage::channelPressureChange(ch,b.value);
    if(b.type=="bend")return juce::MidiMessage::pitchWheel(ch,b.value);
    return {};
}
inline std::optional<juce::MidiMessage> releaseMessage(const Binding& b,int channel)
{
    if(!b.valid()||channel<1||channel>16)return {};
    const int ch=b.resolvedChannel(channel);
    if(b.type=="note")return juce::MidiMessage::noteOff(ch,b.number);
    if(b.type=="bend")return juce::MidiMessage::pitchWheel(ch,8192);
    if(b.type=="pressure")return juce::MidiMessage::channelPressureChange(ch,0);
    if(b.type=="cc"&&b.number==64)return juce::MidiMessage::controllerEvent(ch,64,0);
    return {};
}
}
