// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentPerformance.h"
#include "StudioMixing.h"
#include "StudioSampler.h"
#include "StudioSynth.h"

namespace studio
{
namespace te = tracktion;
using namespace juce;

InstrumentPerformance::InstrumentPerformance()
{
    title.setFont (FontOptions (15.0f, Font::bold));
    detail.setColour (Label::textColourId, colours::secondary);
    hint.setColour (Label::textColourId, colours::secondary);
    const auto configureText = [] (Label& label)
    {
        label.setMinimumHorizontalScale (1.0f);
        label.setJustificationType (Justification::centredLeft);
    };
    for (auto* label : { &title, &detail, &hint }) configureText (*label);
    for (auto* item : std::initializer_list<Component*> { &title, &detail, &hint, &open, &page, &layer })
        addAndMakeVisible (*item);
    layer.setTitle ("Synth layer");
    page.setTitle ("Performance controls page");
    page.onChange = [this] { ++bindingContext; pageIndex = page.getSelectedItemIndex(); refreshControls(); };
    layer.onChange = [this] { ++bindingContext; layerIndex = layer.getSelectedItemIndex(); refreshControls(); };
    open.onClick = [this]
    {
        if (instrument != nullptr && onOpenEditor) onOpenEditor (*instrument);
    };

    for (auto& control : controls)
    {
        control.name.setJustificationType (Justification::centred);
        control.name.setMinimumHorizontalScale (1.0f);
        control.value.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        control.value.setWantsKeyboardFocus (true);
        // One committed edit per gesture through the existing preset editor API.
        control.value.setChangeNotificationOnlyOnRelease (true);
        control.value.onDragStart = [this, &control] { control.gestureContext = bindingContext; };
        control.value.onDragEnd = [&control] { control.gestureContext.reset(); };
        control.value.commit = [this, &control]
        {
            if (editable && control.apply && control.gestureContext.value_or (bindingContext) == bindingContext)
                control.apply (control.value.getValue());
        };
        addChildComponent (control.name);
        addChildComponent (control.value);
    }
}

int InstrumentPerformance::height()
{
    return appearance.control (32, 13) + appearance.px (91);
}

void InstrumentPerformance::clear()
{
    refresh (nullptr, false);
    refreshControls();
}

void InstrumentPerformance::refresh (te::AudioTrack* track, bool canEdit)
{
    auto* plugin = track != nullptr ? findInstrument (*track) : nullptr;
    auto name = track != nullptr ? track->getName() : String();
    const bool changed = instrument.get() != plugin;
    if (changed)
    {
        instrument = plugin;
        layerIndex = pageIndex = 0;
    }
    auto currentRevision = 0u;
    if (const auto* inst = dynamic_cast<synth::Plugin*> (plugin)) currentRevision = inst->changeCount();
    if (const auto* inst = dynamic_cast<sampler::Plugin*> (plugin)) currentRevision = inst->changeCount();
    const auto* hosted = dynamic_cast<te::ExternalPlugin*> (plugin);
    const bool hasProcessor = hosted != nullptr && hosted->getAudioPluginInstance() != nullptr;
    const bool bindingChanged = changed || revision != currentRevision || editable != canEdit
                                || availableProcessor != hasProcessor;
    if (bindingChanged) ++bindingContext;
    if (bindingChanged || trackName != name || hasProcessor
        || (pageIndex == 1 && dynamic_cast<synth::Plugin*> (plugin) != nullptr))
    {
        trackName = std::move (name);
        revision = currentRevision;
        editable = canEdit;
        availableProcessor = hasProcessor;
        refreshControls();
    }
}

void InstrumentPerformance::configureChoices (int layerCount, const StringArray& pages)
{
    if (layer.getNumItems() != layerCount)
    {
        layer.clear (dontSendNotification);
        for (int i = 0; i < layerCount; ++i) layer.addItem ("Layer " + String (i + 1), i + 1);
    }
    bool changed = pages.size() != page.getNumItems();
    for (int i = 0; ! changed && i < pages.size(); ++i) changed = pages[i] != page.getItemText (i);
    if (changed)
    {
        page.clear (dontSendNotification);
        page.addItemList (pages, 1);
    }
    layer.setSelectedItemIndex (layerIndex, dontSendNotification);
    page.setSelectedItemIndex (pageIndex, dontSendNotification);
    layer.setVisible (layerCount > 1);
    page.setVisible (pages.size() > 1);
}

void InstrumentPerformance::define (const String& name, double minimum, double maximum, double step, double value,
                                    std::function<void (double)> apply, String suffix, bool logarithmic)
{
    if (count >= (int) controls.size() || minimum >= maximum) return;
    auto& control = controls[(size_t) count++];
    control.name.setText (name, dontSendNotification);
    control.name.setTooltip (name);
    control.value.setTitle (name);
    control.value.setTooltip (name);
    control.apply = std::move (apply);
    if (! control.value.isMouseButtonDown())
    {
        control.value.setRange (minimum, maximum, step);
        control.value.setSkewFactor (1);
        control.value.setNumDecimalPlacesToDisplay (step >= 1 ? 0 : 2);
        if (logarithmic && minimum > 0)
            control.value.setSkewFactorFromMidPoint (std::sqrt (minimum * maximum));
        control.value.setTextValueSuffix (suffix);
        control.value.setValue (value, dontSendNotification);
    }
}

void InstrumentPerformance::defineAutomatable (te::AutomatableParameter& parameter)
{
    const auto range = parameter.getValueRange();
    te::AutomatableParameter::Ptr binding (&parameter);
    define (parameter.getParameterName(), range.getStart(), range.getEnd(),
            range.getLength() * .001, parameter.getCurrentValue(),
            [binding] (double value)
            {
                binding->parameterChangeGestureBegin();
                binding->setParameter (static_cast<float> (value), sendNotificationSync);
                binding->parameterChangeGestureEnd();
            }, parameter.getLabel().isEmpty() ? "" : " " + parameter.getLabel());
}

void InstrumentPerformance::refreshControls()
{
    count = 0;
    int layers = 0;
    StringArray pages;
    String description, explanation;
    accent = colours::focus;
    const bool native = dynamic_cast<synth::Plugin*> (instrument.get()) != nullptr
                     || dynamic_cast<sampler::Plugin*> (instrument.get()) != nullptr;
    auto* hosted = dynamic_cast<te::ExternalPlugin*> (instrument.get());
    open.setVisible (instrument != nullptr);
    open.setEnabled (editable && (hosted == nullptr || availableProcessor));
    open.setButtonText (native ? "Detailed Editor" : "Open Instrument");

    if (auto* synth = dynamic_cast<synth::Plugin*> (instrument.get()))
    {
        using Family = synth::Family;
        const auto patch = synth->getPatch();
        const auto patchLayers = patch["layers"];
        layers = patchLayers.size();
        layerIndex = jlimit (0, jmax (0, layers - 1), layerIndex);
        const auto current = patchLayers[layerIndex];
        const auto family = synth::parseFamily (current["engine"].toString());
        description = synth->presetName() + "  /  " + synth::familyName (family);
        if (layers > 1) description = synth->presetName() + "  /  Hybrid · " + synth::familyName (family);
        pages = { "Sound", "Output" };
        pageIndex = jlimit (0, 1, pageIndex);
        if (pageIndex == 1)
        {
            if (auto parameter = synth->getAutomatableParameterByID ("studioSynthTrim"))
                defineAutomatable (*parameter);
            const auto routes = patch["modulation"]["routes"];
            const auto* entries = routes.getArray();
            if (entries != nullptr && std::any_of (entries->begin(), entries->end(), [] (const auto& route)
                { return route["source"].toString() == "macro1"; }))
                if (auto parameter = synth->getAutomatableParameterByID ("studioSynthMacroOffset"))
                    defineAutomatable (*parameter);
            if (layers > 1)
                define ("Layer gain", 0, 1, .01, static_cast<double> (current["gain"]),
                        [synth, index = layerIndex] (double value)
                        {
                            auto next = synth->getPatch().clone();
                            if (auto* chosen = next["layers"][index].getDynamicObject())
                            {
                                chosen->setProperty ("gain", value);
                                synth->editPatch (std::move (next), false);
                            }
                        });
        }
        else if (family != Family::hybrid)
        {
            constexpr int ids[6][6] {
                { 6, 7, 13, 16, 17, 18 }, // Analog filter, amp envelope and modulation.
                { 1, 11, 10, 28, 6, 9 }, // FM feedback, operator 2 and velocity response.
                { 2, 3, 12, 14, 5, 8 }, // Wavetable blend and morph.
                { 5, 6, 7, 26, 0, 3 }, // Additive spectrum and harmonic motion.
                { 0, 1, 2, 7, 8, 9 },   // Granular texture and envelope.
                { 1, 4, 3, 9, 5, 2 }   // Physical-model excitation and decay.
            };
            const auto familyIndex = static_cast<int> (family);
            const std::array<Colour, 6> coloursByFamily { colours::strings, colours::winds, colours::keys,
                 colours::brass, colours::percussion, colours::signal };
            accent = coloursByFamily[(size_t) familyIndex];
            const auto& metadata = synth::catalog().parameters[(size_t) familyIndex];
            const auto key = synth::patchKey (family);
            const auto nativePatch = current[key];
            const auto values = nativePatch["values"];
            const int model = static_cast<int> (values["0"]);
            constexpr int bowed[] { 1, 10, 11, 12, 4, 5 };
            constexpr int air[] { 1, 13, 14, 15, 16, 5 };
            constexpr int resonant[] { 1, 17, 18, 19, 4, 5 };
            const auto& selectedIds = family != Family::physical || model < 2 ? ids[familyIndex]
                : model == 2 ? bowed : model < 5 ? air : resonant;
            for (const auto id : selectedIds)
            {
                const auto& parameter = metadata[(size_t) id];
                const auto index = Identifier (String (id));
                const auto value = values[index];
                String label = parameter.name;
                if (family == Family::fm && id == 10) label = "Op 2 ratio";
                if (family == Family::fm && id == 11) label = "Op 2 level";
                if (family == Family::analog && id == 17) label = "LFO rate";
                define (label, parameter.minimum, parameter.maximum, .001,
                        value.isVoid() ? parameter.initial : static_cast<double> (value),
                        [this, synth, key, index, layerNumber = layerIndex, family] (double nextValue)
                        {
                            auto next = synth->getPatch().clone();
                            auto selected = next["layers"][layerNumber];
                            if (synth::parseFamily (selected["engine"].toString()) != family) return;
                            if (auto* object = selected[key]["values"].getDynamicObject())
                            {
                                object->setProperty (index, nextValue);
                                if (auto result = synth->editPatch (std::move (next), false); result.failed())
                                    detail.setText (result.getErrorMessage(), dontSendNotification);
                            }
                        }, family == Family::analog && id == 6 ? " Hz" : "", parameter.logarithmic);
            }
        }
    }
    else if (auto* sampler = dynamic_cast<sampler::Plugin*> (instrument.get()))
    {
        using Settings = sampler::Settings;
        description = "Studio Sampler  /  " + sampler->instrument().name;
        accent = colours::brass;
        const auto set = [&, sampler] (const char* label, float Settings::* member, double min, double max,
                                       double step, double factor, const char* suffix)
        {
            define (label, min, max, step, sampler->instrument().settings.*member * factor,
                    [sampler, member, factor] (double value)
                    {
                        auto settings = sampler->instrument().settings;
                        settings.*member = static_cast<float> (value / factor);
                        sampler->editSettings (settings);
                    }, suffix);
        };
        set ("Attack", &Settings::attack, 0, 10000, 1, 1000, " ms");
        set ("Release", &Settings::release, 0, 15000, 1, 1000, " ms");
        set ("Sustain", &Settings::sustain, 0, 100, 1, 100, "%");
        set ("Tuning", &Settings::semitones, -24, 24, .1, 1, " st");
        set ("Bend range", &Settings::bendRange, 0, 24, 1, 1, " st");
        set ("Level", &Settings::gainDb, -60, 12, .1, 1, " dB");
    }
    else if (hosted != nullptr)
    {
        description = hosted->getName();
        if (! availableProcessor)
            explanation = hosted->isInitialisingAsync() ? "Loading Audio Unit…" : "Audio Unit unavailable on this system";
        else
        {
            Array<te::AutomatableParameter*> parameters;
            for (auto* parameter : hosted->getAutomatableParameters())
                if (parameter != hosted->dryGain.get() && parameter != hosted->wetGain.get()
                    && parameter->getParameterName().isNotEmpty() && ! parameter->getValueRange().isEmpty())
                    parameters.add (parameter);
            const int perPage = (int) controls.size();
            for (int i = 0; i * perPage < parameters.size(); ++i) pages.add ("Parameters " + String (i + 1));
            pageIndex = jlimit (0, jmax (0, pages.size() - 1), pageIndex);
            for (int i = pageIndex * perPage; i < jmin ((pageIndex + 1) * perPage, parameters.size()); ++i)
                defineAutomatable (*parameters[i]);
            if (parameters.isEmpty()) explanation = "Use the instrument editor for its controls.";
        }
    }
    else
        explanation = instrument == nullptr ? "Select an instrument track."
                                            : "Use the instrument editor for its controls.";

    configureChoices (layers, pages);
    title.setText (trackName.isEmpty() ? "Performance" : trackName, dontSendNotification);
    title.setTooltip (title.getText());
    detail.setText (description, dontSendNotification);
    detail.setTooltip (description);
    hint.setText (explanation, dontSendNotification);
    hint.setVisible (count == 0);
    finalizeControls();
}

void InstrumentPerformance::finalizeControls()
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        const bool show = i < (size_t) count;
        const bool wasVisible = control.value.isVisible();
        control.name.setVisible (show);
        control.value.setVisible (show);
        control.value.setEnabled (editable);
        if (show) control.value.setColour (Slider::rotarySliderFillColourId, accent);
        else if (wasVisible || control.apply) control.apply = {};
    }
    resized();
    repaint();
}

