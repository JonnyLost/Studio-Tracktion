// SPDX-License-Identifier: GPL-3.0-or-later
#include "SamplerEditor.h"
#include "SamplerReviewCapture.h"
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
    return File (path).hasFileExtension ("wav;wave;aif;aiff;aifc;caf;flac");
}

bool soundBankFile (const File& file) { return file.hasFileExtension ("sf2;sf3;dls;gig;p1;p3;s1;s3"); }

class BankChooser final : public Component
{
public:
    BankChooser (std::vector<BankProgram> entries, std::function<void(int)> choose)
        : programs (std::move (entries)), selected (std::move (choose))
    {
        setLookAndFeel (&look);
        addAndMakeVisible (description);
        description.setText (String (static_cast<int> (programs.size())) + " programs. Select one instrument to convert.", dontSendNotification);
        addAndMakeVisible (search); search.setTextToShowWhenEmpty ("Search by name, bank or program", colours::secondary);
        search.onTextChange = [this] { refresh(); };
        addAndMakeVisible (list); list.setTextWhenNothingSelected ("No matching programs");
        list.onChange = [this] { accept.setEnabled (list.getSelectedId() > 0); };
        addAndMakeVisible (accept); addAndMakeVisible (cancel);
        accept.onClick = [this]
        {
            const int index = list.getSelectedId() - 1;
            if (index < 0) return;
            auto callback = selected;
            close();
            if (callback) callback (index);
        };
        cancel.onClick = [this] { close(); };
        refresh();
        setSize (620, 220);
    }
    ~BankChooser() override { setLookAndFeel (nullptr); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        description.setBounds (area.removeFromTop (36));
        area.removeFromTop (8); search.setBounds (area.removeFromTop (32));
        area.removeFromTop (8); list.setBounds (area.removeFromTop (36));
        auto row = area.removeFromBottom (32);
        accept.setBounds (row.removeFromRight (160)); row.removeFromRight (8);
        cancel.setBounds (row.removeFromRight (100));
    }
private:
    StudioLookAndFeel look;
    std::vector<BankProgram> programs;
    std::function<void(int)> selected;
    Label description;
    TextEditor search;
    ComboBox list;
    TextButton accept { "Review instrument" }, cancel { "Cancel" };
    void close() { if (auto* d = findParentComponentOfClass<DialogWindow>()) d->exitModalState (0); }
    void refresh()
    {
        const auto text = search.getText().trim();
        const int previous = list.getSelectedId();
        list.clear (dontSendNotification);
        for (std::size_t i=0;i<programs.size();++i)
        {
            const auto& p = programs[i];
            const auto label = p.name + "  |  Bank " + String (p.bank) + " / Program " + String (p.number)
                + (p.drums ? "  |  Percussion" : "");
            if (text.isEmpty() || label.containsIgnoreCase (text)) list.addItem (label,static_cast<int>(i)+1);
        }
        if (list.indexOfItemId(previous)>=0) list.setSelectedId(previous,dontSendNotification);
        else list.setSelectedItemIndex(0,dontSendNotification);
        accept.setEnabled (list.getSelectedId()>0);
    }
};

class ImportProposal final : public Component
{
public:
    ImportProposal (std::shared_ptr<Instrument> model, String summary, int firstZone, std::function<void()> commit, bool sfzImport = false)
        : proposal (std::move (model)), readOnlyMapping (sfzImport)
    {
        setLookAndFeel (&look);
        addAndMakeVisible (report); report.setMultiLine (true); report.setReadOnly (true); report.setText (summary);
        addAndMakeVisible (zones);
        for (std::size_t i = static_cast<std::size_t> (firstZone); i < proposal->zones.size(); ++i) zones.addItem (proposal->zones[i].file.getFileName(), static_cast<int> (i) + 1);
        const StringArray names { "Root note (C4 = 60)", "Lowest key", "Highest key", "Minimum velocity", "Maximum velocity" };
        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible (labels[i]); labels[i].setText (names[i], dontSendNotification);
            addAndMakeVisible (values[i]); values[i].setTitle (names[i]); values[i].setTooltip (names[i]); values[i].setRange (i < 3 ? 0 : 1, 127, 1);
            values[i].setSliderStyle (Slider::LinearHorizontal); values[i].setTextBoxStyle (Slider::TextBoxRight, false, 70, 24);
            values[i].onValueChange = [this, i]
            {
                if (updating || zones.getSelectedItemIndex() < 0) return;
                auto& z = proposal->zones[static_cast<std::size_t> (zones.getSelectedId() - 1)].mapping;
                const int n = roundToInt (values[i].getValue());
                if (i == 0) z.root = n;
                if (i == 1) z.low = jmin (n, z.high);
                if (i == 2) z.high = jmax (n, z.low);
                if (i == 3) z.soft = jmin (n, z.hard);
                if (i == 4) z.hard = jmax (n, z.soft);
                update();
            };
        }
        zones.onChange = [this] { update(); }; zones.setSelectedItemIndex (0, sendNotificationSync);
        if (readOnlyMapping)
        {
            zones.setVisible (false);
            for (int i = 0; i < 5; ++i) { labels[i].setVisible (false); values[i].setVisible (false); }
            accept.setButtonText ("Replace instrument");
        }
        addAndMakeVisible (accept); addAndMakeVisible (cancel);
        accept.onClick = [this, commit] { commit(); close(); };
        cancel.onClick = [this] { close(); };
        setSize (680, 520);
    }
    ~ImportProposal() override { setLookAndFeel (nullptr); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        if (readOnlyMapping)
        {
            auto row = area.removeFromBottom (32); accept.setBounds (row.removeFromRight (180)); row.removeFromRight (8); cancel.setBounds (row.removeFromRight (120));
            report.setBounds (area.withTrimmedBottom (8)); return;
        }
        report.setBounds (area.removeFromTop (150)); area.removeFromTop (8);
        zones.setBounds (area.removeFromTop (30)); area.removeFromTop (8);
        for (int i = 0; i < 5; ++i)
        { auto row = area.removeFromTop (43); labels[i].setBounds (row.removeFromLeft (180)); values[i].setBounds (row); }
        auto row = area.removeFromBottom (32); accept.setBounds (row.removeFromRight (120)); row.removeFromRight (8); cancel.setBounds (row.removeFromRight (120));
    }
private:
    StudioLookAndFeel look;
    std::shared_ptr<Instrument> proposal;
    TextEditor report; ComboBox zones; Label labels[5]; Slider values[5];
    TextButton accept { "Import" }, cancel { "Cancel" }; bool updating = false, readOnlyMapping = false;
    void close() { if (auto* dialog = findParentComponentOfClass<DialogWindow>()) dialog->exitModalState (0); }
    void update()
    {
        const ScopedValueSetter<bool> guard (updating, true);
        if (zones.getSelectedItemIndex() < 0) return;
        const auto& z = proposal->zones[static_cast<std::size_t> (zones.getSelectedId() - 1)].mapping;
        const int data[] { z.root, z.low, z.high, z.soft, z.hard };
        for (int i = 0; i < 5; ++i) values[i].setValue (data[i], dontSendNotification);
    }
};

class EnvelopePlot final : public Component
{
public:
    explicit EnvelopePlot (Plugin& p, std::function<const SampleZone*()> selected) : plugin (p), selection (std::move (selected)) { setTitle ("Amplitude envelope"); }
    void paint (Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::canvas);
        g.fillRoundedRectangle (area, 6);
        area = area.reduced (12, 9);
        auto s = plugin.instrument().settings;
        if (const auto* entry = selection(); entry && entry->mapping.sfz.enabled)
        { const auto& f = entry->mapping.sfz; s.attack = f.attack; s.decay = f.decay; s.sustain = f.sustain; s.release = f.release; }
        const auto attack = 0.12f + 0.15f * std::sqrt (std::min (s.attack / 10, 1.0f));
        const auto decay = 0.11f + 0.14f * std::sqrt (std::min (s.decay / 10, 1.0f));
        const auto release = 0.12f + 0.15f * std::sqrt (std::min (s.release / 15, 1.0f));
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
    std::function<const SampleZone*()> selection;
};
}

