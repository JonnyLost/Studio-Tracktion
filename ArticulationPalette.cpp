// SPDX-License-Identifier: GPL-3.0-or-later
#include "ArticulationPalette.h"
#include "MidiExpressionValues.h"
#include "StudioMixing.h"
#include "StudioUI.h"
#include "StudioSampler.h"
#include <cmath>

using namespace juce;
namespace te = tracktion;

namespace studio
{
namespace
{
const Identifier property { "studioArticulationMappings" };

std::unique_ptr<XmlElement> documentFrom (const String& text)
{
    if (text.isEmpty()) return std::make_unique<XmlElement> ("StudioArticulations");
    auto root = parseXML (text);
    return root != nullptr && root->hasTagName ("StudioArticulations") ? std::move (root) : nullptr;
}

XmlElement* profileFor (XmlElement& root, te::EditItemID instrument)
{
    for (auto* profile : root.getChildWithTagNameIterator ("Instrument"))
        if (profile->getStringAttribute ("instance") == instrument.toString()) return profile;
    return nullptr;
}

te::Plugin* uniqueInstrument (te::AudioTrack* track)
{
    te::Plugin* instrument = nullptr;
    if (track != nullptr)
        for (auto* plugin : track->pluginList)
            if (isInstrumentPlugin (*plugin))
            {
                if (instrument != nullptr) return nullptr;
                instrument = plugin;
            }
    return instrument;
}

bool decimalValue (const TextEditor& editor, int min, int max)
{
    const auto text = editor.getText().trim();
    return text.isNotEmpty() && text.containsOnly ("0123456789")
        && text.getIntValue() >= min && text.getIntValue() <= max;
}
}

ArticulationPalette::ArticulationPalette (PropertiesFile& preferences) : prefs (preferences)
{
    setWantsKeyboardFocus (true);
    subject.setFont (FontOptions (14.0f, Font::bold));
    explanation.setColour (Label::textColourId, colours::secondary);
    for (Component* item : std::initializer_list<Component*> { &subject, &explanation, &orientation,
          &pin, &dock, &insert, &audition, &newMapping, &editMapping, &padViewport, &name, &number, &value,
          &kind, &channel, &shortcut, &apply, &remove, &cancel })
        addAndMakeVisible (*item);
    padViewport.setViewedComponent (&padBank, false);
    padViewport.setScrollBarsShown (true, true);
    orientation.addItem ("Matrix", 1);
    orientation.addItem ("Vertical", 2);
    orientation.addItem ("Horizontal", 3);
    orientation.setTitle ("Articulation layout");
    orientation.setSelectedId (jlimit (1, 3, prefs.getIntValue ("studio.articulationOrientation", 1)), dontSendNotification);
    orientation.onChange = [this]
    {
        prefs.setValue ("studio.articulationOrientation", orientation.getSelectedId());
        prefs.saveIfNeeded();
        layoutChanged();
    };
    pin.setTooltip ("Pin the palette to this track. Click again to follow the selected track.");
    pin.onClick = [this]
    {
        pinnedTrack = pinnedTrack.isValid() ? te::EditItemID() : followedTrack;
        syncTrack();
    };
    dock.onClick = [this] { if (onToggleDock) onToggleDock(); };
    insert.setTooltip ("Explicitly insert the selected mapping at the playhead of the selected MIDI clip.");
    insert.onClick = [this] { insertMapping(); };
    audition.setTooltip ("Send the selected articulation to the instrument for live audition.");
    audition.onClick = [this] { if (auto* mapping = selected()) auditionMapping (*mapping); };
    newMapping.onClick = [this] { startMapping (false); };
    editMapping.onClick = [this] { startMapping (true); };
    apply.onClick = [this] { saveMapping (false); };
    remove.onClick = [this] { saveMapping (true); };
    cancel.onClick = [this] { panelOpen = false; layoutChanged(); };
    kind.addItem ("Keyswitch note", 1);
    kind.addItem ("Control Change", 2);
    kind.setTitle ("MIDI message type");
    channel.setTitle ("Instrument receiving MIDI channel");
    shortcut.setTitle ("Optional audition shortcut");
    for (int ch = 1; ch <= 16; ++ch) channel.addItem ("Channel " + String (ch), ch);
    shortcut.addItem ("No shortcut", 1);
    for (int key = 1; key <= 9; ++key) shortcut.addItem ("Ctrl+Cmd+" + String (key), key + 1);
    name.setTitle ("Articulation name");
    number.setTitle ("MIDI note or controller number");
    value.setTitle ("MIDI velocity or controller value");
    name.setInputRestrictions (64);
    number.setInputRestrictions (3, "0123456789");
    value.setInputRestrictions (3, "0123456789");
    kind.onChange = [this]
    {
        number.setTextToShowWhenEmpty (kind.getSelectedId() == 1 ? "Note 0-127" : "CC 0-127", colours::secondary);
        value.setTextToShowWhenEmpty (kind.getSelectedId() == 1 ? "Velocity 1-127" : "Value 0-127", colours::secondary);
    };
    name.setTextToShowWhenEmpty ("Articulation name", colours::secondary);
    number.setTooltip ("Use the MIDI number from the patch documentation.");
    value.setTooltip ("Enter the documented note-on velocity or CC value.");
    appearanceChanged();
    updateState();
}

ArticulationPalette::~ArticulationPalette()
{
    stopLiveNote();
    padViewport.setViewedComponent (nullptr);
}

void ArticulationPalette::setContext (te::Edit* next)
{
    stopLiveNote();
    selectedClip = nullptr;
    followedTrack = pinnedTrack = loadedTrack = loadedInstrument = {};
    edit = next;
    ready = mappingWritable = panelOpen = false;
    serialized.clear();
    actionStatus.clear();
    selectedId.clear();
    mappings.clear();
    rebuildPads();
    syncTrack();
}

te::AudioTrack* ArticulationPalette::track (te::EditItemID id) const
{
    return edit != nullptr && id.isValid() ? te::findAudioTrackForID (*edit, id) : nullptr;
}

te::AudioTrack* ArticulationPalette::target() const
{
    return track (pinnedTrack.isValid() ? pinnedTrack : followedTrack);
}

void ArticulationPalette::refresh (te::AudioTrack* selectedTrack, te::MidiClip* clip, bool enabled)
{
    if (pinnedTrack.isValid() && track (pinnedTrack) == nullptr) pinnedTrack = {};
    followedTrack = selectedTrack != nullptr ? selectedTrack->itemID : te::EditItemID();
    selectedClip = clip;
    ready = enabled;
    syncTrack();
}

void ArticulationPalette::syncTrack()
{
    auto* t = target();
    auto* instrument = uniqueInstrument (t);
    const auto trackId = t != nullptr ? t->itemID : te::EditItemID();
    const auto instrumentId = instrument != nullptr ? instrument->itemID : te::EditItemID();
    const auto data = t != nullptr ? t->state.getProperty (property).toString() : String();
    auto* sampler = dynamic_cast<sampler::Plugin*> (instrument);
    const bool hasRules = sampler && std::any_of (sampler->instrument().groups.begin(), sampler->instrument().groups.end(), [] (const auto& g) { return g.playback.trigger != sampler::GroupTrigger::normal; });
    const bool nativeChanged = sampler != nullptr && (nativeRevision != sampler->changeCount() || nativeSampler != hasRules);
    nativeSampler = hasRules;
    const bool targetChanged = loadedTrack != trackId || loadedInstrument != instrumentId;
    if (targetChanged || nativeChanged || serialized != data)
    {
        if (targetChanged)
        {
            stopLiveNote();
            selectedId.clear();
            panelOpen = false;
        }
        loadedTrack = trackId;
        loadedInstrument = instrumentId;
        serialized = data;
        actionStatus.clear();
        std::vector<ArticulationMapping> refreshed;
        selectedId.clear();
        auto root = documentFrom (data);
        mappingWritable = root != nullptr;
        if (root != nullptr)
            if (auto* profile = profileFor (*root, loadedInstrument))
            {
                refreshed = readArticulations (*profile);
                selectedId = profile->getStringAttribute ("active");
            }
        if (sampler) { nativeRevision = sampler->changeCount(); if (nativeSampler) { refreshed = sampler->articulations(); mappingWritable = false; } }
        if (targetChanged || refreshed != mappings || refreshed.size() != pads.size())
        {
            mappings = std::move (refreshed);
            rebuildPads();
            layoutChanged();
        }
    }
    subject.setText (t == nullptr ? "Select an instrument track"
        : t->getName() + "  |  " + (instrument != nullptr ? instrument->getName() : String ("Requires one instrument on the track")),
        dontSendNotification);
    subject.setTooltip (subject.getText());
    updateState();
}

void ArticulationPalette::rebuildPads()
{
    pads.clear();
    for (size_t i = 0; i < mappings.size(); ++i)
    {
        const auto& mapping = mappings[i];
        auto pad = std::make_unique<TextButton> (mapping.name);
        pad->setToggleable (true);
        pad->setColour (TextButton::buttonOnColourId, colours::selected.interpolatedWith (colours::focus, .26f));
        pad->setTooltip ("Toggle " + mapping.name + " for composition | Ch " + String (mapping.channel)
            + (mapping.message == ArticulationMessage::keyswitch ? " | Note " : " | CC ")
            + String (mapping.number) + " / " + String (mapping.value)
            + (mapping.shortcut ? " | Ctrl+Cmd+" + String (mapping.shortcut) : String()));
        pad->onClick = [this, i] { selectMapping (i); };
        padBank.addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
    }
}

const ArticulationMapping* ArticulationPalette::selected() const
{
    const auto it = std::find_if (mappings.begin(), mappings.end(),
        [this] (const ArticulationMapping& mapping) { return mapping.id == selectedId; });
    return it == mappings.end() ? nullptr : &*it;
}

bool ArticulationPalette::canWriteTo (const te::MidiClip& clip) const
{
    const auto* mapping = selected();
    auto* t = target();
    const auto* instrument = uniqueInstrument (t);
    return ready && t != nullptr && instrument != nullptr && instrument->itemID == loadedInstrument
        && instrument->isEnabled() && mapping != nullptr && clip.hasValidSequence() && ! clip.getMPEMode()
        && clip.getAudioTrack() == t && ! t->isFrozen (te::Track::anyFreeze)
        && ! t->edit.getTransport().isRecording()
        && matchingClipChannel (mapping->channel, clip.getMidiChannel().getChannelNumber());
}

bool ArticulationPalette::canInsert() const
{
    return selectedClip != nullptr && canWriteTo (*selectedClip);
}

bool ArticulationPalette::canAudition() const
{
    auto* t = target();
    const auto* instrument = uniqueInstrument (t);
    if (! ready || instrument == nullptr || instrument->itemID != loadedInstrument
        || ! instrument->isEnabled() || t->isFrozen (te::Track::anyFreeze)
        || t->edit.getTransport().isRecording() || t->edit.getCurrentPlaybackContext() == nullptr)
        return false;
    const auto* hosted = dynamic_cast<const te::ExternalPlugin*> (instrument);
    return hosted == nullptr || hosted->getAudioPluginInstance() != nullptr;
}

void ArticulationPalette::updateState()
{
    pin.setButtonText (pinnedTrack.isValid() ? "Follow selected" : "Pin track");
    pin.setToggleState (pinnedTrack.isValid(), dontSendNotification);
    pin.setEnabled (pinnedTrack.isValid() || followedTrack.isValid());
    const bool canMap = ! nativeSampler && ready && loadedInstrument.isValid() && mappingWritable && edit != nullptr
        && ! edit->getTransport().isRecording();
    newMapping.setEnabled (canMap);
    editMapping.setEnabled (canMap && selected() != nullptr);
    insert.setEnabled (canInsert());
    audition.setEnabled (selected() != nullptr && canAudition());
    apply.setEnabled (canMap);
    remove.setEnabled (canMap && editingId.isNotEmpty());
    for (auto* field : std::initializer_list<Component*> { &name, &number, &value, &kind, &channel,
                                                           &shortcut, &apply, &remove, &cancel })
        field->setVisible (panelOpen);
    for (size_t i = 0; i < pads.size(); ++i)
    {
        pads[i]->setEnabled (ready && edit != nullptr && ! edit->getTransport().isRecording());
        pads[i]->setToggleState (mappings[i].id == selectedId, dontSendNotification);
    }
    if (panelOpen)
        return;
    const auto* mapping = selected();
    explanation.setText (actionStatus.isNotEmpty() ? actionStatus : mappings.empty() ? (nativeSampler ? "Assign keyswitches or CC ranges in Sampler Groups to expose articulations." : "Unmapped. Add verified messages for this instrument patch.")
        : mapping == nullptr ? "Choose an active articulation. Audition and Insert are separate actions."
        : "Selected: " + mapping->name + "  |  Target channel " + String (mapping->channel)
            + (canInsert() ? "  |  Click Insert to write to the clip." : "  |  Insert requires a matching clip and channel."),
        dontSendNotification);
    explanation.setTooltip (explanation.getText());
}

void ArticulationPalette::setStatus (const String& status)
{
    actionStatus = status;
    updateState();
    if (onStatus) onStatus (status);
}

void ArticulationPalette::startMapping (bool modify)
{
    const auto* mapping = selected();
    if (modify && mapping == nullptr) return;
    actionStatus.clear();
    editingId = modify ? mapping->id : String();
    name.setText (modify ? mapping->name : String(), false);
    number.setText (modify ? String (mapping->number) : String(), false);
    value.setText (modify ? String (mapping->value) : String(), false);
    kind.setSelectedId (modify && mapping->message == ArticulationMessage::controller ? 2 : 1, dontSendNotification);
    if (kind.onChange) kind.onChange();
    channel.setSelectedId (modify ? mapping->channel : 0, dontSendNotification);
    channel.setTextWhenNothingSelected ("Receiving channel");
    shortcut.setSelectedId (modify ? mapping->shortcut + 1 : 1, dontSendNotification);
    explanation.setText ("Use the verified MIDI message and channel from the instrument's patch settings.", dontSendNotification);
    panelOpen = true;
    layoutChanged();
    name.grabKeyboardFocus();
}

void ArticulationPalette::saveMapping (bool removeMapping)
{
    if (! apply.isEnabled() || ! panelOpen) return;
    const auto old = std::find_if (mappings.begin(), mappings.end(),
        [this] (const ArticulationMapping& mapping) { return mapping.id == editingId; });
    if (removeMapping)
    {
        if (old == mappings.end()) return;
        mappings.erase (old);
        if (selectedId == editingId) selectedId.clear();
    }
    else
    {
        ArticulationMapping mapping { editingId.isEmpty() ? Uuid().toString() : editingId, name.getText().trim(),
            kind.getSelectedId() == 1 ? ArticulationMessage::keyswitch : ArticulationMessage::controller,
            channel.getSelectedId(), number.getText().getIntValue(), value.getText().getIntValue(),
            shortcut.getSelectedId() - 1 };
        if (! decimalValue (number, 0, 127) || ! decimalValue (value, 0, 127) || ! validArticulation (mapping))
        {
            explanation.setText ("Enter a name, receiving channel, MIDI number, and valid value.", dontSendNotification);
            return;
        }
        if (mapping.shortcut != 0 && std::any_of (mappings.begin(), mappings.end(), [&] (const auto& existing)
            { return mapping.id != existing.id && mapping.shortcut == existing.shortcut; }))
        {
            explanation.setText ("This instrument already uses that shortcut.", dontSendNotification);
            return;
        }
        selectedId = mapping.id;
        if (old != mappings.end()) *old = std::move (mapping);
        else mappings.push_back (std::move (mapping));
    }
    panelOpen = false;
    persistMappings();
}

void ArticulationPalette::persistMappings()
{
    auto* t = target();
    auto root = documentFrom (serialized);
    if (t == nullptr || root == nullptr || ! loadedInstrument.isValid()) return;
    auto* profile = profileFor (*root, loadedInstrument);
    if (profile == nullptr)
    {
        profile = root->createNewChildElement ("Instrument");
        profile->setAttribute ("instance", loadedInstrument.toString());
    }
    writeArticulations (*profile, mappings);
    profile->setAttribute ("active", selectedId);
    auto& history = t->edit.getUndoManager();
    history.beginNewTransaction ("Edit articulation mappings");
    t->state.setProperty (property, root->toString (XmlElement::TextFormat().singleLine()), &history);
    history.beginNewTransaction();
    syncTrack();
    rebuildPads();
    layoutChanged();
}

void ArticulationPalette::selectMapping (size_t index)
{
    if (! ready || index >= mappings.size() || edit == nullptr || edit->getTransport().isRecording())
        return;
    auto* t = target();
    const auto* instrument = uniqueInstrument (t);
    if (instrument == nullptr || instrument->itemID != loadedInstrument)
        return;

    auto root = documentFrom (serialized);
    if (root == nullptr) return;
    auto* profile = profileFor (*root, loadedInstrument);
    if (profile == nullptr && nativeSampler)
    { profile = root->createNewChildElement ("Instrument"); profile->setAttribute ("instance", loadedInstrument.toString()); }
    if (profile == nullptr) return;
    const auto chosen = mappings[index];
    const bool clear = ! nativeSampler && selectedId == mappings[index].id;
    if (clear) profile->removeAttribute ("active");
    else profile->setAttribute ("active", mappings[index].id);
    auto& history = t->edit.getUndoManager();
    history.beginNewTransaction (clear ? "Clear articulation" : "Select articulation");
    t->state.setProperty (property, root->toString (XmlElement::TextFormat().singleLine()), &history);
    history.beginNewTransaction();
    syncTrack();
    if (nativeSampler) auditionMapping (chosen);
}

void ArticulationPalette::auditionMapping (const ArticulationMapping& mapping)
{
    if (! canAudition())
    {
        setStatus ("Audition requires a loaded, enabled instrument and an active audio context, outside recording.");
        return;
    }
    auto* t = target();
    actionStatus.clear();
    stopLiveNote();
    if (mapping.message == ArticulationMessage::keyswitch)
    {
        t->injectLiveMidiMessage (MidiMessage::noteOn (mapping.channel, mapping.number, (uint8) mapping.value), {});
        held = HeldNote { t->itemID, mapping.channel, mapping.number };
        startTimer (100);
    }
    else
        t->injectLiveMidiMessage (MidiMessage::controllerEvent (mapping.channel, mapping.number, mapping.value), {});
    updateState();
}

void ArticulationPalette::stopLiveNote()
{
    stopTimer();
    if (held)
    {
        if (auto* t = track (held->track))
            t->injectLiveMidiMessage (MidiMessage::noteOff (held->channel, held->number), {});
        held.reset();
    }
}

void ArticulationPalette::insertMapping()
{
    if (! canInsert()) return;
    const auto& mapping = *selected();
    auto& clip = *selectedClip;
    auto playhead = edit->getTransport().getPosition();
    const auto bounds = clip.getPosition();
    if (playhead < bounds.getStart() || playhead >= bounds.getEnd())
    {
        setStatus ("Move the playhead inside the selected MIDI clip.");
        return;
    }
    auto beat = clip.getContentBeatAtTime (playhead);
    auto end = clip.getContentBeatAtTime (bounds.getEnd());
    if (clip.isLooping())
    {
        const auto start = clip.getLoopStartBeats(), limit = start + clip.getLoopLengthBeats();
        const auto length = clip.getLoopLengthBeats().inBeats();
        if (length > 0.0 && beat >= start)
            beat = start + te::BeatDuration::fromBeats (std::fmod ((beat - start).inBeats(), length));
        end = std::min (end, limit);
    }
    if (beat < te::BeatPosition() || end <= beat) return;
    auto* um = clip.getUndoManager();
    if (um) um->beginNewTransaction ("Insert articulation");
    auto& sequence = clip.getSequence();
    if (mapping.message == ArticulationMessage::controller)
        sequence.addControllerEvent (beat, mapping.number, mapping.value << 7, um);
    else
        sequence.addNote (mapping.number, beat, std::min (te::BeatDuration::fromBeats (0.125), end - beat), mapping.value, 0, um);
    if (um) um->beginNewTransaction();
    setStatus ("Inserted " + mapping.name + " on channel " + String (mapping.channel));
}

void ArticulationPalette::writeForDrawnNote (te::MidiClip& clip, te::BeatPosition musicalNote, int pitch)
{
    if (! canWriteTo (clip) || clip.isLooping() || clip.isMuted() || clip.hasAnyTakes()
        || clip.getQuantisation().isEnabled() || clip.getGrooveTemplate().isNotEmpty())
        return;
    const auto& mapping = *selected();
    const auto bounds = clip.getPosition();
    const auto start = clip.getContentBeatAtTime (bounds.getStart());
    const auto end = clip.getContentBeatAtTime (bounds.getEnd());
    if (musicalNote < start || musicalNote >= end)
        return;
    const auto isMappedNote = [this, &mapping] (int note)
    {
        return std::any_of (mappings.begin(), mappings.end(), [&] (const auto& entry)
        {
            return entry.channel == mapping.channel && entry.message == ArticulationMessage::keyswitch && entry.number == note;
        });
    };
    if (isMappedNote (pitch))
        return; // A manually drawn keyswitch is already a deliberate articulation event.

    auto& sequence = clip.getSequence();
    const auto lead = te::BeatDuration::fromBeats (.0625);
    const auto writeAt = mapping.message == ArticulationMessage::keyswitch ? musicalNote - lead : musicalNote;
    if (writeAt < start)
    {
        setStatus ("Keyswitch needs room before the first note. Add a lead-in or use Insert.");
        return;
    }

    te::BeatPosition last = start;
    bool known = false, alreadyActive = false;
    const auto remember = [&] (te::BeatPosition beat, bool active)
    {
        if (beat >= start && beat <= musicalNote && (! known || beat >= last))
        {
            known = true;
            alreadyActive = active;
            last = beat;
        }
    };
    for (const auto* event : sequence.getControllerEvents())
    {
        const auto type = event->getType();
        const auto value = event->getControllerValue() >> 7;
        const bool isSelectedType = mapping.message == ArticulationMessage::controller && mapping.number == type;
        if (isSelectedType || std::any_of (mappings.begin(), mappings.end(), [&] (const auto& entry)
            {
                return entry.channel == mapping.channel && entry.message == ArticulationMessage::controller
                    && entry.number == type && entry.value == value;
            }))
            remember (event->getBeatPosition(), isSelectedType && value == mapping.value);
    }
    for (const auto* note : sequence.getNotes())
        if (isMappedNote (note->getNoteNumber()))
            remember (note->getStartBeat(), mapping.message == ArticulationMessage::keyswitch
                      && note->getNoteNumber() == mapping.number);
    if (alreadyActive)
        return;

    // The preceding keyswitch must sound before the musical note. Avoid
    // inserting behind a manual change placed inside that lead-in.
    if (known && last > writeAt)
    {
        setStatus ("Move the existing articulation earlier before drawing this note.");
        return;
    }
    auto* undo = clip.getUndoManager();
    if (mapping.message == ArticulationMessage::controller)
    {
        if (auto* existing = sequence.getControllerEventAt (writeAt, mapping.number);
            existing != nullptr && existing->getBeatPosition() == writeAt)
            existing->setControllerValue (mapping.value << 7, undo);
        else
            sequence.addControllerEvent (writeAt, mapping.number, mapping.value << 7, undo);
    }
    else
        sequence.addNote (mapping.number, writeAt, lead * .75, mapping.value, 0, undo);
    setStatus ("Wrote " + mapping.name + " with the note.");
}

#if STUDIO_SPATIAL_TESTS
bool ArticulationPalette::addReviewMappings (te::AudioTrack& targetTrack, const std::vector<ArticulationMapping>& entries)
{
    refresh (&targetTrack, nullptr, true);
    if (! loadedInstrument.isValid() || ! mappingWritable) return false;
    mappings = entries;
    persistMappings();
    return true;
}

bool ArticulationPalette::selectReviewMapping (const String& label)
{
    for (size_t i = 0; i < mappings.size(); ++i)
        if (mappings[i].name == label)
        {
            const bool clearing = ! nativeSampler && reviewActiveName() == label;
            selectMapping (i);
            return clearing ? reviewActiveName().isEmpty() : reviewActiveName() == label;
        }
    return false;
}

String ArticulationPalette::reviewActiveName() const
{
    if (const auto* mapping = selected()) return mapping->name;
    return {};
}
#endif

bool ArticulationPalette::keyPressed (const KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! ready || ! mods.isCtrlDown() || ! mods.isCommandDown() || mods.isShiftDown() || mods.isAltDown())
        return false;
    const int numberKey = key.getKeyCode() - '0';
    if (numberKey < 1 || numberKey > 9) return false;
    for (size_t i = 0; i < mappings.size(); ++i)
        if (mappings[i].shortcut == numberKey)
        {
            auditionMapping (mappings[i]);
            return true;
        }
    return false;
}

