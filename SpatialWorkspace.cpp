// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialWorkspace.h"
#include "SpatialRouting.h"
#include "StudioMixing.h"
#include "StudioUI.h"

using namespace juce;
namespace te = tracktion;

namespace studio::spatial
{
namespace
{
const Identifier setupType { "MIC_SETUP" };
constexpr int firstCustomRoom = 100;
constexpr int maximumMics = 32;

template <typename Parent, typename... Items>
void add (Parent& parent, Items&... items)
{
    (parent.addAndMakeVisible (items), ...);
}

template <typename Items>
void populate (ComboBox& combo, const Items& names, int selected = 1)
{
    int index = 1;
    for (auto* name : names) combo.addItem (name, index++);
    combo.setSelectedId (selected, dontSendNotification);
}

Section inferSection (te::AudioTrack& track)
{
    const auto name = track.getName().toLowerCase();
    const auto contains = [&] (std::initializer_list<const char*> words)
    {
        return std::any_of (words.begin(), words.end(), [&] (auto* word) { return name.contains (word); });
    };
    if (contains ({ "violin", "viola", "cello", "contrabass", "strings", "harp" })) return Section::strings;
    if (contains ({ "flute", "oboe", "clarinet", "bassoon", "woodwind", "piccolo", "english horn", "basset horn", "saxophone" })) return Section::woodwinds;
    if (contains ({ "horn", "trumpet", "trombone", "tuba", "brass" })) return Section::brass;
    if (contains ({ "perc", "timpani", "drum", "mallet" })) return Section::percussion;
    if (contains ({ "piano", "keys", "organ", "celesta" })) return Section::keys;
    return Section::other;
}

int fieldHeight() { return appearance.line (12, 1) + appearance.control (28) + appearance.px (4); }
bool sideBySide (int width) { return width >= appearance.span (650); }
int mapHeight() { return appearance.px (236); }

int fieldRows (int width, int fields)
{
    const int columns = jmax (1, width / appearance.span (134));
    return (fields + columns - 1) / columns;
}

void layoutFields (Rectangle<int>& area, std::initializer_list<SpatialField*> fields)
{
    const int columns = jmax (1, area.getWidth() / appearance.span (134));
    int i = 0;
    Rectangle<int> row;
    for (auto* field : fields)
    {
        if (i % columns == 0) row = area.removeFromTop (fieldHeight());
        field->setBounds (row.removeFromLeft (row.getWidth() / (columns - i % columns)));
        ++i;
    }
}

ValueTree namedChild (const ValueTree& tree, const String& name)
{
    return tree.getChildWithProperty ("name", name);
}

String nameOf (const ValueTree& state) { return state.getProperty ("name").toString(); }
String keyOf (const ValueTree& state) { return state.getProperty ("key").toString(); }
}

class MicStrip final : public Component
{
public:
    explicit MicStrip (String keyToUse) : key (std::move (keyToUse))
    {
        add (*this, name, gain, pan, delay, mute, solo);
        name.setFont (FontOptions (13.0f, Font::bold));
        gain.onChange = pan.onChange = delay.onChange = mute.onClick = solo.onClick = [this]
        {
            auto next = state;
            next.gain = gain.get(); next.pan = pan.get(); next.delay = delay.get();
            next.mute = mute.getToggleState(); next.solo = solo.getToggleState();
            if (onChange) onChange (key, next);
        };
        mute.setClickingTogglesState (true);
        solo.setClickingTogglesState (true);
        mute.setTooltip ("Save the planned capsule mute setting. The audio mix is unaffected.");
        solo.setTooltip ("Save the planned capsule solo setting. The audio mix is unaffected.");
    }

    const String key;
    std::function<void(const String&, Mic)> onChange;
    void display (const ValueTree& tree, bool canEdit)
    {
        state = readMic (tree);
        name.setText (nameOf (tree), dontSendNotification);
        name.setTooltip (name.getText());
        gain.set (state.gain); pan.set (state.pan); delay.set (state.delay);
        mute.setToggleState (state.mute, dontSendNotification);
        solo.setToggleState (state.solo, dontSendNotification);
        setEnabled (canEdit);
    }

    void paint (Graphics& g) override
    {
        g.setColour (colours::control);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), (float) appearance.px (5));
    }

    static int height()
    {
        return appearance.line (13, 4) + 3 * fieldHeight() + appearance.control (25) + appearance.px (12);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (appearance.px (5));
        name.setBounds (area.removeFromTop (appearance.line (13, 4)));
        gain.setBounds (area.removeFromTop (fieldHeight()));
        pan.setBounds (area.removeFromTop (fieldHeight()));
        delay.setBounds (area.removeFromTop (fieldHeight()));
        auto row = area.removeFromTop (appearance.control (25));
        mute.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (appearance.px (1)));
        solo.setBounds (row.reduced (appearance.px (1)));
    }

private:
    Mic state;
    Label name;
    SpatialField gain { "Level", -60, 12, 0.5, " dB" }, pan { "Pan", -1, 1, 0.01 },
                 delay { "Delay trim", -30, 30, 0.1, " ms" };
    TextButton mute { "Mute" }, solo { "Solo" };
};

