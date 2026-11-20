// SPDX-License-Identifier: GPL-3.0-or-later
#include "SamplerEditor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio::sampler
{
using namespace juce;
namespace
{
String noteName (int note) { return MidiMessage::getMidiNoteName (note, true, true, 4); }
bool sampleFile (const String& path)
{
    return File (path).hasFileExtension ("wav;wave;aif;aiff;aifc");
}

class EnvelopePlot final : public Component
{
public:
    explicit EnvelopePlot (Plugin& p) : plugin (p) { setTitle ("Amplitude envelope"); }
    void paint (Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::canvas);
        g.fillRoundedRectangle (area, 6);
        area = area.reduced (12, 9);
        const auto& s = plugin.instrument().settings;
        const auto attack = 0.12f + 0.15f * std::sqrt (s.attack / 10);
        const auto decay = 0.11f + 0.14f * std::sqrt (s.decay / 10);
        const auto release = 0.12f + 0.15f * std::sqrt (s.release / 15);
        const auto x = [area] (float v) { return area.getX() + v * area.getWidth(); };
        const auto y = area.getBottom() - 0.87f * s.sustain * area.getHeight();
        Path path;
        path.startNewSubPath (area.getX(), area.getBottom());
        path.lineTo (x (attack), area.getY() + 0.13f * area.getHeight());
        path.lineTo (x (attack + decay), y);
        path.lineTo (x (1 - release), y);
        path.lineTo (area.getRight(), area.getBottom());
        auto fill = path;
        fill.closeSubPath();
        g.setColour (colours::action.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (colours::action);
        g.strokePath (path, PathStrokeType (1.5f));
    }
private:
    Plugin& plugin;
};
}

Editor::Editor (Plugin& p) : plugin (p)
{
    setLookAndFeel (&look);
    setName ("Studio Sampler");
    setTitle ("Studio Sampler instrument");
    limits.setSizeLimits (460, 320, 1600, 1000);
    limits.setMinimumOnscreenAmounts (0x10000, 30, 28, 30);
    for (Component* c : std::initializer_list<Component*> { &name, &preset, &loadPreset, &savePreset,
             &basic, &mapping, &loops, &advanced, &keyboard, &viewport })
        addAndMakeVisible (*c);

    viewport.setViewedComponent (&body, false);
    viewport.setTitle ("Sampler controls");
    viewport.setScrollBarsShown (true, false);
    for (Component* c : std::initializer_list<Component*> { &filename, &status, &meter, &samples, &mode,
             &loopEnabled, &display, &load, &locate, &remove, &audition, &envelope })
        body.addAndMakeVisible (*c);

    auto* plot = owned.add (new EnvelopePlot (p));
    envelope.addAndMakeVisible (*plot);
    envelope.setInterceptsMouseClicks (false, false);
    for (auto* label : { &name, &preset, &filename, &status, &meter })
    {
        label->setBorderSize (BorderSize<int>());
        label->setMinimumHorizontalScale (1);
        label->setColour (Label::textColourId, label == &name ? colours::text : colours::secondary);
    }
    name.setEditable (false, true);
    name.setTooltip ("Double-click to rename the instrument");
    name.setTitle ("Instrument name");
    name.onTextChange = [this] { plugin.rename (name.getText()); };

    for (auto* button : { &loadPreset, &savePreset, &basic, &mapping, &loops, &advanced,
                          &load, &locate, &remove, &audition })
    {
        button->setTitle (button->getButtonText());
        button->setColour (TextButton::buttonColourId, colours::control);
        button->setColour (TextButton::buttonOnColourId, colours::selected);
        button->setWantsKeyboardFocus (true);
    }
    load.setColour (TextButton::buttonColourId, colours::action);
    load.setColour (TextButton::textColourOffId, colours::rail);
    loadPreset.onClick = [this] { choose (true, false, false); };
    savePreset.onClick = [this] { choose (true, true, false); };
    basic.onClick = [this] { setPage (Page::basic); };
    mapping.onClick = [this] { setPage (Page::mapping); };
    loops.onClick = [this] { setPage (Page::loops); };
    advanced.onClick = [this] { setPage (Page::advanced); };
    load.onClick = [this] { choose (false, false, false); };
    locate.onClick = [this] { choose (false, false, true); };
    remove.onClick = [this] { plugin.removeZone (selection); refresh(); };
    audition.onClick = [this] { playAudition(); };

    samples.setTitle ("Sample zones");
    samples.setTextWhenNothingSelected ("Choose a sample zone");
    samples.onChange = [this] { if (! updating) selectZone (samples.getSelectedItemIndex()); };
    mode.setTitle ("Playback mode");
    mode.addItem ("Sustained", 1);
    mode.addItem ("One-shot", 2);
    loopEnabled.setTitle ("Enable sample loop");
    loopEnabled.setTooltip ("Sustained playback repeats the marked range while the note plays.");
    const auto setSetting = [this] (auto member, double factor = 1.0)
    {
        return [this, member, factor] (double value)
        {
            auto settings = plugin.instrument().settings;
            using Value = std::remove_reference_t<decltype (settings.*member)>;
            settings.*member = static_cast<Value> (value * factor);
            plugin.editSettings (settings);
            envelope.repaint();
        };
    };
    setup (attack, "Attack", 0, 10000, 1, " ms", setSetting (&Settings::attack, .001), true);
    setup (decay, "Decay", 0, 10000, 1, " ms", setSetting (&Settings::decay, .001), true);
    setup (sustain, "Sustain", 0, 100, 1, " %", setSetting (&Settings::sustain, .01), true);
    setup (release, "Release", 0, 15000, 1, " ms", setSetting (&Settings::release, .001), true);
    attack.slider.setSkewFactorFromMidPoint (80);
    decay.slider.setSkewFactorFromMidPoint (200);
    release.slider.setSkewFactorFromMidPoint (250);
    setup (gain, "Volume", -60, 12, 0.1, " dB", setSetting (&Settings::gainDb));
    setup (tune, "Tune", -24, 24, 0.1, " st", setSetting (&Settings::semitones));
    setup (voices, "Maximum voices", 1, maxVoices, 1, {}, setSetting (&Settings::polyphony));
    setup (bend, "Pitch-bend range", 0, 24, 1, " st", setSetting (&Settings::bendRange));

    const auto editZone = [this] (auto change)
    {
        return [this, change] (double value)
        {
            if (const auto* entry = zone())
            {
                auto z = entry->mapping;
                change (z, value);
                plugin.editZone (selection, z);
            }
        };
    };
    setup (root, "Root note", 0, 127, 1, {}, editZone ([] (Zone& z, double n) { z.root = roundToInt (n); }));
    setup (low, "Lowest key", 0, 127, 1, {}, editZone ([] (Zone& z, double n) { z.low = jmin (z.high, roundToInt (n)); }));
    setup (high, "Highest key", 0, 127, 1, {}, editZone ([] (Zone& z, double n) { z.high = jmax (z.low, roundToInt (n)); }));
    setup (soft, "Min velocity", 1, 127, 1, {}, editZone ([] (Zone& z, double n) { z.soft = jmin (z.hard, roundToInt (n)); }));
    setup (hard, "Max velocity", 1, 127, 1, {}, editZone ([] (Zone& z, double n) { z.hard = jmax (z.soft, roundToInt (n)); }));
    setup (zoneGain, "Zone gain", -60, 12, 0.1, " dB", editZone ([] (Zone& z, double n) { z.gainDb = static_cast<float> (n); }));
    setup (cents, "Fine tune", -100, 100, 1, " ct", editZone ([] (Zone& z, double n) { z.cents = static_cast<float> (n); }));
    setup (group, "Round-robin group", 0, maxGroups, 1, {}, editZone ([] (Zone& z, double n) { z.group = roundToInt (n); }));
    group.slider.setTooltip ("Zones in a nonzero group take turns when their key and velocity ranges overlap.");
    root.slider.setTooltip ("The key that plays this sample at its original pitch.");
    soft.slider.setTooltip ("Quietest MIDI velocity that triggers this sample zone.");
    hard.slider.setTooltip ("Loudest MIDI velocity that triggers this sample zone.");
    setup (loopStart, "Loop start (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n) { z.loopStart = jlimit (0, jmax (0, z.loopEnd - 2), roundToInt (n)); }));
    setup (loopEnd, "Loop end (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n) { z.loopEnd = jmax (z.loopStart + 2, roundToInt (n)); }));
    for (auto* control : { &root, &low, &high })
    {
        control->slider.textFromValueFunction = [] (double value) { return noteName (roundToInt (value)); };
        control->slider.valueFromTextFunction = [] (const String& text)
        {
            for (int i = 0; i < 128; ++i)
                if (noteName (i).equalsIgnoreCase (text.trim())) return static_cast<double> (i);
            return jlimit (0.0, 127.0, text.getDoubleValue());
        };
    }
    mode.onChange = [this, set = editZone ([] (Zone& z, double v) { z.oneShot = v != 0; })]
    {
        if (! updating) set (mode.getSelectedId() == 2);
    };
    loopEnabled.onClick = [this, set = editZone ([] (Zone& z, double v)
    {
        z.loop = v != 0 && z.sample && validLoop (z, static_cast<int> (z.sample->left.size()));
    })] { set (loopEnabled.getToggleState()); };

    keyboard.setTitle ("Sampler audition keyboard");
    keyboard.setOctaveForMiddleC (4);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (16.0f);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffe0e7f0));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, colours::rail);
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, colours::rail);
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, colours::action.withAlpha (0.82f));
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::focus.withAlpha (0.30f));
    keyboard.setWantsKeyboardFocus (true);
    keys.addListener (this);
    selection = initialZone();
    appearanceChanged();
    refresh();
    setSize (800, 564);
    startTimerHz (24);
}

