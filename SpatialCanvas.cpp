// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialWorkspace.h"
#include "StudioUI.h"
#include "StagePresentation.h"
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

namespace
{
const std::array stageColours { colours::strings, colours::winds, colours::brass,
                               colours::percussion, colours::keys, colours::other };
constexpr std::array familyMarks { "S", "W", "B", "P", "K", "O" };
}

int SpatialMap::legendHeight() const
{
    const int columns = getWidth() >= appearance.span (590) ? 6 : 3;
    return micMode || overview ? 0 : (6 / columns) * appearance.line (11, 5) + appearance.px (5);
}

Rectangle<float> SpatialMap::plot() const
{
    const auto& m = appearance;
    if (! micMode)
    {
        if (overview) return getLocalBounds().toFloat().reduced (5.0f, 2.0f);
        return getLocalBounds().toFloat().reduced ((float) m.px (21), (float) m.px (12))
            .withTrimmedBottom ((float) (m.line (10, 10) + legendHeight()));
    }
    return getLocalBounds().toFloat().reduced ((float) m.px (30), (float) m.px (10))
        .withTrimmedBottom ((float) (m.line (11, 12) + legendHeight()));
}

Point<float> SpatialMap::screen (Position p) const
{
    const auto area = plot();
    if (! micMode)
    {
        const auto [x, d] = StageSemicircle::project (p);
        return { area.getCentreX() + x * area.getWidth() * 0.5f, area.getBottom() - d * area.getHeight() };
    }
    const auto projected = stagePresentations[(size_t) seating].project (p);
    return { area.getX() + projected[0] * area.getWidth(), area.getY() + projected[1] * area.getHeight() };
}

Position SpatialMap::location (Point<float> p) const
{
    const auto area = plot();
    if (area.isEmpty()) return {};
    if (! micMode)
        return StageSemicircle::unproject ((p.x - area.getCentreX()) * 2.0f / area.getWidth(),
                                           (area.getBottom() - p.y) / area.getHeight());
    return stagePresentations[(size_t) seating].unproject ((p.x - area.getX()) / area.getWidth(),
                                                          (p.y - area.getY()) / area.getHeight());
}

bool SpatialMap::containsStagePoint (Point<float> p) const
{
    const auto area = plot();
    return ! area.isEmpty() && StageSemicircle::contains ((p.x - area.getCentreX()) * 2.0f / area.getWidth(),
                                                         (area.getBottom() - p.y) / area.getHeight());
}

void SpatialMap::setOverview (bool value)
{
    if (overview == value) return;
    overview = value;
    setInterceptsMouseClicks (! value, ! value);
    setWantsKeyboardFocus (! value);
    dragged.clear();
    moving = dropTarget = false;
    repaint();
}

bool SpatialMap::isInterestedInDragSource (const SourceDetails& details)
{
    return ! micMode && ! overview && trackFromDrag && trackFromDrag (details).isNotEmpty();
}

void SpatialMap::itemDragMove (const SourceDetails& details)
{
    const bool next = containsStagePoint (details.localPosition.toFloat());
    if (next != dropTarget) { dropTarget = next; repaint(); }
}

void SpatialMap::itemDragExit (const SourceDetails&) { dropTarget = false; repaint(); }

void SpatialMap::itemDropped (const SourceDetails& details)
{
    itemDragExit (details);
    if (! isInterestedInDragSource (details) || ! containsStagePoint (details.localPosition.toFloat())) return;
    const auto key = trackFromDrag (details);
    if (onMove) onMove (key, location (details.localPosition.toFloat()), false);
    if (onSelect) onSelect (key);
}

void SpatialMap::display (std::vector<Marker> next, String key, bool microphones, Seating layout)
{
    if (markers != next || selected != key || micMode != microphones || seating != layout)
    {
        markers = std::move (next);
        selected = std::move (key);
        micMode = microphones;
        seating = layout;
        repaint();
    }
}