SpatialWorkspace::SpatialWorkspace()
    : decay { SpatialField (bandNames[0], 0.25, 2, 0.05, " x"), SpatialField (bandNames[1], 0.25, 2, 0.05, " x"),
              SpatialField (bandNames[2], 0.25, 2, 0.05, " x"), SpatialField (bandNames[3], 0.25, 2, 0.05, " x"),
              SpatialField (bandNames[4], 0.25, 2, 0.05, " x"), SpatialField (bandNames[5], 0.25, 2, 0.05, " x") }
{
    add (*this, stagePage, roomPage, micPage);
    add (stagePage, stageToolbar, stageRoster, stageInspector, stageMap);
    stagePage.addChildComponent (stageSummary);
    stagePage.addChildComponent (openStage);
    openStage.onClick = [this] { if (onExpandStage) onExpandStage(); };
   #if JUCE_MAC
    stageToolbar.addAndMakeVisible (seatingDesigner);
    seatingDesigner.setTooltip ("Browse the VSL catalog and reference seating without changing acoustic positions");
    seatingDesigner.onClick = [this] { if (onOpenSeating) onOpenSeating(); };
   #endif
    add (stageToolbar, stageCheck, seating, arrangeScope, arrange);
    add (stageRoster, rosterTitle, trackList);
    add (stageInspector, selectedName, sections, pan, depth, trim, placeSelected, resetSeat, toggleSend, stageStatus, stageSignal);
    rosterTitle.setText ("Project Tracks", dontSendNotification);
    trackList.setColour (ListBox::backgroundColourId, colours::canvas);
    add (roomPage, roomStatus, roomDraftStatus, roomSignalLabel, roomCheck, roomMixer, roomGraphic, roomList, roomName,
         wall, floor, ceiling, applyRoom, saveRoom,
         renameRoom, deleteRoom, bypassRoom, resetRoom, widthField, lengthField, heightField, treatment,
         early, late, roomGain, preDelay);
    for (auto& band : decay) roomPage.addAndMakeVisible (band);
    add (micPage, micStatus, micMap, micList, patterns, stereo, setups, micName, setupName, placement, micMixer,
         addMic, removeMic, addStereo, renameMic, saveSetup, recallSetup, removeSetup, micRoomBypass,
         micX, micDepth, micHeight, azimuth, elevation, spacing, pairAngle, micBankViewport);
    micBankViewport.setViewedComponent (&micBank, false);
    micBankViewport.setScrollBarsShown (false, true);
    for (auto* status : { &stageStatus, &roomStatus, &roomDraftStatus, &micStatus, &stageSignal, &roomSignalLabel, &stageSummary })
    {
        status->setFont (FontOptions (12.0f));
        status->setColour (Label::textColourId, colours::secondary);
    }
    populate (seating, seatingNames);
    populate (arrangeScope, std::array { "All tracks", "Selected tracks", "Selected section" });
    populate (sections, sectionNames);
    for (int i = 0; i < (int) surfaceNames.size(); ++i)
    {
        const String material = surfaceNames[(size_t) i];
        wall.addItem ("Walls: " + material, i + 1);
        floor.addItem ("Floor: " + material, i + 1);
        ceiling.addItem ("Ceiling: " + material, i + 1);
    }
    populate (patterns, patternNames, 2);
    populate (stereo, stereoNames, 2);
    seating.setTitle ("Stage seating preset");
    seating.setTooltip ("Choose a layout, then Auto Arrange to apply it");
    arrange.setTooltip ("Place instruments in the chosen layout and scope. Undo restores the previous placement.");
    applyRoom.setTooltip ("Use the draft room for the shared Stage return");
    arrangeScope.setTitle ("Auto Arrange scope");
    trackList.setTitle ("Project tracks by instrument family");
    sections.setTitle ("Track instrument section");
    roomList.setTitle ("Room Library");
    wall.setTitle ("Wall material");
    floor.setTitle ("Floor material");
    ceiling.setTitle ("Ceiling material");
    micList.setTitle ("Virtual capsule");
    setups.setTitle ("Named microphone setup");
    patterns.setTitle ("Pickup pattern");
    stereo.setTitle ("Stereo geometry preset");
    for (auto* field : { &roomName, &micName, &setupName }) field->setInputRestrictions (64);
    roomName.setTitle ("Room name"); roomName.setTextToShowWhenEmpty ("Room name", colours::secondary);
    micName.setTitle ("Capsule name"); micName.setTextToShowWhenEmpty ("Capsule name", colours::secondary);
    setupName.setTitle ("Setup name"); setupName.setTextToShowWhenEmpty ("Setup name", colours::secondary);
    stageMap.setWantsKeyboardFocus (true);
    stageMap.setTitle ("Orchestral Stage seating plan");
    micMap.setWantsKeyboardFocus (true);
    micMap.setTitle ("Virtual microphone geometry");
    micMap.setDescription ("Select a diamond capsule and use arrow keys to move it.");
    stageMap.onSelect = [this] (const String& key)
    {
        auto found = std::find_if (tracks.begin(), tracks.end(), [&] (const auto& t) { return t.id.toString() == key; });
        if (found == tracks.end()) return;
        selectedTrack = found->id;
        if (onSelectTrack) onSelectTrack (selectedTrack);
        syncStage();
        syncMaps();
    };
    stageMap.onMove = [this] (const String& key, Position p, bool section) { moveTrack (key, p, section); };
    stageMap.trackFromDrag = [this] (const DragAndDropTarget::SourceDetails& details) -> String
    {
        const auto payload = details.description.toString();
        if (! editable || dragSession.isEmpty() || ! payload.startsWith (dragSession + "|")
            || details.sourceComponent == nullptr || ! stagePage.isParentOf (details.sourceComponent.get())) return {};
        const auto key = payload.fromFirstOccurrenceOf ("|", false, false);
        const auto found = std::find_if (tracks.begin(), tracks.end(), [&] (const auto& row) { return row.id.toString() == key; });
        if (found != tracks.end())
            if (auto* track = lookup (found->id); track != nullptr && ! track->isFrozen (te::Track::anyFreeze)) return key;
        return {};
    };
    placeSelected.onClick = [this] { placeSelectedTrack(); };
    arrange.onClick = [this] { arrangeTracks(); };
    stageMap.setDescription ("Drag a project track here to place it. Move a seat by dragging or using the arrow keys. Shift moves its section.");
    resetSeat.onClick = [this]
    {
        if (auto* track = lookup (selectedTrack); editable && track != nullptr && ! track->isFrozen (te::Track::anyFreeze))
        {
            auto value = readSeat (track->state.getChildWithName (seatType), inferSection (*track));
            value.position = {};
            begin ("Reset seat");
            setSeat (*track, value);
            finish();
        }
    };
    toggleSend.onClick = [this] { toggleTrackAudio(); };
    sections.onChange = [this] { editSeat (SeatField::family); };
    pan.onChange = [this] { editSeat (SeatField::pan); };
    depth.onChange = [this] { editSeat (SeatField::depth); };
    trim.onChange = [this] { editSeat (SeatField::trim); };

    roomList.onChange = [this]
    {
        const auto id = roomList.getSelectedId();
        if (isPositiveAndBelow (id - 1, (int) factoryRooms.size()))
        {
            const auto& preset = factoryRooms[(size_t) id - 1];
            setDraft (preset.room, preset.name);
        }
        else if (id >= firstCustomRoom)
        {
            auto preset = library().getChild (id - firstCustomRoom);
            if (preset.isValid()) setDraft (readRoom (preset), nameOf (preset));
        }
        roomDraftDirty = true;
        syncRooms();
    };
    const auto draftChanged = [this]
    {
        roomDraftDirty = true;
        roomGraphic.setRoom (readDraft());
        syncRooms();
    };
    for (auto* field : { &widthField, &lengthField, &heightField, &treatment, &early, &late, &roomGain, &preDelay })
        field->onChange = draftChanged;
    for (auto& band : decay) band.onChange = draftChanged;
    wall.onChange = floor.onChange = ceiling.onChange = draftChanged;
    applyRoom.onClick = [this] { apply (readDraft(), roomName.getText().trim()); };
    bypassRoom.onClick = micRoomBypass.onClick = [this] { toggleRoomBypass(); };
    stageCheck.onClick = roomCheck.onClick = [this] { toggleRoomCheck(); };
    for (auto* button : { &stageCheck, &roomCheck })
        button->setTooltip ("Temporarily raises only the wet reverb by 12 dB for up to 15 seconds.");
    roomMixer.onClick = [this]
    {
        if (edit != nullptr && onOpenRoomMixer)
            if (auto* track = roomReturnTrack (*edit)) onOpenRoomMixer (track->itemID);
    };
    resetRoom.onClick = [this, draftChanged]
    {
        auto current = readDraft();
        current.early = Room().early; current.late = Room().late; current.gain = Room().gain;
        current.preDelayMs = Room().preDelayMs;
        setDraft (current, roomName.getText());
        draftChanged();
    };
    roomName.onTextChange = draftChanged;
    saveRoom.onClick = [this] { writeRoomPreset (false, false); };
    renameRoom.onClick = [this] { writeRoomPreset (true, false); };
    deleteRoom.onClick = [this] { writeRoomPreset (false, true); };

    placement.onClick = [this] { mixer = false; layoutChanged(); };
    micMixer.onClick = [this] { mixer = true; layoutChanged(); };
    stereo.onChange = [this]
    {
        const auto i = stereo.getSelectedItemIndex();
        if (isPositiveAndBelow (i, (int) stereoPairs.size()))
        {
            spacing.set (stereoPairs[(size_t) i].spacingMetres);
            pairAngle.set (stereoPairs[(size_t) i].angleDegrees);
        }
    };
    stereo.onChange();
    micMap.onSelect = [this] (const String& key)
    {
        selectedMic = key;
        micName.setText (nameOf (selectedMicState()), false);
        syncMicrophones();
        syncMaps();
    };
    micMap.onMove = [this] (const String& key, Position point, bool)
    {
        auto old = microphones().getChildWithProperty ("key", key);
        if (! old.isValid()) return;
        auto value = readMic (old);
        value.position = point;
        updateMic (key, value);
    };
    micList.onChange = [this]
    {
        const int index = micList.getSelectedItemIndex();
        if (index >= 0) micMap.onSelect (keyOf (microphones().getChild (index)));
    };
    for (auto* field : { &micX, &micDepth, &micHeight, &azimuth, &elevation })
        field->onChange = [this] { editMic(); };
    patterns.onChange = [this] { editMic(); };
    addMic.onClick = [this] { createMics (false); };
    addStereo.onClick = [this] { createMics (true); };
    renameMic.onClick = [this] { renameSelectedMic (false); };
    removeMic.onClick = [this] { renameSelectedMic (true); };
    saveSetup.onClick = [this] { manageSetup (false, false); };
    recallSetup.onClick = [this] { manageSetup (true, false); };
    removeSetup.onClick = [this] { manageSetup (false, true); };
    setDraft ({}, "Chamber Hall");
    setView (View::stage);
}

