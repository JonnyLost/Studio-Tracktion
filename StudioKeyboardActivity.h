// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StudioKeyboardNoteState.h"
#include "StudioKeyboardPerformanceState.h"
#include <JuceHeader.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
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
        performance.reset();
    }

    std::bitset<128> snapshot() noexcept { return notes.snapshot(); }
    KeyboardPerformanceValues consumePerformance() noexcept { return performance.consume(); }

private:
    void handleIncomingMidiMessage (const juce::MidiMessage& midi) override
    {
        const int ch = midi.getChannel();
        if (midi.isNoteOn())
            notes.noteOn (ch, midi.getNoteNumber());
        else if (midi.isNoteOff())
            notes.noteOff (ch, midi.getNoteNumber());
        else if (midi.isPitchWheel())
            performance.receivePitchBend (midi.getPitchWheelValue());
        else if (midi.isController())
        {
            performance.receiveController (midi.getControllerNumber(), midi.getControllerValue());
            if (midi.getControllerNumber() == 64)
                notes.sustain (ch, midi.getControllerValue() >= 64);
            else if (midi.getControllerNumber() == 120
                     || midi.getControllerNumber() == 121
                     || midi.getControllerNumber() == 123)
                notes.allNotesOff (ch);
        }
    }

    KeyboardNoteState notes;
    KeyboardPerformanceState performance;
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
                lit.set (static_cast<std::size_t> (pitch));
        }
    }
    return lit;
}


// Visually chase the last relevant MIDI controller event for active standard
// MIDI regions. This is for UI feedback only; plug-ins receive their normal
// MIDI through Tracktion's established playback engine.
inline KeyboardPerformanceValues keyboardPlaybackPerformance (te::AudioTrack& track,
                                                              te::TimePosition current)
{
    KeyboardPerformanceValues values;
    if (track.isMuted (true)) return values;

    for (auto* clipBase : track.getClips())
    {
        auto* clip = dynamic_cast<te::MidiClip*> (clipBase);
        if (clip == nullptr || clip->isMuted() || ! clip->hasValidSequence())
            continue;
        const auto bounds = clip->getPosition();
        if (current < bounds.getStart() || current >= bounds.getEnd())
            continue;

        double beat = clip->getContentBeatAtTime (current).inBeats();
        if (clip->isLooping())
        {
            const auto loopStart = clip->getLoopStartBeats().inBeats();
            const auto loopLength = clip->getLoopLengthBeats().inBeats();
            if (loopLength > 0.0 && beat >= loopStart)
                beat = loopStart + std::fmod (beat - loopStart, loopLength);
        }

        for (auto* cc : clip->getSequence().getControllerEvents())
        {
            if (cc->getBeatPosition().inBeats() > beat)
                continue;
            const int type = cc->getType();
            auto raw = cc->getControllerValue();
            if (type == te::MidiControllerEvent::pitchWheelType)
            {
                values.bend = juce::jlimit (0, 16383, raw);
                values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::pitch);
                continue;
            }
            raw = juce::jlimit (0, 127, raw >> 7);
            switch (type)
            {
                case 1:
                    values.mod = raw;
                    values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::modulation);
                    break;
                case 11:
                    values.expr = raw;
                    values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::expression);
                    break;
                case 7:
                    values.vol = raw;
                    values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::volume);
                    break;
                case 74:
                    values.tone = raw;
                    values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::brightness);
                    break;
                case 91:
                    values.reverb = raw;
                    values.changed |= KeyboardPerformanceValues::bit (KeyboardPerformanceValues::room);
                    break;
                default: break;
            }
        }
    }
    return values;
}
} // namespace studio
