// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "StudioKeyboardPerformanceState.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>

namespace studio
{
namespace atelierKeyboard
{
inline const juce::Colour wood       { 0xff231913 };
inline const juce::Colour warmWood   { 0xff39271c };
inline const juce::Colour panel      { 0xff181615 };
inline const juce::Colour gold       { 0xffdca95e };
inline const juce::Colour faintGold  { 0xff765a3d };
inline const juce::Colour parchment  { 0xfff1e2c7 };
inline const juce::Colour secondary  { 0xffc5b49d };
}

// Performance sliders use compact brushed-bronze wheels and rotary controls.
// This is a local LookAndFeel; other Studio controls remain unchanged.
class AtelierKeyboardControls final : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float position, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, position,
                                             minPos, maxPos, style, slider);
            return;
        }
        const float cx = x + w * 0.5f;
        const auto slot = juce::Rectangle<float> (cx - 5.0f, (float) y + 4.0f,
                                                   10.0f, (float) h - 8.0f);
        g.setColour (juce::Colour (0xff0b0c10));
        g.fillRoundedRectangle (slot, 4.0f);
        g.setColour (atelierKeyboard::faintGold.withAlpha (0.65f));
        g.drawRoundedRectangle (slot, 4.0f, 1.0f);
        if (slider.getName() == "Pitch Bend")
        {
            g.setColour (atelierKeyboard::secondary.withAlpha (0.55f));
            g.drawLine (cx - 14.0f, y + h * 0.5f, cx + 14.0f, y + h * 0.5f, 1.0f);
        }
        const auto thumb = juce::Rectangle<float> (cx - 14.0f, position - 9.0f, 28.0f, 18.0f);
        g.setGradientFill (juce::ColourGradient (atelierKeyboard::gold.brighter (0.12f),
                                                thumb.getTopLeft(), atelierKeyboard::faintGold,
                                                thumb.getBottomLeft(), false));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (atelierKeyboard::parchment.withAlpha (0.65f));
        g.drawLine (thumb.getX() + 5.0f, thumb.getCentreY(),
                    thumb.getRight() - 5.0f, thumb.getCentreY(), 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float proportion, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const float radius = std::min (w, h) * 0.37f;
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.49f;
        g.setColour (juce::Colour (0xff0d0f11));
        g.fillEllipse (cx - radius - 4.0f, cy - radius - 4.0f,
                       (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);
        juce::Path rail;
        rail.addCentredArc (cx, cy, radius + 4.0f, radius + 4.0f, 0.0f,
                            startAngle, endAngle, true);
        g.setColour (atelierKeyboard::faintGold);
        g.strokePath (rail, juce::PathStrokeType (2.3f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        juce::Path active;
        const float angle = startAngle + proportion * (endAngle - startAngle);
        active.addCentredArc (cx, cy, radius + 4.0f, radius + 4.0f,
                              0.0f, startAngle, angle, true);
        g.setColour (atelierKeyboard::gold);
        g.strokePath (active, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff66503a),
                                                cx - radius, cy - radius,
                                                atelierKeyboard::wood,
                                                cx + radius, cy + radius, false));
        g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (atelierKeyboard::gold.withAlpha (0.6f));
        g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 0.8f);
        const float dx = std::sin (angle), dy = -std::cos (angle);
        g.setColour (atelierKeyboard::parchment);
        g.drawLine (cx + dx * radius * 0.27f, cy + dy * radius * 0.27f,
                    cx + dx * radius * 0.78f, cy + dy * radius * 0.78f, 2.2f);
    }
};