SpatialWorkspace::~SpatialWorkspace()
{
    trackList.setModel (nullptr);
    micBankViewport.setViewedComponent (nullptr);
}

void SpatialWorkspace::setEdit (te::Edit* next)
{
    if (edit != nullptr) setRoomCheck (*edit, false);
    edit = next;
    editable = routable = false;
    selectedTrack = lastArrangementSelection = {};
    arrangementSelection.clear();
    selectedMic.clear();
    statusMessage.clear();
    lastRoomStamp.clear(); lastMicStamp.clear();
    tracks.clear();
    trackListRows.clear();
    dragSession = next == nullptr ? String() : Uuid().toString();
    appliedSeating = -1;
    {
        const ScopedValueSetter<bool> refreshing (refreshingTrackList, true);
        trackList.updateContent();
    }
    roomDraftDirty = false;
    setDraft (readRoom (activeRoom()), activeRoom().isValid() ? nameOf (activeRoom()) : String ("Chamber Hall"));
    syncRooms();
    syncStage();
    syncMicrophones();
    syncMaps();
}

void SpatialWorkspace::setView (View next)
{
    view = next;
    statusMessage.clear();
    stagePage.setVisible (view == View::stage);
    roomPage.setVisible (view == View::rooms);
    micPage.setVisible (view == View::microphones);
    resized();
}

void SpatialWorkspace::setCompactStage (bool value)
{
    if (compactStage == value) return;
    compactStage = value;
    stageMap.setOverview (value);
    for (auto* item : { &stageToolbar, &stageRoster, &stageInspector }) item->setVisible (! value);
    stageSummary.setVisible (value);
    openStage.setVisible (value);
    resized();
}

ValueTree SpatialWorkspace::scene() const { return edit != nullptr ? edit->state.getChildWithName (sceneType) : ValueTree(); }

bool SpatialWorkspace::supported() const
{
    auto root = scene();
    return ! root.isValid() || (int) root.getProperty ("version", -1) == version;
}

ValueTree SpatialWorkspace::ensureChild (ValueTree parent, const Identifier& type)
{
    return parent.isValid() ? parent.getOrCreateChildWithName (type, &edit->getUndoManager()) : ValueTree();
}

ValueTree SpatialWorkspace::requireScene()
{
    if (edit == nullptr || ! supported()) return {};
    auto root = scene();
    if (! root.isValid())
    {
        root = ValueTree (sceneType);
        root.setProperty ("version", version, nullptr);
        edit->state.addChild (root, -1, &edit->getUndoManager());
    }
    return root;
}

void SpatialWorkspace::begin (const char* action) { edit->getUndoManager().beginNewTransaction (action); }

void SpatialWorkspace::finish()
{
    edit->getUndoManager().beginNewTransaction();
    updateTracks (te::getAudioTracks (*edit));
    syncStage();
    syncRooms();
    syncMicrophones();
    syncMaps();
}

void SpatialWorkspace::setStatus (const String& status)
{
    statusMessage = status;
    syncStage();
    syncRooms();
    syncMicrophones();
}

te::AudioTrack* SpatialWorkspace::lookup (te::EditItemID id) const
{
    return edit != nullptr && id.isValid() ? te::findAudioTrackForID (*edit, id) : nullptr;
}

void SpatialWorkspace::refresh (const Array<te::AudioTrack*>& items, const Array<te::AudioTrack*>& selection,
                               bool canEdit, bool canRoute)
{
    editable = canEdit && edit != nullptr && supported();
    routable = editable && canRoute;
    arrangementSelection.clear();
    for (auto* track : selection)
        if (! isRoomReturn (*track)) arrangementSelection.addIfNotAlreadyThere (track->itemID);
    const auto next = arrangementSelection.isEmpty() ? te::EditItemID() : arrangementSelection.getFirst();
    if (next != lastArrangementSelection)
    {
        lastArrangementSelection = next;
        if (next.isValid()) selectedTrack = next;
    }
    updateTracks (items);
    if (view == View::stage) syncStage();
    if (view == View::rooms) syncRooms();
    if (view == View::microphones) syncMicrophones();
    if (view != View::rooms) syncMaps();
}

void SpatialWorkspace::updateTracks (const Array<te::AudioTrack*>& items)
{
    std::vector<TrackRow> next;
    std::array<int, sectionNames.size()> totals {}, counts {};
    for (auto* track : items)
        if (! isRoomReturn (*track))
        {
            auto v = track->state.getChildWithName (seatType);
            auto data = readSeat (v, inferSection (*track));
            ++totals[(size_t) data.section];
            auto* instrument = studio::findInstrument (*track);
            next.push_back ({ track->itemID, track->getName(), instrument != nullptr ? instrument->getName() : String(),
                              data, v.isValid(), hasRoomSend (*track) });
        }

    const auto style = (Seating) jlimit (0, 2, (int) scene().getProperty ("seating", 0));
    if (appliedSeating != (int) style)
    {
        appliedSeating = (int) style;
        seating.setSelectedId (appliedSeating + 1, dontSendNotification);
    }
    for (auto& row : next)
    {
        const auto group = (size_t) row.seat.section;
        const auto index = counts[group]++;
        if (! row.placed) row.seat.position = seat (style, row.seat.section, index, totals[group]);
    }
    const bool listChanged = next.size() != tracks.size()
        || ! std::equal (next.begin(), next.end(), tracks.begin(), [] (const auto& a, const auto& b)
        {
            return a.id == b.id && a.name == b.name && a.instrument == b.instrument && a.placed == b.placed
                && a.stageAudio == b.stageAudio && a.seat.section == b.seat.section;
        });
    tracks = std::move (next);
    if (listChanged)
    {
        trackListRows.clear();
        for (size_t group = 0; group < totals.size(); ++group)
            if (totals[group] > 0)
            {
                const auto section = static_cast<Section> (group);
                trackListRows.push_back ({ section, -1 });
                for (int i = 0; i < (int) tracks.size(); ++i)
                    if (tracks[(size_t) i].seat.section == section) trackListRows.push_back ({ section, i });
            }
        const ScopedValueSetter<bool> refreshing (refreshingTrackList, true);
        trackList.updateContent();
        trackList.repaint();
    }
    if (std::none_of (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; }))
        selectedTrack = tracks.empty() ? te::EditItemID() : tracks.front().id;
}

int SpatialWorkspace::getNumRows() { return (int) trackListRows.size(); }

String SpatialWorkspace::getNameForRow (int index)
{
    if (! isPositiveAndBelow (index, getNumRows())) return {};
    const auto group = trackListRows[(size_t) index];
    if (group.trackIndex < 0) return sectionNames[(size_t) group.section];
    const auto& row = tracks[(size_t) group.trackIndex];
    return row.name + (row.placed ? ", on stage" : ", unplaced");
}

void SpatialWorkspace::paintListBoxItem (int index, Graphics& g, int width, int height, bool selected)
{
    if (refreshingTrackList || ! isPositiveAndBelow (index, getNumRows())) return;
    const auto group = trackListRows[(size_t) index];
    auto area = Rectangle<int> (width, height).reduced (appearance.px (8), 0);
    if (group.trackIndex < 0)
    {
        g.setColour (colours::secondary);
        g.setFont (uiFont (11.0f, Font::bold));
        g.drawText (sectionNames[(size_t) group.section], area, Justification::centredLeft);
        return;
    }
    const auto& row = tracks[(size_t) group.trackIndex];
    if (selected) { g.setColour (colours::selected); g.fillRect (0, 0, width, height); }
    g.setColour (colours::text);
    g.setFont (uiFont (12.0f, Font::bold));
    g.drawFittedText (row.name, area.removeFromTop (height / 2 + appearance.px (2)), Justification::centredLeft, 1);
    g.setColour (colours::secondary);
    g.setFont (uiFont (10.0f));
    const auto status = ! row.placed ? "Unplaced" : row.stageAudio ? "Stage audio on" : "On stage";
    const auto instrument = row.instrument == row.name ? String() : row.instrument;
    g.drawFittedText (instrument.isEmpty() ? String (status) : instrument + "  |  " + status, area, Justification::centredLeft, 1);
}