void SpatialMap::paintSeating (Graphics& g, Rectangle<float> area) const
{
    const auto& m = appearance;
    const auto& style = stagePresentations[(size_t) seating];
    const auto arc = [&] (Path& path, float depth, float width, bool start, bool reverse)
    {
        for (int i = 0; i <= 20; ++i)
        {
            const float x = width * (1.0f - (float) i * 0.1f) * (reverse ? 1.0f : -1.0f);
            const auto p = screen ({ x, depth });
            if (i == 0 && start) path.startNewSubPath (p);
            else path.lineTo (p);
        }
    };

    Path shell;
    arc (shell, 1.0f, 1.0f, true, false);
    arc (shell, 0.0f, 1.0f, false, true);
    shell.closeSubPath();
    g.setGradientFill (ColourGradient (colours::control, area.getCentreX(), area.getBottom(),
                                       colours::well, area.getCentreX(), area.getY(), false));
    g.fillPath (shell);
    g.setColour (colours::divider.brighter (0.32f));
    g.strokePath (shell, PathStrokeType (1.25f));

    for (int i = 0; i < style.rowCount; ++i)
    {
        const float depth = style.rows[(size_t) i];
        const float width = seating == Seating::scoring ? 0.87f : 0.91f;
        Path band;
        arc (band, depth + 0.047f, width, true, false);
        arc (band, depth - 0.047f, width, false, true);
        band.closeSubPath();
        g.setColour (stageColours[(size_t) i].withAlpha (0.085f));
        g.fillPath (band);
        Path row;
        arc (row, depth, width, true, false);
        g.setColour (stageColours[(size_t) i].withAlpha (0.28f));
        g.strokePath (row, PathStrokeType (1.0f));
    }

    const auto centre = screen ({ 0.0f, 0.0f });
    const float podium = (float) m.px (7);
    g.setColour (colours::action);
    g.fillRoundedRectangle ({ centre.x - podium, centre.y - podium * 0.55f, 2 * podium, podium }, 2.0f);
    g.setFont (uiFont (11.0f, Font::bold));
    g.drawText ("Conductor", 0, roundToInt (centre.y + podium * 0.5f), getWidth(), m.line (11, 5),
                Justification::centred);

}

