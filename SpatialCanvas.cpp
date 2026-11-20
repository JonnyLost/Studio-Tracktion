// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialWorkspace.h"
#include "StudioUI.h"
#include <numbers>

using namespace juce;

namespace studio::spatial
{
SpatialField::SpatialField (const char* title, double min, double max, double interval, const char* suffix)
{
    label.setText (title, dontSendNotification);
    label.setFont (FontOptions (12.0f));
    label.setColour (Label::textColourId, colours::secondary);
    label.setTooltip (title);
    number.setRange (min, max, interval);
    number.setName (title);
    number.setTooltip (title);
    number.setTextValueSuffix (suffix);
    number.onValueChange = [this] { if (onChange) onChange(); };
    addAndMakeVisible (label);
    addAndMakeVisible (number);
}

void SpatialField::set (double value) { number.setValue (value, dontSendNotification); }
float SpatialField::get() const { return (float) number.getValue(); }
void SpatialField::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (3), m.px (2));
    label.setBounds (area.removeFromTop (m.line (12, 1)));
    number.setTextBoxStyle (Slider::TextBoxLeft, false, jmin (m.span (104), area.getWidth() * 2 / 3), m.line (13, 2));
    number.setBounds (area);
}

Rectangle<float> SpatialMap::plot() const
{
    const auto& m = appearance;
    return getLocalBounds().toFloat().reduced ((float) m.px (30), (float) m.px (24));
}

Point<float> SpatialMap::screen (Position p) const
{
    const auto area = plot();
    return { area.getX() + 0.5f * (p.x + 1.0f) * area.getWidth(), area.getBottom() - p.depth * area.getHeight() };
}

Position SpatialMap::location (Point<float> p) const
{
    const auto area = plot();
    if (area.isEmpty()) return {};
    return validPosition ({ 2.0f * (p.x - area.getX()) / area.getWidth() - 1.0f,
                            (area.getBottom() - p.y) / area.getHeight() });
}

void SpatialMap::display (std::vector<Marker> next, String key, bool microphones)
{
    if (markers != next || selected != key || micMode != microphones)
    {
        markers = std::move (next);
        selected = std::move (key);
        micMode = microphones;
        repaint();
    }
}

void SpatialMap::paint (Graphics& g)
{
    const auto& m = appearance;
    const auto area = plot();
    if (area.isEmpty()) return;
    g.setColour (colours::timeline);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) m.px (6));
    constexpr std::array bands { 0x18233b58, 0x222c4059, 0x2a394956, 0x35445258, 0x4154646c };
    for (int i = 0; i < 5; ++i)
    {
        const float y = area.getY() + (float) i * area.getHeight() / 5.0f;
        g.setColour (Colour (bands[(size_t) i]));
        g.fillRect (Rectangle<float> (area.getX(), y, area.getWidth(), area.getHeight() / 5.0f));
        g.setColour (colours::divider);
        g.drawLine (area.getX(), y, area.getRight(), y);
    }
    g.setColour (colours::divider);
    g.drawVerticalLine (roundToInt (area.getCentreX()), area.getY(), area.getBottom());
    g.setFont (uiFont (11.0f));
    g.setColour (colours::secondary);
    g.drawText ("Rear", 0, 0, getWidth(), m.px (24), Justification::centred);
    g.drawText ("Conductor / Front", 0, getHeight() - m.px (23), getWidth(), m.px (22), Justification::centred);
    g.drawText ("L", m.px (5), roundToInt (area.getCentreY()) - m.line (11) / 2, m.span (23), m.line (11), Justification::centred);
    g.drawText ("R", getWidth() - m.span (23), roundToInt (area.getCentreY()) - m.line (11) / 2, m.span (18), m.line (11), Justification::centred);

    constexpr std::array hues { 0xff95b4d7, 0xff7cc8c3, 0xffdab582, 0xffc594ad, 0xffb7a5d4, 0xffa8b5c2 };
    const auto drawMarker = [&] (const Marker& marker)
    {
        const bool active = marker.microphone == micMode && marker.key == selected;
        const auto point = screen (moving && marker.key == dragged && marker.microphone == micMode ? preview : marker.point);
        if (marker.microphone)
        {
            const float radius = (float) m.px (13);
            const float azimuth = marker.aim * std::numbers::pi_v<float> / 180.0f;
            const auto direction = Point<float> (std::sin (azimuth), -std::cos (azimuth));
            const auto tint = colours::warning.withAlpha (micMode ? 0.85f : 0.52f);
            g.setColour (tint.withAlpha (0.23f));
            Path polar;
            for (int i = 0; i <= 24; ++i)
            {
                const auto a = (float) i * std::numbers::pi_v<float> / 12.0f;
                const auto c = std::cos (a);
                const float response = marker.pattern == Pattern::omni ? 1.0f : marker.pattern == Pattern::figureEight
                    ? std::abs (c) : marker.pattern == Pattern::supercardioid ? std::abs (0.37f + 0.63f * c) : 0.5f + 0.5f * c;
                const auto p = point + Point<float> (std::sin (azimuth + a), -std::cos (azimuth + a)) * (radius * 2.4f * response);
                if (i == 0) polar.startNewSubPath (p); else polar.lineTo (p);
            }
            polar.closeSubPath();
            g.fillPath (polar);
            g.setColour (tint);
            g.drawLine ({ point, point + direction * (radius * 2.1f) }, (float) m.px (2));
            Path diamond;
            diamond.addQuadrilateral (point.x, point.y - radius, point.x + radius, point.y,
                                      point.x, point.y + radius, point.x - radius, point.y);
            g.fillPath (diamond);
        }
        else
        {
            const auto r = (float) m.px (10);
            g.setColour (Colour (hues[(size_t) marker.section]).withAlpha (micMode ? 0.32f : 0.88f));
            g.fillEllipse (point.x - r, point.y - r, 2 * r, 2 * r);
            if (active)
            {
                g.setColour (colours::focus);
                g.drawEllipse (point.x - r - 2.0f, point.y - r - 2.0f, 2 * r + 4.0f, 2 * r + 4.0f, 2.0f);
            }
        }
        if (! micMode || marker.microphone)
        {
            const int labelWidth = m.span (125);
            const auto left = jlimit (0, jmax (0, getWidth() - labelWidth), roundToInt (point.x) - labelWidth / 2);
            g.setFont (uiFont (10.5f, active ? Font::bold : Font::plain));
            g.setColour (active ? colours::focus : colours::text);
            const int labelY = roundToInt (point.y) + m.px (12);
            g.drawFittedText (marker.name, { left, labelY, labelWidth, m.line (10, 2) },
                              Justification::centred, 1, 1.0f);
        }
    };
    for (const auto& marker : markers)
        if (marker.key != selected || marker.microphone != micMode) drawMarker (marker);
    for (const auto& marker : markers)
        if (marker.key == selected && marker.microphone == micMode) drawMarker (marker);
}