void SpatialWorkspace::selectedRowsChanged (int index)
{
    if (refreshingTrackList || ! isPositiveAndBelow (index, getNumRows())) return;
    const auto row = trackListRows[(size_t) index];
    if (row.trackIndex >= 0) stageMap.onSelect (tracks[(size_t) row.trackIndex].id.toString());
    else syncStage();
}

void SpatialWorkspace::returnKeyPressed (int index)
{
    if (! isPositiveAndBelow (index, getNumRows()) || trackListRows[(size_t) index].trackIndex < 0) return;
    selectedRowsChanged (index);
    placeSelectedTrack();
}

var SpatialWorkspace::getDragSourceDescription (const SparseSet<int>& rows)
{
    if (rows.size() != 1 || ! isPositiveAndBelow (rows[0], getNumRows())) return {};
    const auto row = trackListRows[(size_t) rows[0]];
    if (row.trackIndex < 0 || ! editable) return {};
    return dragSession + "|" + tracks[(size_t) row.trackIndex].id.toString();
}

void SpatialWorkspace::syncStage()
{
    auto found = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; });
    auto* track = lookup (selectedTrack);
    const bool hasTrack = found != tracks.end() && track != nullptr;
    const bool enabled = hasTrack && found->stageAudio;
    const bool canChangeSeat = editable && hasTrack && ! track->isFrozen (te::Track::anyFreeze);
    SparseSet<int> selectedRows;
    if (hasTrack)
        for (int i = 0; i < getNumRows(); ++i)
            if (trackListRows[(size_t) i].trackIndex == (int) (found - tracks.begin())) selectedRows.addRange ({ i, i + 1 });
    if (trackList.getSelectedRows() != selectedRows) trackList.setSelectedRows (selectedRows, dontSendNotification);
    for (Component* component : std::initializer_list<Component*> { &sections, &pan, &depth, &trim, &resetSeat })
        component->setEnabled (canChangeSeat);
    resetSeat.setEnabled (canChangeSeat && found->placed);
    placeSelected.setEnabled (canChangeSeat && ! found->placed);
    selectedName.setText (! hasTrack ? String ("Select a project track") : found->name
        + (found->instrument.isEmpty() || found->instrument == found->name ? String() : "  |  " + found->instrument), dontSendNotification);
    selectedName.setTooltip (selectedName.getText());
    seating.setEnabled (editable); arrangeScope.setEnabled (editable);
    arrange.setEnabled (editable && ! tracks.empty());
    toggleSend.setButtonText (enabled ? "Disable stage audio" : "Enable stage audio");
    toggleSend.setEnabled (routable && hasTrack);
    if (hasTrack)
    {
        const auto seatValue = found->seat;
        sections.setSelectedId ((int) seatValue.section + 1, dontSendNotification);
        pan.set (seatValue.position.x); depth.set (seatValue.position.depth); trim.set (seatValue.sendTrim);
        toggleSend.setTooltip (enabled ? "Remove Studio's stage balance and owned aux send."
            : "Explicitly add Studio stage balance and a shared room send to this track.");
    }
    const auto liveGain = enabled ? activeRoomSendDb (*track) : std::nullopt;
    const auto signal = liveGain ? "Stage audio on | Room send " + String (*liveGain, 1) + " dB"
        : enabled ? String ("Stage audio | Room send bypassed")
                  : hasTrack && ! found->placed ? String ("Unplaced | Drag this track onto the stage") : String ("Stage audio off");
    stageStatus.setText (edit == nullptr ? "Create or open a project to place instruments." : ! supported()
        ? "This project uses a newer spatial state version." : statusMessage.isNotEmpty() ? statusMessage : signal, dontSendNotification);
    stageStatus.setTooltip (stageStatus.getText());
    const auto placed = (int) std::count_if (tracks.begin(), tracks.end(), [] (const auto& row) { return row.placed; });
    stageSummary.setText (tracks.empty() ? String ("Stage | Open a project to place tracks")
        : String (placed) + " placed  |  " + String ((int) tracks.size() - placed) + " unplaced", dontSendNotification);
    syncRoomAudioControls();
}

void SpatialWorkspace::syncMaps()
{
    std::vector<SpatialMap::Marker> markers;
    markers.reserve (tracks.size() + (size_t) microphones().getNumChildren());
    for (const auto& row : tracks)
        if (row.placed || view == View::microphones)
            markers.push_back ({ row.id.toString(), row.name, row.seat.position, (int) row.seat.section, false, 0, Pattern::omni });
    if (view == View::microphones)
        for (auto mic : microphones())
        {
            const auto value = readMic (mic);
            markers.push_back ({ keyOf (mic), nameOf (mic), value.position, 0, true, value.azimuth, value.pattern });
        }
    const auto seatingLayout = (Seating) jlimit (0, 2, (int) scene().getProperty ("seating", 0));
    if (view == View::microphones) micMap.display (std::move (markers), selectedMic, true, seatingLayout);
    else stageMap.display (std::move (markers), selectedTrack.toString(), false, seatingLayout);
}

void SpatialWorkspace::setSeat (te::AudioTrack& track, Seat value)
{
    auto& undo = edit->getUndoManager();
    const auto properties = writeSeat (track.itemID.toString(), value);
    auto existing = track.state.getChildWithName (seatType);
    if (! existing.isValid()) track.state.addChild (properties, -1, &undo);
    else
        for (int i = 0; i < properties.getNumProperties(); ++i)
        {
            const auto key = properties.getPropertyName (i);
            existing.setProperty (key, properties.getProperty (key), &undo);
        }
    updateRoomSend (track, value.position, value.sendTrim);
}

void SpatialWorkspace::moveTrack (const String& key, Position point, bool group)
{
    if (! editable) return;
    const auto found = std::find_if (tracks.begin(), tracks.end(), [&] (const auto& row) { return row.id.toString() == key; });
    if (found == tracks.end()) return;
    auto* selected = lookup (found->id);
    if (selected == nullptr || selected->isFrozen (te::Track::anyFreeze)) return;
    point = validPosition (point);
    group = group && found->placed;
    auto delta = Position { point.x - found->seat.position.x, point.depth - found->seat.position.depth };
    if (group)
    {
        float left = -2, right = 2, front = -1, back = 1;
        for (const auto& row : tracks)
            if (row.placed && row.seat.section == found->seat.section)
            {
                const auto p = row.seat.position;
                left = jmax (left, -1.0f - p.x); right = jmin (right, 1.0f - p.x);
                front = jmax (front, -p.depth); back = jmin (back, 1.0f - p.depth);
            }
        delta = { jlimit (left, right, delta.x), jlimit (front, back, delta.depth) };
    }
    if (found->placed && delta.x == 0.0f && delta.depth == 0.0f) return;
    begin (! found->placed ? "Place stage track" : group ? "Move stage section" : "Move stage seat");
    for (const auto& row : tracks)
        if (row.id == found->id || (group && row.placed && row.seat.section == found->seat.section))
            if (auto* track = lookup (row.id); track != nullptr && ! track->isFrozen (te::Track::anyFreeze))
            {
                auto value = row.seat;
                value.position = validPosition ({ value.position.x + delta.x, value.position.depth + delta.depth });
                setSeat (*track, value);
            }
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::placeSelectedTrack()
{
    const auto found = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; });
    if (found != tracks.end() && ! found->placed) moveTrack (found->id.toString(), found->seat.position, false);
}