// Activity colours are drawn without changing MidiKeyboardState. The state
// listeners below send only genuine on-screen user note gestures.
class ActivityMidiKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit ActivityMidiKeyboard (juce::MidiKeyboardState& state)
        : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        setColour (keyDownOverlayColourId, atelierKeyboard::gold.withAlpha (0.85f));
        setColour (mouseOverKeyOverlayColourId, atelierKeyboard::gold.withAlpha (0.24f));
        setColour (whiteNoteColourId, atelierKeyboard::parchment);
        setColour (blackNoteColourId, juce::Colour (0xff161819));
        setColour (keySeparatorLineColourId, juce::Colour (0xff7a6555));
        setColour (textLabelColourId, juce::Colour (0xff615047));
    }

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
            down || (note >= 0 && note < 128 && activity.test (static_cast<std::size_t> (note))),
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
        for (auto* button : { &dockModeButton, &hideButton })
        {
            addAndMakeVisible (*button);
            button->setColour (juce::TextButton::buttonColourId, atelierKeyboard::panel);
            button->setColour (juce::TextButton::buttonOnColourId, atelierKeyboard::warmWood);
            button->setColour (juce::TextButton::textColourOffId, atelierKeyboard::gold);
            button->setColour (juce::TextButton::textColourOnId, atelierKeyboard::parchment);
        }
        dockModeButton.onClick = [this] { if (onToggleDock) onToggleDock(); };
        hideButton.onClick = [this] { if (onHide) onHide(); };
        hideButton.setTooltip ("Hide the docked performance keyboard");
        setDocked (false);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setColour (juce::Label::textColourId, atelierKeyboard::parchment);
        status.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        status.setText ("Select an instrument track to audition", juce::dontSendNotification);
        for (int n = 1; n <= 16; ++n)
            channel.addItem ("MIDI " + juce::String (n), n);
        channel.setSelectedId (1, juce::dontSendNotification);
        channel.setTooltip ("MIDI channel for on-screen audition controls");
        channel.onChange = [this]
        {
            releaseNotes();
            activeChannel = channel.getSelectedId();
            keys.setMidiChannel (activeChannel);
        };
        channel.setColour (juce::ComboBox::backgroundColourId, atelierKeyboard::panel);
        channel.setColour (juce::ComboBox::textColourId, atelierKeyboard::parchment);
        channel.setColour (juce::ComboBox::outlineColourId, atelierKeyboard::faintGold);
        keys.setMidiChannel (1);
        keys.setAvailableRange (21, 108);
        keys.setLowestVisibleKey (48);
        keys.setKeyWidth (19.0f);

        for (auto* control : controls())
        {
            addAndMakeVisible (*control);
            control->setLookAndFeel (&controlStyle);
            control->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            control->setScrollWheelEnabled (false);
        }

        for (auto* wheel : { &bend, &mod })
            wheel->setSliderStyle (juce::Slider::LinearVertical);
        for (auto* knob : { &expression, &volume, &brightness, &room })
        {
            knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.23f,
                                       juce::MathConstants<float>::pi * 2.77f, true);
        }

        bend.setName ("Pitch Bend");
        bend.setTooltip ("Pitch Bend, returns to centre when released");
        bend.setRange (0, 16383, 1);
        bend.setDoubleClickReturnValue (true, 8192.0);
        mod.setName ("Modulation");
        mod.setTooltip ("Modulation wheel - MIDI CC1");
        mod.setRange (0, 127, 1);
        expression.setTooltip ("Expression - MIDI CC11");
        volume.setTooltip ("MIDI Channel Volume - CC7");
        brightness.setTooltip ("Timbre / Brightness - MIDI CC74");
        room.setTooltip ("Reverb Send - MIDI CC91");
        for (auto* knob : { &expression, &volume, &brightness, &room })
            knob->setRange (0, 127, 1);

        resetPerformance();
        bend.onValueChange = [this]
        {
            if (send)
                send (juce::MidiMessage::pitchWheel (activeChannel, juce::roundToInt (bend.getValue())));
            repaint();
        };
        bend.onDragEnd = [this] { bend.setValue (8192, juce::sendNotificationSync); };
        for (const auto& [control, cc] : { std::pair<juce::Slider*, int> { &mod, 1 },
                                            { &expression, 11 }, { &volume, 7 },
                                            { &brightness, 74 }, { &room, 91 } })
        {
            control->onValueChange = [this, control, cc]
            {
                if (send)
                    send (juce::MidiMessage::controllerEvent (activeChannel, cc,
                                                              juce::roundToInt (control->getValue())));
                repaint();
            };
        }
    }

    ~AuditionKeyboardPanel() override
    {
        releaseNotes();
        for (auto* control : controls())
            control->setLookAndFeel (nullptr);
        state.removeListener (this);
    }

    // Existing instrument audition path, reused by the new wheel/knob gestures.
    // Hardware MIDI updates call reflectPerformance with dontSendNotification.
    std::function<void (const juce::MidiMessage&)> send;
    std::function<void()> onToggleDock;
    std::function<void()> onHide;

    // The same instrument console is used for both modes. Only its parent
    // and header action change; note monitoring and MIDI routing stay intact.
    void setDocked (bool docked)
    {
        dockedMode = docked;
        dockModeButton.setButtonText (docked ? "Float" : "Dock");
        dockModeButton.setTooltip (docked
            ? "Open the Atelier keyboard in a floating window"
            : "Attach this keyboard to the bottom of Studio");
        hideButton.setVisible (docked);
        resized();
    }

    bool isDocked() const noexcept { return dockedMode; }

    void setSelectedInstrument (const juce::String& name)
    {
        const auto label = name.isNotEmpty() ? name : juce::String ("Select an instrument track");
        if (status.getText() != label)
            status.setText (label, juce::dontSendNotification);
    }

    void releaseNotes() { state.allNotesOff (activeChannel); }
    void displayActivity (const std::bitset<128>& notes) { keys.showActivity (notes); }
    void clearActivity() { keys.showActivity ({}); }

    void reflectPerformance (const KeyboardPerformanceValues& values)
    {
        const auto changed = values.changed;
        using V = KeyboardPerformanceValues;
        if (changed & V::bit (V::pitch)) bend.setValue (values.bend, juce::dontSendNotification);
        if (changed & V::bit (V::modulation)) mod.setValue (values.mod, juce::dontSendNotification);
        if (changed & V::bit (V::expression)) expression.setValue (values.expr, juce::dontSendNotification);
        if (changed & V::bit (V::volume)) volume.setValue (values.vol, juce::dontSendNotification);
        if (changed & V::bit (V::brightness)) brightness.setValue (values.tone, juce::dontSendNotification);
        if (changed & V::bit (V::room)) room.setValue (values.reverb, juce::dontSendNotification);
        if (changed != 0) repaint();
    }

    // Preserve wheel and knob positions when switching between the docked
    // and floating instances. Reading values does not send any MIDI.
    KeyboardPerformanceValues performanceSnapshot() const
    {
        KeyboardPerformanceValues values;
        values.bend = juce::roundToInt (bend.getValue());
        values.mod = juce::roundToInt (mod.getValue());
        values.expr = juce::roundToInt (expression.getValue());
        values.vol = juce::roundToInt (volume.getValue());
        values.tone = juce::roundToInt (brightness.getValue());
        values.reverb = juce::roundToInt (room.getValue());
        values.changed = (1u << 6) - 1u;
        return values;
    }

    void resetPerformance()
    {
        bend.setValue (8192, juce::dontSendNotification);
        mod.setValue (0, juce::dontSendNotification);
        expression.setValue (127, juce::dontSendNotification);
        volume.setValue (100, juce::dontSendNotification);
        brightness.setValue (64, juce::dontSendNotification);
        room.setValue (40, juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto outer = getLocalBounds().toFloat().reduced (1.0f);
        g.setGradientFill (juce::ColourGradient (atelierKeyboard::warmWood,
                                                outer.getTopLeft(), atelierKeyboard::wood,
                                                outer.getBottomRight(), false));
        g.fillRoundedRectangle (outer, 9.0f);

        // Restrained walnut-grain lines and brass binding match Atelier's
        // palette without loading an image asset into every project.
        g.setColour (atelierKeyboard::gold.withAlpha (0.045f));
        for (int row = 0; row < getHeight(); row += 13)
            g.drawLine (10.0f, (float) row, (float) getWidth() - 10.0f,
                        (float) row + 3.0f, 0.7f);
        g.setColour (atelierKeyboard::gold.withAlpha (0.85f));
        g.drawRoundedRectangle (outer.reduced (1.0f), 8.0f, 1.1f);
        g.setColour (atelierKeyboard::faintGold);
        g.drawLine (13.0f, (float) topEdge, (float) getWidth() - 13.0f,
                    (float) topEdge, 1.0f);

        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (atelierKeyboard::gold);
        g.drawFittedText ("CINEMATIC ATELIER", juce::Rectangle<int> (19, 11, 196, 23),
                          juce::Justification::centredLeft, 1);

        for (const auto plate : { leftPlate, keyPlate, rightPlate })
        {
            g.setColour (atelierKeyboard::panel.withAlpha (0.92f));
            g.fillRoundedRectangle (plate.toFloat(), 6.0f);
            g.setColour (atelierKeyboard::faintGold.withAlpha (0.67f));
            g.drawRoundedRectangle (plate.toFloat().reduced (0.5f), 6.0f, 0.85f);
        }

        const auto drawCaption = [&g] (juce::String label, juce::Rectangle<int> r)
        {
            g.setColour (atelierKeyboard::secondary);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawFittedText (label, r, juce::Justification::centred, 1);
        };
        drawCaption ("PITCH", captionArea (bend));
        drawCaption ("MOD " + juce::String (juce::roundToInt (mod.getValue())), captionArea (mod));
        drawCaption ("EXPR " + juce::String (juce::roundToInt (expression.getValue())),
                     captionArea (expression));
        drawCaption ("VOL " + juce::String (juce::roundToInt (volume.getValue())),
                     captionArea (volume));
        drawCaption ("TONE " + juce::String (juce::roundToInt (brightness.getValue())),
                     captionArea (brightness));
        drawCaption ("ROOM " + juce::String (juce::roundToInt (room.getValue())),
                     captionArea (room));
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (12, 10);
        auto top = content.removeFromTop (topEdge - 12);
        channel.setBounds (top.removeFromRight (91).reduced (2, 3));
        if (dockedMode)
            hideButton.setBounds (top.removeFromRight (53).reduced (2, 3));
        else
            hideButton.setBounds ({});
        dockModeButton.setBounds (top.removeFromRight (72).reduced (2, 3));
        top.removeFromLeft (juce::jmin (203, top.getWidth() / 3));
        status.setBounds (top.reduced (5, 2));
        content.removeFromTop (6);

        const int fullWidth = content.getWidth();
        int leftW = juce::jlimit (90, 147, fullWidth / 7);
        int rightW = juce::jlimit (176, 254, fullWidth / 4);
        if (fullWidth < 760) { leftW = 93; rightW = 176; }
        leftPlate = content.removeFromLeft (leftW);
        content.removeFromLeft (7);
        rightPlate = content.removeFromRight (rightW);
        content.removeFromRight (7);
        keyPlate = content;

        auto left = leftPlate.reduced (8, 7);
        const int wheelW = left.getWidth() / 2;
        bend.setBounds (left.removeFromLeft (wheelW).removeFromTop (
            juce::jmax (30, leftPlate.getHeight() - 33)));
        mod.setBounds (left.removeFromLeft (left.getWidth()).removeFromTop (
            juce::jmax (30, leftPlate.getHeight() - 33)));

        auto right = rightPlate.reduced (7, 3);
        const int colW = right.getWidth() / 2;
        const int rowH = right.getHeight() / 2;
        auto upper = right.removeFromTop (rowH);
        expression.setBounds (upper.removeFromLeft (colW).reduced (2, 6).withTrimmedBottom (13));
        volume.setBounds (upper.reduced (2, 6).withTrimmedBottom (13));
        brightness.setBounds (right.removeFromLeft (colW).reduced (2, 6).withTrimmedBottom (13));
        room.setBounds (right.reduced (2, 6).withTrimmedBottom (13));

        keys.setBounds (keyPlate.reduced (8, 10));
        keys.setKeyWidth (juce::jlimit (13.0f, 21.0f, keys.getWidth() / 33.0f));
    }

private:
    static constexpr int topEdge = 42;
    juce::MidiKeyboardState state;
    ActivityMidiKeyboard keys;
    AtelierKeyboardControls controlStyle;
    juce::Slider bend, mod, expression, volume, brightness, room;
    juce::ComboBox channel;
    juce::Label status;
    juce::TextButton dockModeButton { "Dock" }, hideButton { "Hide" };
    juce::Rectangle<int> leftPlate, keyPlate, rightPlate;
    int activeChannel = 1;
    bool dockedMode = false;

    std::array<juce::Slider*, 6> controls()
    {
        return {{ &bend, &mod, &expression, &volume, &brightness, &room }};
    }

    static juce::Rectangle<int> captionArea (const juce::Slider& control)
    {
        return control.getBounds().withY (control.getBottom() + 1).withHeight (13);
    }

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
        : juce::DocumentWindow ("Studio - Cinematic Atelier Keyboard",
              juce::Colour (0xff241912), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (780, 218, 1800, 370);
        setContentNonOwned (&panel, false);
        centreWithSize (1100, 246);
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
