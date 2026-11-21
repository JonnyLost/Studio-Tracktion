// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StudioUI.h"
#include <optional>

namespace studio
{
enum class PerformanceMode { full = 1, articulations, hidden };

class InstrumentPerformance final : public juce::Component
{
public:
    InstrumentPerformance();
    void refresh (tracktion::AudioTrack*, bool editable);
    void clear();
    void resized() override;
    void paint (juce::Graphics&) override;
    static int height();

    std::function<void (tracktion::Plugin&)> onOpenEditor;

   #if STUDIO_SPATIAL_TESTS
    bool exerciseControl (const juce::String&, double);
    juce::StringArray visibleControls() const;
   #endif

private:
    struct Dial : juce::Slider
    {
        std::function<void()> commit;
        void valueChanged() override { if (commit) commit(); }
    };

    struct Control
    {
        juce::Label name;
        Dial value;
        std::function<void (double)> apply;
        std::optional<unsigned> gestureContext;
    };
    void refreshControls();
    void define (const juce::String&, double min, double max, double step, double value,
                 std::function<void (double)>, juce::String suffix = {}, bool logarithmic = false);
    void defineAutomatable (tracktion::AutomatableParameter&);
    void configureChoices (int layerCount, const juce::StringArray& pages);
    void finalizeControls();

    tracktion::Plugin::Ptr instrument;
    juce::String trackName;
    juce::Label title, detail, hint;
    juce::TextButton open { "Open Instrument" };
    juce::ComboBox page, layer;
    std::array<Control, 6> controls;
    unsigned revision = 0, bindingContext = 0;
    int layerIndex = 0, pageIndex = 0, count = 0;
    bool editable = false, availableProcessor = false;
    juce::Colour accent = colours::focus;
};
}