void SpatialMap::mouseDown (const MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown()) return;
    auto distance = (float) appearance.px (22);
    const Marker* best = nullptr;
    for (const auto& marker : markers)
        if (marker.microphone == micMode)
        {
            const auto d = screen (marker.point).getDistanceFrom (event.position);
            if (d < distance || (d == distance && marker.key == selected))
            {
                best = &marker;
                distance = d;
            }
        }

    if (best == nullptr) return;
    const auto key = best->key;
    dragOrigin = preview = best->point;
    dragged = key;
    moving = false;
    grabKeyboardFocus();
    if (onSelect) onSelect (key);
}

void SpatialMap::mouseDrag (const MouseEvent& event)
{
    if (dragged.isEmpty() || ! event.mouseWasDraggedSinceMouseDown()) return;
    preview = location (screen (dragOrigin) + event.getOffsetFromDragStart().toFloat());
    moving = true;
    repaint();
}

void SpatialMap::mouseUp (const MouseEvent& event)
{
    const bool commit = moving && preview != dragOrigin;
    const auto key = dragged;
    moving = false;
    dragged.clear();
    if (commit && onMove) onMove (key, preview, event.mods.isShiftDown());
    repaint();
}

bool SpatialMap::keyPressed (const KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code != KeyPress::leftKey && code != KeyPress::rightKey && code != KeyPress::upKey && code != KeyPress::downKey)
        return false;
    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown()) return false;
    for (const auto& marker : markers)
        if (marker.key == selected && marker.microphone == micMode && onMove)
        {
            auto next = marker.point;
            next.x += code == KeyPress::leftKey ? -0.02f : code == KeyPress::rightKey ? 0.02f : 0;
            next.depth += code == KeyPress::downKey ? -0.02f : code == KeyPress::upKey ? 0.02f : 0;
            onMove (selected, validPosition (next), key.getModifiers().isShiftDown());
            return true;
        }
    return false;
}

void RoomGraphic::setRoom (Room next)
{
    next = validate (next);
    if (next != value) { value = next; repaint(); }
}

void RoomGraphic::paint (Graphics& g)
{
    const auto& m = appearance;
    auto row = getLocalBounds().reduced (m.px (6), m.px (3));
    const int available = row.getWidth();
    auto plan = row.removeFromLeft (available / 3).reduced (m.px (3));
    auto chart = row.reduced (m.px (4));
    g.setColour (colours::timeline);
    g.fillRoundedRectangle (plan.toFloat(), (float) m.px (4));
    g.fillRoundedRectangle (chart.toFloat(), (float) m.px (4));
    plan.removeFromTop (m.line (11, 3));
    plan.reduce (m.px (10), m.px (7));
    const float scale = jmin ((float) plan.getWidth() / value.width, (float) plan.getHeight() / value.length);
    const auto footprint = plan.toFloat().withSizeKeepingCentre (scale * value.width, scale * value.length);
    if (! footprint.isEmpty())
    {
        g.setColour (colours::selected);
        g.fillRect (footprint);
        g.setColour (colours::focus);
        g.drawRect (footprint, 1.0f);
        g.fillRect (footprint.withHeight (jmin (footprint.getHeight(), (float) m.px (5))));
    }
    g.setFont (uiFont (11.0f));
    g.setColour (colours::text);
    g.drawFittedText ("Room proportions", getLocalBounds().withWidth (available / 3).withHeight (m.line (11, 8)),
                      Justification::centred, 1, 1.0f);
    g.drawFittedText ("Estimated decay targets (seconds)", chart.removeFromTop (m.line (11, 3)),
                      Justification::centred, 1, 1.0f);
    const auto decay = predictDecay (value);
    const float top = jmax (1.0f, *std::max_element (decay.begin(), decay.end()));
    for (size_t i = 0; i < decay.size(); ++i)
    {
        auto col = chart.removeFromLeft (chart.getWidth() / (int) (decay.size() - i)).reduced (m.px (3), 0);
        g.setColour (colours::secondary);
        g.drawFittedText (bandNames[i], col.removeFromBottom (m.line (10, 2)), Justification::centred, 1, 1.0f);
        g.drawFittedText (String (decay[i], 1), col.removeFromTop (m.line (11, 1)), Justification::centred, 1, 1.0f);
        g.setColour (colours::action.withAlpha (0.66f));
        g.fillRect (col.withSizeKeepingCentre (jmax (1, col.getWidth() / 2), col.getHeight())
                       .removeFromBottom (roundToInt ((float) col.getHeight() * decay[i] / top)));
    }
}
}