Editor::Editor (Plugin& p) : plugin (p)
{
    setLookAndFeel (&look);
    setName ("Studio Sampler");
    setTitle ("Studio Sampler instrument");
    limits.setSizeLimits (460, 320, 1600, 1000);
    limits.setMinimumOnscreenAmounts (0x10000, 30, 28, 30);
    for (Component* c : std::initializer_list<Component*> { &name, &preset, &loadPreset, &savePreset, &audition,
             &basic, &mapping, &groups, &loops, &advanced, &keyboard, &viewport, &cancelImport })
        addAndMakeVisible (*c);

    viewport.setViewedComponent (&body, false);
    viewport.setTitle ("Sampler controls");
    viewport.setScrollBarsShown (true, false);
    for (Component* c : std::initializer_list<Component*> { &filename, &status, &meter, &samples, &mode,
             &loopEnabled, &display, &load, &locate, &remove, &envelope, &groupList, &groupName, &addGroup, &copyGroup, &deleteGroup, &moveGroup, &groupRows, &duplicate, &undo, &redo, &editMapping, &groupMute, &groupSolo, &releaseTrigger, &triggerMode, &triggerChannel, &triggerLabel, &channelLabel, &playView })
        body.addAndMakeVisible (*c);

    auto* plot = owned.add (new EnvelopePlot (p, [this] { return zone(); }));
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

    for (auto* button : { &loadPreset, &savePreset, &basic, &mapping, &groups, &loops, &advanced,
                          &load, &locate, &remove, &audition })
    {
        button->setTitle (button->getButtonText());
        button->setColour (TextButton::buttonColourId, colours::control);
        button->setColour (TextButton::buttonOnColourId, colours::selected);
        button->setWantsKeyboardFocus (true);
    }
    for (auto* b : { &groups, &duplicate, &undo, &redo, &addGroup, &copyGroup, &deleteGroup, &moveGroup })
    { b->setTitle (b->getButtonText()); b->setTooltip (b->getButtonText()); }
    moveGroup.setTooltip ("Move the selected zone(s) into the group selected in the list. Group membership does not mute layers.");
    deleteGroup.setTooltip ("Delete this organizational group and move its zones into a remaining group.");
    copyGroup.setTooltip ("Duplicate this group's zones, sharing decoded samples without editing recordings.");
    undo.setTooltip ("Undo the last project edit, including sampler mapping changes.");
    redo.setTooltip ("Redo the last undone project edit.");
    cancelImport.setVisible (false);
    cancelImport.onClick = [this] { if (importTask) importTask->cancelled.store (true); };
    editMapping.onClick = [this] { setPage (Page::mapping); };
    editMapping.setTooltip ("Build the instrument: import recordings, assign notes and create velocity layers.");
    groupMute.setTooltip ("Silence this group, including selected-zone audition.");
    groupSolo.setTooltip ("Hear only soloed groups. Switching rules still apply.");
    releaseTrigger.setTooltip ("Play this group's samples once at note release or sustain-pedal lift. Uses the articulation selected when the note began.");
    triggerMode.addItem ("Normal notes", 1); triggerMode.addItem ("Keyswitch note", 2); triggerMode.addItem ("MIDI CC range", 3);
    triggerLabel.setText ("Group plays when", dontSendNotification);
    channelLabel.setText ("Receiving channel", dontSendNotification);
    triggerChannel.addItem ("Any channel", 1);
    for (int ch = 1; ch <= 16; ++ch) triggerChannel.addItem ("Channel " + String (ch), ch + 1);
    triggerMode.onChange = [this] { if (! updating) editGroup ([this] (Group& g) { g.trigger = static_cast<GroupTrigger> (triggerMode.getSelectedId() - 1); }); };
    triggerChannel.onChange = [this] { if (! updating) editGroup ([this] (Group& g) { g.channel = triggerChannel.getSelectedId() - 1; }); };
    groupMute.onClick = [this] { editGroup ([this] (Group& g) { g.mute = groupMute.getToggleState(); }); };
    groupSolo.onClick = [this] { editGroup ([this] (Group& g) { g.solo = groupSolo.getToggleState(); }); };
    releaseTrigger.onClick = [this] { editGroup ([this] (Group& g) { g.releaseTrigger = releaseTrigger.getToggleState(); }); };
    playView.setTooltip ("Audition the visible waveform region once, without changing sample boundaries or loops.");
    playView.onClick = [this]
    {
        if (auditionNote >= 0) { playAudition(); return; }
        if (const auto* z = zone(); z && z->mapping.sample)
        {
            const int frames = z->mapping.sample->frameCount();
            const int visible = jmax (2, roundToInt (frames / waveZoom));
            const int first = roundToInt ((frames - visible) * waveOffset);
            plugin.previewRange (selection, first, first + visible);
            auditionNote = z->mapping.root; clearAudition = false; audition.setButtonText ("Stop");
        }
    };
    load.setColour (TextButton::buttonColourId, colours::action);
    load.setColour (TextButton::textColourOffId, colours::rail);
    loadPreset.onClick = [this] { choose (true, false, false); };
    loadPreset.setTooltip ("Load a Studio preset or import SFZ, EXS, Decent Sampler, legacy Kontakt, SoundFont, DLS, GIG or extracted Akai.");
    savePreset.onClick = [this] { choose (true, true, false); };
    basic.onClick = [this] { setPage (Page::basic); };
    groups.onClick = [this] { setPage (Page::groups); };
    undo.onClick = [this] { plugin.undoMapping (false); refresh(); };
    redo.onClick = [this] { plugin.undoMapping (true); refresh(); };
    duplicate.onClick = [this]
    {
        auto next = plugin.instrument();
        auto chosen = selectedZones; if (chosen.empty()) chosen.insert (selection);
        if (next.zones.size() + chosen.size() > maxZones) { setStatus ("Zone limit reached (128)."); return; }
        for (int i : chosen) if (isPositiveAndBelow (i, static_cast<int> (plugin.instrument().zones.size()))) next.zones.push_back (plugin.instrument().zones[static_cast<std::size_t> (i)]);
        plugin.commitImport (std::move (next)); refresh();
    };
    addGroup.onClick = [this]
    {
        auto list = plugin.instrument().groups;
        for (int id = 0; id < maxGroups; ++id)
            if (std::none_of (list.begin(), list.end(), [id] (auto& g) { return g.id == id; }))
            { list.push_back ({ id, "New group" }); plugin.editGroups (list); refresh(); groupList.setSelectedId (id + 1); return; }
        setStatus ("Group limit reached (16).");
    };
    copyGroup.onClick = [this]
    {
        auto next = plugin.instrument(); const int original = groupList.getSelectedId() - 1;
        for (int id = 0; id < maxGroups; ++id)
            if (std::none_of (next.groups.begin(), next.groups.end(), [id] (auto& g) { return g.id == id; }))
            {
                std::vector<SampleZone> copies;
                for (auto z : next.zones) if (z.folder == original) { z.folder = id; copies.push_back (z); }
                if (next.zones.size() + copies.size() > maxZones) { setStatus ("Zone limit reached (128)."); return; }
                auto copied = *std::find_if (next.groups.begin(), next.groups.end(), [original] (const auto& g) { return g.id == original; });
                copied.id = id; copied.playback.id = id; copied.name += " copy"; next.groups.push_back (copied);
                next.zones.insert (next.zones.end(), copies.begin(), copies.end());
                plugin.commitImport (std::move (next)); refresh(); return;
            }
        setStatus ("Group limit reached (16).");
    };
    deleteGroup.onClick = [this]
    {
        auto list = plugin.instrument().groups;
        if (list.size() <= 1) { setStatus ("Keep at least one group."); return; }
        std::erase_if (list, [this] (auto& g) { return g.id == groupList.getSelectedId() - 1; });
        plugin.editGroups (list); refresh();
    };
    moveGroup.onClick = [this]
    {
        auto next = plugin.instrument(); auto chosen = selectedZones; if (chosen.empty()) chosen.insert (selection);
        for (int i : chosen) if (isPositiveAndBelow (i, static_cast<int> (next.zones.size()))) next.zones[static_cast<std::size_t> (i)].folder = groupList.getSelectedId() - 1;
        plugin.commitImport (std::move (next)); refresh();
    };
    groupName.setEditable (false, true);
    groupName.setTooltip ("Double-click to rename this organizational group. Round robin is configured independently.");
    groupList.onChange = [this]
    {
        for (const auto& g : plugin.instrument().groups)
            if (g.id == groupList.getSelectedId() - 1) groupName.setText (g.name, dontSendNotification);
        if (! updating) refresh();
    };
    groupName.onTextChange = [this]
    {
        if (updating) return;
        auto list = plugin.instrument().groups;
        for (auto& g : list) if (g.id == groupList.getSelectedId() - 1) g.name = groupName.getText().trim().substring (0, 120);
        plugin.editGroups (list);
    };
    mapping.onClick = [this] { setPage (Page::mapping); };
    loops.onClick = [this] { setPage (Page::loops); };
    advanced.onClick = [this] { setPage (Page::advanced); };
    load.onClick = [this] { choose (false, false, false); };
    locate.onClick = [this] { choose (false, false, true); };
    remove.onClick = [this]
    {
        auto next = plugin.instrument(); auto chosen = selectedZones; if (chosen.empty()) chosen.insert (selection);
        for (auto i = chosen.rbegin(); i != chosen.rend(); ++i) if (isPositiveAndBelow (*i, static_cast<int> (next.zones.size()))) next.zones.erase (next.zones.begin() + *i);
        selectedZones.clear(); plugin.commitImport (std::move (next)); refresh();
    };
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
    const auto setEnvelope = [this, setSetting] (float Settings::* native, float SfzRegion::* imported, double factor)
    {
        return [this, setSetting, native, imported, factor] (double value)
        {
            if (const auto* entry = zone(); entry && entry->mapping.sfz.enabled)
            {
                auto z = entry->mapping;
                z.sfz.*imported = static_cast<float> (value * factor);
                plugin.editZone (selection, z);
                envelope.repaint();
            }
            else setSetting (native, factor) (value);
        };
    };
    setup (attack, "Attack", 0, 10000, 1, " ms", setEnvelope (&Settings::attack, &SfzRegion::attack, .001), true);
    setup (decay, "Decay", 0, 10000, 1, " ms", setEnvelope (&Settings::decay, &SfzRegion::decay, .001), true);
    setup (sustain, "Sustain", 0, 100, 1, " %", setEnvelope (&Settings::sustain, &SfzRegion::sustain, .01), true);
    setup (release, "Release", 0, 15000, 1, " ms", setEnvelope (&Settings::release, &SfzRegion::release, .001), true);
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
            batchEdit (change, value);
        };
    };
    setup (mapZoom, "Map zoom", 1, 8, 1, " x", [this] (double n) { mapScale = n; display.repaint(); });
    setup (mapPosition, "Map position", 0, 100, 1, " %", [this] (double n) { mapOffset = n / 100; display.repaint(); });
    setup (keySwitch, "Keyswitch (C4 = 60)", 0, 127, 1, {}, [this] (double n) { editGroup ([n] (Group& g) { g.key = roundToInt (n); }); });
    keySwitch.slider.textFromValueFunction = [] (double n) { return noteName (roundToInt (n)); };
    setup (ccNumber, "Controller number", 0, 119, 1, {}, [this] (double n) { editGroup ([n] (Group& g) { g.controller = roundToInt (n); }); });
    setup (ccLow, "Lowest CC value", 0, 127, 1, {}, [this] (double n) { editGroup ([n] (Group& g) { g.ccLow = jmin (g.ccHigh, roundToInt (n)); }); });
    setup (ccHigh, "Highest CC value", 0, 127, 1, {}, [this] (double n) { editGroup ([n] (Group& g) { g.ccHigh = jmax (g.ccLow, roundToInt (n)); }); });
    setup (fadeIn, "Velocity fade in", 0, 127, 1, " steps", editZone ([] (Zone& z, double n) { z.fadeIn = roundToInt (n); }));
    setup (fadeOut, "Velocity fade out", 0, 127, 1, " steps", editZone ([] (Zone& z, double n) { z.fadeOut = roundToInt (n); }));
    fadeIn.slider.setTooltip ("Fade in over this many velocity steps from the lowest velocity. Overlap adjacent zones to crossfade.");
    fadeOut.slider.setTooltip ("Fade out towards the highest velocity. Example: soft 1-95 / fade out 32, loud 64-127 / fade in 32.");
    setup (loopFade, "Loop crossfade (frames)", -1, 1000000, 1, {}, editZone ([] (Zone& z, double n) { z.loopCrossfade = roundToInt (n); }));
    loopFade.slider.textFromValueFunction = [] (double n) { return n < 0 ? String ("Legacy 64") : String (roundToInt (n)); };
    loopFade.slider.setTooltip ("Blend the loop tail with its beginning. Zero disables smoothing; Legacy preserves old presets. Limited to a quarter of the loop length.");
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
    setup (zoom, "Waveform zoom", 1, 16, 1, " x", [this] (double n) { waveZoom = n; display.repaint(); });
    setup (position, "Waveform position", 0, 100, 1, " %", [this] (double n) { waveOffset = n / 100; display.repaint(); });
    setup (sampleStart, "Sample start (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n)
    { z.start = jlimit (0, (z.end ? z.end : z.sample->frameCount()) - 2, roundToInt (n)); }));
    setup (sampleEnd, "Sample end (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n)
    { z.end = jmax (z.start + 2, roundToInt (n)); }));
    setup (loopStart, "Loop start (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n) { z.loopStart = jlimit (0, jmax (0, z.loopEnd - 2), roundToInt (n)); }));
    setup (loopEnd, "Loop end (frame)", 0, 1, 1, {}, editZone ([] (Zone& z, double n) { z.loopEnd = jmax (z.loopStart + 2, roundToInt (n)); }));
    for (auto* control : { &root, &low, &high, &keySwitch })
    {
        control->slider.textFromValueFunction = [] (double value) { return noteName (roundToInt (value)); };
        control->slider.valueFromTextFunction = [] (const String& text)
        {
            for (int i = 0; i < 128; ++i)
                if (noteName (i).equalsIgnoreCase (text.trim())) return static_cast<double> (i);
            return jlimit (0.0, 127.0, text.getDoubleValue());
        };
        control->slider.updateText();
    }
    mode.onChange = [this, set = editZone ([] (Zone& z, double v) { z.oneShot = v != 0; })]
    {
        if (! updating) set (mode.getSelectedId() == 2);
    };
    loopEnabled.onClick = [this, set = editZone ([] (Zone& z, double v)
    {
        z.loop = v != 0 && z.sample && validLoop (z, z.sample->frameCount());
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
    importTask.reset();
    if (importReview) importReview->exitModalState (0);
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
    c.slider.onDragEnd = [this] { refresh(); };
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
    selectedZones = { index };
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
        filenames.add (entry.file.getFileName() + " [group " + String (entry.folder + 1) + "]" + (entry.mapping.sample ? "" : " (Unavailable)"));
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
                            + (z.sfz.enabled ? "  |  SFZ region" : "") + (z.sample && z.sample->stream ? "  |  Disk streaming" : "")
                            : String(), dontSendNotification);
    filename.setTooltip (entry ? entry->file.getFullPathName() : String());
    if (plugin.stateError().isNotEmpty()) setStatus (plugin.stateError());
    else if (entry && entry->problem.isNotEmpty()) setStatus (entry->problem + " | " + entry->file.getFileName());
    else
    {
        int overlaps = 0, keyGaps = 0, velocityGaps = 0;
        if (entry)
        {
            std::array<bool, 128> notes {}, velocities {};
            int first = 127, last = 0;
            for (const auto& other : model.zones) if (other.folder == (page == Page::groups ? groupList.getSelectedId() - 1 : entry->folder))
            {
                const auto& m = other.mapping;
                first = jmin (first, m.low); last = jmax (last, m.high);
                for (int n = m.low; n <= m.high; ++n) notes[static_cast<std::size_t> (n)] = true;
                if (z.root >= m.low && z.root <= m.high) for (int v = m.soft; v <= m.hard; ++v) velocities[static_cast<std::size_t> (v)] = true;
                if (&other != entry && m.low <= z.high && m.high >= z.low && m.soft <= z.hard && m.hard >= z.soft) ++overlaps;
            }
            for (int n = first; n <= last; ++n) if (! notes[static_cast<std::size_t> (n)]) ++keyGaps;
            for (int v = 1; v <= 127; ++v) if (! velocities[static_cast<std::size_t> (v)]) ++velocityGaps;
            setStatus (String (selectedZones.empty() ? 1 : static_cast<int> (selectedZones.size())) + " selected | Group: " + String (keyGaps) + " key gaps, "
                + String (velocityGaps) + " velocity gaps at " + noteName (z.root) + ", " + String (overlaps) + " overlaps. Overlaps may be intentional layers or crossfades.");
        }
        else setStatus ({});
    }

    const auto value = [] (Control& c, double v)
    {
        if (! c.slider.isMouseButtonDown() && ! c.slider.hasKeyboardFocus (true))
            c.slider.setValue (v, dontSendNotification);
    };
    const bool sfzEnvelope = z.sfz.enabled;
    if (attack.slider.getMaximum() != (sfzEnvelope ? 100000 : 10000))
    {
        attack.slider.setRange (0, sfzEnvelope ? 100000 : 10000, 1);
        decay.slider.setRange (0, sfzEnvelope ? 100000 : 10000, 1);
        release.slider.setRange (0, sfzEnvelope ? 100000 : 15000, 1);
        attack.slider.setSkewFactorFromMidPoint (80);
        decay.slider.setSkewFactorFromMidPoint (200);
        release.slider.setSkewFactorFromMidPoint (250);
    }
    for (auto* c : { &attack, &decay, &sustain, &release })
        c->slider.setTooltip (sfzEnvelope ? "Imported envelope for the selected SFZ region." : "Instrument amplitude envelope.");
    value (attack, (sfzEnvelope ? z.sfz.attack : s.attack) * 1000);
    value (decay, (sfzEnvelope ? z.sfz.decay : s.decay) * 1000);
    value (sustain, (sfzEnvelope ? z.sfz.sustain : s.sustain) * 100);
    value (release, (sfzEnvelope ? z.sfz.release : s.release) * 1000);
    value (gain, s.gainDb);
    value (tune, s.semitones);
    value (voices, s.polyphony);
    value (bend, s.bendRange);
    value (fadeIn, z.fadeIn); value (fadeOut, z.fadeOut); value (loopFade, z.loopCrossfade);
    value (root, z.root);
    value (low, z.low);
    value (high, z.high);
    value (soft, z.soft);
    value (hard, z.hard);
    value (zoneGain, z.gainDb);
    value (cents, z.cents);
    value (group, z.group);
    const auto length = playable ? z.sample->frameCount() : jmax (2, z.loopEnd);
    if (loopStart.slider.getMaximum() != length)
    {
        loopStart.slider.setRange (0, length, 1);
        loopEnd.slider.setRange (0, length, 1);
        sampleStart.slider.setRange (0, length, 1);
        sampleEnd.slider.setRange (0, length, 1);
    }
    value (sampleStart, z.start);
    value (sampleEnd, z.end ? z.end : length);
    sampleStart.slider.setEnabled (playable);
    sampleEnd.slider.setEnabled (playable);
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
    groups.setToggleState (page == Page::groups, dontSendNotification);
    const bool grouping = page == Page::groups;
    for (Component* c : std::initializer_list<Component*> { &groupList, &groupName, &addGroup, &copyGroup, &deleteGroup, &moveGroup, &groupRows }) c->setVisible (grouping);
    for (Component* c : std::initializer_list<Component*> { &duplicate, &undo, &redo }) c->setVisible (page == Page::mapping || grouping);
    groupRows.updateContent();
    const int gid = groupList.getSelectedId();
    groupList.clear (dontSendNotification);
    for (const auto& g : model.groups) groupList.addItem (g.name, g.id + 1);
    groupList.setSelectedId (gid > 0 && groupList.indexOfItemId (gid) >= 0 ? gid : model.groups.front().id + 1, dontSendNotification);
    for (const auto& g : model.groups) if (g.id == groupList.getSelectedId() - 1) groupName.setText (g.name, dontSendNotification);
    for (const auto& g : model.groups) if (g.id == groupList.getSelectedId() - 1)
    {
        const auto& p = g.playback;
        triggerMode.setSelectedId (static_cast<int> (p.trigger) + 1, dontSendNotification);
        triggerChannel.setSelectedId (p.channel + 1, dontSendNotification);
        groupMute.setToggleState (p.mute, dontSendNotification); groupSolo.setToggleState (p.solo, dontSendNotification);
        releaseTrigger.setToggleState (p.releaseTrigger, dontSendNotification);
        value (keySwitch, p.key); value (ccNumber, p.controller); value (ccLow, p.ccLow); value (ccHigh, p.ccHigh);
    }
    for (Component* c : std::initializer_list<Component*> { &groupMute, &groupSolo, &releaseTrigger, &triggerMode, &triggerChannel, &triggerLabel, &channelLabel }) c->setVisible (grouping);
    editMapping.setVisible (page == Page::basic);
    playView.setVisible (page == Page::loops); playView.setEnabled (playable);
    loops.setToggleState (page == Page::loops, dontSendNotification);
    advanced.setToggleState (page == Page::advanced, dontSendNotification);
    const bool main = page == Page::basic, map = page == Page::mapping || page == Page::groups, loop = page == Page::loops;
    load.setButtonText (map ? "Add Sample" : "Load Sample");
    load.setTitle (load.getButtonText());
    load.setVisible (main || map);
    locate.setVisible ((main || map || loop) && entry && (! playable || map));
    locate.setButtonText (playable ? "Replace Sample" : "Locate Sample");
    locate.setTitle (locate.getButtonText());
    remove.setVisible (map);
    remove.setEnabled (entry != nullptr);
    duplicate.setEnabled (entry != nullptr);
    moveGroup.setEnabled (entry != nullptr);
    audition.setVisible (true);
    audition.setEnabled (playable);
    display.setVisible (main || map || loop);
    samples.setVisible (! main);
    filename.setVisible (main || loop);
    mode.setVisible (loop);
    loopEnabled.setVisible (loop);
    envelope.setVisible (main);
    meter.setVisible (page == Page::advanced);
    for (auto* c : { &attack, &decay, &sustain, &release, &gain, &tune, &root, &low, &high, &soft, &hard,
                     &zoneGain, &cents, &mapZoom, &mapPosition, &keySwitch, &ccNumber, &ccLow, &ccHigh, &fadeIn, &fadeOut, &loopFade, &zoom, &position, &sampleStart, &sampleEnd, &loopStart, &loopEnd, &voices, &bend, &group })
    {
        const bool show = main ? c == &attack || c == &decay || c == &sustain || c == &release || c == &gain || c == &root
            : map ? (grouping ? (c == &keySwitch && triggerMode.getSelectedId() == 2) || ((c == &ccNumber || c == &ccLow || c == &ccHigh) && triggerMode.getSelectedId() == 3) : c == &root || c == &low || c == &high || c == &soft || c == &hard || c == &zoneGain || c == &fadeIn || c == &fadeOut) || c == &mapZoom || c == &mapPosition
            : loop ? c == &loopFade || c == &zoom || c == &position || c == &sampleStart || c == &sampleEnd || c == &loopStart || c == &loopEnd : c == &voices || c == &bend || c == &tune || c == &group || c == &cents;
        c->label.setVisible (show);
        c->slider.setVisible (show);
    }
    for (auto* c : { &root, &low, &high, &soft, &hard, &zoneGain, &cents, &group })
        c->slider.setEnabled (entry != nullptr);
    for (auto* c : { &fadeIn, &fadeOut })
    {
        c->slider.setEnabled (entry != nullptr && ! z.sfz.enabled);
        c->slider.setTooltip (z.sfz.enabled ? "SFZ crossfade endpoints preserved from the source definition." : "Native velocity crossfade steps.");
    }
    loopFade.slider.setEnabled (entry != nullptr);
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
                     &zoneGain, &cents, &mapZoom, &mapPosition, &keySwitch, &ccNumber, &ccLow, &ccHigh, &fadeIn, &fadeOut, &loopFade, &zoom, &position, &sampleStart, &sampleEnd, &loopStart, &loopEnd, &voices, &bend, &group })
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
    setPage (static_cast<Page> (jlimit (0, 4, stored.getIntValue())));
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
    auto buttons = header.removeFromRight (jmin (header.getWidth() * 3 / 5, m.span (300)));
    auto buttonRow = buttons.withSizeKeepingCentre (buttons.getWidth(), button);
    const auto half = (buttonRow.getWidth() - 2 * gap) / 3;
    put (buttonRow, loadPreset, half);
    put (buttonRow, savePreset, half);
    put (buttonRow, audition, half);
    name.setBounds (header.removeFromTop (m.line (20, 1)));
    auto subtitle = header.removeFromTop (line);
    if (importTask) cancelImport.setBounds (subtitle.removeFromRight (jmin (subtitle.getWidth(), m.span (110))));
    preset.setBounds (subtitle);

    area.removeFromTop (gap);
    auto tabs = area.removeFromTop (button);
    const int tabWidth = jmax (0, jmin (m.span (107), (tabs.getWidth() - 4 * gap) / 5));
    for (auto* tab : { &basic, &mapping, &groups, &loops, &advanced }) put (tabs, *tab, tabWidth);
    area.removeFromTop (gap);
    keyboard.setBounds (area.removeFromBottom (m.px (61)));
    area.removeFromBottom (gap);
    viewport.setBounds (area);

    const int width = jmax (1, viewport.getMaximumVisibleWidth() - viewport.getScrollBarThickness());
    if (page == Page::mapping || page == Page::groups)
    {
        const bool wide = width >= m.span (720);
        const int propertyHeight = line + button + m.px (4);
        const int toolColumns = wide ? 6 : 3;
        const int toolRows = 6 / toolColumns;
        const int toolsHeight = (toolRows + 2) * (button + gap);
        const int propertyRows = page == Page::groups ? 8 : 4;
        const int graphHeight = wide ? jmax (m.px (300), jmax (8 * propertyHeight, viewport.getHeight() - toolsHeight - 2 * line)) : m.px (240);
        body.setSize (width, jmax (viewport.getHeight(), toolsHeight + graphHeight + line * 2 + (wide ? 0 : propertyRows * propertyHeight)));
        auto content = body.getLocalBounds();
        samples.setBounds (content.removeFromTop (button)); content.removeFromTop (gap);
        std::array<Component*, 6> tools { &load, &locate, &duplicate, &undo, &redo, &remove };
        for (int rowIndex = 0; rowIndex < toolRows; ++rowIndex)
        {
            auto row = content.removeFromTop (button);
            for (int col = 0; col < toolColumns; ++col) put (row, *tools[static_cast<std::size_t> (rowIndex * toolColumns + col)], (width - (toolColumns - 1) * gap) / toolColumns);
            content.removeFromTop (gap);
        }
        auto zoomRow = content.removeFromTop (button); content.removeFromTop (gap);
        for (auto* c : { &mapZoom, &mapPosition })
        { auto cell = zoomRow.removeFromLeft (width / 2); c->label.setBounds (cell.removeFromLeft (m.span (95))); c->slider.setBounds (cell.reduced (gap, 0)); }
        auto graph = content.removeFromTop (graphHeight);
        if (page == Page::groups)
        {
            auto left = graph.removeFromLeft (jmin (m.span (160), width / 3)); graph.removeFromLeft (gap);
            groupList.setVisible (false);
            groupName.setBounds (left.removeFromTop (button)); left.removeFromTop (gap);
            for (auto* action : { &moveGroup, &deleteGroup, &copyGroup, &addGroup })
            { action->setBounds (left.removeFromBottom (button)); left.removeFromBottom (gap); }
            groupRows.setBounds (left);
        }
        auto properties = wide ? graph.removeFromRight (m.span (220)) : content.removeFromTop (propertyRows * propertyHeight);
        if (wide) graph.removeFromRight (gap);
        display.setBounds (graph);
        if (page == Page::groups)
        {
            auto row = properties.removeFromTop (button); groupMute.setBounds (row.removeFromLeft (row.getWidth() / 2)); groupSolo.setBounds (row);
            triggerLabel.setBounds (properties.removeFromTop (line)); triggerMode.setBounds (properties.removeFromTop (button)); properties.removeFromTop (gap);
            channelLabel.setBounds (properties.removeFromTop (line)); triggerChannel.setBounds (properties.removeFromTop (button)); properties.removeFromTop (gap);
            for (auto* c : { &keySwitch, &ccNumber, &ccLow, &ccHigh }) if (c->slider.isVisible())
            { auto cell = properties.removeFromTop (propertyHeight); c->label.setBounds (cell.removeFromTop (line)); c->slider.setBounds (cell.removeFromTop (button)); }
            releaseTrigger.setBounds (properties.removeFromTop (button));
            status.setVisible (true); status.setBounds (content.removeFromTop (line * 2));
            return;
        }
        std::array<Control*, 8> controls { &root, &low, &high, &soft, &hard, &zoneGain, &fadeIn, &fadeOut };
        for (int i = 0; i < 8; ++i)
        {
            Rectangle<int> cell;
            if (wide) cell = properties.removeFromTop (propertyHeight);
            else
            {
                const int column = i % 2, row = i / 2;
                cell = Rectangle<int> (properties.getX() + column * (properties.getWidth() + gap) / 2,
                    properties.getY() + row * propertyHeight, (properties.getWidth() - gap) / 2, propertyHeight);
            }
            controls[static_cast<std::size_t> (i)]->label.setBounds (cell.removeFromTop (line));
            controls[static_cast<std::size_t> (i)]->slider.setBounds (cell.removeFromTop (button));
        }
        status.setVisible (status.getText().isNotEmpty()); status.setBounds (content.removeFromTop (line * 2));
        return;
    }
    const int waveform = m.px (page == Page::basic ? 125 : 230);
    const int dialHeight = m.px (74) + line;
    const int gridColumns = page == Page::mapping ? (width < m.span (495) ? 3 : 4)
        : page == Page::loops ? (width >= m.span (720) ? 3 : 2) : (width < m.span (500) ? 2 : 3);
    const int cells = page == Page::mapping || page == Page::groups ? 8 : page == Page::loops ? 7 : page == Page::basic ? 2 : 5;
    const int rows = (cells + gridColumns - 1) / gridColumns;
    const int actionMinimum = m.span (132);
    const int actionColumns = jlimit (1, 4, (width + gap) / (actionMinimum + gap));
    std::array<Component*, 4> actions {};
    int actionCount = 0;
    for (auto* action : { &load, &locate, &remove, &editMapping })
        if (action->isVisible()) actions[static_cast<std::size_t> (actionCount++)] = action;
    const int actionRows = (actionCount + actionColumns - 1) / actionColumns;
    const bool sampleList = page != Page::basic;
    const bool hasWaveform = page != Page::advanced;
    const bool sampleName = page == Page::basic || page == Page::loops;
    const bool hasStatus = status.getText().isNotEmpty();
    const int builderRows = (page == Page::mapping ? 1 : page == Page::groups ? 3 : 0);
    const int height = builderRows * (button + gap) + (actionRows + (sampleList ? 1 : 0)) * (button + gap)
        + (sampleName ? line : 0) + (hasWaveform ? waveform + gap : 0) + (hasStatus ? line + gap : 0)
        + (page == Page::basic ? dialHeight + gap : 0)
        + (page == Page::loops ? button + gap : 0)
        + rows * (line + button + gap) + (page == Page::advanced ? line * 3 : 0);
    body.setSize (width, jmax (viewport.getHeight(), height));
    auto content = body.getLocalBounds();
    if (page == Page::mapping || page == Page::groups)
    {
        auto row = content.removeFromTop (button);
        put (row, duplicate, m.span (140)); put (row, undo, m.span (70)); put (row, redo, m.span (70));
        content.removeFromTop (gap);
    }
    if (page == Page::groups)
    {
        auto row = content.removeFromTop (button);
        put (row, groupList, width / 2 - gap); put (row, groupName, width / 2 - gap);
        content.removeFromTop (gap); row = content.removeFromTop (button);
        put (row, addGroup, width / 4 - gap); put (row, copyGroup, width / 4 - gap); put (row, deleteGroup, width / 4 - gap); put (row, moveGroup, width / 4 - gap);
        content.removeFromTop (gap);
    }
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
        {
            auto graph = content.removeFromTop (waveform);
            if (page == Page::groups) { groupRows.setBounds (graph.removeFromLeft (jmin (m.span (180), width / 3))); graph.removeFromLeft (gap); }
            display.setBounds (graph);
        }
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
    else if (page == Page::mapping || page == Page::groups)
        placeGrid ({ &root, &low, &high, &soft, &hard, &zoneGain, &cents, &group }, gridColumns);
    else if (page == Page::loops)
    {
        auto options = content.removeFromTop (button);
        put (options, mode, m.span (130));
        put (options, loopEnabled, m.span (150));
        put (options, playView, m.span (100));
        content.removeFromTop (gap);
        placeGrid ({ &zoom, &position, &sampleStart, &sampleEnd, &loopStart, &loopEnd }, 2);
    }
    else
    {
        placeGrid ({ &voices, &bend, &tune, &group, &cents }, gridColumns);
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
    chooser = std::make_unique<FileChooser> (presetFile ? (save ? "Save Studio Sampler preset" : "Load Studio preset, SFZ, EXS or sound bank")
                                                       : (replace ? "Locate sample" : "Load sample"),
        save ? File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (File::createLegalFileName (plugin.instrument().name) + ".studiosampler") : File(),
        presetFile ? (save ? "*.studiosampler" : "*.studiosampler;*.sfz;*.sf2;*.sf3;*.dls;*.exs;*.gig;*.p1;*.p3;*.s1;*.s3;*.dspreset;*.dslibrary;*.nki") : (replace ? "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.caf;*.flac" : "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.caf;*.flac;*.sfz;*.sf2;*.sf3;*.dls;*.exs;*.gig;*.p1;*.p3;*.s1;*.s3;*.dspreset;*.dslibrary;*.nki"), true, false, this);
    const auto modeFlags = save ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
                                : FileBrowserComponent::openMode;
    chooser->launchAsync (modeFlags | FileBrowserComponent::canSelectFiles | (! presetFile && ! replace ? FileBrowserComponent::canSelectMultipleItems | FileBrowserComponent::canSelectDirectories : 0),
        [safe = Component::SafePointer<Editor> (this), presetFile, save, replace, index = selection,
         revision = plugin.changeCount()] (const FileChooser& dialog)
        {
            if (safe == nullptr) return;
            const auto files = dialog.getResults();
            const auto file = files.isEmpty() ? File() : files.getFirst();
            MessageManager::callAsync ([safe, file, files, presetFile, save, replace, index, revision]
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
                    if (replace) safe->startImport ({ file }, false, index);
                    else { StringArray paths; for (const auto& f : files) paths.add (f.getFullPathName()); safe->importFiles (paths); }
                    return;
                }
                const auto destination = file.withFileExtension (".studiosampler");
                if (save && destination != file && destination.existsAsFile())
                {
                    safe->setStatus ("An existing preset has that name. Select the .studiosampler file to replace it.");
                    return;
                }
                if (! save) { safe->startImport ({ file }, false, -1, true); return; }
                const auto result = safe->plugin.savePreset (destination);
                if (result.wasOk() && ! save) safe->selection = safe->initialZone();
                safe->refresh();
                if (result.failed()) safe->setStatus (result.getErrorMessage());
            });
        });
}

