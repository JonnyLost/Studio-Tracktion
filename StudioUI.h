// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "AppearanceMetrics.h"
#include "StudioDesignTokens.h"
#include "StudioThemes.h"

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
inline juce::Colour rail { design::rail };
inline juce::Colour canvas { design::canvas };
inline juce::Colour timeline { design::timeline };
inline juce::Colour panel { design::panel };
inline juce::Colour control { design::control };
inline juce::Colour raised { design::raised };
inline juce::Colour divider { design::divider };
inline juce::Colour selected { design::selected };
inline juce::Colour well { design::well };
inline juce::Colour grid { design::grid };
inline juce::Colour text { design::text };
inline juce::Colour secondary { design::secondary };
inline juce::Colour action { design::action };
inline juce::Colour focus { design::focus };
inline juce::Colour playhead { design::playhead };
inline juce::Colour warning { design::warning };
inline juce::Colour record { design::record };
inline juce::Colour signal { design::signal };
inline const juce::Colour strings   { design::strings };
inline const juce::Colour winds     { design::winds };
inline const juce::Colour brass     { design::brass };
inline const juce::Colour percussion{ design::percussion };
inline const juce::Colour keys      { design::keys };
inline const juce::Colour other     { design::other };
inline juce::Colour midiClip { design::midiClip };
inline juce::Colour audioClip { design::audioClip };
inline const juce::Colour roomReturn{ design::roomReturn };
inline juce::Colour atelierAccent { design::atelierAccent };
}

// UI-only theme state. Project track colours and DSP settings are not changed.
inline int activeTheme = 0;
inline std::array<juce::Colour*, themes::count> themeSlots()
{
    return { &colours::rail, &colours::canvas, &colours::timeline, &colours::panel, &colours::control, &colours::raised, &colours::divider, &colours::selected, &colours::well, &colours::grid, &colours::text, &colours::secondary, &colours::action, &colours::focus, &colours::playhead, &colours::warning, &colours::record, &colours::signal, &colours::atelierAccent, &colours::midiClip, &colours::audioClip };
}
inline void installTheme(int index)
{
    activeTheme = themes::validIndex(index);
    auto slots=themeSlots();
    for(size_t i=0;i<slots.size();++i) *slots[i]=juce::Colour(themes::palettes[(size_t)activeTheme].argb[i]);
}
// Translate explicit component colours which were set from the previous
// shared palette; leave custom track and third-party editor colours alone.
inline void retintStudioComponent(juce::Component& root, int previous)
{
    using namespace juce;
    constexpr int ids[] { Label::textColourId, Label::backgroundColourId,
        TextButton::buttonColourId,TextButton::buttonOnColourId,TextButton::textColourOffId,TextButton::textColourOnId,
        ComboBox::backgroundColourId,ComboBox::textColourId,ComboBox::outlineColourId,ComboBox::focusedOutlineColourId,
        TextEditor::backgroundColourId,TextEditor::textColourId,TextEditor::outlineColourId,TextEditor::focusedOutlineColourId,
        Slider::trackColourId,Slider::rotarySliderFillColourId,Slider::rotarySliderOutlineColourId,Slider::thumbColourId,
        Slider::textBoxTextColourId,Slider::textBoxBackgroundColourId,Slider::textBoxOutlineColourId,Slider::backgroundColourId,
        ListBox::backgroundColourId, ListBox::textColourId, ScrollBar::thumbColourId };
    const auto& old = themes::palettes[(size_t)themes::validIndex(previous)].argb;
    const auto& next = themes::palettes[(size_t)activeTheme].argb;
    for(int id:ids) if(root.isColourSpecified(id))
    {
        auto value=root.findColour(id,false);
        for(size_t i=0;i<old.size();++i)
            if((value.getARGB() | 0xff000000u)==old[i])
            { root.setColour(id,Colour(next[i]).withAlpha(value.getFloatAlpha())); break; }
    }
    for(int i=0;i<root.getNumChildComponents();++i)
        if(auto* child=root.getChildComponent(i)) retintStudioComponent(*child,previous);
    root.repaint();
}

