// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SpatialPublication.h"
#include "SpatialReverb.h"

namespace studio::spatial
{
class ReverbPlugin final : public tracktion::Plugin
{
public:
    static constexpr const char* xmlTypeName = "studioReverbV1";
    explicit ReverbPlugin (tracktion::PluginCreationInfo);
    ~ReverbPlugin() override;
    juce::String getName() const override { return "Studio Reverb"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Studio"; }
    juce::String getSelectableDescription() override { return "Studio algorithmic stereo reverb"; }
    bool canBeAddedToRack() override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool noTail() override { return false; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    double getTailLength() const override { return 12.4; }
    int getNumOutputChannelsGivenInputs (int n) override { return std::max (2, n); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    Room room() const;
    void setRoom (Room);
    std::array<float, 2> levels() const noexcept;

private:
    void publish();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    RoomReverb dsp;
    Publication<RoomPlan> publication;
    std::atomic<float> input { 0 }, output { 0 };
    float meterFall = 14400;
    unsigned accepted = 0;
};

class ReverbControls final : public juce::Component
{
public:
    explicit ReverbControls (bool effect, bool advanced);
    ~ReverbControls() override;
    void setRoom (Room);
    Room readRoom() const;
    void resized() override;
    int preferredHeight (int width) const;
    std::function<void()> onChange;

private:
    class Field;
    std::vector<std::unique_ptr<Field>> fields;
    juce::ComboBox quality;
    Room reference;
};

class ReverbWindow final : public juce::DocumentWindow
{
public:
    using Resolve = std::function<ReverbPlugin*()>;
    explicit ReverbWindow (Resolve);
    void closeButtonPressed() override { setVisible (false); }
    void appearanceChanged();
};
}
