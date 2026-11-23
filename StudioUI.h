// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "AppearanceMetrics.h"

namespace studio
{
inline juce::FontOptions uiFont (float points, int style = juce::Font::plain)
{
    return juce::FontOptions (appearance.fontSize (points), style);
}

struct ToolbarItem
{
    juce::Component* component;
    int baseWidth;
    int fixedWidth = 0;
    int width() const { return fixedWidth > 0 ? fixedWidth : appearance.span (baseWidth); }
};

inline int toolbarHeight (int width, const std::initializer_list<ToolbarItem>& items, int rowHeight = 0)
{
    int rows = 1, used = 0;
    for (const auto& item : items)
    {
        const int next = juce::jmin (width, item.width());
        if (used > 0 && used + next > width)
        {
            ++rows;
            used = 0;
        }
        used += next;
    }
    return rows * (rowHeight > 0 ? rowHeight : appearance.control (30));
}

inline void layoutToolbar (juce::Rectangle<int>& area, const std::initializer_list<ToolbarItem>& items, int height = 0)
{
    const auto rowHeight = height > 0 ? height : appearance.control (30);
    const int width = area.getWidth();
    auto row = area.removeFromTop (rowHeight);
    for (const auto& item : items)
    {
        const int next = juce::jmin (width, item.width());
        if (row.getWidth() < next)
            row = area.removeFromTop (rowHeight);
        item.component->setBounds (row.removeFromLeft (next).reduced (appearance.px (1)));
    }
}

namespace colours
{
inline const juce::Colour rail      { 0xff10141c };
inline const juce::Colour canvas    { 0xff151a23 };
inline const juce::Colour timeline  { 0xff19212d };
inline const juce::Colour panel     { 0xff202834 };
inline const juce::Colour control   { 0xff293443 };
inline const juce::Colour raised    { 0xff353f4e };
inline const juce::Colour divider   { 0xff414d5e };
inline const juce::Colour selected  { 0xff394e66 };
inline const juce::Colour well      { 0xff131923 };
inline const juce::Colour grid      { 0xff3b4b61 };
inline const juce::Colour text      { 0xfff3f5f7 };
inline const juce::Colour secondary { 0xffb9c0ca };
inline const juce::Colour action    { 0xffbbcdde };
inline const juce::Colour focus     { 0xff91c8ff };
inline const juce::Colour playhead  { 0xff2b91ff };
inline const juce::Colour warning   { 0xffffcb70 };
inline const juce::Colour record    { 0xffef6674 };
inline const juce::Colour signal    { 0xff91d1b8 };
inline const juce::Colour strings   { 0xff9abfe8 };
inline const juce::Colour winds     { 0xff8ed0c3 };
inline const juce::Colour brass     { 0xffe0bf85 };
inline const juce::Colour percussion{ 0xffd7a4ba };
inline const juce::Colour keys      { 0xffb9a8e0 };
inline const juce::Colour other     { 0xffb2becb };
inline const juce::Colour midiClip  { 0xff365a77 };
inline const juce::Colour audioClip { 0xff305b5f };
inline const juce::Colour roomReturn{ 0xffa992d3 };
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
        setColour (PopupMenu::textColourId, colours::text);
        setColour (PopupMenu::highlightedTextColourId, colours::text);
        setColour (TextEditor::backgroundColourId, colours::well);
        setColour (TextEditor::outlineColourId, colours::divider);
        setColour (TextEditor::focusedOutlineColourId, colours::focus);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::FontOptions (juce::jmin (appearance.fontSize (13.0f), (float) height * 0.8f));
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        const auto base = label.getFont();
        return base.withHeight (appearance.fontSize (base.getHeight()));
    }

    // ComboBox installs this as the child's base Label font. getLabelFont
    // applies the user's appearance when the label draws it.
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    int getDefaultScrollbarWidth() override { return appearance.scrollbar(); }