Editor::~Editor()
{
    stopTimer();
    keys.removeListener (this);
    setLookAndFeel (nullptr);
    plugin.preview (1, 1, -1, 0);
    plugin.preview (3, 1, -1, 0);
}

void Editor::setup (Control& c, String label, double min, double max, double interval, String suffix,
                    std::function<void(double)> apply, bool dial)
{
    body.addAndMakeVisible (c.label);
    body.addAndMakeVisible (c.slider);
    c.label.setText (label, dontSendNotification);
    c.label.setJustificationType (dial ? Justification::centred : Justification::centredLeft);
    c.label.setBorderSize (BorderSize<int>());
    c.label.setMinimumHorizontalScale (1);
    c.label.setColour (Label::textColourId, colours::text);
    c.slider.setTitle (label);
    c.slider.setTooltip (label);
    c.slider.setSliderStyle (dial ? Slider::RotaryHorizontalVerticalDrag : Slider::LinearHorizontal);
    c.slider.setTextBoxStyle (dial ? Slider::TextBoxBelow : Slider::TextBoxRight, false, 72, 22);
    c.slider.setRange (min, max, interval);
    c.slider.setTextValueSuffix (suffix);
    c.slider.setColour (Slider::rotarySliderFillColourId, colours::action);
    c.slider.setColour (Slider::trackColourId, colours::action);
    c.slider.setColour (Slider::textBoxOutlineColourId, colours::divider);
    c.slider.setWantsKeyboardFocus (true);
    c.slider.onValueChange = [this, &c, apply = std::move (apply)]
    {
        if (! updating) apply (c.slider.getValue());
    };
}

