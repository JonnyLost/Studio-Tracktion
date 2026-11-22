// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "MidiFocusPolicy.h"
#include <array>
#include <atomic>
#include <memory>

namespace studio
{
namespace te = tracktion;

// Owns at most one temporary live-monitor target. MIDI recording remains
// explicitly armed by the user, independently of the selected track.
class MidiPerformanceFocus final : private te::PhysicalMidiInputDevice::Listener
{
public:
    ~MidiPerformanceFocus() override { detachListener(); }

    void release (te::Edit* edit)
    {
        if (owned && edit != nullptr)
            if (auto* input = findInput (*edit, deviceID))
            {
                const auto targets = input->getTargets();
                // Do not overwrite any routing/arming the user changed since
                // Studio installed the temporary audition destination.
                if (targets.size() == 1 && targets[0] == target
                    && ! input->isRecording() && ! input->isRecordingEnabled (target))
                {
                    if (previousTarget.isValid() && te::findAudioTrackForID (*edit, previousTarget) != nullptr)
                        [[maybe_unused]] auto restored = input->setTarget (previousTarget, true, nullptr, 0);
                    else
                        [[maybe_unused]] auto removed = input->removeTarget (target, nullptr);
                }
                if (input->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on
                    && previousMonitor != te::InputDevice::MonitorMode::on)
                    input->getInputDevice().setMonitorMode (previousMonitor);
            }

        clearOwnership();
    }

    // Called on Studio's message thread, only while the project is stable.
    // A default physical input wins; without one, exactly one enabled physical
    // keyboard must exist, so arbitrary devices aren't captured unexpectedly.
    juce::String follow (te::Edit& edit, te::AudioTrack* selected, bool enabled, bool stable,
                         te::DeviceManager& devices)
    {
        if (! enabled)
        {
            if (owned && stable && ! hasHeldInput())
                release (&edit);
            return "MIDI follow off";
        }

        if (! stable || edit.getTransport().isRecording() || edit.getTransport().isStopping())
            return "MIDI focus paused while transport or project is busy";

        auto* desired = selected != nullptr && ! selected->isFrozen (te::Track::anyFreeze)
                        && hasInstrument (*selected) ? selected : nullptr;
        auto* input = chooseInput (edit, devices);

        if (owned)
        {
            auto* existing = findInput (edit, deviceID);
            const bool unchanged = existing != nullptr && existing->getTargets().size() == 1
                                   && existing->getTargets()[0] == target;
            if (unchanged && input != nullptr && input != existing)
            {
                if (hasHeldInput()) return "Release notes/pedal before changing MIDI keyboard";
                // Device selection changed. Restore our previous device before
                // allowing Studio to manage the new physical keyboard.
                release (&edit);
            }
            else if (! unchanged)
            {
                // A changed assignment belongs to the user; don't restore old
                // routing over it. Only restore the prior monitor mode.
                if (existing != nullptr && existing->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on
                    && previousMonitor != te::InputDevice::MonitorMode::on)
                    existing->getInputDevice().setMonitorMode (previousMonitor);
                clearOwnership();
            }
        }

        if (desired == nullptr || input == nullptr)
        {
            if (owned)
            {
                if (hasHeldInput()) return "Release notes/pedal to change MIDI focus";
                release (&edit);
            }
            return desired == nullptr ? "Select an instrument track to play"
                                      : "No unique enabled physical MIDI keyboard";
        }

        const auto targets = input->getTargets();
        bool armed = false;
        for (const auto id : targets)
            armed |= input->isRecordingEnabled (id);
        const bool alreadyLive = targets.size() == 1 && targets[0] == desired->itemID
            && input->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on;

        if (alreadyLive)
            return "MIDI keyboard → " + desired->getName();

        const MidiFocusConditions conditions {
            true,
            edit.getTransport().isRecording(),
            input->isRecording(),
            armed,
            targets.size() > 1,
            owned && hasHeldInput()
        };
        switch (midiFocusDecision (conditions))
        {
            case MidiFocusDecision::deferRecording: return "MIDI focus held during recording";
            case MidiFocusDecision::preserveRouting:
                return armed ? "MIDI input record-armed; disarm to follow track"
                             : "Manual multi-track MIDI routing preserved";
            case MidiFocusDecision::waitForRelease: return "Release notes/pedal to switch MIDI track";
            case MidiFocusDecision::release: return "No playable track";
            case MidiFocusDecision::switchToTrack: break;
        }

        // Also check keys on an input we have not yet acquired.
        if (keysHeld (input->getInputDevice()))
            return "Release keyboard notes to switch MIDI track";
        if (owned && target == desired->itemID)
            return "MIDI keyboard → " + desired->getName();

        if (! owned)
        {
            deviceID = input->getInputDevice().getDeviceID();
            previousTarget = targets.size() == 1 ? targets[0] : te::EditItemID {};
            previousMonitor = input->getInputDevice().getMonitorMode();
            owned = true;
            attachListener (devices, deviceID);
        }

        if (targets.size() != 1 || targets[0] != desired->itemID)
        {
            if (auto result = input->setTarget (desired->itemID, true, nullptr, 0); ! result)
            {
                release (&edit);
                return "Could not focus MIDI keyboard: " + result.error();
            }
        }
        target = desired->itemID;
        // 'on' enables live monitoring even though record-arm stays off.
        if (input->getInputDevice().getMonitorMode() != te::InputDevice::MonitorMode::on)
            input->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::on);
        return "MIDI keyboard → " + desired->getName();
    }

