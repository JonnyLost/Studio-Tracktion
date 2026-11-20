// SPDX-License-Identifier: GPL-3.0-or-later
#include "../AppearanceMetrics.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
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

            for (const int windowWidth : { 1024, 1280 })
            {
                const auto dockWidth = windowWidth - metrics.span (76);
                const auto musicalWidth = dockWidth - metrics.trackHeader() - metrics.trackFooter() - metrics.scrollbar();
                assert (musicalWidth >= 200);
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