void ArticulationPalette::setFloating (bool floating)
{
    dock.setButtonText (floating ? "Dock" : "Float");
}

void ArticulationPalette::appearanceChanged()
{
    padViewport.setScrollBarThickness (appearance.scrollbar());
    for (auto* field : { &name, &number, &value })
    {
        field->setFont (uiFont (13.0f));
        field->applyFontToAllText (uiFont (13.0f));
    }
    resized();
    repaint();
}

void ArticulationPalette::layoutChanged()
{
    updateState();
    resized();
    repaint();
    if (onLayoutChange) onLayoutChange();
}

int ArticulationPalette::preferredPadRows (int width) const
{
    if (pads.empty()) return 0;
    if (orientation.getSelectedId() == (int) Orientation::horizontal) return 1;
    if (orientation.getSelectedId() == (int) Orientation::vertical) return jmin (4, (int) pads.size());
    const int columns = jmax (1, width / appearance.span (124));
    return jmin (4, ((int) pads.size() + columns - 1) / columns);
}

int ArticulationPalette::minimumContentHeight (int width)
{
    const auto& m = appearance;
    const int inner = jmax (1, width - m.px (14));
    int height = m.line (14, 4) + 2 * m.line (13, 1) + m.px (14);
    height += toolbarHeight (inner, { { &orientation, 112 }, { &pin, 128 }, { &dock, 55 },
        { &newMapping, 114 }, { &editMapping, 112 }, { &audition, 84 }, { &insert, 140 } });
    if (panelOpen) height += toolbarHeight (inner, { { &name, 164 }, { &kind, 129 }, { &channel, 127 },
        { &number, 98 }, { &value, 110 }, { &shortcut, 131 }, { &apply, 110 }, { &remove, 75 }, { &cancel, 75 } });
    return height + preferredPadRows (inner) * (m.control (34) + m.px (3)) + m.scrollbar();
}