// Shared vector knob; paint-only, with no resource loading or audio state changes.
// Respect component-specific fill/thumb colours, and leave keyboard/mouse behaviour to JUCE.
inline void drawStudioRotary (juce::Graphics& g, int x, int y, int width, int height,
                              float position, float startAngle, float endAngle, juce::Slider& slider,
                              juce::Colour accent)
{
    using namespace juce;
    const float diameter = (float) jmin (width, height) * .80f;
    if (diameter < 8.0f) return;
    const auto centre = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre();
    const float radius = diameter * .5f;
    const float angle = startAngle + jlimit (0.0f, 1.0f, position) * (endAngle - startAngle);
    const float stroke = jlimit (1.0f, (float) appearance.px (3), radius * .18f);
    const float opacity = slider.isEnabled() ? 1.0f : .45f;
    if (themes::hardwareConsole (activeTheme))
    {
        const auto ink = Colour (themes::hardwareScale);
        const auto point = [&] (float a, float r) { return Point<float> (centre.x + std::sin (a) * r, centre.y - std::cos (a) * r); };
        constexpr int ticks = 24;
        for (int i = 0; i <= ticks; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / ticks;
            const auto p = point (a, radius * (i % 6 == 0 ? .78f : .87f));
            const auto q = point (a, radius);
            g.setColour ((a <= angle ? ink : colours::divider).withMultipliedAlpha (opacity));
            g.drawLine (p.x, p.y, q.x, q.y, jlimit (1.0f, 2.0f, radius * .075f));
        }
        auto face = Rectangle<float> (diameter * .69f, diameter * .69f).withCentre (centre);
        g.setColour (colours::well.withMultipliedAlpha (opacity));
        g.fillEllipse (face.expanded (1.0f));
        g.setColour (colours::control.withMultipliedAlpha (opacity));
        g.fillEllipse (face);
        g.setColour (colours::divider.withMultipliedAlpha (opacity));
        g.drawEllipse (face, 1.0f);
        auto cap = face.reduced (face.getWidth() * .16f);
        g.setColour (Colour (themes::hardwareCap).interpolatedWith (colours::control, .42f).withMultipliedAlpha (opacity));
        g.fillEllipse (cap);
        const auto p = point (angle, radius * .14f), q = point (angle, radius * .59f);
        g.setColour (Colour (themes::hardwareIvory).withMultipliedAlpha (opacity));
        g.drawLine (p.x, p.y, q.x, q.y, stroke);
        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (colours::focus);
            g.drawEllipse (face.expanded (1.5f), 1.5f);
        }
        return;
    }
    Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
    g.setColour (colours::divider.withMultipliedAlpha (opacity));
    g.strokePath (track, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
    if (angle > startAngle)
    {
        Path active;
        active.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, angle, true);
        g.setColour (accent.withMultipliedAlpha (opacity));
        g.strokePath (active, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const auto face = Rectangle<float> (diameter * .73f, diameter * .73f).withCentre (centre);
    g.setColour (colours::control.withMultipliedAlpha (opacity));
    g.fillEllipse (face);
    g.setColour (slider.hasKeyboardFocus (true) ? colours::focus : colours::divider);
    g.drawEllipse (face.reduced (.5f), slider.hasKeyboardFocus (true) ? 1.5f : 1.0f);
    const auto inner = Point<float> (centre.x + std::sin (angle) * radius * .25f,
                                     centre.y - std::cos (angle) * radius * .25f);
    const auto outer = Point<float> (centre.x + std::sin (angle) * radius * .60f,
                                     centre.y - std::cos (angle) * radius * .60f);
    g.setColour (slider.findColour (Slider::thumbColourId).withMultipliedAlpha (opacity));
    g.drawLine (inner.x, inner.y, outer.x, outer.y, stroke);
}

class StudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel() { refreshTheme(); }
    void refreshTheme()
    {
        using namespace juce;
        setColourScheme (themes::palettes[(size_t)activeTheme].light ? getLightColourScheme() : getDarkColourScheme());
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
        setColour (Slider::rotarySliderFillColourId, colours::action);
        setColour (Slider::rotarySliderOutlineColourId, colours::divider);
        setColour (Slider::textBoxTextColourId, colours::text);
        setColour (Slider::textBoxBackgroundColourId, colours::well);
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
        return juce::FontOptions (juce::jmin (appearance.fontSize (design::body), (float) height * 0.8f));
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

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        drawStudioRotary (g, x, y, width, height, position, startAngle, endAngle, slider,
                          slider.findColour (juce::Slider::rotarySliderFillColourId));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float minimum, float maximum, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool hardware = themes::hardwareConsole (activeTheme);
        if (style != juce::Slider::LinearVertical || (! slider.getProperties().contains ("studioFader")
            && ! (hardware && slider.getProperties().contains ("studioHardwareFader"))))
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, minimum, maximum, style, slider);
            return;
        }
        const auto centre = (float) x + (float) width * 0.5f;
        const auto groove = juce::Rectangle<float> (centre - (float) appearance.px (2), (float) y,
                                                     (float) appearance.px (4), (float) height);
        if (hardware && width >= 12 && height > 0)
        {
            using namespace juce;
            const float opacity = slider.isEnabled() ? 1.0f : .45f;
            const float halfWidth = (float) width * .5f;
            const float halfThumb = jmin (halfWidth - 1.0f, (float) appearance.px (13));
            const float tickOuter = jmin (halfWidth - 1.0f, (float) appearance.px (17));
            const float tickInner = jmin (tickOuter, (float) appearance.px (6));
            g.setColour (colours::well.withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (groove.expanded (1.0f, 0.0f), 2.0f);
            for (int i = 0; i <= 10; ++i)
            {
                const float yy = (float) y + (float) height * i / 10.0f;
                g.setColour ((i % 5 == 0 ? Colour (themes::hardwareScale) : colours::secondary).withMultipliedAlpha (opacity));
                g.drawLine (centre - tickOuter, yy, centre - tickInner, yy, 1.0f);
                g.drawLine (centre + tickInner, yy, centre + tickOuter, yy, 1.0f);
            }
            const float halfHeight = (float) appearance.px (8);
            const auto thumb = Rectangle<float> (2.0f * halfThumb, 2.0f * halfHeight).withCentre ({ centre, position });
            g.setColour (colours::well.withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (thumb.translated (0.0f, 1.0f), 2.0f);
            g.setColour (Colour (themes::hardwareCap).withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (thumb, 2.0f);
            g.setColour ((slider.hasKeyboardFocus (true) ? colours::focus : colours::divider).withMultipliedAlpha (opacity));
            g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
            g.setColour (colours::well.withMultipliedAlpha (opacity));
            g.drawLine (thumb.getX() + 2.0f, position, thumb.getRight() - 2.0f, position, 1.5f);
            return;
        }
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
        const bool ivoryPad = themes::hardwareConsole (activeTheme) && button.getToggleState()
            && button.getProperties().contains ("studioArticulationPad");
        const auto ink = ivoryPad ? juce::Colour (themes::hardwareInk)
            : button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour (ink.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (appearance.px (3), 0),
                          juce::Justification::centred, 1, 1.0f);
    }

    // In Picture view the embedded AVKit NSView renders above JUCE's ordinary
    // tooltip peer. Keep help bubbles entirely in the side consoles instead.
    void setNativeVideoTooltipAvoidance (juce::Rectangle<int> screenBounds)
    {
        nativeVideoTooltipAvoidance = screenBounds;
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> position,
                                           juce::Rectangle<int> available) override
    {
        const int inset = appearance.px (7);
        const auto placeIn = [&] (juce::Rectangle<int> bounds)
        {
            const int contentWidth = juce::jmax (1, juce::jmin (bounds.getWidth() - 2 * inset,
                                                                appearance.span (355)));
            auto layout = tooltipLayout (text, contentWidth);
            return juce::Rectangle<int> (position.x + inset, position.y + appearance.px (16),
                juce::roundToInt (std::ceil (layout.getWidth())) + 2 * inset,
                juce::roundToInt (std::ceil (layout.getHeight())) + 2 * inset).constrainedWithin (bounds);
        };

        const auto preferred = placeIn (available);
        if (nativeVideoTooltipAvoidance.isEmpty())
            return preferred;
        const auto excluded = nativeVideoTooltipAvoidance.expanded (appearance.px (6));
        if (! preferred.intersects (excluded))
            return preferred;

        auto left = juce::Rectangle<int> (available.getX(), available.getY(),
            juce::jmax (0, excluded.getX() - available.getX()), available.getHeight());
        auto right = juce::Rectangle<int> (excluded.getRight(), available.getY(),
            juce::jmax (0, available.getRight() - excluded.getRight()), available.getHeight());

        // Prefer the side with the hovered control. Constrain and reflow long
        // descriptions instead of drawing them underneath the native picture.
        auto safe = position.x < excluded.getCentreX() ? left : right;
        if (safe.getWidth() < appearance.span (125))
            safe = left.getWidth() > right.getWidth() ? left : right;
        return safe.getWidth() >= 2 * inset + 1 ? placeIn (safe) : preferred;
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
        const bool hardware = themes::hardwareConsole (activeTheme);
        const bool pad = hardware && button.getToggleState() && button.getProperties().contains ("studioArticulationPad");
        const auto base = pad ? juce::Colour (themes::hardwareIvory) : colour;
        const auto fill = pad ? (down ? base.darker (.12f) : base)
            : down ? base.brighter (0.2f) : hover ? base.brighter (0.1f) : base;
        g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        const float radius = (float) appearance.px (hardware ? 3 : design::corner);
        g.fillRoundedRectangle (bounds, radius);
        if (hardware && !pad && bounds.getHeight() > 5.0f && bounds.getWidth() > 6.0f)
        {
            g.setColour (fill.brighter (.20f).withAlpha (button.isEnabled() ? .65f : .25f));
            g.drawHorizontalLine (juce::roundToInt (bounds.getY() + 1.0f), bounds.getX() + 3.0f, bounds.getRight() - 3.0f);
        }
        const bool active = button.getToggleState() && button.isEnabled();
        g.setColour (active ? colours::action : colours::divider.withAlpha (button.isEnabled() ? .85f : .35f));
        g.drawRoundedRectangle (bounds, radius, active ? 1.5f : 1.0f);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.0f);
        }
    }

