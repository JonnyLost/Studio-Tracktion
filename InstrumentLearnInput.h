// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "InstrumentMidiBinding.h"
#include <algorithm>
#include <memory>

namespace studio::designer
{
// Reuse the same Tracktion device listener mechanism as KeyboardInputActivity.
// Learn is observational; it does not open ports, mute or reroute MIDI.
class LearnInput final : private tracktion::PhysicalMidiInputDevice::Listener
{
public:
    ~LearnInput() override {detach();}
    bool attach(tracktion::Edit& edit,tracktion::EditItemID target,tracktion::DeviceManager& manager)
    {
        detach();
        for(auto* input:edit.getAllInputDevices())
        {
            if(input==nullptr||!input->getTargets().contains(target)||!input->getInputDevice().isEnabled()
                ||input->getInputDevice().getDeviceType()!=tracktion::InputDevice::physicalMidiDevice)continue;
            const auto id=input->getInputDevice().getDeviceID();
            for(const auto& device:manager.getMidiInDevices())
                if(device!=nullptr&&device->getDeviceID()==id)
                    if(auto physical=std::dynamic_pointer_cast<tracktion::PhysicalMidiInputDevice>(device))
                        if(std::find(devices.begin(),devices.end(),physical)==devices.end())
                            devices.push_back(std::move(physical));
        }
        if(devices.empty())return false;
        mailbox.arm();
        for(auto& device:devices)device->addListener(this);
        return true;
    }
    void detach()
    {
        mailbox.cancel();
        for(auto& device:devices)device->removeListener(this);
        devices.clear();
    }
    std::optional<Binding> poll(){return mailbox.consume();}
private:
    void handleIncomingMidiMessage(const juce::MidiMessage& m) override {mailbox.capture(m);}
    LearnMailbox mailbox;
    std::vector<std::shared_ptr<tracktion::PhysicalMidiInputDevice>> devices;
};
}