    int getSliderThumbRadius (juce::Slider& slider) override
    {
        return juce::jmin (appearance.px (12), (slider.isHorizontal() ? slider.getHeight() : slider.getWidth()) / 2);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float minimum, float maximum, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical || ! slider.getProperties().contains ("studioFader"))
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, minimum, maximum, style, slider);
            return;
        }
        const auto centre = (float) x + (float) width * 0.5f;
        const auto groove = juce::Rectangle<float> (centre - (float) appearance.px (2), (float) y,
                                                     (float) appearance.px (4), (float) height);
        const auto tint = slider.isEnabled() ? colours::action : colours::secondary.withAlpha (0.5f);
        g.setColour (colours::well);
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (colours::divider);
        const auto unitPosition = slider.getPositionOfValue (0.0);
        if (unitPosition >= (float) y && unitPosition <= (float) (y + height))
            g.drawHorizontalLine (juce::roundToInt (unitPosition), centre - (float) appearance.px (12),
                                  centre + (float) appearance.px (12));
        const auto thumb = juce::Rectangle<float> ((float) appearance.px (25), (float) appearance.px (12))
                               .withCentre ({ centre, position });
        g.setColour (tint);
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (slider.hasKeyboardFocus (true) ? colours::focus : colours::well);
        g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
        g.drawHorizontalLine (juce::roundToInt (position), thumb.getX() + 3.0f, thumb.getRight() - 3.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                               : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (appearance.px (3), 0),
                          juce::Justification::centred, 1, 1.0f);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> position,
                                           juce::Rectangle<int> available) override
    {
        const int inset = appearance.px (7);
        const auto width = juce::jmax (1, juce::jmin (available.getWidth() - 2 * inset, appearance.span (355)));
        auto layout = tooltipLayout (text, width);
        return juce::Rectangle<int> (position.x + inset, position.y + appearance.px (16),
            juce::roundToInt (std::ceil (layout.getWidth())) + 2 * inset,
            juce::roundToInt (std::ceil (layout.getHeight())) + 2 * inset).constrainedWithin (available);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        const int inset = appearance.px (7);
        auto bounds = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (colours::control);
        g.fillRoundedRectangle (bounds, (float) appearance.px (4));
        g.setColour (colours::divider);
        g.drawRoundedRectangle (bounds.reduced (0.5f), (float) appearance.px (4), 1.0f);
        tooltipLayout (text, juce::jmax (1, width - 2 * inset)).draw (g, bounds.reduced ((float) inset));
    }

    void drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name,
                                int, int width, int height, bool hovered, bool down, int flags) override
    {
        auto area = juce::Rectangle<int> (width, height).reduced (appearance.px (5), 0);
        if (hovered || down)
            g.fillAll (colours::selected);
        g.setColour (colours::text);
        g.setFont (uiFont (12.0f, juce::Font::bold));
        if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
        {
            auto direction = area.removeFromRight (appearance.px (14));
            g.drawText ((flags & juce::TableHeaderComponent::sortedForwards) != 0 ? juce::String::charToString (0x2191) : juce::String::charToString (0x2193),
                        direction, juce::Justification::centred);
        }
        g.drawFittedText (name, area, juce::Justification::centredLeft, 1, 1.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& colour, bool hover, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
        const auto fill = down ? colour.brighter (0.2f) : hover ? colour.brighter (0.1f) : colour;
        g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.fillRoundedRectangle (bounds, (float) appearance.px (4));
        g.setColour (colours::divider.withAlpha (button.isEnabled() ? 0.48f : 0.23f));
        g.drawRoundedRectangle (bounds, (float) appearance.px (4), 0.75f);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (bounds, (float) appearance.px (4), 1.25f);
        }
    }

private:
    static juce::TextLayout tooltipLayout (const juce::String& text, int width)
    {
        juce::AttributedString content;
        content.append (text, uiFont (13.0f), colours::text);
        juce::TextLayout layout;
        layout.createLayout (content, (float) width);
        return layout;
    }
};

enum class TransportIcon { start, play, stop, record };

class TransportButton final : public juce::TextButton
{
public:
    TransportButton (const char* label, TransportIcon symbol) : juce::TextButton (label), icon (symbol)
    {
        setTitle (label);
        setTooltip (label);
    }

    void paintButton (juce::Graphics& g, bool hovered, bool down) override
    {
        const bool recording = icon == TransportIcon::record;
        const auto tint = recording ? colours::record : icon == TransportIcon::play && getToggleState()
            ? colours::signal : colours::action;
        const auto colour = getToggleState() ? colours::control.interpolatedWith (tint, 0.17f) : colours::well;
        getLookAndFeel().drawButtonBackground (g, *this, colour, hovered, down);
        const auto side = (float) appearance.px (17);
        const auto area = getLocalBounds().toFloat().withSizeKeepingCentre (side, side);
        g.setColour (tint.withAlpha (isEnabled() ? 1.0f : 0.38f));
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (side / 16.0f).translated (area.getX(), area.getY()));
        juce::Path path;
        switch (icon)
        {
            case TransportIcon::start:
                g.fillRoundedRectangle (2.0f, 3.0f, 2.0f, 10.0f, 0.7f);
                path.addTriangle (13.0f, 2.6f, 5.0f, 8.0f, 13.0f, 13.4f);
                g.fillPath (path);
                break;
            case TransportIcon::play:
                path.addTriangle (4.0f, 2.0f, 13.0f, 8.0f, 4.0f, 14.0f);
                g.fillPath (path);
                break;
            case TransportIcon::stop:
                g.fillRoundedRectangle (3.1f, 3.1f, 9.8f, 9.8f, 1.2f);
                break;
            case TransportIcon::record:
                g.drawEllipse (1.8f, 1.8f, 12.4f, 12.4f, 1.25f);
                g.fillEllipse (4.5f, 4.5f, 7.0f, 7.0f);
                break;
        }
    }

private:
    TransportIcon icon;
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
        const auto bounds = getLocalBounds().toFloat().reduced (0.75f);
        g.setColour (colours::well);
        g.fillRoundedRectangle (bounds, (float) appearance.px (4));
        g.setColour (colours::divider);
        g.drawRoundedRectangle (bounds, (float) appearance.px (4), 0.75f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (appearance.px (2), appearance.px (1));
        caption.setBounds (bounds.removeFromTop (appearance.line (9, 1)));
        value.setBounds (bounds);
    }

private:
    juce::Label caption, value;
};

enum class RailIcon { instruments, channel, stage, picture, articulations, piano, mixer, more };

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
        auto label = bounds.removeFromBottom (appearance.line (11, 2));
        const int side = juce::jmin (appearance.px (18), bounds.getHeight());
        const auto iconArea = bounds.withSizeKeepingCentre (side, side).toFloat();
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
        g.setFont (uiFont (10.5f));
        g.drawFittedText (getButtonText(), label, juce::Justification::centredTop, 1, 1.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale ((float) side / 18.0f)
                                              .translated (iconArea.getX(), iconArea.getY()));
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
            case RailIcon::picture:
                g.drawRoundedRectangle (2.0f, 3.0f, 14.0f, 12.0f, 1.0f, 1.2f);
                for (int x : { 4, 13 })
                    for (int y : { 5, 9, 13 })
                        g.fillRect (x, y, 2, 2);
                g.drawLine (8.0f, 6.0f, 11.5f, 9.0f, 1.3f);
                g.drawLine (11.5f, 9.0f, 8.0f, 12.0f, 1.3f);
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