private:
    juce::Rectangle<int> nativeVideoTooltipAvoidance;

    static juce::TextLayout tooltipLayout (const juce::String& text, int width)
    {
        juce::AttributedString content;
        content.append (text, uiFont (13.0f), colours::text);
        juce::TextLayout layout;
        layout.createLayout (content, (float) width);
        return layout;
    }
};

enum class TransportIcon { start, play, stop, record, captureMidi, cycle, click };

class TransportButton final : public juce::TextButton
{
public:
    TransportButton (const char* label, TransportIcon symbol) : juce::TextButton (label), icon (symbol)
    {
        setTitle (label);
        setTooltip (label);
    }

    std::function<void()> onContextMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu) onContextMenu();
            return;
        }
        juce::TextButton::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool hovered, bool down) override
    {
        const bool recording = icon == TransportIcon::record || icon == TransportIcon::captureMidi;
        const bool hardware = themes::hardwareConsole (activeTheme);
        const bool redKey = hardware && icon == TransportIcon::record;
        const auto tint = hardware ? (icon == TransportIcon::captureMidi ? colours::record : colours::text)
            : recording ? colours::record : getToggleState() ? colours::signal : colours::action;
        const auto colour = redKey ? juce::Colour (themes::hardwareRecord)
            : hardware ? (getToggleState() ? colours::raised : colours::control)
            : getToggleState() ? colours::control.interpolatedWith (tint, 0.17f) : colours::well;
        getLookAndFeel().drawButtonBackground (g, *this, colour, hovered, down);
        if (hardware && getToggleState() && isEnabled())
        {
            const float led = (float) appearance.px (4);
            g.setColour (redKey ? colours::text : colours::signal);
            g.fillEllipse ((float) getWidth() - led - 3.0f, 3.0f, led, led);
        }
        const auto side = juce::jlimit ((float) appearance.px (17), (float) appearance.px (29),
                                        (float) getHeight() * 0.57f);
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
            case TransportIcon::captureMidi:
                // Render with JUCE primitives, never font glyphs that may mojibake.
                g.drawEllipse (5.0f, 5.0f, 9.0f, 9.0f, 1.3f);
                g.fillEllipse (7.3f, 7.3f, 4.4f, 4.4f);
                path.startNewSubPath (8.0f, 3.5f);
                path.cubicTo (4.8f, 1.0f, 1.3f, 3.8f, 2.0f, 7.0f);
                g.strokePath (path, juce::PathStrokeType (1.3f));
                path.clear();
                path.addTriangle (0.2f, 6.0f, 4.2f, 6.0f, 1.8f, 9.3f);
                g.fillPath (path);
                break;
            case TransportIcon::cycle:
                g.drawLine (2.0f, 5.2f, 13.4f, 5.2f, 1.35f);
                g.drawLine (13.3f, 5.2f, 10.1f, 2.0f, 1.35f);
                g.drawLine (13.3f, 5.2f, 10.1f, 8.4f, 1.35f);
                g.drawLine (13.6f, 10.8f, 2.2f, 10.8f, 1.35f);
                g.drawLine (2.3f, 10.8f, 5.5f, 7.6f, 1.35f);
                g.drawLine (2.3f, 10.8f, 5.5f, 14.0f, 1.35f);
                break;
            case TransportIcon::click:
                path.startNewSubPath (5.8f, 2.2f);
                path.lineTo (10.2f, 2.2f);
                path.lineTo (13.5f, 13.7f);
                path.lineTo (2.5f, 13.7f);
                path.closeSubPath();
                g.strokePath (path, juce::PathStrokeType (1.25f));
                g.drawLine (8.0f, 11.0f, 13.8f, 3.0f, 1.2f);
                break;
        }
    }