const SampleZone* Editor::zone() const
{
    const auto& zones = plugin.instrument().zones;
    return isPositiveAndBelow (selection, static_cast<int> (zones.size())) ? &zones[static_cast<std::size_t> (selection)] : nullptr;
}

int Editor::initialZone() const
{
    const auto& zones = plugin.instrument().zones;
    const auto missing = std::find_if (zones.begin(), zones.end(), [] (const auto& entry)
    {
        return entry.mapping.sample == nullptr;
    });
    return missing == zones.end() ? 0 : static_cast<int> (missing - zones.begin());
}

void Editor::selectZone (int index)
{
    selection = index;
    refresh();
}

void Editor::setPage (Page value)
{
    page = value;
    viewport.setViewPosition (0, 0);
    refresh();
}

void Editor::setStatus (const String& value)
{
    const bool changesLayout = status.getText().isEmpty() != value.isEmpty();
    status.setText (value, dontSendNotification);
    status.setTooltip (value);
    if (changesLayout && ! updating) resized();
}

void Editor::refresh()
{
    const ScopedValueSetter<bool> quiet (updating, true);
    seen = plugin.changeCount();
    const auto& model = plugin.instrument();
    if (! name.isBeingEdited()) name.setText (model.name, dontSendNotification);
    preset.setText ((model.preset.isEmpty() ? String ("New Instrument") : model.preset)
                        + (model.dirty ? "  |  Unsaved changes" : ""), dontSendNotification);
    preset.setTooltip (preset.getText());
    if (selection >= static_cast<int> (model.zones.size())) selection = jmax (0, static_cast<int> (model.zones.size()) - 1);

    StringArray filenames;
    for (const auto& entry : model.zones)
        filenames.add (entry.file.getFileName() + (entry.mapping.sample ? "" : " (Unavailable)"));
    bool changed = samples.getNumItems() != filenames.size();
    for (int i = 0; ! changed && i < filenames.size(); ++i) changed = samples.getItemText (i) != filenames[i];
    if (changed)
    {
        samples.clear (dontSendNotification);
        samples.addItemList (filenames, 1);
    }
    samples.setSelectedItemIndex (zone() ? selection : -1, dontSendNotification);
    const auto* entry = zone();
    const Zone z = entry ? entry->mapping : Zone();
    const auto& s = model.settings;
    const bool playable = entry && z.sample;
    filename.setText (entry ? entry->file.getFileName() + "  |  " + noteName (z.low) + " to " + noteName (z.high)
                            : String(), dontSendNotification);
    filename.setTooltip (entry ? entry->file.getFullPathName() : String());
    if (plugin.stateError().isNotEmpty()) setStatus (plugin.stateError());
    else if (entry && entry->problem.isNotEmpty()) setStatus (entry->problem + " | " + entry->file.getFileName());
    else setStatus ({});

    const auto value = [] (Control& c, double v)
    {
        if (! c.slider.isMouseButtonDown() && ! c.slider.hasKeyboardFocus (true))
            c.slider.setValue (v, dontSendNotification);
    };
    value (attack, s.attack * 1000);
    value (decay, s.decay * 1000);
    value (sustain, s.sustain * 100);
    value (release, s.release * 1000);
    value (gain, s.gainDb);
    value (tune, s.semitones);
    value (voices, s.polyphony);
    value (bend, s.bendRange);
    value (root, z.root);
    value (low, z.low);
    value (high, z.high);
    value (soft, z.soft);
    value (hard, z.hard);
    value (zoneGain, z.gainDb);
    value (cents, z.cents);
    value (group, z.group);
    const auto length = playable ? static_cast<int> (z.sample->left.size()) : jmax (2, z.loopEnd);
    if (loopStart.slider.getMaximum() != length)
    {
        loopStart.slider.setRange (0, length, 1);
        loopEnd.slider.setRange (0, length, 1);
    }
    value (loopStart, z.loopStart);
    value (loopEnd, z.loopEnd);
    mode.setSelectedId (z.oneShot ? 2 : 1, dontSendNotification);
    loopEnabled.setToggleState (z.loop, dontSendNotification);
    loopEnabled.setEnabled (entry && ! z.oneShot && (playable || z.loop));
    loopStart.slider.setEnabled (playable && ! z.oneShot);
    loopEnd.slider.setEnabled (playable && ! z.oneShot);
    mode.setEnabled (entry != nullptr);

    basic.setToggleState (page == Page::basic, dontSendNotification);
    mapping.setToggleState (page == Page::mapping, dontSendNotification);
    loops.setToggleState (page == Page::loops, dontSendNotification);
    advanced.setToggleState (page == Page::advanced, dontSendNotification);
    const bool main = page == Page::basic, map = page == Page::mapping, loop = page == Page::loops;
    load.setButtonText (map ? "Add Sample" : "Load Sample");
    load.setTitle (load.getButtonText());
    load.setVisible (main || map);
    locate.setVisible ((main || map || loop) && entry && (! playable || map));
    locate.setButtonText (playable ? "Replace Sample" : "Locate Sample");
    locate.setTitle (locate.getButtonText());
    remove.setVisible (map);
    remove.setEnabled (entry != nullptr);
    audition.setVisible (! (page == Page::advanced));
    audition.setEnabled (playable);
    display.setVisible (main || map || loop);
    samples.setVisible (! main);
    filename.setVisible (main || loop);
    mode.setVisible (loop);
    loopEnabled.setVisible (loop);
    envelope.setVisible (main);
    meter.setVisible (page == Page::advanced);
    for (auto* c : { &attack, &decay, &sustain, &release, &gain, &tune, &root, &low, &high, &soft, &hard,
                     &zoneGain, &cents, &loopStart, &loopEnd, &voices, &bend, &group })
    {
        const bool show = main ? c == &attack || c == &decay || c == &sustain || c == &release || c == &gain || c == &root
            : map ? c == &root || c == &low || c == &high || c == &soft || c == &hard || c == &zoneGain || c == &cents || c == &group
            : loop ? c == &loopStart || c == &loopEnd : c == &voices || c == &bend || c == &tune;
        c->label.setVisible (show);
        c->slider.setVisible (show);
    }
    for (auto* c : { &root, &low, &high, &soft, &hard, &zoneGain, &cents, &group })
        c->slider.setEnabled (entry != nullptr);
    resized();
    display.repaint();
    keyboard.repaint();
}