    bool isFollowing (te::EditItemID track) const noexcept { return owned && target == track; }

private:
    static bool hasInstrument (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList)
            if (plugin->isSynth())
                return true;
        return false;
    }

    static bool keysHeld (te::InputDevice& device)
    {
        if (auto* midi = dynamic_cast<te::MidiInputDevice*> (&device))
            for (int note = 0; note < 128; ++note)
                if (midi->keyboardState.isNoteOnForChannels (0xffff, note))
                    return true;
        return false;
    }

    bool hasHeldInput() const
    {
        return (listened != nullptr && keysHeld (*listened)) || pedalBits.load (std::memory_order_relaxed) != 0;
    }

    static te::InputDeviceInstance* findInput (te::Edit& edit, const juce::String& id)
    {
        for (auto* input : edit.getAllInputDevices())
            if (input->getInputDevice().getDeviceID() == id && input->getInputDevice().isEnabled())
                return input;
        return nullptr;
    }

    static te::InputDeviceInstance* chooseInput (te::Edit& edit, te::DeviceManager& devices)
    {
        const auto* preferred = devices.getDefaultMidiInDevice();
        te::InputDeviceInstance* candidate = nullptr;
        int count = 0;
        for (auto* input : edit.getAllInputDevices())
        {
            auto& device = input->getInputDevice();
            if (device.getDeviceType() != te::InputDevice::physicalMidiDevice || ! device.isEnabled())
                continue;
            if (&device == preferred) return input;
            candidate = input;
            ++count;
        }
        return count == 1 ? candidate : nullptr;
    }

    void attachListener (te::DeviceManager& devices, const juce::String& id)
    {
        detachListener();
        for (auto& device : devices.getMidiInDevices())
            if (device != nullptr && device->getDeviceID() == id)
            {
                listened = std::dynamic_pointer_cast<te::PhysicalMidiInputDevice> (device);
                if (listened != nullptr)
                    listened->addListener (this);
                break;
            }
    }

    void detachListener()
    {
        if (listened != nullptr)
            listened->removeListener (this);
        listened.reset();
        pedalBits.store (0, std::memory_order_relaxed);
    }

    void clearOwnership()
    {
        detachListener();
        owned = false;
        target = previousTarget = {};
        deviceID.clear();
    }

    void handleIncomingMidiMessage (const juce::MidiMessage& message) override
    {
        if (! message.isController() || message.getChannel() < 1 || message.getChannel() > 16)
            return;
        const auto mask = static_cast<unsigned> (1u << (message.getChannel() - 1));
        if (message.getControllerNumber() == 64)
        {
            if (message.getControllerValue() >= 64)
                pedalBits.fetch_or (mask, std::memory_order_relaxed);
            else
                pedalBits.fetch_and (~mask, std::memory_order_relaxed);
        }
        else if (message.getControllerNumber() == 121)
            pedalBits.fetch_and (~mask, std::memory_order_relaxed);
    }

    std::shared_ptr<te::PhysicalMidiInputDevice> listened;
    std::atomic<unsigned> pedalBits { 0 };
    te::EditItemID target, previousTarget;
    te::InputDevice::MonitorMode previousMonitor = te::InputDevice::MonitorMode::automatic;
    juce::String deviceID;
    bool owned = false;
};

} // namespace studio