void Editor::importFiles (const StringArray& paths)
{
    if (importTask) { setStatus ("An import is already running. Cancel it before starting another."); return; }
    for (const auto& path : paths)
        if (File (path).hasFileExtension ("sfz;exs;dspreset;dslibrary;nki") || soundBankFile (File (path)))
        {
            if (paths.size() != 1) { setStatus ("Import one instrument definition or bank at a time, separately from recordings."); return; }
            startImport ({ File (path) }, false);
            return;
        }
    const auto safe = Component::SafePointer<Editor> (this);
    AlertWindow::showAsync (MessageBoxOptions().withTitle ("Import recordings").withMessage ("Choose a starting map. Review and edit the proposal before committing. Filename notes take priority over file root-note metadata. C4 = MIDI 60; unknown roots are provisional.")
        .withButton ("Map Automatically", 1).withButton ("Map Manually", 2).withButton ("Cancel", 0), [safe, paths] (int choice)
    {
        if (! safe || choice == 0) return;
        Array<File> files; for (const auto& path : paths) files.add (File (path));
        safe->startImport (files, choice == 1);
    });
}
void Editor::startImport (Array<File> files, bool automatic, int replace, bool presetFile, int bankProgram, int64 expectedSize, int64 expectedTime, File exsRoot, bool exsRootChosen)
{
    if (importTask || importReview) { setStatus ("Finish or cancel the current import first."); return; }
    const bool isExs = replace < 0 && files.size() == 1 && files.getFirst().hasFileExtension ("exs");
    if (isExs && ! exsRootChosen) { chooseExsRoot (files.getFirst()); return; }
    auto initial = plugin.instrument();
    importTask = std::make_unique<ImportTask>();
    auto* task = importTask.get(); task->replace = replace; task->preset = presetFile; task->revision = plugin.changeCount();
    task->sfz = replace < 0 && files.size() == 1 && files.getFirst().hasFileExtension ("sfz");
    task->soundBank = replace < 0 && files.size() == 1 && soundBankFile (files.getFirst());
    task->bankProgram = bankProgram;
    task->exs = isExs;
    task->xmlInstrument = replace < 0 && files.size() == 1 && files.getFirst().hasFileExtension ("dspreset;dslibrary;nki");
    if (task->xmlInstrument) task->bankMedia = std::make_shared<BankImportMedia>();
    task->first = task->sfz || task->soundBank || task->exs || task->xmlInstrument ? 0 : static_cast<int> (initial.zones.size());
    if (task->soundBank)
    {
        task->bankFile = files.getFirst();
        task->bankSize = task->bankFile.getSize();
        task->bankTime = task->bankFile.getLastModificationTime().toMilliseconds();
        if (bankProgram >= 0 && (task->bankSize != expectedSize || task->bankTime != expectedTime))
        {
            importTask.reset(); setStatus ("The bank changed. Reopen its instrument catalog."); return;
        }
        if (bankProgram >= 0) task->bankMedia = std::make_shared<BankImportMedia>();
    }
    task->worker = std::thread ([task, initial = std::move (initial), files = std::move (files), automatic, exsRoot] () mutable
    {
        const auto result = task->xmlInstrument ? Plugin::prepareXmlInstrument (files.getFirst(), initial, task->proposal, task->summary, *task->bankMedia, &task->cancelled, &task->progress)
            : task->exs ? Plugin::prepareExs (files.getFirst(), exsRoot, initial, task->proposal, task->summary, &task->cancelled, &task->progress)
            : task->soundBank
            ? (task->bankProgram < 0
                ? Plugin::catalogSoundBank (task->bankFile, task->catalog, &task->cancelled)
                : Plugin::prepareSoundBank (task->bankFile, task->bankProgram, task->proposal, task->summary,
                                            *task->bankMedia, &task->cancelled, &task->progress))
            : task->sfz ? Plugin::prepareSfz (files.getFirst(), initial, task->proposal, task->summary, &task->cancelled, &task->progress)
            : task->preset ? Plugin::preparePreset (files.getFirst(), initial, task->proposal, &task->cancelled)
            : task->replace >= 0 ? Plugin::prepareSample (std::move (initial), files.getFirst(), task->replace, task->proposal, &task->cancelled)
            : Plugin::prepareImport (std::move (initial), files, automatic, task->proposal, task->summary, &task->cancelled, &task->progress);
        task->error = result.getErrorMessage(); task->done.store (true, std::memory_order_release);
    });
    cancelImport.setVisible (true); resized();
}
void Editor::finishImport()
{
    if (! importTask) return;
    preset.setText (importTask->soundBank && importTask->bankProgram < 0 ? "Reading bank catalog..." : "Preparing recordings: " + String (importTask->progress.load()) + "%", dontSendNotification);
    if (! importTask->done.load (std::memory_order_acquire)) return;
    auto task = std::move (importTask);
    cancelImport.setVisible (false);
    if (task->cancelled.load() || task->error.isNotEmpty())
    {
        refresh(); setStatus (task->cancelled.load() ? "Import cancelled; instrument unchanged." : task->error);
        if ((task->sfz || task->soundBank || task->exs || task->xmlInstrument) && ! task->cancelled.load() && task->error.isNotEmpty())
            AlertWindow::showAsync (MessageBoxOptions().withTitle ("Instrument import could not complete")
                .withMessage (task->error.substring (0, 5000)).withButton ("OK"), nullptr);
        return;
    }
    if (task->revision != plugin.changeCount()) { refresh(); setStatus ("Instrument changed during import. Import again to make a fresh proposal."); return; }
    if (task->soundBank && (task->bankFile.getSize() != task->bankSize
        || task->bankFile.getLastModificationTime().toMilliseconds() != task->bankTime))
    { refresh(); setStatus ("Bank changed during import. Reopen the catalog."); return; }
    if (task->soundBank && task->bankProgram < 0)
    { refresh(); showBankCatalog (*task); return; }
    if (! task->soundBank && ! task->sfz && ! task->exs && ! task->xmlInstrument && (task->preset || task->replace >= 0))
    {
        const int replaced = task->replace;
        plugin.commitImport (std::move (task->proposal), ! task->preset);
        selection = replaced >= 0 ? replaced : initialZone(); refresh(); return;
    }
    auto proposal = std::make_shared<Instrument> (std::move (task->proposal));
    const auto summary = task->summary; const auto revision = task->revision;
    const auto safe = Component::SafePointer<Editor> (this);
    DialogWindow::LaunchOptions dialog;
    dialog.dialogTitle = task->xmlInstrument ? "Review sample instrument conversion - confirm replacement" : task->exs ? "Review EXS24 conversion - confirm replacement" : task->soundBank ? "Review sound bank conversion - confirm replacement" : task->sfz ? "Review SFZ conversion - confirm replacement" : "Review and revise mapping - C4 = MIDI 60";
    dialog.dialogBackgroundColour = colours::panel;
    dialog.useNativeTitleBar = true; dialog.resizable = true; dialog.componentToCentreAround = this;
    const auto media = task->bankMedia;
    dialog.content.setOwned (new ImportProposal (proposal, summary, task->first, [safe, proposal, revision, summary, media]
    {
        if (! safe) return;
        if (safe->plugin.changeCount() != revision) { safe->setStatus ("Instrument changed. Import again to make a fresh proposal."); return; }
        if (media) media->keep = true;
        safe->plugin.commitImport (std::move (*proposal)); safe->selection = safe->initialZone(); safe->setPage (Page::mapping); safe->setStatus (summary);
    }, task->sfz || task->soundBank || task->exs || task->xmlInstrument));
    refresh(); importReview = dialog.launchAsync();
}
void Editor::chooseExsRoot (const File& definition)
{
    const auto safe = Component::SafePointer<Editor> (this);
    const auto revision = plugin.changeCount();
    AlertWindow::showAsync (MessageBoxOptions().withTitle ("Locate EXS24 recordings")
        .withMessage ("Studio can try stored paths and nearby sample folders, or you can choose a folder containing your relocated recordings. Duplicate filenames are not automatically assigned.")
        .withButton ("Find automatically", 1).withButton ("Choose sample folder", 2).withButton ("Cancel", 0),
        [safe, definition, revision] (int choice)
    {
        if (! safe || choice == 0) return;
        if (safe->plugin.changeCount() != revision || safe->importTask || safe->importReview)
        { safe->setStatus ("Instrument changed. Reopen the EXS file."); return; }
        if (choice == 1)
        { safe->startImport ({definition}, false, -1, false, -1, -1, 0, {}, true); return; }
        safe->chooser = std::make_unique<FileChooser> ("Choose EXS sample folder", definition.getParentDirectory(), "*", true, false, safe.getComponent());
        safe->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
            [safe, definition, revision] (const FileChooser& dialog)
        {
            const auto root = dialog.getResult();
            MessageManager::callAsync ([safe,definition,root,revision]
            {
                if (! safe) return;
                safe->chooser.reset();
                if (!root.isDirectory()) return;
                if (safe->plugin.changeCount() != revision)
                { safe->setStatus ("Instrument changed. Reopen the EXS file."); return; }
                safe->startImport ({definition},false,-1,false,-1,-1,0,root,true);
            });
        });
    });
}
void Editor::showBankCatalog (const ImportTask& task)
{
    const auto safe = Component::SafePointer<Editor> (this);
    const auto file = task.bankFile; const auto size = task.bankSize; const auto time = task.bankTime;
    const auto revision = task.revision;
    DialogWindow::LaunchOptions dialog;
    dialog.dialogTitle = "Choose instrument - " + file.getFileName();
    dialog.dialogBackgroundColour = colours::panel;
    dialog.useNativeTitleBar = true; dialog.resizable = false; dialog.componentToCentreAround = this;
    dialog.content.setOwned (new BankChooser (task.catalog, [safe,file,size,time,revision] (int index)
    {
        MessageManager::callAsync ([safe,file,size,time,revision,index]
        {
            if (! safe) return;
            safe->importReview = nullptr;
            if (safe->plugin.changeCount() != revision)
            { safe->setStatus ("Instrument changed. Reopen the bank catalog."); return; }
            safe->startImport ({file},false,-1,false,index,size,time);
        });
    }));
    importReview = dialog.launchAsync();
}
void Editor::editGroup (const std::function<void(Group&)>& apply)
{
    auto list = plugin.instrument().groups;
    for (auto& g : list) if (g.id == groupList.getSelectedId() - 1) apply (g.playback);
    plugin.editGroups (std::move (list)); refresh();
}
void Editor::batchEdit (const std::function<void(Zone&, double)>& apply, double value)
{
    std::vector<std::pair<int, Zone>> changes;
    auto chosen = selectedZones;
    if (chosen.empty() || (page != Page::mapping && page != Page::groups)) chosen = { selection };
    for (int i : chosen) if (isPositiveAndBelow (i, static_cast<int> (plugin.instrument().zones.size())))
    { auto z = plugin.instrument().zones[static_cast<std::size_t> (i)].mapping; apply (z, value); changes.emplace_back (i, std::move (z)); }
    const auto result = plugin.editZones (changes);
    if (result.failed()) { refresh(); setStatus (result.getErrorMessage()); }
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
        audition.setButtonText ("Stop");
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
    finishImport();
    if (seen != plugin.changeCount()) refresh();
    const auto stats = plugin.resourceSnapshot();
    if (page == Page::advanced)
        meter.setText (String (stats.activeVoices.value_or (0)) + " active voices  |  "
            + String (static_cast<double> (stats.memoryBytes.value_or (0)) / (1024 * 1024), 2) + " MiB samples + cache"
            + "\nCC1 and channel pressure control vibrato. MIDI channels respond independently.", dontSendNotification);
    if (auditionNote >= 0 && clearAudition && stats.activeVoices.value_or (0) == 0)
    {
        auditionNote = -1;
        audition.setButtonText ("Audition");
    }
    clearAudition = auditionNote >= 0;
    keyboard.repaint();
}