void Editor::appearanceChanged()
{
    name.setFont (FontOptions (20, Font::bold));
    for (auto* label : { &preset, &filename, &status, &meter })
        label->setFont (FontOptions (12));
    for (auto* c : { &attack, &decay, &sustain, &release, &gain, &tune, &root, &low, &high, &soft, &hard,
                     &zoneGain, &cents, &loopStart, &loopEnd, &voices, &bend, &group })
    {
        c->label.setFont (FontOptions (12));
        c->slider.setTextBoxStyle (c->slider.getTextBoxPosition(), false, appearance.span (67), appearance.control (23, 10));
    }
    viewport.setScrollBarThickness (appearance.scrollbar());
    keyboard.setKeyWidth (static_cast<float> (appearance.px (16)));
    resized();
}

int Editor::minimumDockHeight() const
{
    return appearance.line (20, 1) + appearance.line (12, 5) + appearance.control (28, 12) + appearance.px (156);
}

String Editor::viewState() const { return String (static_cast<int> (page)); }

void Editor::restoreView (const String& stored)
{
    setPage (static_cast<Page> (jlimit (0, 3, stored.getIntValue())));
}

void Editor::resized()
{
    const auto& m = appearance;
    const auto gap = m.px (9);
    const auto line = m.line (12, 5);
    const auto button = m.control (28, 12);
    auto area = getLocalBounds().reduced (m.px (14), m.px (9));
    auto header = area.removeFromTop (jmax (m.line (20, 1) + line, button));
    const auto put = [gap] (Rectangle<int>& row, Component& c, int width)
    {
        c.setBounds (row.removeFromLeft (jmin (width, row.getWidth())));
        row.removeFromLeft (gap);
    };
    auto buttons = header.removeFromRight (jmin (header.getWidth() / 2, m.span (232)));
    auto buttonRow = buttons.withSizeKeepingCentre (buttons.getWidth(), button);
    const auto half = (buttonRow.getWidth() - gap) / 2;
    put (buttonRow, loadPreset, half);
    put (buttonRow, savePreset, half);
    name.setBounds (header.removeFromTop (m.line (20, 1)));
    preset.setBounds (header.removeFromTop (line));

    area.removeFromTop (gap);
    auto tabs = area.removeFromTop (button);
    const int tabWidth = jmax (0, jmin (m.span (107), (tabs.getWidth() - 3 * gap) / 4));
    for (auto* tab : { &basic, &mapping, &loops, &advanced }) put (tabs, *tab, tabWidth);
    area.removeFromTop (gap);
    keyboard.setBounds (area.removeFromBottom (m.px (61)));
    area.removeFromBottom (gap);
    viewport.setBounds (area);

    const int width = jmax (1, viewport.getMaximumVisibleWidth() - viewport.getScrollBarThickness());
    const int waveform = m.px (page == Page::mapping ? 148 : 145);
    const int dialHeight = m.px (88) + line;
    const int gridColumns = page == Page::mapping ? (width < m.span (495) ? 3 : 4)
        : page == Page::loops ? 2 : (width < m.span (500) ? 2 : 3);
    const int cells = page == Page::mapping ? 8 : page == Page::loops || page == Page::basic ? 2 : 3;
    const int rows = (cells + gridColumns - 1) / gridColumns;
    const int actionMinimum = m.span (132);
    const int actionColumns = jlimit (1, 4, (width + gap) / (actionMinimum + gap));
    std::array<Component*, 4> actions {};
    int actionCount = 0;
    for (auto* action : { &load, &locate, &audition, &remove })
        if (action->isVisible()) actions[static_cast<std::size_t> (actionCount++)] = action;
    const int actionRows = (actionCount + actionColumns - 1) / actionColumns;
    const bool sampleList = page != Page::basic;
    const bool hasWaveform = page != Page::advanced;
    const bool sampleName = page == Page::basic || page == Page::loops;
    const bool hasStatus = status.getText().isNotEmpty();
    const int height = (actionRows + (sampleList ? 1 : 0)) * (button + gap)
        + (sampleName ? line : 0) + (hasWaveform ? waveform + gap : 0) + (hasStatus ? line + gap : 0)
        + (page == Page::basic ? dialHeight + gap : 0)
        + (page == Page::loops ? button + gap : 0)
        + rows * (line + button + gap) + (page == Page::advanced ? line * 3 : 0);
    body.setSize (width, jmax (viewport.getHeight(), height));
    auto content = body.getLocalBounds();
    if (sampleList)
    {
        auto row = content.removeFromTop (button);
        put (row, samples, jmin (m.span (480), width));
        content.removeFromTop (gap);
    }
    for (int first = 0; first < actionCount; first += actionColumns)
    {
        auto row = content.removeFromTop (button);
        const auto w = jmin (m.span (146), (width - gap * (actionColumns - 1)) / actionColumns);
        for (int i = first; i < jmin (first + actionColumns, actionCount); ++i)
            put (row, *actions[static_cast<std::size_t> (i)], w);
        content.removeFromTop (gap);
    }
    if (page == Page::basic || page == Page::loops)
        filename.setBounds (content.removeFromTop (line));
    if (page != Page::advanced)
        display.setBounds (content.removeFromTop (waveform));
    content.removeFromTop (gap);
    status.setVisible (hasStatus);
    if (hasStatus)
    {
        status.setBounds (content.removeFromTop (line));
        content.removeFromTop (gap);
    }

    const auto placeGrid = [=, &content] (std::initializer_list<Control*> items, int columns)
    {
        auto list = items.begin();
        int remaining = static_cast<int> (items.size());
        while (remaining > 0)
        {
            auto row = content.removeFromTop (line + button);
            const auto cellWidth = jmax (1, (row.getWidth() - (columns - 1) * gap) / columns);
            for (int i = 0; i < columns && remaining > 0; ++i, --remaining, ++list)
            {
                auto cell = row.removeFromLeft (cellWidth);
                (*list)->label.setBounds (cell.removeFromTop (line));
                (*list)->slider.setBounds (cell);
                row.removeFromLeft (gap);
            }
            content.removeFromTop (gap);
        }
    };

    if (page == Page::basic)
    {
        auto shape = content.removeFromTop (dialHeight);
        auto dials = shape.removeFromLeft (shape.getWidth() * 2 / 3);
        const auto dialWidth = dials.getWidth() / 4;
        for (auto* c : { &attack, &decay, &sustain, &release })
        {
            auto cell = dials.removeFromLeft (dialWidth);
            c->label.setBounds (cell.removeFromTop (line));
            c->slider.setBounds (cell);
        }
        envelope.setBounds (shape.reduced (gap));
        owned[0]->setBounds (envelope.getLocalBounds());
        content.removeFromTop (gap);
        placeGrid ({ &root, &gain }, 2);
    }
    else if (page == Page::mapping)
        placeGrid ({ &root, &low, &high, &soft, &hard, &zoneGain, &cents, &group }, gridColumns);
    else if (page == Page::loops)
    {
        auto options = content.removeFromTop (button);
        put (options, mode, m.span (130));
        put (options, loopEnabled, m.span (150));
        content.removeFromTop (gap);
        placeGrid ({ &loopStart, &loopEnd }, 2);
    }
    else
    {
        placeGrid ({ &voices, &bend, &tune }, gridColumns);
        meter.setBounds (content.removeFromTop (line * 3));
    }
}