private:
    TransportIcon icon;
};

class TransportReadout final : public juce::TextButton
{
public:
    explicit TransportReadout (const juce::String& name, bool adjustable = false)
        : juce::TextButton (name), label (name), canEdit (adjustable)
    {
        setTitle (name);
        setWantsKeyboardFocus (adjustable);
        setMouseCursor (adjustable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    std::function<void()> onContextMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu) onContextMenu();
            return;
        }
        juce::TextButton::mouseDown (e);
    }

    void setValue (const juce::String& content)
    {
        if (value == content) return;
        value = content;
        setTitle (label + " " + value);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool hovered, bool down) override
    {
        auto bounds = getLocalBounds().reduced (appearance.px (2), appearance.px (1));
        if (canEdit && (hovered || hasKeyboardFocus (true)) && isEnabled())
        {
            g.setColour (colours::selected.withAlpha (down ? .75f : .45f));
            g.fillRoundedRectangle (bounds.toFloat(), (float) appearance.px (4));
        }
        if (hasKeyboardFocus (true))
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (bounds.toFloat().reduced (.5f), (float) appearance.px (4), 1);
        }
        const bool large = getHeight() >= appearance.px (48);
        g.setColour (colours::secondary);
        g.setFont (uiFont (large ? 11.0f : 9.5f, juce::Font::bold));
        const auto caption = bounds.removeFromTop (appearance.line (large ? 12 : 10, 1));
        g.drawFittedText (label.toUpperCase() + (canEdit ? "  ›" : ""), caption, juce::Justification::centred, 1, .88f);
        const float size = label == "Bars / Beats" ? (large ? 25.0f : 20.0f)
                                                    : (large ? 19.0f : 15.5f);
        g.setColour (label == "Bars / Beats" ? colours::signal : colours::text);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), appearance.fontSize (size), juce::Font::bold));
        g.drawFittedText (value, bounds, juce::Justification::centred, 1, .9f);
    }

private:
    juce::String label, value;
    const bool canEdit;
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
