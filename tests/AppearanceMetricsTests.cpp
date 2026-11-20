// SPDX-License-Identifier: GPL-3.0-or-later
#include "../AppearanceMetrics.h"
#include "../StagePresentation.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
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
    std::cout << "Appearance metrics passed at all 15 combinations.\n";
}