void Editor::paint (Graphics& g)
{
    g.fillAll (colours::panel);
}

void Editor::choose (bool presetFile, bool save, bool replace)
{
    if (chooser) return;
    chooser = std::make_unique<FileChooser> (presetFile ? (save ? "Save Studio Sampler preset" : "Load Studio Sampler preset")
                                                       : (replace ? "Locate sample" : "Load sample"),
        save ? File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (File::createLegalFileName (plugin.instrument().name) + ".studiosampler") : File(),
        presetFile ? "*.studiosampler" : "*.wav;*.wave;*.aif;*.aiff;*.aifc", true, false, this);
    const auto modeFlags = save ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
                                : FileBrowserComponent::openMode;
    chooser->launchAsync (modeFlags | FileBrowserComponent::canSelectFiles,
        [safe = Component::SafePointer<Editor> (this), presetFile, save, replace, index = selection,
         revision = plugin.changeCount()] (const FileChooser& dialog)
        {
            if (safe == nullptr) return;
            const auto file = dialog.getResult();
            MessageManager::callAsync ([safe, file, presetFile, save, replace, index, revision]
            {
                if (safe == nullptr) return;
                safe->chooser.reset();
                if (file == File()) return;
                if (replace && safe->plugin.changeCount() != revision)
                {
                    safe->setStatus ("The zone changed. Choose its replacement again.");
                    return;
                }
                if (! presetFile)
                {
                    safe->import (file, replace ? index : -1);
                    return;
                }
                const auto destination = file.withFileExtension (".studiosampler");
                if (save && destination != file && destination.existsAsFile())
                {
                    safe->setStatus ("An existing preset has that name. Select the .studiosampler file to replace it.");
                    return;
                }
                const auto result = save ? safe->plugin.savePreset (destination)
                                         : safe->plugin.loadPreset (file);
                if (result.wasOk() && ! save) safe->selection = safe->initialZone();
                safe->refresh();
                if (result.failed()) safe->setStatus (result.getErrorMessage());
            });
        });
}

void Editor::import (const File& file, int replace)
{
    const auto result = plugin.loadSample (file, replace);
    if (result.wasOk())
        selectZone (replace >= 0 ? replace : static_cast<int> (plugin.instrument().zones.size()) - 1);
    else
        setStatus (file.getFileName() + " | " + result.getErrorMessage());
}

void Editor::playAudition()
{
    if (auditionNote >= 0)
    {
        plugin.preview (3, 1, -1, 0);
        auditionNote = -1;
        audition.setButtonText ("Audition");
        return;
    }
    if (const auto* entry = zone())
    {
        const auto& z = entry->mapping;
        auditionNote = z.root;
        plugin.preview (3, 1, auditionNote, 100, selection);
        clearAudition = false;
        audition.setButtonText ("Stop Audition");
    }
}

