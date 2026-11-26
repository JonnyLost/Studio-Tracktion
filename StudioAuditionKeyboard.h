// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <functional>
#include <bitset>
#include <cstddef>

namespace studio
{
// A lightweight performance surface. Uses the already-loaded Tracktion
// instrument's live MIDI path; it never records clips, instantiates an AU or
// creates a new CoreMIDI device.
// Visual overlays never modify the MIDI state that sends audition notes.
class ActivityMidiKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit ActivityMidiKeyboard (juce::MidiKeyboardState& state)
        : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard) {}

    void showActivity (const std::bitset<128>& notes)
    {
        if (activity == notes) return;
        activity = notes;
        repaint();
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool down, bool over, juce::Colour line, juce::Colour text) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote (note, g, area,
            down || (note >= 0 && note < 128 && activity.test (static_cast<std::size_t> (note))),
            over, line, text);
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool down, bool over, juce::Colour fill) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (note, g, area,
            down || (note >= 0 && note < 128 && activity.test (static_cast<size_t> (note))),
            over, fill);
    }

private:
    std::bitset<128> activity;
};

class AuditionKeyboardPanel final : public juce::Component,
                                    private juce::MidiKeyboardStateListener
{
public:
    AuditionKeyboardPanel()
        : keys (state)
    {
        state.addListener (this);
        addAndMakeVisible (keys);
        addAndMakeVisible (channel);
        addAndMakeVisible (status);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setColour (juce::Label::textColourId, juce::Colour (0xffdde1e8));
        status.setText ("Select an instrument track to audition.", juce::dontSendNotification);
        for (int n = 1; n <= 16; ++n)
            channel.addItem ("MIDI " + juce::String (n), n);
        channel.setSelectedId (1, juce::dontSendNotification);
        channel.setTooltip ("Audition channel (does not change track or clip MIDI channels)");
        channel.onChange = [this]
        {
            releaseNotes(); // Note-offs must use the previous MIDI channel.
            activeChannel = channel.getSelectedId();
            keys.setMidiChannel (activeChannel);
        };
        keys.setMidiChannel (1);
        keys.setAvailableRange (21, 108);
        keys.setLowestVisibleKey (48);
        keys.setKeyWidth (22.0f);
    }

    ~AuditionKeyboardPanel() override
    {
        releaseNotes();
        state.removeListener (this);
    }

    std::function<void (const juce::MidiMessage&)> send;
    void setSelectedInstrument (const juce::String& name)
    {
        const auto label = name.isNotEmpty() ? "Audition - " + name
            : juce::String ("Select an instrument track to audition.");
        if (status.getText() != label)
            status.setText (label, juce::dontSendNotification);
    }
    void releaseNotes() { state.allNotesOff (activeChannel); }
    void displayActivity (const std::bitset<128>& notes) { keys.showActivity (notes); }
    void clearActivity() { keys.showActivity ({}); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 7);
        auto top = area.removeFromTop (28);
        channel.setBounds (top.removeFromRight (110));
        status.setBounds (top);
        area.removeFromTop (5);
        keys.setBounds (area);
    }

private:
    juce::MidiKeyboardState state;
    ActivityMidiKeyboard keys;
    juce::ComboBox channel;
    juce::Label status;
    int activeChannel = 1;

    void handleNoteOn (juce::MidiKeyboardState*, int channelNumber,
                       int midiNote, float velocity) override
    {
        if (send)
            send (juce::MidiMessage::noteOn (channelNumber, midiNote,
                (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f))));
    }

    void handleNoteOff (juce::MidiKeyboardState*, int channelNumber,
                        int midiNote, float) override
    {
        if (send) send (juce::MidiMessage::noteOff (channelNumber, midiNote));
    }
};

class AuditionKeyboardWindow final : public juce::DocumentWindow
{
public:
    AuditionKeyboardWindow()
        : juce::DocumentWindow ("Studio - On-Screen MIDI Keyboard",
              juce::Colour (0xff202b3a), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (500, 145, 1800, 320);
        setContentNonOwned (&panel, false);
        centreWithSize (790, 182);
    }

    ~AuditionKeyboardWindow() override
    {
        panel.releaseNotes();
        clearContentComponent();
    }

    AuditionKeyboardPanel panel;
    std::function<void()> onClose;
    void closeButtonPressed() override
    {
        panel.releaseNotes();
        if (onClose) onClose();
    }
};
} // namespace studio