void ArticulationPalette::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (7));
    subject.setBounds (area.removeFromTop (m.line (14, 4)));
    layoutToolbar (area, { { &orientation, 112 }, { &pin, 128 }, { &dock, 55 },
        { &newMapping, 114 }, { &editMapping, 112 }, { &audition, 84 }, { &insert, 140 } });
    explanation.setBounds (area.removeFromTop (2 * m.line (13, 1)));
    if (panelOpen) layoutToolbar (area, { { &name, 164 }, { &kind, 129 }, { &channel, 127 },
        { &number, 98 }, { &value, 110 }, { &shortcut, 131 }, { &apply, 110 }, { &remove, 75 }, { &cancel, 75 } });
    padViewport.setBounds (area);
    const int height = m.control (34) + m.px (3), padWidth = m.span (124);
    const auto presentation = (Orientation) orientation.getSelectedId();
    int width = jmax (1, area.getWidth());
    int columns = presentation == Orientation::horizontal ? jmax (1, (int) pads.size())
        : presentation == Orientation::vertical ? 1 : jmax (1, width / padWidth);
    if (presentation != Orientation::horizontal && ((int) pads.size() + columns - 1) / columns * height > area.getHeight())
    {
        width = jmax (1, width - m.scrollbar());
        if (presentation == Orientation::matrix) columns = jmax (1, width / padWidth);
    }
    const int cellWidth = presentation == Orientation::horizontal ? padWidth
        : presentation == Orientation::vertical ? jmin (width, m.span (184)) : width / columns;
    padBank.setSize (cellWidth * columns, ((int) pads.size() + columns - 1) / columns * height);
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setBounds (Rectangle<int> (i % columns * cellWidth, i / columns * height,
                                                      cellWidth, height).reduced (m.px (2)));
}