void Editor::handleNoteOn (MidiKeyboardState*, int channel, int note, float velocity)
{
    plugin.preview (1, channel, note, jlimit (1, 127, roundToInt (velocity * 127)));
}
void Editor::handleNoteOff (MidiKeyboardState*, int channel, int note, float)
{
    plugin.preview (1, channel, note, 0);
}

void Editor::timerCallback()
{
    if (seen != plugin.changeCount()) refresh();
    const auto stats = plugin.resourceSnapshot();
    if (page == Page::advanced)
        meter.setText (String (stats.activeVoices.value_or (0)) + " active voices  |  "
            + String (static_cast<double> (stats.sampleBytes.value_or (0)) / (1024 * 1024), 2) + " MiB in memory"
            + "\nCC1 and channel pressure control vibrato. MIDI channels respond independently.", dontSendNotification);
    if (auditionNote >= 0 && clearAudition && stats.activeVoices.value_or (0) == 0)
    {
        auditionNote = -1;
        audition.setButtonText ("Audition");
    }
    clearAudition = auditionNote >= 0;
    keyboard.repaint();
}

Editor::Keyboard::Keyboard (Editor& editor, MidiKeyboardState& state)
    : MidiKeyboardComponent (state, horizontalKeyboard), owner (editor) {}

void Editor::Keyboard::drawRange (int note, Graphics& g, Rectangle<float> area)
{
    const auto* selected = owner.zone();
    if (selected && note >= selected->mapping.low && note <= selected->mapping.high)
    {
        g.setColour (colours::action);
        g.fillRect (area.removeFromTop (3));
    }
}
void Editor::Keyboard::drawWhiteNote (int note, Graphics& g, Rectangle<float> area, bool down, bool over,
                                      Colour line, Colour text)
{
    MidiKeyboardComponent::drawWhiteNote (note, g, area, down || owner.plugin.noteActive (note), over, line, text);
    drawRange (note, g, area);
}
void Editor::Keyboard::drawBlackNote (int note, Graphics& g, Rectangle<float> area, bool down, bool over, Colour colour)
{
    MidiKeyboardComponent::drawBlackNote (note, g, area, down || owner.plugin.noteActive (note), over, colour);
    drawRange (note, g, area);
}

Editor::Display::Display (Editor& editor) : owner (editor) { setTitle ("Sample waveform or keyboard mapping"); }
Rectangle<int> Editor::Display::plot() const
{
    auto area = getLocalBounds().reduced (appearance.px (12), appearance.px (8));
    if (owner.page == Page::mapping) area.removeFromLeft (appearance.span (24));
    return area;
}
Rectangle<float> Editor::Display::zoneBounds (const Zone& z) const
{
    const auto area = plot().withTrimmedBottom (appearance.line (10, 4)).toFloat();
    return { area.getX() + z.low * area.getWidth() / 128, area.getY() + (127 - z.hard) * area.getHeight() / 127,
             (z.high - z.low + 1) * area.getWidth() / 128, (z.hard - z.soft + 1) * area.getHeight() / 127 };
}

