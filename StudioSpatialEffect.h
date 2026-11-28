// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "SpatialSource.h"
#include <atomic>
#include <functional>

namespace studio::spatial
{
using SourceParameters=source::Parameters;
using ParameterValues=std::array<float,15>;
ParameterValues valuesOf(SourceParameters) noexcept;
SourceParameters parametersOf(ParameterValues) noexcept;
juce::String encodeSource(SourceParameters);
SourceParameters decodeSource(const juce::String&);
struct SourcePreset { const char* name; SourceParameters parameters; };
const std::array<SourcePreset,8>& sourcePresets();

class SourcePlugin final : public tracktion::Plugin
{
public:
    static constexpr const char* xmlTypeName="studioSpatialSourceV1";
    explicit SourcePlugin(tracktion::PluginCreationInfo);
    ~SourcePlugin() override;
    juce::String getName() const override { return "Studio Spatial"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Studio"; }
    juce::String getSelectableDescription() override { return "Native distance and binaural positioning"; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    bool noTail() override { return false; }
    double getTailLength() const override { return .4; }
    void reset() override;
    bool producesAudioWhenNoAudioInput() override { return true; }
    bool reflectionsAvailable() const noexcept { return earlyReady.load(std::memory_order_relaxed); }
    int getNumOutputChannelsGivenInputs(int n) override { return juce::jmax(2,n); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise(const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer(const tracktion::PluginRenderContext&) override;
    SourceParameters parameters() const;
    void setParameters(SourceParameters);
    void beginGesture(const juce::String&);
    void endGesture();
    source::Peaks peaks() const noexcept;
private:
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free);
    std::array<std::atomic<float>,15> published{};
    std::atomic<unsigned> revision{0};
    unsigned accepted=1;
    std::atomic<float> peakL{0},peakR{0};
    source::Processor dsp;
    early::Engine reflections;
    std::atomic<bool> earlyReady{false};
    double rate=48000;
    bool active=false;
    void publish();
    bool read(ParameterValues&,unsigned&) const noexcept;
    void valueTreePropertyChanged(juce::ValueTree&,const juce::Identifier&) override;
};

// One editor per view; callbacks resolve an attached plug-in by edit/track/item
// identity on the message thread. Never retain a plug-in or an Edit in a window.
class SourceWindow final : public juce::DocumentWindow
{
public:
    using Resolve=std::function<SourcePlugin*()>;
    explicit SourceWindow(Resolve);
    void appearanceChanged();
    void closeButtonPressed() override { setVisible(false); }
};
}
