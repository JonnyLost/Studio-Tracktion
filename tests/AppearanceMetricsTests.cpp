// SPDX-License-Identifier: GPL-3.0-or-later
#include "../AppearanceMetrics.h"
#include "../StudioDesignTokens.h"
#include "../StudioActivities.h"
#include "../AtelierLayout.h"
#include "../StagePresentation.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
double luminance (std::uint32_t argb)
{
    const auto linear = [] (unsigned channel)
    {
        const double value = channel / 255.0;
        return value <= .04045 ? value / 12.92 : std::pow ((value + .055) / 1.055, 2.4);
    };
    return .2126 * linear ((argb >> 16) & 255u) + .7152 * linear ((argb >> 8) & 255u)
        + .0722 * linear (argb & 255u);
}
double contrast (std::uint32_t a, std::uint32_t b)
{
    const double first = luminance (a), second = luminance (b);
    return (std::max (first, second) + .05) / (std::min (first, second) + .05);
}
bool designContrast()
{
    using namespace studio::design;
    // Explicit return values keep checks active in Release builds.
    for (const auto surface : { rail, canvas, timeline, panel, control, raised, selected, well })
        for (const auto foreground : { text, secondary, action, atelierAccent, focus })
            if (contrast (foreground, surface) < 4.5)
            {
                std::cerr << "Studio design contrast below 4.5:1 for " << std::hex
                          << foreground << " on " << surface << '\n';
                return false;
            }
    for (const auto state : { studio::design::signal, record, warning, focus })
        if (contrast (state, control) < 3.0) return false;
    return atelierAccent != focus && record != studio::design::signal && focus != playhead;
}
}