void InstrumentPerformance::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (11), m.px (4));
    auto head = area.removeFromTop (m.control (28, 12));
    if (open.isVisible()) open.setBounds (head.removeFromRight (m.span (129)).reduced (m.px (2), 0));
    if (page.isVisible()) page.setBounds (head.removeFromRight (m.span (95)).reduced (m.px (2), 0));
    if (layer.isVisible()) layer.setBounds (head.removeFromRight (m.span (79)).reduced (m.px (2), 0));
    title.setBounds (head.removeFromLeft (jmin (m.span (160), detail.getText().isEmpty() ? head.getWidth() : head.getWidth() * 2 / 5)));
    detail.setBounds (head);
    hint.setBounds (area);
    if (count == 0) return;
    area.removeFromTop (m.px (3));
    const int cellWidth = jmin (m.span (157), area.getWidth() / count);
    for (int i = 0; i < count; ++i)
    {
        auto cell = area.removeFromLeft (cellWidth).reduced (m.px (4), 0);
        auto& item = controls[(size_t) i];
        item.name.setBounds (cell.removeFromTop (m.line (11, 2)));
        const auto textWidth = jmin (cell.getWidth(), m.span (95));
        item.value.setTextBoxStyle (Slider::TextBoxBelow, false, textWidth, m.line (12, 2));
        item.value.setBounds (cell);
    }
}

void InstrumentPerformance::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (accent.withAlpha (.56f));
    g.fillRect (getLocalBounds().removeFromTop (appearance.px (1)));
}

#if STUDIO_SPATIAL_TESTS
StringArray InstrumentPerformance::visibleControls() const
{
    StringArray names;
    for (int i = 0; i < count; ++i)
        names.add (controls[(size_t) i].name.getText());
    return names;
}

bool InstrumentPerformance::exerciseControl (const String& name, double value)
{
    for (auto& control : controls)
        if (control.value.isVisible() && control.value.isEnabled() && control.name.getText() == name)
        {
            control.value.setValue (value, sendNotificationSync);
            return exactlyEqual (control.value.getValue(), value);
        }
    return false;
}
#endif
}