void SpatialWorkspace::editSeat (SeatField field)
{
    auto* track = lookup (selectedTrack);
    const auto found = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; });
    if (! editable || track == nullptr || track->isFrozen (te::Track::anyFreeze) || found == tracks.end()) return;
    auto value = found->seat;
    switch (field)
    {
        case SeatField::family: value.section = (Section) jlimit (0, (int) sectionNames.size() - 1, sections.getSelectedId() - 1); break;
        case SeatField::pan: value.position.x = pan.get(); break;
        case SeatField::depth: value.position.depth = depth.get(); break;
        case SeatField::trim: value.sendTrim = trim.get(); break;
    }
    begin ("Edit stage seat");
    setSeat (*track, value);
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::arrangeTracks()
{
    if (! editable || tracks.empty()) return;
    auto selected = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; });
    const int scope = arrangeScope.getSelectedId();
    const auto included = [&] (const TrackRow& row)
    {
        return scope == 1 || (scope == 2 && arrangementSelection.contains (row.id))
            || (scope == 3 && selected != tracks.end() && row.seat.section == selected->seat.section);
    };
    std::array<int, sectionNames.size()> totals {}, counts {};
    for (const auto& row : tracks)
        if (included (row)) ++totals[(size_t) row.seat.section];
    if (std::all_of (totals.begin(), totals.end(), [] (int count) { return count == 0; }))
    {
        setStatus ("Select tracks in the arrangement before applying this scope.");
        return;
    }
    const auto style = (Seating) jlimit (0, 2, seating.getSelectedId() - 1);
    begin ("Auto arrange stage");
    requireScene().setProperty ("seating", (int) style, &edit->getUndoManager());
    for (const auto& row : tracks)
        if (included (row))
            if (auto* track = lookup (row.id); track != nullptr && ! track->isFrozen (te::Track::anyFreeze))
            {
                auto value = row.seat;
                const auto index = (size_t) value.section;
                value.position = seat (style, value.section, counts[index]++, totals[index]);
                setSeat (*track, value);
            }
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::toggleTrackAudio()
{
    auto* track = lookup (selectedTrack);
    const auto row = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& item) { return item.id == selectedTrack; });
    if (! routable || track == nullptr || row == tracks.end()) return;
    begin ("Change stage routing");
    const auto result = setRoomSend (*track, ! hasRoomSend (*track), row->seat.position, row->seat.sendTrim, readRoom (activeRoom()));
    statusMessage = result.getErrorMessage();
    if (result.wasOk() && ! row->placed) setSeat (*track, row->seat);
    finish();
}

#if STUDIO_SPATIAL_TESTS
void SpatialWorkspace::arrangeForInterfaceReview (Seating layout)
{
    seating.setSelectedId ((int) layout + 1, dontSendNotification);
    arrangeTracks();
}

bool SpatialWorkspace::verifyDropForInterfaceReview (te::AudioTrack& track, Position position)
{
    const auto index = std::find_if (trackListRows.begin(), trackListRows.end(), [&] (const auto& row)
    {
        return row.trackIndex >= 0 && tracks[(size_t) row.trackIndex].id == track.itemID;
    });
    if (index == trackListRows.end()) return false;
    const int row = (int) (index - trackListRows.begin());
    if (! getNameForRow (row).contains ("unplaced")) return false;
    SparseSet<int> selectedRows;
    selectedRows.addRange ({ row, row + 1 });
    const auto data = getDragSourceDescription (selectedRows);
    stageMap.itemDropped ({ data, &trackList, { 0, 0 } });
    if (track.state.getChildWithName (seatType).isValid()) return false;
    const DragAndDropTarget::SourceDetails details { data, &trackList, stageMap.screen (position).roundToInt() };
    if (! stageMap.isInterestedInDragSource (details)) return false;
    stageMap.itemDropped (details);
    const auto stored = track.state.getChildWithName (seatType);
    const auto placed = readSeat (stored).position;
    return stored.isValid() && stored["trackKey"].toString() == track.itemID.toString() && ! hasRoomSend (track)
        && selectedTrack == track.itemID && std::abs (placed.x - position.x) < 0.015f
        && std::abs (placed.depth - position.depth) < 0.015f;
}

bool SpatialWorkspace::verifyDeletedTrackForInterfaceReview (te::EditItemID id)
{
    const auto key = id.toString();
    const DragAndDropTarget::SourceDetails stale { dragSession + "|" + key, &trackList, stageMap.screen ({}).roundToInt() };
    return selectedTrack != id && ! stageMap.isInterestedInDragSource (stale)
        && std::none_of (tracks.begin(), tracks.end(), [&] (const auto& row) { return row.id == id; })
        && std::none_of (stageMap.markers.begin(), stageMap.markers.end(), [&] (const auto& marker) { return marker.key == key; });
}

bool SpatialWorkspace::verifyInspectorForInterfaceReview (te::AudioTrack& track)
{
    const auto before = readSeat (track.state.getChildWithName (seatType));
    stageMap.onSelect (track.itemID.toString());
    trim.set (-21.0f);
    trim.onChange();
    const auto after = readSeat (track.state.getChildWithName (seatType));
    const auto send = activeRoomSendDb (track);
    return before.position == after.position && before.section == after.section && after.sendTrim == -21.0f
        && send && std::abs (*send - sendDb (after.position, after.sendTrim)) < 0.0001f;
}

bool SpatialWorkspace::verifyMoveForInterfaceReview (te::AudioTrack& track)
{
    const auto read = [&] { return readSeat (track.state.getChildWithName (seatType)); };
    const auto before = read();
    const auto beforeSend = activeRoomSendDb (track);
    const auto target = validPosition ({ before.position.x + 0.13f, before.position.depth + 0.09f });
    const auto start = stageMap.screen (before.position);
    const auto end = stageMap.screen (target);
    const auto time = Time::getCurrentTime();
    const auto source = Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (Point<float> point, int flags, bool moved)
    {
        return MouseEvent (source, point, ModifierKeys (flags), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &stageMap, &stageMap, time, start, time, 1, moved);
    };
    stageMap.mouseDown (event (start, ModifierKeys::leftButtonModifier, false));
    stageMap.mouseDrag (event (end, ModifierKeys::leftButtonModifier, true));
    stageMap.mouseUp (event (end, 0, true));
    const auto after = read();
    const auto routed = [&] (Seat value)
    {
        if (! beforeSend) return ! hasRoomSend (track);
        const auto send = activeRoomSendDb (track);
        const auto panner = std::find_if (track.pluginList.begin(), track.pluginList.end(), [] (auto* plugin)
        {
            return plugin->state.hasProperty ("studioStagePan");
        });
        return send && std::abs (*send - sendDb (value.position, value.sendTrim)) < 0.0001f
            && panner != track.pluginList.end() && std::abs ((float) (*panner)->state["studioStagePan"] - value.position.x) < 0.0001f;
    };
    const auto moved = std::abs (after.position.x - target.x) < 0.0001f
        && std::abs (after.position.depth - target.depth) < 0.0001f && before != after && routed (after);
    const bool undone = edit->getUndoManager().undo() && read() == before && routed (before);
    const bool redone = edit->getUndoManager().redo() && read() == after && routed (after);
    if (! (moved && undone && redone))
        std::fprintf (stderr, "Stage drag expected %.4f, %.4f and reached %.4f, %.4f (undo %d, redo %d).\n",
                      (double) target.x, (double) target.depth, (double) after.position.x, (double) after.position.depth,
                      (int) undone, (int) redone);
    return moved && undone && redone;
}
#endif

ValueTree SpatialWorkspace::library() const { return scene().getChildWithName (libraryType); }
ValueTree SpatialWorkspace::presets() const { return scene().getChildWithName (setupsType); }
ValueTree SpatialWorkspace::microphones() const { return scene().getChildWithName (microphonesType); }
ValueTree SpatialWorkspace::activeRoom() const { return scene().getChildWithName (roomType); }
ValueTree SpatialWorkspace::selectedMicState() const { return microphones().getChildWithProperty ("key", selectedMic); }

void SpatialWorkspace::setDraft (Room next, const String& name)
{
    draft = validate (next);
    widthField.set (draft.width); lengthField.set (draft.length); heightField.set (draft.height);
    treatment.set (100 * draft.treatment); early.set (100 * draft.early); late.set (100 * draft.late); roomGain.set (draft.gain);
    preDelay.set (draft.preDelayMs);
    wall.setSelectedId ((int) draft.wall + 1, dontSendNotification);
    floor.setSelectedId ((int) draft.floor + 1, dontSendNotification);
    ceiling.setSelectedId ((int) draft.ceiling + 1, dontSendNotification);
    for (size_t i = 0; i < decay.size(); ++i) decay[i].set (draft.decayTrim[i]);
    roomName.setText (name, false);
    draftBypass = draft.bypass;
    roomGraphic.setRoom (draft);
}