int main()
{
    if (!designContrast()) return 1;
    using studio::Activity;
    using studio::Workspace;
    if (studio::activities.size() != 6
        || studio::activityForWorkspace (Workspace::piano) != Activity::compose
        || studio::activityForWorkspace (Workspace::articulations) != Activity::perform
        || studio::activityForWorkspace (Workspace::instrumentEditor) != Activity::design
        || studio::activityForWorkspace (Workspace::rooms) != Activity::mix
        || studio::activityForWorkspace (Workspace::filmScoring) != Activity::score
        || studio::activityForWorkspace (Workspace::resources).has_value()) return 1;
    studio::AppearanceMetrics activityMetrics;
    for (int ui : studio::AppearanceMetrics::interfaceOptions)
        for (int text : studio::AppearanceMetrics::textOptions)
        {
            activityMetrics.set (ui, text);
            using studio::AtelierProfile;
            for (int width : { 205, 270, 365, 510 })
            {
                const auto essential = studio::atelierLayout (width, 8, AtelierProfile::hosted, false, activityMetrics);
                const auto expanded = studio::atelierLayout (width, 8, AtelierProfile::hosted, true, activityMetrics);
                const auto sampler = studio::atelierLayout (width, 2, AtelierProfile::sampler, false, activityMetrics);
                const auto synth = studio::atelierLayout (width, 0, AtelierProfile::synth, false, activityMetrics);
                if (essential.controllerRows * essential.columns != 4 || expanded.controllerRows * expanded.columns != 8
                    || expanded.total <= essential.total || sampler.articulationRows != 1
                    || synth.controllerRows != 0 || synth.articulationRows != 0 || synth.total >= essential.total
                    || ! studio::atelierControllerVisible (AtelierProfile::sampler, true, 3)
                    || studio::atelierControllerVisible (AtelierProfile::sampler, true, 5)
                    || studio::atelierControllerVisible (AtelierProfile::synth, true, 0)) return 1;
            }
            const int minimum = 6 * activityMetrics.span (79) + activityMetrics.span (125);
            if (studio::activitiesFit (minimum - 1, activityMetrics)
                || ! studio::activitiesFit (minimum, activityMetrics)) return 1;
        }
    using namespace studio::spatial;
    for (const auto& layout : stagePresentations)
        for (float x : { -1.0f, -0.7f, 0.0f, 0.7f, 1.0f })
            for (float depth : { 0.0f, 0.12f, 0.5f, 0.9f, 1.0f })
            {
                const auto projected = layout.project ({ x, depth });
                for (float coordinate : projected) assert (coordinate >= 0.0f && coordinate <= 1.0f);
                const auto restored = layout.unproject (projected[0], projected[1]);
                assert (std::abs (restored.x - x) < 0.0001f && std::abs (restored.depth - depth) < 0.0001f);
            }
    assert (stagePresentations[0].curvature > stagePresentations[1].curvature);
    assert (stagePresentations[1].rearWidth < stagePresentations[0].rearWidth);
    assert (stagePresentations[2].frontWidth > stagePresentations[0].frontWidth);
    assert (seat (Seating::scoring, Section::keys, 0, 1) != seat (Seating::concert, Section::keys, 0, 1));

    studio::WorkspaceScroll scroll;
    assert (scroll.position (studio::Workspace::stage) == 0);
    scroll.store (studio::Workspace::stage, 180);
    scroll.store (studio::Workspace::mix, 32);
    assert (scroll.position (studio::Workspace::stage) == 180 && scroll.position (studio::Workspace::mix) == 32);

    studio::AppearanceMetrics metrics;
    metrics.set (999, -1);
    assert (metrics.interfacePercent() == 100 && metrics.textPercent() == 100);
    metrics.set (125, 777);
    assert (metrics.interfacePercent() == 125 && metrics.textPercent() == 100);

    const auto choices = studio::AppearanceMetrics::interfaceOptions;
    for (std::size_t i = 0; i < choices.size(); ++i)
    {
        const int ui = choices[i];
        metrics.set (ui, 100);
        assert (metrics.adjacentInterfacePercent (-1) == choices[i == 0 ? 0 : i - 1]);
        assert (metrics.adjacentInterfacePercent (1) == choices[std::min (i + 1, choices.size() - 1)]);
        const auto iconAtStandardText = metrics.px (18);
        assert (std::abs (metrics.fontSize (12.0f) - 12.0f * (float) ui / 100.0f) < 0.001f);
        for (int text : studio::AppearanceMetrics::textOptions)
        {
            metrics.set (ui, text);
            assert (metrics.px (18) == iconAtStandardText);
            assert (metrics.control() >= metrics.fontSize (13.0f));
            assert (metrics.line (14) >= metrics.fontSize (14.0f));

            for (const int available : { 60, 275, 315, 385, 490, 665 })
            {
                const auto minimumToolbar = metrics.control (32) + metrics.control (26) + metrics.px (9);
                const auto split = metrics.dockRoom (available, metrics.px (270), studio::DockMode::split);
                const auto expanded = metrics.dockRoom (available, metrics.px (270), studio::DockMode::expanded);
                const auto compact = metrics.dockRoom (available, metrics.px (270), studio::DockMode::compact);
                for (const auto geometry : { split, expanded, compact })
                {
                    assert (geometry.panelHeight >= 0);
                    assert (geometry.panelHeight <= std::max (0, available - minimumToolbar));
                }
                assert (split.panelHeight <= split.splitLimit);
                assert (expanded.panelHeight >= split.panelHeight);
            }

            for (const int windowWidth : { 1024, 1280, 1600 })
            {
                const auto dockWidth = windowWidth - metrics.span (76);
                const auto musicalWidth = dockWidth - metrics.trackHeader() - metrics.trackFooter() - metrics.scrollbar();
                assert (musicalWidth >= 200);
                assert (metrics.resourcesFitHeader (windowWidth - metrics.px (20)));
                assert (metrics.transportPrimaryWidth() <= windowWidth - metrics.px (20));
                assert (2 * metrics.mixerStripWidth (dockWidth) + metrics.scrollbar() < dockWidth);
                for (const int windowHeight : { 640, 800, 960 })
                {
                    const int chrome = 2 * metrics.control (34) + metrics.transportRowHeight()
                        + metrics.px (10) + metrics.line (13, 5);
                    const auto split = metrics.dockRoom (windowHeight - chrome, metrics.px (270), studio::DockMode::split);
                    assert (split.panelHeight >= metrics.control (34) + metrics.control (26));
                }
            }
        }
    }

    metrics.set (100, 100);
    assert (metrics.compactBrowser (958));
    assert (! metrics.compactBrowser (1206));
    assert (! metrics.browserFilterRow (958));
    metrics.set (175, 130);
    assert (metrics.browserFilterRow (840));
    assert (metrics.stackedBrowserDetails (840));
    assert (! metrics.stackedBrowserDetails (1100));
    assert (metrics.resourcesFitHeader (1024 - metrics.px (20)));
    assert (! metrics.dropoutsFitHeader (1024 - metrics.px (20)));
    assert (metrics.dropoutsFitHeader (1280 - metrics.px (20)));
    std::cout << "Appearance metrics passed at all 15 combinations.\n";
}
