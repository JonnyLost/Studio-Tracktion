// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "OstinatoAudition.h"
#include <memory>
namespace studio::ostinato
{
// Passive listener on an explicitly selected existing physical input.
// A listener does not consume MIDI. Host must verify input has no destinations
// and automatic MIDI focus is off before attaching and while active.
class ConductingInput final:private tracktion::PhysicalMidiInputDevice::Listener
{
public:
    ~ConductingInput() override{detach();}
    void attach(std::shared_ptr<tracktion::PhysicalMidiInputDevice> d,int ch)
    {
        detach();channel=ch;device=std::move(d);
        if(device)device->addListener(this);
    }
    void detach()
    {
        if(device)device->removeListener(this);
        device.reset();mailbox.reset();
    }
    bool attached()const{return bool(device);}
    bool matches(const std::shared_ptr<tracktion::PhysicalMidiInputDevice>& d)const{return device==d;}
    ConductingValues consume(ConductingValues current){return mailbox.consume(current);}
private:
    void handleIncomingMidiMessage(const juce::MidiMessage& m)override
    {
        if(m.isController()&&m.getChannel()==channel)
            mailbox.receive(m.getControllerNumber(),m.getControllerValue());
    }
    std::shared_ptr<tracktion::PhysicalMidiInputDevice> device;
    ConductingMailbox mailbox;
    int channel=1;
};
}