Room SpatialWorkspace::readDraft() const
{
    auto next = draft;
    next.width = widthField.get(); next.length = lengthField.get(); next.height = heightField.get();
    next.treatment = treatment.get() / 100; next.early = early.get() / 100; next.late = late.get() / 100; next.gain = roomGain.get();
    next.preDelayMs = preDelay.get();
    next.wall = (Surface) (wall.getSelectedId() - 1); next.floor = (Surface) (floor.getSelectedId() - 1);
    next.ceiling = (Surface) (ceiling.getSelectedId() - 1);
    for (size_t i = 0; i < decay.size(); ++i) next.decayTrim[i] = decay[i].get();
    next.bypass = draftBypass;
    return validate (next);
}

void SpatialWorkspace::syncRooms()
{
    auto active = activeRoom();
    if (currentRoomSnapshot != active)
    {
        currentRoomSnapshot = active;
        if (! roomDraftDirty) setDraft (readRoom (active), active.isValid() ? nameOf (active) : String ("Chamber Hall"));
    }
    StringArray names;
    for (const auto child : library()) names.add (nameOf (child));
    const String signature = names.joinIntoString ("\n");
    if (roomList.getNumItems() == 0 || signature != lastRoomStamp)
    {
        const auto previous = roomList.getText();
        lastRoomStamp = signature;
        roomList.clear (dontSendNotification);
        int id = 1;
        for (const auto& preset : factoryRooms)
        {
            roomList.addSectionHeading (preset.category);
            roomList.addItem (preset.name, id++);
        }
        if (! names.isEmpty()) roomList.addSectionHeading ("Custom rooms");
        for (int i = 0; i < names.size(); ++i) roomList.addItem (names[i], firstCustomRoom + i);
        roomList.setText (previous.isNotEmpty() ? previous : roomName.getText(), dontSendNotification);
    }
    const bool custom = roomList.getSelectedId() >= firstCustomRoom;
    renameRoom.setEnabled (editable && custom);
    deleteRoom.setEnabled (editable && custom);
    saveRoom.setEnabled (editable);
    applyRoom.setEnabled (editable);
    const auto current = active.isValid() ? nameOf (active) : String ("Chamber Hall");
    roomStatus.setText (edit == nullptr ? "Open or create a project to apply a room." : ! supported()
        ? "This project uses a newer spatial state version."
        : "Active room  |  " + current, dontSendNotification);
    roomStatus.setTooltip (roomStatus.getText());
    roomDraftStatus.setText (statusMessage.isNotEmpty() ? statusMessage
        : roomDraftDirty ? "Draft room  |  Apply Room to hear these changes" : String(), dontSendNotification);
    roomDraftStatus.setColour (Label::textColourId, colours::warning);
    roomDraftStatus.setTooltip (roomDraftStatus.getText());
    applyRoom.setToggleState (roomDraftDirty, dontSendNotification);
    syncRoomAudioControls();
}

void SpatialWorkspace::syncRoomAudioControls()
{
    const auto signal = edit != nullptr ? roomSignal (*edit) : std::nullopt;
    const bool checking = signal && signal->checkEnabled;
    for (auto* button : { &stageCheck, &roomCheck })
    {
        button->setEnabled (signal && (checking || (editable && ! signal->bypassed && signal->reachesMaster)));
        button->setToggleState (checking, dontSendNotification);
        button->setButtonText (checking ? "Stop Room Check" : "Room Check +12 dB");
    }
    bypassRoom.setEnabled (editable && signal.has_value());
    micRoomBypass.setEnabled (editable && signal.has_value());
    const bool bypassed = signal && signal->bypassed;
    bypassRoom.setButtonText (bypassed ? "Enable return" : "Bypass return");
    micRoomBypass.setButtonText (bypassed ? "Enable shared room" : "Bypass shared room");
    roomMixer.setEnabled (signal.has_value());

    const auto levelText = [] (float peak)
    {
        return peak > 1.0e-4f ? String (Decibels::gainToDecibels (peak), 0) + " dBFS" : String ("silent");
    };
    String text = "Enable stage audio on a track to create the room return.";
    if (signal)
    {
        text = "Room DSP input " + levelText (signal->inputPeak) + " | Wet output " + levelText (signal->wetPeak);
        if (bypassed) text << " | Return bypassed";
        if (checking) text << " | Temporary +12 dB";
        if (! signal->reachesMaster) text << " | Return muted or off Master";
    }
    for (auto* label : { &stageSignal, &roomSignalLabel })
    {
        label->setText (text, dontSendNotification);
        label->setTooltip (text + " | Levels are measured before the return fader.");
    }
}

void SpatialWorkspace::toggleRoomCheck()
{
    if (edit == nullptr) return;
    if (auto signal = roomSignal (*edit); signal && (signal->checkEnabled || (editable && signal->reachesMaster)))
        setRoomCheck (*edit, ! signal->checkEnabled);
    syncRoomAudioControls();
}