int Editor::getNumRows() { return static_cast<int> (plugin.instrument().groups.size()); }
void Editor::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    if (! isPositiveAndBelow (row, getNumRows())) return;
    g.fillAll (selected ? colours::selected : colours::canvas);
    g.setColour (colours::text); g.setFont (uiFont (12));
    g.drawFittedText (plugin.instrument().groups[static_cast<std::size_t> (row)].name, 8, 0, width - 16, height, Justification::centredLeft, 1);
}
void Editor::selectedRowsChanged (int row)
{
    if (isPositiveAndBelow (row, getNumRows())) groupList.setSelectedId (plugin.instrument().groups[static_cast<std::size_t> (row)].id + 1);
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
Editor::Display::~Display() { clearThumbnail(); }
void Editor::Display::clearThumbnail()
{
    if (streamThumbnail)
    {
        streamThumbnail->thumbnail.removeChangeListener (this);
        streamThumbnail.reset();
    }
    thumbnailFile = File();
    thumbnailTime = 0;
    thumbnailSize = -1;
}
Rectangle<int> Editor::Display::plot() const
{
    auto area = getLocalBounds().reduced (appearance.px (12), appearance.px (8));
    if ((owner.page == Page::mapping || owner.page == Page::groups)) area.removeFromLeft (appearance.span (24));
    return area;
}
Rectangle<float> Editor::Display::zoneBounds (const Zone& z) const
{
    const auto area = plot().withTrimmedBottom (appearance.line (10, 4)).toFloat();
    return { area.getX() + (z.low - owner.mapFirst()) * area.getWidth() / owner.mapSpan(), area.getY() + (127 - z.hard) * area.getHeight() / 127,
             (z.high - z.low + 1) * area.getWidth() / owner.mapSpan(), (z.hard - z.soft + 1) * area.getHeight() / 127 };
}

void Editor::Display::paint (Graphics& g)
{
    g.setColour (draggingFiles ? colours::selected : colours::canvas);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7);
    auto area = plot();
    const auto* entry = owner.zone();
    if ((owner.page == Page::mapping || owner.page == Page::groups) && entry)
    {
        if (streamThumbnail) clearThumbnail();
        const auto keys = area.removeFromBottom (appearance.line (10, 4));
        g.setFont (uiFont (10));
        g.setColour (colours::secondary);
        const auto legend = Rectangle<int> (appearance.px (4), area.getY(), area.getX() - appearance.px (8), area.getHeight());
        g.drawText ("127", legend, Justification::topRight);
        g.drawText ("1", legend, Justification::bottomRight);
        for (int i = owner.mapFirst(); i < owner.mapFirst() + owner.mapSpan(); ++i)
        {
            const float left = static_cast<float> (keys.getX()) + static_cast<float> ((i - owner.mapFirst()) * keys.getWidth()) / owner.mapSpan();
            const auto w = static_cast<float> (keys.getWidth()) / owner.mapSpan();
            g.setColour (MidiMessage::isMidiNoteBlack (i) ? colours::rail : colours::secondary);
            g.fillRect (Rectangle<float> (left, static_cast<float> (keys.getY()), jmax (1.0f, w - 0.5f), 5));
            if (i % 12 != 0 && owner.mapSpan() > 32) continue;
            g.setColour (colours::divider);
            g.drawVerticalLine (roundToInt (left), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
            g.setFont (uiFont (10));
            g.setColour (colours::secondary);
            g.drawText (noteName (i), roundToInt (left), keys.getY() + 4, roundToInt (12 * w), keys.getHeight() - 4, Justification::centredLeft);
        }
        const auto draw = [&] (const SampleZone& sample, const Zone& z, bool selected)
        {
            auto block = zoneBounds (z).getIntersection (area.toFloat());
            if (block.isEmpty()) return;
            const bool related = owner.page != Page::groups || sample.folder == owner.groupList.getSelectedId() - 1;
            selected = selected && related;
            auto colour = Colour::fromHSV (0.54f + (z.soft / 127.0f) * 0.16f, 0.42f, selected ? 0.66f : 0.46f, related ? .85f : .20f);
            g.setColour (colour);
            g.fillRect (block);
            g.setColour (related ? (selected ? colours::focus : colours::divider) : colours::divider.withAlpha (.2f));
            g.drawRect (block, selected ? 1.5f : 1.0f);
            const auto rootX = plot().getX() + (z.root - owner.mapFirst() + 0.5f) * plot().getWidth() / owner.mapSpan();
            if (rootX >= block.getX() && rootX <= block.getRight()) { g.setColour (colours::warning.withAlpha (related ? 1.0f : .2f)); g.drawVerticalLine (roundToInt (rootX), block.getY(), block.getBottom()); }
            g.setColour (colours::text.withAlpha (related ? 1.0f : .25f));
            g.setFont (uiFont (12));
            g.drawFittedText (sample.file.getFileNameWithoutExtension(), block.toNearestInt().reduced (5, 1),
                              Justification::centred, 1, 1.0f);
        };
        for (const auto& sample : owner.plugin.instrument().zones)
            if (&sample != entry) draw (sample, sample.mapping, owner.selectedZones.count (static_cast<int> (&sample - owner.plugin.instrument().zones.data())) != 0);
        draw (*entry, marker != 0 ? dragZone : entry->mapping, true);
        return;
    }

    if (! entry || ! entry->mapping.sample)
    {
        if (streamThumbnail) clearThumbnail();
        g.setColour (colours::text);
        g.setFont (uiFont (16));
        const auto empty = (owner.page == Page::mapping || owner.page == Page::groups) ? "Drop a sample here or choose Add Sample."
            : owner.page == Page::loops ? "Load a sample in Basic to edit its loop."
            : "Drop WAV/AIFF files or a sample folder here. Or choose Load Sample.";
        g.drawFittedText (entry ? "Sample unavailable" : empty,
                          area, Justification::centred, 2, 1.0f);
        return;
    }

    const auto& z = marker != 0 ? dragZone : entry->mapping;
    const auto& sample = *z.sample;
    const auto length = sample.frameCount();
    if (area.getWidth() <= 0) return;
    const int visible = jmax (2, roundToInt (length / owner.waveZoom));
    const int origin = roundToInt ((length - visible) * owner.waveOffset);
    const auto position = [area, visible, origin] (int frame)
    {
        return area.getX() + static_cast<float> (area.getWidth()) * (frame - origin) / visible;
    };
    if (owner.page == Page::loops)
    {
        const auto start = position (jlimit (0, length, z.loopStart));
        const auto end = position (jlimit (0, length, z.loopEnd));
        g.setColour (colours::action.withAlpha (z.loop && ! z.oneShot ? 0.20f : 0.07f));
        g.fillRect (Rectangle<float> (start, static_cast<float> (area.getY()), end - start, static_cast<float> (area.getHeight())));
        const int blend = jmin (z.loopCrossfade < 0 ? 64 : z.loopCrossfade, (z.loopEnd - z.loopStart) / 4);
        if (z.loop && blend > 0)
        { g.setColour (colours::warning.withAlpha (.28f)); g.fillRect (Rectangle<float> (position (z.loopEnd - blend), static_cast<float> (area.getY()), end - position (z.loopEnd - blend), static_cast<float> (area.getHeight()))); }
    }

    const auto wave = [&] (const std::vector<float>& data, Rectangle<int> lane)
    {
        const float center = lane.getCentreY(), scale = lane.getHeight() * 0.45f;
        g.setColour (colours::divider);
        g.drawHorizontalLine (lane.getCentreY(), static_cast<float> (lane.getX()), static_cast<float> (lane.getRight()));
        g.setColour (colours::action);
        for (int x = 0; x < lane.getWidth(); ++x)
        {
            const auto first = static_cast<int> (origin + static_cast<int64> (x) * visible / lane.getWidth());
            const auto end = jmax (first + 1, static_cast<int> (origin + static_cast<int64> (x + 1) * visible / lane.getWidth()));
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
    if (sample.stream)
    {
        // A thumbnail is an overview of the source file, not the attack
        // buffer. JUCE scans it asynchronously at a bounded resolution.
        if (! streamThumbnail || thumbnailFile != entry->file
            || thumbnailTime != sample.sourceTime || thumbnailSize != sample.sourceBytes)
        {
            clearThumbnail();
            streamThumbnail = std::make_unique<StreamThumbnail> (jmax (512, length / 32768 + 1));
            thumbnailFile = entry->file;
            thumbnailTime = sample.sourceTime;
            thumbnailSize = sample.sourceBytes;
            streamThumbnail->thumbnail.addChangeListener (this);
            streamThumbnail->thumbnail.setSource (new FileInputSource (entry->file));
        }
        auto& thumbnail = streamThumbnail->thumbnail;
        g.setColour (colours::action);
        thumbnail.drawChannels (g, area, origin / sample.rate, (origin + static_cast<double> (visible)) / sample.rate, 0.9f);
        if (! thumbnail.isFullyLoaded())
        {
            g.setColour (colours::secondary);
            g.setFont (uiFont (11));
            g.drawText ("Building streamed waveform…", area.withTrimmedTop (18).withHeight (18), Justification::centred);
        }
    }
    else
    {
        if (streamThumbnail) clearThumbnail();
        if (sample.right.empty()) wave (sample.left, area);
        else
        {
            auto stereo = area;
            wave (sample.left, stereo.removeFromTop (stereo.getHeight() / 2));
            wave (sample.right, stereo);
        }
    }
    if (owner.page == Page::loops)
    {
        g.setFont (uiFont (11));
        g.setColour (colours::text);
        g.drawText ("Top: loop markers  |  Bottom: sample boundaries (gold)", area.withHeight (18), Justification::centred);
        for (int frame : { z.loopStart, z.loopEnd, z.start, z.end ? z.end : length })
        {
            const auto x = roundToInt (position (jlimit (0, length, frame)));
            g.setColour (frame == z.start || frame == (z.end ? z.end : length) ? colours::warning : colours::focus);
            if (x < area.getX() || x > area.getRight()) continue;
            g.drawVerticalLine (x, static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
            g.fillEllipse (static_cast<float> (x - 4), static_cast<float> (area.getY()), 8, 8);
        }
    }
}

void Editor::Display::mouseDown (const MouseEvent& event)
{
    dragRevision = owner.plugin.changeCount();
    if ((owner.page == Page::mapping || owner.page == Page::groups))
    {
        const auto& zones = owner.plugin.instrument().zones;
        const auto hit = [&] (const Zone& z) { return zoneBounds (z).expanded (4, 2).contains (event.position); };
        const auto* entry = owner.zone();
        if (owner.page == Page::groups && entry && entry->folder != owner.groupList.getSelectedId() - 1) entry = nullptr;
        if (event.mods.isCommandDown() || ! entry || ! hit (entry->mapping))
            for (int i = static_cast<int> (zones.size()); --i >= 0;)
                if ((owner.page != Page::groups || zones[static_cast<std::size_t> (i)].folder == owner.groupList.getSelectedId() - 1) && hit (zones[static_cast<std::size_t> (i)].mapping) && (! event.mods.isCommandDown() || ! owner.selectedZones.count (i)))
                {
                    if (event.mods.isCommandDown())
                    {
                        if (owner.selectedZones.count (i)) owner.selectedZones.erase (i); else owner.selectedZones.insert (i);
                        owner.selection = i; owner.refresh();
                    }
                    else owner.selectZone (i);
                    break;
                }
        if ((entry = owner.zone()) && (owner.page != Page::groups || entry->folder == owner.groupList.getSelectedId() - 1) && hit (entry->mapping))
        {
            dragSelection = owner.selection;
            dragZone = entry->mapping;
            const auto bounds = zoneBounds (dragZone);
            const auto x = event.position.x;
            if (std::abs (x - bounds.getX()) < 8) marker = 1;
            else if (std::abs (x - bounds.getRight()) < 8) marker = 2;
            else marker = event.mods.isShiftDown() ? 4 : 3;
        }
    }
    else if (owner.page == Page::loops)
        if (const auto* entry = owner.zone(); entry && entry->mapping.sample)
        {
            dragSelection = owner.selection;
            dragZone = entry->mapping;
            const auto area = plot();
            const int length = dragZone.sample->frameCount();
            const double visible = length / owner.waveZoom;
            const auto frame = (length - visible) * owner.waveOffset + (event.position.x - area.getX()) * visible / jmax (1, area.getWidth());
            if (! dragZone.oneShot && event.position.y < area.getCentreY()) marker = std::abs (frame - dragZone.loopStart) < std::abs (frame - dragZone.loopEnd) ? 1 : 2;
            else marker = std::abs (frame - dragZone.start) < std::abs (frame - (dragZone.end ? dragZone.end : dragZone.sample->frameCount())) ? 3 : 4;
        }
}

void Editor::Display::mouseDrag (const MouseEvent& event)
{
    if (marker == 0) return;
    const auto area = plot();
    const auto ratio = jlimit (0.0f, 1.0f, (event.position.x - area.getX()) / jmax (1, area.getWidth()));
    if ((owner.page == Page::mapping || owner.page == Page::groups))
    {
        const int note = jlimit (0, 127, roundToInt (ratio * owner.mapSpan()) + owner.mapFirst() - (marker == 2 ? 1 : 0));
        if (marker == 1) dragZone.low = jmin (note, dragZone.high);
        else if (marker == 2) dragZone.high = jmax (note, dragZone.low);
        else if (marker == 4) dragZone.root = note;
        else
        {
            const auto& original = owner.plugin.instrument().zones[static_cast<std::size_t> (dragSelection)].mapping;
            int leftLimit = -original.low, rightLimit = 127 - original.high;
            for (int i : owner.selectedZones)
                if (isPositiveAndBelow (i, static_cast<int> (owner.plugin.instrument().zones.size())))
                { const auto& z = owner.plugin.instrument().zones[static_cast<std::size_t> (i)].mapping; leftLimit = jmax (leftLimit, -z.low); rightLimit = jmin (rightLimit, 127 - z.high); }
            const int delta = jlimit (leftLimit, rightLimit, roundToInt (event.getDistanceFromDragStartX() * owner.mapSpan() / jmax (1, area.getWidth())));
            dragZone.low = original.low + delta; dragZone.high = original.high + delta;
            dragZone.root = jlimit (0, 127, original.root + delta);
        }
    }
    else
    {
        const int length = dragZone.sample->frameCount();
        const double visible = length / owner.waveZoom;
        const int frame = roundToInt ((length - visible) * owner.waveOffset + ratio * visible);
        if (marker == 1) dragZone.loopStart = jlimit (0, jmax (0, dragZone.loopEnd - 2), frame);
        else if (marker == 2) dragZone.loopEnd = jlimit (jmin (length, dragZone.loopStart + 2), length, frame);
        else if (marker == 3) dragZone.start = jlimit (0, (dragZone.end ? dragZone.end : length) - 2, frame);
        else dragZone.end = jlimit (dragZone.start + 2, length, frame);
    }
    repaint();
}

void Editor::Display::mouseUp (const MouseEvent& event)
{
    if (marker != 0 && event.mouseWasDraggedSinceMouseDown() && dragRevision == owner.plugin.changeCount())
    {
        if (marker == 3 && (owner.page == Page::mapping || owner.page == Page::groups) && owner.selectedZones.size() > 1)
        {
            auto next = owner.plugin.instrument();
            const int delta = dragZone.low - next.zones[static_cast<std::size_t> (dragSelection)].mapping.low;
            for (int i : owner.selectedZones) if (isPositiveAndBelow (i, static_cast<int> (next.zones.size())))
            { auto& z = next.zones[static_cast<std::size_t> (i)].mapping; z.low += delta; z.high += delta; z.root = jlimit (0, 127, z.root + delta); }
            owner.plugin.commitImport (std::move (next));
        }
        else owner.plugin.editZone (dragSelection, dragZone);
    }
    marker = 0;
    dragZone = {};
    owner.refresh();
}

bool Editor::Display::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& file : files)
        if (sampleFile (file) || File (file).hasFileExtension ("sfz;exs;dspreset;dslibrary;nki") || soundBankFile (File (file)) || File (file).isDirectory()) return true;
    return false;
}
void Editor::Display::fileDragEnter (const StringArray&, int, int) { draggingFiles = true; repaint(); }
void Editor::Display::fileDragExit (const StringArray&) { draggingFiles = false; repaint(); }
void Editor::Display::filesDropped (const StringArray& files, int, int)
{
    draggingFiles = false;
    owner.importFiles (files);
    repaint();
}

#if STUDIO_SPATIAL_TESTS
bool Editor::runReview (const File& first, const File& second, const File& output,
                        const std::function<bool()>& auditionCheck)
{
    const auto reviewFailure = [] (int line) { std::fprintf (stderr, "Sampler editor review failed at line %d\n", line); return false; };
    const auto capture = [this, &output, &reviewFailure] (const char* file)
    {
        if (output == File()) return true;
        if (output.createDirectory().failed()) return reviewFailure (__LINE__);
        captureNativeScreen (*this, output.getChildFile (String ("native-") + file));
        FileOutputStream stream (output.getChildFile (file));
        return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds()), stream);
    };
    if (page != Page::basic || zone() || audition.isEnabled() || ! capture ("sampler-empty.png")) return reviewFailure (__LINE__);
    startImport ({ first, second }, true);
    cancelImport.onClick();
    const auto importDeadline = Time::getMillisecondCounter() + 5000;
    while (importTask && Time::getMillisecondCounter() < importDeadline) { timerCallback(); MessageManager::getInstance()->runDispatchLoopUntil (10); }
    if (importTask || ! plugin.instrument().zones.empty()) return reviewFailure (__LINE__);
    startImport ({ first }, true);
    while (importTask && Time::getMillisecondCounter() < importDeadline + 5000) { timerCallback(); MessageManager::getInstance()->runDispatchLoopUntil (10); }
    if (importTask || ! importReview) return reviewFailure (__LINE__);
    if (output != File())
    {
        captureNativeScreen (*importReview, output.getChildFile ("native-sampler-import-review.png"));
        FileOutputStream file (output.getChildFile ("sampler-import-review.png"));
        if (! PNGImageFormat().writeImageToStream (importReview->createComponentSnapshot (importReview->getLocalBounds()), file)) return reviewFailure (__LINE__);
    }
    bool accepted = false;
    for (auto* child : importReview->getContentComponent()->getChildren())
        if (auto* button = dynamic_cast<TextButton*> (child); button && button->getButtonText() == "Import") { button->onClick(); accepted = true; break; }
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    if (! accepted) return reviewFailure (__LINE__);
    basic.onClick();
    if (! zone() || ! zone()->mapping.sample || ! audition.isEnabled() || ! capture ("sampler-basic.png")) return reviewFailure (__LINE__);
    attack.slider.setValue (23, sendNotificationSync);
    if (std::abs (plugin.instrument().settings.attack - .023f) > .0001f)
    {
        std::fprintf (stderr, "Sampler Attack control did not change the instrument\n");
        return reviewFailure (__LINE__);
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
        return reviewFailure (__LINE__);
    }
    auditionCheck(); // Pump the queued release before checking the active key meter.
    if (plugin.noteActive (60)) return reviewFailure (__LINE__);

    audition.onClick();
    if (! auditionCheck() || audition.getButtonText() != "Stop") return reviewFailure (__LINE__);
    audition.onClick();
    auditionCheck();
    if (plugin.noteActive (60) || audition.getButtonText() != "Audition") return reviewFailure (__LINE__);

    mapping.onClick();
    if (page != Page::mapping || ! samples.isVisible()) return reviewFailure (__LINE__);
    import (second, -1);
    if (samples.getNumItems() != 2 || selection != 1 || ! zone() || ! zone()->mapping.sample) return reviewFailure (__LINE__);
    root.slider.setValue (67, sendNotificationSync);
    low.slider.setValue (65, sendNotificationSync);
    samples.setSelectedItemIndex (0, sendNotificationSync);
    if (selection != 0 || ! zone()) return reviewFailure (__LINE__);
    high.slider.setValue (64, sendNotificationSync);
    if (zone()->mapping.high != 64 || ! capture ("sampler-mapping.png")) return reviewFailure (__LINE__);

    loops.onClick();
    if (page != Page::loops || ! loopEnabled.isEnabled()) return reviewFailure (__LINE__);
    loopStart.slider.setValue (12000, sendNotificationSync);
    loopEnd.slider.setValue (48000, sendNotificationSync);
    loopEnabled.setToggleState (true, sendNotificationSync);
    if (! zone()->mapping.loop || zone()->mapping.loopStart != 12000 || zone()->mapping.loopEnd != 48000
        || ! capture ("sampler-loops.png")) return reviewFailure (__LINE__);
    const auto preserved = plugin.instrument();
    auto layered = preserved;
    layered.name = "Synthetic Studio Keys";
    layered.groups = { { 0, "Piano Soft" }, { 1, "Piano Loud" } };
    for (auto& entry : layered.zones) { entry.mapping.hard = 63; entry.folder = 0; }
    for (auto entry : preserved.zones) { entry.mapping.soft = 64; entry.folder = 1; layered.zones.push_back (entry); }
    plugin.commitImport (layered); setSize (960, 700); mapping.onClick();
    if (! capture ("sampler-velocity-layers.png")) return reviewFailure (__LINE__);
    groups.onClick();
    if (! capture ("sampler-groups.png")) return reviewFailure (__LINE__);
    loops.onClick(); sampleStart.slider.setValue (100, sendNotificationSync); sampleEnd.slider.setValue (60000, sendNotificationSync);
    if (zone()->mapping.start != 100 || zone()->mapping.end != 60000 || ! capture ("sampler-waveform.png")) return reviewFailure (__LINE__);
    basic.onClick(); if (! capture ("sampler-builder-basic.png")) return reviewFailure (__LINE__);
    const auto reopened = output == File() ? first.getSiblingFile ("Builder.studiosampler") : output.getChildFile ("Builder.studiosampler");
    if (plugin.savePreset (reopened).failed() || plugin.loadPreset (reopened).failed()) return reviewFailure (__LINE__);
    mapping.onClick(); if (! capture ("sampler-reopened.png")) return reviewFailure (__LINE__);
    auto crossfaded = layered;
    for (auto& z : crossfaded.zones)
        if (z.folder == 0) { z.mapping.hard = 95; z.mapping.fadeOut = 32; }
        else { z.mapping.soft = 64; z.mapping.fadeIn = 32; }
    plugin.commitImport (crossfaded); selectZone (0); mapping.onClick();
    if (! capture ("sampler-velocity-crossfade.png")) return reviewFailure (__LINE__);
    selectedZones = { 0, 1 }; soft.slider.setValue (2, sendNotificationSync);
    if (plugin.instrument().zones[0].mapping.soft != 2 || plugin.instrument().zones[1].mapping.soft != 2) return reviewFailure (__LINE__);
    plugin.undoMapping (false); refresh();
    if (plugin.instrument().zones[0].mapping.soft != 1 || plugin.instrument().zones[1].mapping.soft != 1) return reviewFailure (__LINE__);
    selectedZones = { 0 }; setSize (1100, 800); loops.onClick(); loopFade.slider.setValue (256, sendNotificationSync);
    zoom.slider.setValue (8, sendNotificationSync); position.slider.setValue (69, sendNotificationSync);
    if (plugin.instrument().zones[0].mapping.loopCrossfade != 256 || ! capture ("sampler-loop-crossfade.png")) return reviewFailure (__LINE__);
    zoom.slider.setValue (1, sendNotificationSync); position.slider.setValue (0, sendNotificationSync);
    auto violin = preserved; violin.name = "Synthetic Violin"; violin.groups.clear(); violin.zones.clear();
    const StringArray names { "Sustain", "Staccato", "Pizzicato" };
    for (int i = 0; i < 3; ++i)
    {
        InstrumentGroup g { i, names[i] }; g.playback.id = i; g.playback.trigger = GroupTrigger::keyswitch; g.playback.key = 24 + i;
        violin.groups.push_back (g);
        auto z = preserved.zones[0]; z.folder = i; z.mapping.low = 48; z.mapping.high = 84; z.mapping.soft = 1; z.mapping.hard = 127;
        z.mapping.oneShot = i != 0; if (i) { z.mapping.loop = false; z.mapping.end = i == 1 ? 12000 : 8000; }
        violin.zones.push_back (z);
    }
    plugin.commitImport (violin); selectZone (0); setSize (1000, 760); groups.onClick(); groupRows.selectRow (1);
    const Point<float> groupPoint { display.getWidth() * .53f, display.getHeight() * .5f };
    const MouseEvent groupClick (Desktop::getInstance().getMainMouseSource(), groupPoint, ModifierKeys (ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
        &display, &display, Time::getCurrentTime(), groupPoint, Time::getCurrentTime(), 1, false);
    display.mouseDown (groupClick); display.mouseUp (groupClick);
    if (selection != 1) return reviewFailure (__LINE__);
    if (triggerMode.getSelectedId() != 2 || keySwitch.slider.getValue() != 25 || ! capture ("sampler-articulation-groups.png")) return reviewFailure (__LINE__);
    keySwitch.slider.setValue (30, sendNotificationSync);
    if (plugin.instrument().groups[1].playback.key != 30 || ! capture ("sampler-keyswitch.png")) return reviewFailure (__LINE__);
    triggerMode.setSelectedId (3, sendNotificationSync); ccNumber.slider.setValue (20, sendNotificationSync); ccLow.slider.setValue (64, sendNotificationSync);
    if (plugin.instrument().groups[1].playback.trigger != GroupTrigger::controller || ! capture ("sampler-cc-range.png")) return reviewFailure (__LINE__);
    releaseTrigger.setToggleState (true, sendNotificationSync);
    if (! plugin.instrument().groups[1].playback.releaseTrigger || ! capture ("sampler-release-trigger.png")) return reviewFailure (__LINE__);
    plugin.commitImport (violin); groupRows.selectRow (0); groups.onClick();
    const auto violinFile = first.getSiblingFile ("Violin.studiosampler");
    if (plugin.savePreset (violinFile).failed() || plugin.loadPreset (violinFile).failed()
        || plugin.instrument().groups[2].playback.key != 26 || plugin.articulations().size() != 3) return reviewFailure (__LINE__);
    plugin.commitImport (preserved); selectZone (0);
    advanced.onClick();
    if (page != Page::advanced || ! voices.slider.isVisible()) return reviewFailure (__LINE__);
    voices.slider.setValue (16, sendNotificationSync);
    if (plugin.instrument().settings.polyphony != 16) return reviewFailure (__LINE__);
    const ScopedValueSetter<AppearanceMetrics> appearanceScope (appearance, appearance);
    appearance.set (175, 130);
    appearanceChanged();
    setSize (618, 460);
    basic.onClick();
    if (! getLocalBounds().contains (keyboard.getBounds()) || viewport.getHeight() <= 0
        || ! body.getLocalBounds().contains (gain.slider.getBounds())
        || ! getLocalBounds().contains (loadPreset.getBounds()) || loadPreset.getBounds().intersects (savePreset.getBounds()))
        return reviewFailure (__LINE__);
    return std::abs (plugin.instrument().settings.attack - .023f) < .0001f;
}
#endif

std::unique_ptr<tracktion::Plugin::EditorComponent> Plugin::createEditor()
{
    return std::make_unique<Editor> (*this);
}
}
