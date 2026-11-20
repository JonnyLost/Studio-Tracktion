// SPDX-License-Identifier: GPL-3.0-or-later
#include "ArticulationPalette.h"
#include "MidiExpressionValues.h"
#include "StudioMixing.h"
#include "StudioUI.h"
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
          &pin, &dock, &insert, &newMapping, &editMapping, &padViewport, &name, &number, &value,
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
    for (int key = 1; key <= 9; ++key) shortcut.addItem ("Ctrl+Shift+" + String (key), key + 1);
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
    lastSentId.clear();
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
    const bool targetChanged = loadedTrack != trackId || loadedInstrument != instrumentId;
    if (targetChanged || serialized != data)
    {
        if (targetChanged)
        {
            stopLiveNote();
            selectedId.clear();
            lastSentId.clear();
            panelOpen = false;
        }
        loadedTrack = trackId;
        loadedInstrument = instrumentId;
        serialized = data;
        actionStatus.clear();
        mappings.clear();
        auto root = documentFrom (data);
        mappingWritable = root != nullptr;
        if (root != nullptr)
            if (auto* profile = profileFor (*root, loadedInstrument)) mappings = readArticulations (*profile);
        rebuildPads();
        layoutChanged();
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
        pad->setTooltip (mapping.name + " | Ch " + String (mapping.channel)
            + (mapping.message == ArticulationMessage::keyswitch ? " | Note " : " | CC ")
            + String (mapping.number) + " / " + String (mapping.value)
            + (mapping.shortcut ? " | Ctrl+Shift+" + String (mapping.shortcut) : String()));
        pad->onClick = [this, i] { activate (i); };
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

bool ArticulationPalette::canInsert() const
{
    const auto* mapping = selected();
    auto* t = target();
    const auto* instrument = uniqueInstrument (t);
    return ready && t != nullptr && instrument != nullptr && instrument->itemID == loadedInstrument
        && mapping != nullptr && selectedClip != nullptr
        && selectedClip->hasValidSequence() && ! selectedClip->getMPEMode()
        && selectedClip->getAudioTrack() == t && ! t->isFrozen (te::Track::anyFreeze)
        && ! t->edit.getTransport().isRecording()
        && matchingClipChannel (mapping->channel, selectedClip->getMidiChannel().getChannelNumber());
}

void ArticulationPalette::updateState()
{
    pin.setButtonText (pinnedTrack.isValid() ? "Follow selected" : "Pin track");
    pin.setToggleState (pinnedTrack.isValid(), dontSendNotification);
    pin.setEnabled (pinnedTrack.isValid() || followedTrack.isValid());
    const bool canMap = ready && loadedInstrument.isValid() && mappingWritable && edit != nullptr
        && ! edit->getTransport().isRecording();
    newMapping.setEnabled (canMap);
    editMapping.setEnabled (canMap && selected() != nullptr);
    insert.setEnabled (canInsert());
    apply.setEnabled (canMap);
    remove.setEnabled (canMap && editingId.isNotEmpty());
    for (auto* field : std::initializer_list<Component*> { &name, &number, &value, &kind, &channel,
                                                           &shortcut, &apply, &remove, &cancel })
        field->setVisible (panelOpen);
    for (size_t i = 0; i < pads.size(); ++i)
    {
        pads[i]->setEnabled (ready);
        pads[i]->setToggleState (mappings[i].id == lastSentId, dontSendNotification);
    }
    if (panelOpen)
        return;
    const auto* mapping = selected();
    explanation.setText (actionStatus.isNotEmpty() ? actionStatus : mappings.empty() ? "Unmapped. Add verified messages for this instrument patch."
        : mapping == nullptr ? "Click a pad to audition. Insert explicitly to change a clip."
        : "Selected: " + mapping->name + "  |  Target channel " + String (mapping->channel)
            + (canInsert() ? "  |  Click Insert to write to the clip." : "  |  Insert requires a matching clip and channel."),
        dontSendNotification);
    explanation.setTooltip (explanation.getText());
}

void ArticulationPalette::setStatus (const String& status)
{
    actionStatus = status;
    updateState();
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
        if (lastSentId == editingId) lastSentId.clear();
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
    auto& history = t->edit.getUndoManager();
    history.beginNewTransaction ("Edit articulation mappings");
    t->state.setProperty (property, root->toString (XmlElement::TextFormat().singleLine()), &history);
    history.beginNewTransaction();
    syncTrack();
}

void ArticulationPalette::activate (size_t index)
{
    if (index >= mappings.size()) return;
    auto* t = target();
    if (! ready || t == nullptr) return;
    const auto* instrument = uniqueInstrument (t);
    if (instrument == nullptr || instrument->itemID != loadedInstrument) return;
    const auto& mapping = mappings[index];
    selectedId = mapping.id;
    actionStatus.clear();
    stopLiveNote();
    if (instrument->isEnabled() && ! t->isFrozen (te::Track::anyFreeze)
        && t->edit.getCurrentPlaybackContext() != nullptr)
    {
        if (mapping.message == ArticulationMessage::keyswitch)
        {
            t->injectLiveMidiMessage (MidiMessage::noteOn (mapping.channel, mapping.number, (uint8) mapping.value), {});
            held = HeldNote { t->itemID, mapping.channel, mapping.number };
            startTimer (100);
        }
        else
            t->injectLiveMidiMessage (MidiMessage::controllerEvent (mapping.channel, mapping.number, mapping.value), {});
        lastSentId = mapping.id;
        updateState();
    }
    else
    {
        updateState();
        setStatus ("Live audition requires an active audio context and an enabled instrument on an unfrozen track.");
    }
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

bool ArticulationPalette::keyPressed (const KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! isShowing() || ! ready || ! mods.isCtrlDown() || ! mods.isShiftDown() || mods.isCommandDown() || mods.isAltDown())
        return false;
    const int numberKey = key.getKeyCode() - '0';
    if (numberKey < 1 || numberKey > 9) return false;
    for (size_t i = 0; i < mappings.size(); ++i)
        if (mappings[i].shortcut == numberKey)
        {
            activate (i);
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
        { &newMapping, 114 }, { &editMapping, 112 }, { &insert, 140 } });
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
        { &newMapping, 114 }, { &editMapping, 112 }, { &insert, 140 } });
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
}