void SpatialMap::paintSemicircle (Graphics& g, Rectangle<float> area) const
{
    const auto& m = appearance;
    const auto arc = [&] (float scale)
    {
        Path path;
        for (int i = 0; i <= 48; ++i)
        {
            const float a = (float) i * std::numbers::pi_v<float> / 48.0f;
            const Point<float> p { area.getCentreX() + std::cos (a) * area.getWidth() * 0.5f * scale,
                                   area.getBottom() - std::sin (a) * area.getHeight() * scale };
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        return path;
    };
    auto floor = arc (1);
    floor.closeSubPath();
    g.setGradientFill (ColourGradient (colours::control, area.getCentreX(), area.getBottom(),
                                       colours::panel, area.getCentreX(), area.getY(), false));
    g.fillPath (floor);
    g.setColour (dropTarget ? colours::focus : colours::divider);
    g.strokePath (floor, PathStrokeType (dropTarget ? 2.0f : 1.2f));
    if (overview) return;
    g.setColour (colours::divider.withAlpha (0.55f));
    for (float radius : { 0.26f, 0.48f, 0.70f, 0.90f })
        g.strokePath (arc (radius), PathStrokeType (0.8f));
    g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
    g.setColour (colours::focus);
    g.fillEllipse (Rectangle<float> ((float) m.px (5), (float) m.px (5)).withCentre ({ area.getCentreX(), area.getBottom() + m.px (5) }));
    g.setFont (uiFont (10.0f, Font::bold));
    g.setColour (colours::secondary);
    g.drawText ("Conductor", 0, roundToInt (area.getBottom()) + m.px (8), getWidth(), m.line (10, 2), Justification::centred);
    {
        auto legend = getLocalBounds().removeFromBottom (legendHeight()).reduced (m.px (8), 0);
        const int columns = getWidth() >= m.span (590) ? 6 : 3;
        const int itemWidth = legend.getWidth() / columns;
        const int height = m.line (11, 5);
        for (int i = 0; i < (int) stageColours.size(); ++i)
        {
            auto item = Rectangle<int> (legend.getX() + (i % columns) * itemWidth,
                legend.getY() + (i / columns) * height, itemWidth, height).reduced (m.px (3), 0);
            const auto swatch = item.removeFromLeft (m.px (15)).withSizeKeepingCentre (m.px (7), m.px (7));
            g.setColour (stageColours[(size_t) i]);
            g.fillRoundedRectangle (swatch.toFloat(), 2.0f);
            g.setColour (colours::secondary);
            g.setFont (uiFont (11.0f));
            g.drawFittedText (sectionNames[(size_t) i], item, Justification::centredLeft, 1, 1.0f);
        }
    }
}

void SpatialMap::paint (Graphics& g)
{
    const auto& m = appearance;
    const auto area = plot();
    g.setColour (colours::timeline);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) m.px (6));
    if (area.isEmpty()) return;
    if (micMode) paintSeating (g, area); else paintSemicircle (g, area);
    if (overview)
    {
        for (const auto& marker : markers)
        {
            g.setColour (stageColours[(size_t) jlimit (0, 5, marker.section)]);
            g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (screen (marker.point)));
        }
        return;
    }
    if (! micMode && markers.empty())
    {
        g.setFont (uiFont (13.0f));
        g.setColour (colours::secondary);
        g.drawFittedText ("Drag a project track onto the stage", area.toNearestInt(), Justification::centred, 2);
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::focus);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), (float) m.px (6), 1.25f);
    }
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
            const auto family = (size_t) jlimit (0, 5, marker.section);
            const float radius = (float) m.px (11);
            const auto tile = Rectangle<float> (2 * radius, 2 * radius).withCentre (point);
            const auto accent = stageColours[family];
            g.setColour (accent.interpolatedWith (colours::well, micMode ? 0.83f : 0.68f));
            if (micMode) g.fillRoundedRectangle (tile, (float) m.px (5)); else g.fillEllipse (tile);
            g.setColour (active ? colours::focus : accent.withAlpha (micMode ? 0.35f : 0.82f));
            if (micMode) g.drawRoundedRectangle (tile, (float) m.px (5), active ? 2.0f : 1.0f);
            else g.drawEllipse (tile, active ? 2.0f : 1.0f);
            g.setFont (uiFont (10.0f, Font::bold));
            g.setColour (accent.withAlpha (micMode ? 0.55f : 1.0f));
            g.drawText (familyMarks[family], tile.toNearestInt(), Justification::centred);
        }
        if (active || (! micMode && markers.size() <= 24))
        {
            const int labelWidth = m.span (active ? 125 : 88);
            const auto left = jlimit (0, jmax (0, getWidth() - labelWidth), roundToInt (point.x) - labelWidth / 2);
            g.setFont (uiFont (10.5f, active ? Font::bold : Font::plain));
            g.setColour (active ? colours::focus : colours::text);
            const int labelHeight = m.line (10, 2);
            int labelY = roundToInt (point.y) + m.px (13);
            if (labelY + labelHeight > roundToInt (area.getBottom()))
                labelY = roundToInt (point.y) - m.px (15) - labelHeight;
            const auto label = Rectangle<int> (left, labelY, labelWidth, labelHeight);
            g.setColour (colours::well);
            if (active) g.fillRoundedRectangle (label.toFloat(), (float) m.px (3));
            g.setColour (colours::text);
            g.drawFittedText (marker.name, label.reduced (m.px (4), 0), Justification::centred, 1, 1.0f);
        }
    };
    for (const auto& marker : markers)
        if (marker.key != selected || marker.microphone != micMode) drawMarker (marker);
    for (const auto& marker : markers)
        if (marker.key == selected && marker.microphone == micMode) drawMarker (marker);
}

void SpatialMap::mouseDown (const MouseEvent& event)
{
    const auto radius = (float) appearance.px (17);
    if (event.mods.isPopupMenu())
    {
        PopupMenu menu;
        const SafePointer<SpatialMap> safe (this);
        for (const auto& marker : markers)
            if (marker.microphone == micMode && screen (marker.point).getDistanceFrom (event.position) <= radius)
                menu.addItem (marker.name, true, marker.key == selected, [safe, key = marker.key]
                {
                    if (safe != nullptr && safe->onSelect) safe->onSelect (key);
                });
        if (menu.getNumItems() > 0) menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (
            Rectangle<int> (event.getScreenPosition().x, event.getScreenPosition().y, 1, 1)));
        return;
    }
    if (! event.mods.isLeftButtonDown()) return;
    auto distance = radius;
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
    if (isShowing()) grabKeyboardFocus();
    if (onSelect) onSelect (key);
}

void SpatialMap::mouseDrag (const MouseEvent& event)
{
    if (dragged.isEmpty() || ! event.mouseWasDraggedSinceMouseDown()) return;
    preview = location (screen (dragOrigin) + (event.position - event.mouseDownPosition));
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
