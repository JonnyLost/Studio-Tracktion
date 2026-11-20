// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

namespace studio
{
namespace colours
{
inline const juce::Colour rail      { 0xff111317 };
inline const juce::Colour canvas    { 0xff14161a };
inline const juce::Colour timeline  { 0xff161b25 };
inline const juce::Colour panel     { 0xff202329 };
inline const juce::Colour control   { 0xff282c33 };
inline const juce::Colour raised    { 0xff333841 };
inline const juce::Colour divider   { 0xff393e47 };
inline const juce::Colour selected  { 0xff384a5e };
inline const juce::Colour text      { 0xfff3f5f7 };
inline const juce::Colour secondary { 0xffa7adb6 };
inline const juce::Colour action    { 0xffbbcdde };
inline const juce::Colour focus     { 0xff91c8ff };
inline const juce::Colour playhead  { 0xff2b91ff };
inline const juce::Colour warning   { 0xffffcb70 };
inline const juce::Colour record    { 0xffef6674 };
}

class StudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        using namespace juce;
        setColourScheme (getDarkColourScheme());
        setColour (TextButton::buttonColourId, colours::control);
        setColour (TextButton::buttonOnColourId, colours::selected);
        setColour (TextButton::textColourOnId, colours::text);
        setColour (TextButton::textColourOffId, colours::text);
        setColour (Label::textColourId, colours::text);
        setColour (ComboBox::backgroundColourId, colours::control);
        setColour (ComboBox::outlineColourId, colours::divider);
        setColour (ComboBox::focusedOutlineColourId, colours::focus);
        setColour (ComboBox::textColourId, colours::text);
        setColour (Slider::trackColourId, colours::action);
        setColour (Slider::thumbColourId, colours::action);
        setColour (Slider::textBoxOutlineColourId, colours::divider);
        setColour (ScrollBar::thumbColourId, colours::secondary.withAlpha (0.6f));
        setColour (ListBox::backgroundColourId, colours::canvas);
        setColour (ResizableWindow::backgroundColourId, colours::canvas);
        setColour (PopupMenu::backgroundColourId, colours::panel);
        setColour (PopupMenu::highlightedBackgroundColourId, colours::selected);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::FontOptions (juce::jlimit (10.0f, 13.0f, height * 0.55f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& colour, bool hover, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
        const auto fill = down ? colour.brighter (0.2f) : hover ? colour.brighter (0.1f) : colour;
        g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.fillRoundedRectangle (bounds, 4.0f);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (bounds, 4.0f, 1.25f);
        }
    }
};

class TransportReadout final : public juce::Component
{
public:
    explicit TransportReadout (const juce::String& name)
    {
        caption.setText (name.toUpperCase(), juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, colours::secondary);
        value.setTitle (name);
        value.setJustificationType (juce::Justification::centred);
        value.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
        addAndMakeVisible (caption);
        addAndMakeVisible (value);
    }

    void setValue (const juce::String& content) { value.setText (content, juce::dontSendNotification); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (2, 1);
        caption.setBounds (bounds.removeFromTop (13));
        value.setBounds (bounds);
    }

private:
    juce::Label caption, value;
};

enum class RailIcon { instruments, channel, stage, articulations, piano, mixer, more };

class RailButton final : public juce::TextButton
{
public:
    RailButton (const juce::String& label, RailIcon iconToDraw)
        : TextButton (label), icon (iconToDraw)
    {
        setTitle (label);
        setTooltip (label);
        setClickingTogglesState (false);
    }

    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        auto bounds = getLocalBounds();
        const auto iconArea = bounds.removeFromTop (getHeight() - 17)
                                   .withSizeKeepingCentre (18, 18).toFloat();
        const bool active = getToggleState();
        if (active || hover || down)
        {
            g.setColour (active ? colours::selected : colours::control);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 1.25f);
        }
        g.setColour (active ? colours::text : colours::secondary);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (getButtonText(), bounds, juce::Justification::centredTop, 1);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::translation (iconArea.getX(), iconArea.getY()));
        paintIcon (g);
    }

private:
    RailIcon icon;

    void paintIcon (juce::Graphics& g) const
    {
        switch (icon)
        {
            case RailIcon::instruments:
            case RailIcon::articulations:
                g.fillEllipse (6.0f, 11.0f, 5.0f, 4.0f);
                g.drawLine (10.2f, 13.0f, 10.2f, 3.0f, 1.4f);
                if (icon == RailIcon::instruments)
                {
                    for (int y : { 4, 7, 10 })
                        g.drawHorizontalLine (y, 1.0f, 6.0f);
                    g.drawLine (10.2f, 3.0f, 16.0f, 2.0f, 1.5f);
                }
                else
                    g.drawLine (10.2f, 3.0f, 15.5f, 6.0f, 1.5f);
                break;
            case RailIcon::stage:
                for (int y = 3; y <= 13; y += 5)
                    for (int x = 3; x <= 13; x += 5)
                        g.drawEllipse ((float) x, (float) y, 2.8f, 2.8f, 1.0f);
                break;
            case RailIcon::piano:
                g.drawRoundedRectangle (2.0f, 3.0f, 14.0f, 12.0f, 1.5f, 1.1f);
                for (int x : { 5, 9, 13 })
                    g.drawVerticalLine (x, 4.0f, 14.5f);
                g.fillRect (4, 4, 2, 5);
                g.fillRect (12, 4, 2, 5);
                break;
            case RailIcon::channel:
            case RailIcon::mixer:
                if (icon == RailIcon::mixer)
                    g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, 9.0f, 9.0f));
                for (int y : { 4, 9, 14 })
                {
                    g.fillRect (2, y, 14, 1);
                    g.fillRoundedRectangle ((float) (y == 9 ? 10 : 5), (float) y - 1.5f, 3.0f, 4.0f, 0.8f);
                }
                break;
            case RailIcon::more:
                for (int x : { 3, 8, 13 })
                    g.fillEllipse ((float) x, 8.0f, 2.5f, 2.5f);
                break;
        }
    }
};
}
