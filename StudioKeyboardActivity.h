// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StudioKeyboardNoteState.h"
#include <JuceHeader.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <memory>
#include <limits>
#include <optional>
#include <vector>

namespace studio
{
namespace te = tracktion;

// Listens to Tracktion's *existing* physical MIDI devices. Does not open a
// second CoreMIDI port, change routing, arm a track, or emit MIDI to playback.
class KeyboardInputActivity final : private te::PhysicalMidiInputDevice::Listener
{
public:
    ~KeyboardInputActivity() override { detach(); }

    void watch (te::Edit& edit, te::EditItemID trackId, te::DeviceManager& manager)
    {
        std::vector<std::shared_ptr<te::PhysicalMidiInputDevice>> wanted;
        if (trackId.isValid())
            for (auto* input : edit.getAllInputDevices())
            {
                if (input == nullptr || ! input->getTargets().contains (trackId)
                    || ! input->getInputDevice().isEnabled()
                    || input->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice)
                    continue;
                const auto id = input->getInputDevice().getDeviceID();
                for (const auto& device : manager.getMidiInDevices())
                    if (device != nullptr && device->getDeviceID() == id)
                        if (auto physical = std::dynamic_pointer_cast<te::PhysicalMidiInputDevice> (device))
                            if (std::find (wanted.begin(), wanted.end(), physical) == wanted.end())
                                wanted.push_back (std::move (physical));
            }

        if (wanted == attached && trackId == currentTrack)
            return;
        detach();
        currentTrack = trackId;
        attached = std::move (wanted);
        for (const auto& device : attached)
            device->addListener (this);
    }

    void detach()
    {
        for (const auto& device : attached)
            device->removeListener (this);
        attached.clear();
        currentTrack = {};
        notes.clear();
    }

    std::bitset<128> snapshot() noexcept { return notes.snapshot(); }

private:
    void handleIncomingMidiMessage (const juce::MidiMessage& midi) override
    {
        const int ch = midi.getChannel();
        if (midi.isNoteOn())
            notes.noteOn (ch, midi.getNoteNumber());
        else if (midi.isNoteOff())
            notes.noteOff (ch, midi.getNoteNumber());
        else if (midi.isController())
        {
            if (midi.getControllerNumber() == 64)
                notes.sustain (ch, midi.getControllerValue() >= 64);
            else if (midi.getControllerNumber() == 120
                     || midi.getControllerNumber() == 121
                     || midi.getControllerNumber() == 123)
                notes.allNotesOff (ch);
        }
    }

    KeyboardNoteState notes;
    te::EditItemID currentTrack;
    std::vector<std::shared_ptr<te::PhysicalMidiInputDevice>> attached;
};

// A read-only projection of standard MIDI regions onto the selected track's
// transport clock. Clip-specific offset and loop translation matches the
// Piano Roll. Complex MIDI-generating plug-ins are not observed here.
inline std::bitset<128> keyboardPlaybackNotes (te::AudioTrack& track,
                                               te::TimePosition current,
                                               std::optional<te::TimePosition> previous)
{
    std::bitset<128> lit;
    if (track.isMuted (true))
        return lit;

    const auto currentSeconds = current.inSeconds();
    const bool shortInterval = previous.has_value()
        && currentSeconds >= previous->inSeconds()
        && currentSeconds - previous->inSeconds() <= 0.15;

    for (auto* c : track.getClips())
    {
        auto* clip = dynamic_cast<te::MidiClip*> (c);
        if (clip == nullptr || clip->isMuted() || ! clip->hasValidSequence())
            continue;

        const auto bounds = clip->getPosition();
        if (current < bounds.getStart() || current >= bounds.getEnd())
            continue;

        const auto clipBeat = [] (te::MidiClip& midi, te::TimePosition time)
        {
            double beat = midi.getContentBeatAtTime (time).inBeats();
            if (midi.isLooping())
            {
                const double loopStart = midi.getLoopStartBeats().inBeats();
                const double loopLength = midi.getLoopLengthBeats().inBeats();
                if (loopLength > 0.0 && beat >= loopStart)
                    beat = loopStart + std::fmod (beat - loopStart, loopLength);
            }
            return beat;
        };

        const double nowBeat = clipBeat (*clip, current);
        const bool chase = shortInterval && previous.value() < current;
        const double earlier = chase
            ? clipBeat (*clip, std::max (bounds.getStart(), *previous))
            : nowBeat;
        const double loopStart = clip->getLoopStartBeats().inBeats();
        const double loopEnd = loopStart + clip->getLoopLengthBeats().inBeats();

        // Chase sustain controller 64 at the visible playhead. MIDI controller
        // values are stored at 14-bit resolution in Tracktion's sequence.
        bool pedalDown = false;
        double pedalDownBeat = std::numeric_limits<double>::infinity();
        for (const auto* cc : clip->getSequence().getControllerEvents())
        {
            if (cc->getType() != 64 || cc->getBeatPosition().inBeats() > nowBeat)
                continue;
            const bool down = (cc->getControllerValue() >> 7) >= 64;
            if (down && ! pedalDown)
                pedalDownBeat = cc->getBeatPosition().inBeats();
            pedalDown = down;
        }

        for (const auto* note : clip->getSequence().getNotes())
        {
            const int pitch = note->getNoteNumber();
            if (pitch < 0 || pitch >= 128) continue;
            const double start = note->getStartBeat().inBeats();
            const double end = note->getEndBeat().inBeats();
            const bool sounding = start <= nowBeat && nowBeat < end;
            const bool justPlayed = chase && (
                (earlier <= nowBeat && start > earlier && start <= nowBeat)
                || (clip->isLooping() && nowBeat < earlier
                    && ((start > earlier && start < loopEnd)
                        || (start >= loopStart && start <= nowBeat))));
            const bool sustainedNote = pedalDown && start <= nowBeat
                && end >= pedalDownBeat && end <= nowBeat;
            if (sounding || justPlayed || sustainedNote)
                lit.set (static_cast<size_t> (pitch));
        }
    }
    return lit;
}
} // namespace studio