ArticulationStrip::ArticulationStrip (ArticulationPalette& source) : palette (source)
{
    subject.setFont (FontOptions (13.0f, Font::bold));
    empty.setText ("No verified articulation mappings", dontSendNotification);
    empty.setColour (Label::textColourId, colours::secondary);
    for (auto* item : std::initializer_list<Component*> { &subject, &empty, &bank, &follow, &audition, &insert, &manage })
        addAndMakeVisible (*item);
    bank.setTitle ("Articulation bank");
    bank.onChange = [this] { refresh(); };
    manage.setTooltip ("Open the full articulation palette to add or edit verified mappings.");
    manage.onClick = [this] { if (onManage) onManage(); };
    follow.setTooltip ("Pin these mappings or follow the selected track.");
    follow.onClick = [this] { if (palette.pin.onClick) palette.pin.onClick(); refresh(); };
    audition.setTooltip ("Send the selected articulation to the instrument for audition.");
    audition.onClick = [this] { if (auto* mapping = palette.selected()) palette.auditionMapping (*mapping); };
    insert.setTooltip ("Write the selected articulation at the playhead in an editable matching MIDI clip.");
    insert.onClick = [this] { palette.insertMapping(); };
    for (size_t i = 0; i < pads.size(); ++i)
    {
        pads[i].setToggleable (true);
        pads[i].setColour (TextButton::buttonOnColourId, colours::selected.interpolatedWith (colours::focus, .26f));
        addChildComponent (pads[i]);
        pads[i].onClick = [this, i]
        {
            palette.selectMapping ((size_t) jmax (0, bank.getSelectedItemIndex()) * pads.size() + i);
            refresh();
        };
    }
}