void SpatialWorkspace::toggleRoomBypass()
{
    if (! editable || edit == nullptr) return;
    const auto signal = roomSignal (*edit);
    if (! signal) return;
    const auto active = activeRoom();
    auto settings = readRoom (active);
    settings.bypass = ! signal->bypassed;
    begin (settings.bypass ? "Bypass room return" : "Enable room return");
    auto root = requireScene();
    replaceChild (root, active, writeRoom (settings, active.isValid() ? nameOf (active) : String ("Chamber Hall")),
                  edit->getUndoManager());
    updateReturn (*edit, settings);
    draftBypass = settings.bypass;
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::apply (Room settings, const String& name)
{
    if (! editable) return;
    begin ("Apply studio room");
    auto root = requireScene();
    const auto room = name.isNotEmpty() ? name : String ("Custom room");
    replaceChild (root, activeRoom(), writeRoom (settings, room), edit->getUndoManager());
    updateReturn (*edit, settings);
    roomDraftDirty = false;
    setDraft (settings, room);
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::writeRoomPreset (bool rename, bool remove)
{
    if (! editable) return;
    const auto name = roomName.getText().trim();
    auto old = rename || remove ? library().getChild (roomList.getSelectedId() - firstCustomRoom) : ValueTree();
    if ((rename || remove) && ! old.isValid()) return;
    if (! remove && (name.isEmpty() || (namedChild (library(), name).isValid() && namedChild (library(), name) != old)))
    {
        setStatus ("Enter a unique name for the custom room.");
        return;
    }
    if (! remove && std::any_of (factoryRooms.begin(), factoryRooms.end(), [&] (const auto& r) { return name == r.name; }))
    {
        setStatus ("Enter a new name for the custom room copy.");
        return;
    }
    begin (remove ? "Delete room preset" : rename ? "Rename room preset" : "Save room preset");
    auto parent = ensureChild (requireScene(), libraryType);
    if (remove) parent.removeChild (old, &edit->getUndoManager());
    else if (rename) old.setProperty ("name", name, &edit->getUndoManager());
    else parent.addChild (writeRoom (readDraft(), name), -1, &edit->getUndoManager());
    statusMessage.clear();
    finish();
    if (! remove) roomList.setText (name, dontSendNotification);
    syncRooms();
}

void SpatialWorkspace::syncMicrophones()
{
    auto current = microphones();
    auto selected = selectedMicState();
    if (! selected.isValid())
    {
        selected = current.getChild (0);
        selectedMic = keyOf (selected);
        micName.setText (nameOf (selected), false);
    }
    StringArray signature;
    for (const auto child : current) signature.add (keyOf (child) + "|" + nameOf (child));
    for (const auto setup : presets()) signature.add ("setup|" + nameOf (setup));
    const auto next = signature.joinIntoString ("\n");
    if (next != lastMicStamp)
    {
        lastMicStamp = next;
        micList.clear (dontSendNotification);
        const auto lastSetup = setups.getText();
        setups.clear (dontSendNotification);
        micStrips.clear();
        int index = 1;
        for (const auto child : current)
        {
            micList.addItem (nameOf (child), index++);
            auto strip = std::make_unique<MicStrip> (keyOf (child));
            strip->onChange = [this] (const String& key, Mic data) { updateMic (key, data); };
            micBank.addAndMakeVisible (*strip);
            micStrips.push_back (std::move (strip));
        }
        index = 1;
        for (const auto setup : presets()) setups.addItem (nameOf (setup), index++);
        if (lastSetup.isNotEmpty()) setups.setText (lastSetup, dontSendNotification);
        resized();
    }
    if (selected.isValid())
    {
        micList.setSelectedItemIndex (current.indexOf (selected), dontSendNotification);
        const auto mic = readMic (selected);
        micX.set (mic.position.x); micDepth.set (mic.position.depth);
        micHeight.set (mic.height); azimuth.set (mic.azimuth); elevation.set (mic.elevation);
        patterns.setSelectedId ((int) mic.pattern + 1, dontSendNotification);
    }
    for (size_t i = 0; mixer && i < micStrips.size(); ++i) micStrips[i]->display (current.getChild ((int) i), editable);
    for (Component* control : std::initializer_list<Component*> { &micX, &micDepth, &micHeight, &azimuth,
          &elevation, &patterns, &renameMic, &removeMic })
        control->setEnabled (editable && selected.isValid());
    addMic.setEnabled (editable && current.getNumChildren() < maximumMics);
    addStereo.setEnabled (editable && current.getNumChildren() <= maximumMics - 2);
    recallSetup.setEnabled (editable && presets().getNumChildren() > 0);
    removeSetup.setEnabled (editable && presets().getNumChildren() > 0);
    saveSetup.setEnabled (editable && current.getNumChildren() > 0);
    if (view == View::microphones && mixer) syncRoomAudioControls();
    micStatus.setText (! supported() ? "This project uses a newer spatial state version."
        : "Visual setup only  |  Microphone capture is unavailable."
            + (statusMessage.isNotEmpty() ? " " + statusMessage : String()), dontSendNotification);
    micStatus.setTooltip (micStatus.getText());
}

void SpatialWorkspace::updateMic (const String& key, Mic data)
{
    if (! editable) return;
    auto list = microphones();
    auto old = list.getChildWithProperty ("key", key);
    if (! old.isValid() || readMic (old) == validate (data)) return;
    begin ("Edit virtual microphone");
    replaceChild (list, old, writeMic (key, nameOf (old), data), edit->getUndoManager());
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::editMic()
{
    auto state = selectedMicState();
    if (! state.isValid()) return;
    auto value = readMic (state);
    value.position = { micX.get(), micDepth.get() };
    value.height = micHeight.get(); value.azimuth = azimuth.get(); value.elevation = elevation.get();
    value.pattern = (Pattern) (patterns.getSelectedId() - 1);
    updateMic (selectedMic, value);
}

void SpatialWorkspace::createMics (bool pair)
{
    if (! editable || microphones().getNumChildren() > maximumMics - (pair ? 2 : 1)) return;
    begin (pair ? "Place virtual stereo pair" : "Add virtual microphone");
    auto list = ensureChild (requireScene(), microphonesType);
    const auto count = list.getNumChildren();
    for (int i = 0; i < (pair ? 2 : 1); ++i)
    {
        const auto key = Uuid().toString();
        Mic m;
        String name = "Mic " + String (count + i + 1);
        if (pair)
        {
            const int side = i == 0 ? -1 : 1;
            const auto configuration = stereoPairs[(size_t) jlimit (0, 3, stereo.getSelectedItemIndex())];
            m.pattern = configuration.pattern;
            // Normalised X maps to half the active room's width.
            m.position.x = (float) side * spacing.get() / readRoom (activeRoom()).width;
            m.azimuth = (float) side * pairAngle.get() * 0.5f;
            m.pan = 0.8f * (float) side;
            name += i == 0 ? " L" : " R";
        }
        list.addChild (writeMic (key, name, m), -1, &edit->getUndoManager());
        if (i == 0) selectedMic = key;
    }
    micName.setText (nameOf (selectedMicState()), false);
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::renameSelectedMic (bool remove)
{
    auto old = selectedMicState();
    if (! editable || ! old.isValid()) return;
    const auto name = micName.getText().trim();
    if (! remove && name.isEmpty()) return;
    begin (remove ? "Remove virtual microphone" : "Rename virtual microphone");
    if (remove) microphones().removeChild (old, &edit->getUndoManager());
    else old.setProperty ("name", name, &edit->getUndoManager());
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::manageSetup (bool recall, bool remove)
{
    if (! editable) return;
    auto stored = presets().getChild (setups.getSelectedItemIndex());
    if (recall || remove)
    {
        if (! stored.isValid()) { setStatus ("Select a saved microphone setup."); return; }
        begin (recall ? "Recall mic setup" : "Delete mic setup");
        if (recall)
        {
            auto copy = stored.getChildWithName (microphonesType).createCopy();
            if (copy.isValid()) replaceChild (requireScene(), microphones(), copy, edit->getUndoManager());
        }
        else presets().removeChild (stored, &edit->getUndoManager());
    }
    else
    {
        const auto name = setupName.getText().trim();
        if (name.isEmpty() || namedChild (presets(), name).isValid())
        { setStatus ("Enter a unique name for the microphone setup."); return; }
        if (! microphones().isValid()) return;
        begin ("Save mic setup");
        auto setup = ValueTree (setupType).setProperty ("name", name, nullptr);
        setup.addChild (microphones().createCopy(), -1, nullptr);
        ensureChild (requireScene(), setupsType).addChild (setup, -1, &edit->getUndoManager());
    }
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::layoutChanged()
{
    syncMicrophones();
    resized();
    if (onLayoutChange) onLayoutChange();
}

void SpatialWorkspace::appearanceChanged()
{
    trackList.setRowHeight (appearance.line (12, 3) + appearance.line (10, 3));
    rosterTitle.setFont (FontOptions (13.0f, Font::bold));
    selectedName.setFont (FontOptions (13.0f, Font::bold));
    for (auto* editBox : { &roomName, &micName, &setupName })
    {
        editBox->setFont (uiFont (13.0f));
        editBox->applyFontToAllText (uiFont (13.0f));
    }
    micBankViewport.setScrollBarThickness (appearance.scrollbar());
    resized();
    repaint();
}

int SpatialWorkspace::minimumContentHeight (int width)
{
    return layoutWorkspace (width, false, 0);
}

void SpatialWorkspace::paint (Graphics& g)
{
    if (view != View::stage || compactStage) return;
    for (auto* panel : { &stageRoster, &stageInspector })
    {
        const auto bounds = panel->getBounds().toFloat();
        g.setColour (colours::canvas);
        g.fillRoundedRectangle (bounds, (float) appearance.px (6));
        g.setColour (colours::divider);
        g.drawRoundedRectangle (bounds.reduced (0.5f), (float) appearance.px (6), 1.0f);
    }
}

void SpatialWorkspace::resized()
{
    const auto bounds = getLocalBounds();
    for (auto* page : std::array<Component*, 3> { &stagePage, &roomPage, &micPage }) page->setBounds (bounds);
    const int extra = jmax (0, bounds.getHeight() - minimumContentHeight (bounds.getWidth()));
    layoutWorkspace (bounds.getWidth(), true, extra);
}

int SpatialWorkspace::layoutWorkspace (int width, bool bounds, int extra)
{
    const auto& m = appearance;
    Rectangle<int> area (m.px (6), m.px (5), jmax (1, width - m.px (12)), 32768);
    const int start = area.getY();
    const auto toolbar = [&] (Rectangle<int>& region, std::initializer_list<ToolbarItem> items)
    {
        if (bounds) layoutToolbar (region, items);
        else region.removeFromTop (toolbarHeight (region.getWidth(), items));
    };
    const auto place = [&] (Rectangle<int>& region, Component& item, int height)
    {
        auto itemBounds = region.removeFromTop (height);
        if (bounds) item.setBounds (itemBounds);
    };
    const auto fields = [&] (Rectangle<int>& region, std::initializer_list<SpatialField*> values)
    {
        if (bounds) layoutFields (region, values);
        else region.removeFromTop (fieldRows (region.getWidth(), (int) values.size()) * fieldHeight());
    };
    const auto mapAndInspector = [&] (SpatialMap& map, const auto& inspector)
    {
        if (sideBySide (area.getWidth()))
        {
            const int sidebar = m.span (292);
            auto dummy = area.withWidth (sidebar - m.px (5));
            inspector (dummy, false);
            const int inspectorHeight = dummy.getY() - area.getY();
            auto row = area.removeFromTop (jmax (mapHeight(), inspectorHeight) + extra);
            auto column = row.removeFromRight (sidebar).withTrimmedLeft (m.px (5));
            if (bounds) map.setBounds (row);
            inspector (column, bounds);
        }
        else
        {
            place (area, map, mapHeight() + extra);
            area.removeFromTop (m.px (5));
            inspector (area, bounds);
        }
    };
    if (view == View::stage)
    {
        if (compactStage)
        {
            if (bounds)
            {
                auto row = getLocalBounds().reduced (m.px (8), 1);
                stageMap.setBounds (row.removeFromLeft (jmin (row.getWidth() / 4, m.px (130))));
                openStage.setBounds (row.removeFromRight (m.span (116)).reduced (m.px (2), 0));
                stageSummary.setBounds (row.withTrimmedLeft (m.px (7)));
            }
            return 1;
        }
        const auto controls = { ToolbarItem { &seating, 163 }, ToolbarItem { &arrangeScope, 137 },
                                ToolbarItem { &arrange, 110 }, ToolbarItem { &stageCheck, 150 }
                               #if JUCE_MAC
                                , ToolbarItem { &seatingDesigner, 174 }
                               #endif
                              };
        const auto top = area.removeFromTop (toolbarHeight (area.getWidth(), controls));
        if (bounds)
        {
            stageToolbar.setBounds (top);
            auto row = stageToolbar.getLocalBounds();
            layoutToolbar (row, controls);
        }
        const int gap = m.px (6);
        area.removeFromTop (gap);
        const int inspectorWidth = m.span (246);
        const int rosterWidth = m.span (196);
        const bool columns = area.getWidth() >= m.span (804);
        const bool withRoster = columns || area.getWidth() >= m.span (510);
        const auto layoutInspector = [&] (Rectangle<int> region, bool assign)
        {
            const int startY = region.getY();
            region = region.withTrimmedTop (gap).reduced (gap, 0);
            const auto put = [&] (Component& item, int h)
            {
                auto field = region.removeFromTop (h);
                if (assign) item.setBounds (field);
            };
            put (selectedName, m.line (13, 6));
            put (sections, m.control (26));
            region.removeFromTop (gap);
            auto axes = region.removeFromTop (fieldHeight());
            if (assign)
            {
                pan.setBounds (axes.removeFromLeft (axes.getWidth() / 2));
                depth.setBounds (axes);
            }
            put (trim, fieldHeight());
            auto buttons = region.removeFromTop (m.control (26));
            if (assign)
            {
                placeSelected.setBounds (buttons.removeFromLeft (buttons.getWidth() * 57 / 100).withTrimmedRight (m.px (2)));
                resetSeat.setBounds (buttons);
            }
            region.removeFromTop (m.px (3));
            put (toggleSend, m.control (26));
            put (stageStatus, 2 * m.line (11, 1));
            put (stageSignal, 2 * m.line (11, 1));
            return region.getY() - startY + gap;
        };
        const int inspectionHeight = layoutInspector (area.withWidth (columns ? inspectorWidth : area.getWidth()), false);
        auto row = area.removeFromTop ((columns ? jmax (mapHeight(), inspectionHeight) : mapHeight()) + extra);
        const auto layoutRoster = [&] (Rectangle<int> sidebar)
        {
            if (! bounds) return;
            stageRoster.setBounds (sidebar);
            auto inside = stageRoster.getLocalBounds().reduced (gap);
            rosterTitle.setBounds (inside.removeFromTop (m.line (13, 5)));
            trackList.setBounds (inside);
        };
        if (withRoster) layoutRoster (row.removeFromLeft (rosterWidth + gap).withTrimmedRight (gap));
        if (columns)
        {
            const auto sidebar = row.removeFromRight (inspectorWidth + gap).withTrimmedLeft (gap);
            if (bounds) { stageInspector.setBounds (sidebar); layoutInspector (stageInspector.getLocalBounds(), true); }
        }
        if (bounds) stageMap.setBounds (row);
        if (! withRoster)
        {
            area.removeFromTop (gap);
            layoutRoster (area.removeFromTop (m.px (132)));
        }
        if (! columns)
        {
            area.removeFromTop (gap);
            auto sidebar = area.removeFromTop (inspectionHeight);
            if (bounds) { stageInspector.setBounds (sidebar); layoutInspector (stageInspector.getLocalBounds(), true); }
        }
    }
    else if (view == View::rooms)
    {
        toolbar (area, { { &roomList, 188 }, { &roomName, 162 }, { &applyRoom, 110 },
            { &saveRoom, 91 }, { &renameRoom, 76 }, { &deleteRoom, 114 } });
        place (area, roomStatus, m.line (12, 1));
        place (area, roomDraftStatus, m.line (12, 1));
        toolbar (area, { { &bypassRoom, 133 }, { &roomCheck, 161 }, { &roomMixer, 182 } });
        place (area, roomSignalLabel, 2 * m.line (12, 1));
        place (area, roomGraphic, m.px (112) + extra);
        fields (area, { &widthField, &lengthField, &heightField, &treatment });
        toolbar (area, { { &wall, 150 }, { &floor, 150 }, { &ceiling, 153 } });
        fields (area, { &decay[0], &decay[1], &decay[2], &decay[3], &decay[4], &decay[5] });
        fields (area, { &early, &late, &preDelay, &roomGain });
        toolbar (area, { { &resetRoom, 122 } });
    }
    else
    {
        toolbar (area, { { &placement, 106 }, { &micMixer, 106 } });
        place (area, micStatus, 2 * m.line (12, 1));
        toolbar (area, { { &setups, 137 }, { &setupName, 140 }, { &saveSetup, 105 }, { &recallSetup, 112 }, { &removeSetup, 108 } });
        if (mixer)
        {
            toolbar (area, { { &micRoomBypass, 177 } });
            place (area, micBankViewport, MicStrip::height() + m.scrollbar() + extra);
            if (bounds)
            {
                const int strip = m.span (155);
                micBank.setSize (jmax (1, (int) micStrips.size() * strip), MicStrip::height());
                for (size_t i = 0; i < micStrips.size(); ++i)
                    micStrips[i]->setBounds ((int) i * strip, 0, strip, MicStrip::height());
            }
        }
        else
        {
            mapAndInspector (micMap, [&] (Rectangle<int>& region, bool assign)
            {
                const auto items = { ToolbarItem { &micList, 142 }, ToolbarItem { &micName, 133 }, ToolbarItem { &addMic, 76 },
                    ToolbarItem { &renameMic, 75 }, ToolbarItem { &removeMic, 104 }, ToolbarItem { &patterns, 145 } };
                if (assign) layoutToolbar (region, items);
                else region.removeFromTop (toolbarHeight (region.getWidth(), items));
                if (assign) layoutFields (region, { &micX, &micDepth, &micHeight, &azimuth, &elevation });
                else region.removeFromTop (fieldRows (region.getWidth(), 5) * fieldHeight());
            });
            toolbar (area, { { &stereo, 184 }, { &addStereo, 145 } });
            fields (area, { &spacing, &pairAngle });
        }
        if (bounds)
        {
            placement.setToggleState (! mixer, dontSendNotification);
            micMixer.setToggleState (mixer, dontSendNotification);
            for (Component* item : std::initializer_list<Component*> { &micMap, &micList, &micName, &addMic,
                 &renameMic, &removeMic, &patterns, &micX, &micDepth, &micHeight, &azimuth, &elevation, &stereo,
                 &addStereo, &spacing, &pairAngle })
                item->setVisible (! mixer);
            for (Component* item : std::initializer_list<Component*> { &micRoomBypass, &micBankViewport })
                item->setVisible (mixer);
        }
    }

    return area.getY() - start + m.px (10);
}
}