void Editor::Display::paint (Graphics& g)
{
    g.setColour (draggingFiles ? colours::selected : colours::canvas);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7);
    auto area = plot();
    const auto* entry = owner.zone();
    if (owner.page == Page::mapping && entry)
    {
        const auto keys = area.removeFromBottom (appearance.line (10, 4));
        g.setFont (uiFont (10));
        g.setColour (colours::secondary);
        const auto legend = Rectangle<int> (appearance.px (4), area.getY(), area.getX() - appearance.px (8), area.getHeight());
        g.drawText ("127", legend, Justification::topRight);
        g.drawText ("1", legend, Justification::bottomRight);
        for (int i = 0; i < 128; ++i)
        {
            const float left = static_cast<float> (keys.getX()) + static_cast<float> (i * keys.getWidth()) / 128;
            const auto w = static_cast<float> (keys.getWidth()) / 128;
            g.setColour (MidiMessage::isMidiNoteBlack (i) ? colours::rail : colours::secondary);
            g.fillRect (Rectangle<float> (left, static_cast<float> (keys.getY()), jmax (1.0f, w - 0.5f), 5));
            if (i % 12 != 0) continue;
            g.setColour (colours::divider);
            g.drawVerticalLine (roundToInt (left), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
            g.setFont (uiFont (10));
            g.setColour (colours::secondary);
            g.drawText (noteName (i), roundToInt (left), keys.getY() + 4, roundToInt (12 * w), keys.getHeight() - 4, Justification::centredLeft);
        }
        const auto draw = [&] (const SampleZone& sample, const Zone& z, bool selected)
        {
            auto block = zoneBounds (z);
            g.setColour ((selected ? colours::selected : colours::midiClip).withAlpha (0.70f));
            g.fillRect (block);
            g.setColour (selected ? colours::focus : colours::divider);
            g.drawRect (block, selected ? 1.5f : 1.0f);
            g.setColour (colours::text);
            g.setFont (uiFont (12));
            g.drawFittedText (sample.file.getFileNameWithoutExtension(), block.toNearestInt().reduced (5, 1),
                              Justification::centred, 1, 1.0f);
        };
        for (const auto& sample : owner.plugin.instrument().zones)
            if (&sample != entry) draw (sample, sample.mapping, false);
        draw (*entry, marker != 0 ? dragZone : entry->mapping, true);
        return;
    }

    if (! entry || ! entry->mapping.sample)
    {
        g.setColour (colours::text);
        g.setFont (uiFont (16));
        const auto empty = owner.page == Page::mapping ? "Drop a sample here or choose Add Sample."
            : owner.page == Page::loops ? "Load a sample in Basic to edit its loop."
            : "Drop a sample here or choose Load Sample.";
        g.drawFittedText (entry ? "Sample unavailable" : empty,
                          area, Justification::centred, 2, 1.0f);
        return;
    }

    const auto& z = marker != 0 ? dragZone : entry->mapping;
    const auto& sample = *z.sample;
    const auto length = static_cast<int> (sample.left.size());
    if (area.getWidth() <= 0) return;
    const auto position = [area, length] (int frame)
    {
        return area.getX() + static_cast<float> (area.getWidth()) * frame / length;
    };
    if (owner.page == Page::loops)
    {
        const auto start = position (jlimit (0, length, z.loopStart));
        const auto end = position (jlimit (0, length, z.loopEnd));
        g.setColour (colours::action.withAlpha (z.loop && ! z.oneShot ? 0.20f : 0.07f));
        g.fillRect (Rectangle<float> (start, static_cast<float> (area.getY()), end - start, static_cast<float> (area.getHeight())));
    }

    const auto wave = [&] (const std::vector<float>& data, Rectangle<int> lane)
    {
        const float center = lane.getCentreY(), scale = lane.getHeight() * 0.45f;
        g.setColour (colours::divider);
        g.drawHorizontalLine (lane.getCentreY(), static_cast<float> (lane.getX()), static_cast<float> (lane.getRight()));
        g.setColour (colours::action);
        for (int x = 0; x < lane.getWidth(); ++x)
        {
            const auto first = static_cast<int> (static_cast<int64> (x) * length / lane.getWidth());
            const auto end = jmax (first + 1, static_cast<int> (static_cast<int64> (x + 1) * length / lane.getWidth()));
            const auto stride = jmax (1, (end - first) / 64);
            float lo = 1, hi = -1;
            for (int frame = first; frame < end; frame += stride)
            {
                const auto v = data[static_cast<std::size_t> (frame)];
                lo = jmin (lo, v);
                hi = jmax (hi, v);
            }
            g.drawVerticalLine (lane.getX() + x, center - hi * scale,
                               jmax (center - hi * scale + 1, center - lo * scale));
        }
    };
    if (sample.right.empty()) wave (sample.left, area);
    else
    {
        auto stereo = area;
        wave (sample.left, stereo.removeFromTop (stereo.getHeight() / 2));
        wave (sample.right, stereo);
    }
    if (owner.page == Page::loops)
    {
        for (int frame : { z.loopStart, z.loopEnd })
        {
            const auto x = roundToInt (position (jlimit (0, length, frame)));
            g.setColour (colours::focus);
            g.drawVerticalLine (x, static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
            g.fillEllipse (static_cast<float> (x - 4), static_cast<float> (area.getY()), 8, 8);
        }
    }
}

void Editor::Display::mouseDown (const MouseEvent& event)
{
    dragRevision = owner.plugin.changeCount();
    if (owner.page == Page::mapping)
    {
        const auto& zones = owner.plugin.instrument().zones;
        const auto hit = [&] (const Zone& z) { return zoneBounds (z).expanded (4, 2).contains (event.position); };
        const auto* entry = owner.zone();
        if (! entry || ! hit (entry->mapping))
            for (int i = static_cast<int> (zones.size()); --i >= 0;)
                if (hit (zones[static_cast<std::size_t> (i)].mapping))
                {
                    owner.selectZone (i);
                    break;
                }
        if ((entry = owner.zone()) && hit (entry->mapping))
        {
            dragSelection = owner.selection;
            dragZone = entry->mapping;
            const auto bounds = zoneBounds (dragZone);
            const auto x = event.position.x;
            if (std::abs (x - bounds.getX()) < 8) marker = 1;
            else if (std::abs (x - bounds.getRight()) < 8) marker = 2;
        }
    }
    else if (owner.page == Page::loops)
        if (const auto* entry = owner.zone(); entry && entry->mapping.sample && ! entry->mapping.oneShot)
        {
            dragSelection = owner.selection;
            dragZone = entry->mapping;
            const auto area = plot();
            const auto frame = (event.position.x - area.getX()) * dragZone.sample->left.size() / jmax (1, area.getWidth());
            marker = std::abs (frame - dragZone.loopStart) < std::abs (frame - dragZone.loopEnd) ? 1 : 2;
        }
}

void Editor::Display::mouseDrag (const MouseEvent& event)
{
    if (marker == 0) return;
    const auto area = plot();
    const auto ratio = jlimit (0.0f, 1.0f, (event.position.x - area.getX()) / jmax (1, area.getWidth()));
    if (owner.page == Page::mapping)
    {
        const int note = jlimit (0, 127, roundToInt (ratio * 128) - (marker == 2 ? 1 : 0));
        if (marker == 1) dragZone.low = jmin (note, dragZone.high);
        else dragZone.high = jmax (note, dragZone.low);
    }
    else
    {
        const int length = static_cast<int> (dragZone.sample->left.size());
        const int frame = roundToInt (ratio * length);
        if (marker == 1) dragZone.loopStart = jlimit (0, jmax (0, dragZone.loopEnd - 2), frame);
        else dragZone.loopEnd = jlimit (jmin (length, dragZone.loopStart + 2), length, frame);
    }
    repaint();
}

void Editor::Display::mouseUp (const MouseEvent& event)
{
    if (marker != 0 && event.mouseWasDraggedSinceMouseDown() && dragRevision == owner.plugin.changeCount())
        owner.plugin.editZone (dragSelection, dragZone);
    marker = 0;
    dragZone = {};
    owner.refresh();
}

bool Editor::Display::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& file : files)
        if (sampleFile (file)) return true;
    return false;
}
void Editor::Display::fileDragEnter (const StringArray&, int, int) { draggingFiles = true; repaint(); }
void Editor::Display::fileDragExit (const StringArray&) { draggingFiles = false; repaint(); }
void Editor::Display::filesDropped (const StringArray& files, int, int)
{
    draggingFiles = false;
    for (const auto& file : files)
        if (sampleFile (file)) owner.import (File (file), -1);
    repaint();
}