int ArticulationStrip::height()
{
    return 2 * appearance.control (26, 12) + appearance.px (6);
}

void ArticulationStrip::refresh()
{
    const auto bankCount = static_cast<int> ((palette.mappings.size() + pads.size() - 1) / pads.size());
    const bool changedTrack = displayedTrack != palette.loadedTrack || displayedInstrument != palette.loadedInstrument;
    if (changedTrack || bankCount != bank.getNumItems())
    {
        int next = changedTrack ? 0 : jmax (0, bank.getSelectedItemIndex());
        const auto active = std::find_if (palette.mappings.begin(), palette.mappings.end(),
            [this] (const auto& mapping) { return mapping.id == palette.selectedId; });
        if (changedTrack && active != palette.mappings.end())
            next = (int) (std::distance (palette.mappings.begin(), active) / pads.size());
        displayedTrack = palette.loadedTrack;
        displayedInstrument = palette.loadedInstrument;
        bank.clear (dontSendNotification);
        for (int i = 0; i < bankCount; ++i) bank.addItem ("Bank " + String (i + 1), i + 1);
        bank.setSelectedItemIndex (jmin (bankCount - 1, next), dontSendNotification);
    }
    const bool showBank = bankCount > 1;
    const bool bankChanged = bank.isVisible() != showBank;
    bank.setVisible (showBank);
    auto subjectText = palette.loadedTrack.isValid() ? palette.subject.getText() : "Articulations";
    subject.setText (subjectText, dontSendNotification);
    subject.setTooltip (palette.actionStatus.isNotEmpty() ? palette.actionStatus : subjectText);
    follow.setButtonText (palette.pinnedTrack.isValid() ? "Follow" : "Pin");
    follow.setEnabled (palette.pin.isEnabled());
    follow.setToggleState (palette.pinnedTrack.isValid(), dontSendNotification);
    audition.setEnabled (palette.selected() != nullptr && palette.canAudition());
    insert.setEnabled (palette.canInsert());
    empty.setVisible (bankCount == 0);

    const auto start = (size_t) jmax (0, bank.getSelectedItemIndex()) * pads.size();
    bool needsLayout = bankChanged;
    for (size_t i = 0; i < pads.size(); ++i)
    {
        auto& pad = pads[i];
        const bool visible = start + i < palette.mappings.size();
        needsLayout |= pad.isVisible() != visible;
        pad.setVisible (visible);
        if (! visible) continue;
        const auto& entry = palette.mappings[start + i];
        pad.setButtonText (entry.name);
        pad.setTooltip ((palette.nativeSampler ? "Select and send " : "Toggle ") + entry.name + " for composition on MIDI channel " + String (entry.channel));
        pad.setEnabled (palette.ready && palette.edit != nullptr && ! palette.edit->getTransport().isRecording());
        pad.setToggleState (entry.id == palette.selectedId, dontSendNotification);
    }
    if (needsLayout) resized();
}

void ArticulationStrip::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (9), m.px (3));
    auto header = area.removeFromTop (m.control (26, 12));
    for (const auto item : { ToolbarItem { &manage, 88 }, { &insert, 65 }, { &audition, 80 }, { &follow, 64 } })
        item.component->setBounds (header.removeFromRight (item.width()).reduced (m.px (2), m.px (1)));
    subject.setBounds (header);
    auto choices = area.withHeight (m.control (26, 12));
    if (bank.isVisible())
        bank.setBounds (choices.removeFromRight (m.span (88)).reduced (m.px (2), m.px (1)));
    empty.setBounds (choices);
    const int visible = (int) std::count_if (pads.begin(), pads.end(), [] (const auto& pad) { return pad.isVisible(); });
    if (visible == 0) return;
    const int cell = jmin (m.span (142), choices.getWidth() / visible);
    for (auto& pad : pads)
        if (pad.isVisible()) pad.setBounds (choices.removeFromLeft (cell).reduced (m.px (2), m.px (1)));
}

void ArticulationStrip::paint (Graphics& g)
{
    g.fillAll (colours::well);
    g.setColour (colours::divider);
    g.fillRect (getLocalBounds().removeFromTop (appearance.px (1)));
}
}