#if STUDIO_SPATIAL_TESTS
bool Editor::runReview (const File& first, const File& second, const File& output,
                        const std::function<bool()>& auditionCheck)
{
    const auto capture = [this, &output] (const char* file)
    {
        if (output == File()) return true;
        if (output.createDirectory().failed()) return false;
        FileOutputStream stream (output.getChildFile (file));
        return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds()), stream);
    };
    if (page != Page::basic || zone() || audition.isEnabled() || ! capture ("sampler-empty.png")) return false;
    display.filesDropped ({ first.getFullPathName() }, 30, 30);
    if (! zone() || ! zone()->mapping.sample || ! audition.isEnabled() || ! capture ("sampler-basic.png")) return false;
    attack.slider.setValue (23, sendNotificationSync);
    if (std::abs (plugin.instrument().settings.attack - .023f) > .0001f)
    {
        std::fprintf (stderr, "Sampler Attack control did not change the instrument\n");
        return false;
    }

    const auto rect = keyboard.getRectangleForKey (60);
    const Point<float> key { rect.getCentreX(), rect.getBottom() - 7 };
    const auto time = Time::getCurrentTime();
    const auto mouse = Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (int flags)
    {
        return MouseEvent (mouse, key, ModifierKeys (flags), 0, 0, 0, 0, 0,
                           &keyboard, &keyboard, time, key, time, 1, false);
    };
    const bool pointerCanReachKeyboard = keyboard.reallyContains (key, false);
    if (pointerCanReachKeyboard)
        keyboard.mouseDown (event (ModifierKeys::leftButtonModifier));
    else
    {
        // CI can draw native windows without WindowServer exposing them for
        // hit-testing. In that environment send the notes from the same JUCE
        // keyboard state that mouse and QWERTY interaction normally update.
        std::puts ("Sampler keyboard: testing note dispatch through JUCE keyboard state (window is unavailable to pointer hit-testing).");
        keys.noteOn (1, 60, 0.9f);
    }
    const bool down = keys.isNoteOn (1, 60);
    const bool sounded = auditionCheck();
    if (pointerCanReachKeyboard)
        keyboard.mouseUp (event (0));
    else
        keys.noteOff (1, 60, 0.0f);
    if (! down || ! sounded || keys.isNoteOn (1, 60))
    {
        std::fprintf (stderr, "Sampler keyboard did not sound and release (%u voices, key %d,%d).\n",
                      plugin.resourceSnapshot().activeVoices.value_or (0), roundToInt (key.x), roundToInt (key.y));
        return false;
    }
    auditionCheck(); // Pump the queued release before checking the active key meter.
    if (plugin.noteActive (60)) return false;

    audition.onClick();
    if (! auditionCheck() || audition.getButtonText() != "Stop Audition") return false;
    audition.onClick();
    auditionCheck();
    if (plugin.noteActive (60) || audition.getButtonText() != "Audition") return false;

    mapping.onClick();
    if (page != Page::mapping || ! samples.isVisible()) return false;
    display.filesDropped ({ second.getFullPathName() }, 30, 30);
    if (samples.getNumItems() != 2 || selection != 1 || ! zone() || ! zone()->mapping.sample) return false;
    root.slider.setValue (67, sendNotificationSync);
    low.slider.setValue (65, sendNotificationSync);
    samples.setSelectedItemIndex (0, sendNotificationSync);
    if (selection != 0 || ! zone()) return false;
    high.slider.setValue (64, sendNotificationSync);
    if (zone()->mapping.high != 64 || ! capture ("sampler-mapping.png")) return false;

    loops.onClick();
    if (page != Page::loops || ! loopEnabled.isEnabled()) return false;
    loopStart.slider.setValue (12000, sendNotificationSync);
    loopEnd.slider.setValue (48000, sendNotificationSync);
    loopEnabled.setToggleState (true, sendNotificationSync);
    if (! zone()->mapping.loop || zone()->mapping.loopStart != 12000 || zone()->mapping.loopEnd != 48000
        || ! capture ("sampler-loops.png")) return false;
    advanced.onClick();
    if (page != Page::advanced || ! voices.slider.isVisible()) return false;
    voices.slider.setValue (16, sendNotificationSync);
    if (plugin.instrument().settings.polyphony != 16) return false;
    const ScopedValueSetter<AppearanceMetrics> appearanceScope (appearance, appearance);
    appearance.set (175, 130);
    appearanceChanged();
    setSize (618, 460);
    basic.onClick();
    if (! getLocalBounds().contains (keyboard.getBounds()) || viewport.getHeight() <= 0
        || ! body.getLocalBounds().contains (gain.slider.getBounds())
        || ! getLocalBounds().contains (loadPreset.getBounds()) || loadPreset.getBounds().intersects (savePreset.getBounds()))
        return false;
    return std::abs (plugin.instrument().settings.attack - .023f) < .0001f;
}
#endif

std::unique_ptr<tracktion::Plugin::EditorComponent> Plugin::createEditor()
{
    return std::make_unique<Editor> (*this);
}
}
