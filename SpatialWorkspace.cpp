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
    if (contains ({ "flute", "oboe", "clarinet", "bassoon", "woodwind", "piccolo" })) return Section::woodwinds;
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
    add (stagePage, stageStatus, stageSignal, stageCheck, stageMap, seating, arrangeScope, trackList, sections,
         arrange, resetSeat, toggleSend, pan, depth, trim);
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
    for (auto* status : { &stageStatus, &roomStatus, &roomDraftStatus, &micStatus, &stageSignal, &roomSignalLabel })
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
    trackList.setTitle ("Stage track");
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
    trackList.onChange = [this]
    {
        const int i = trackList.getSelectedItemIndex();
        if (isPositiveAndBelow (i, (int) tracks.size())) stageMap.onSelect (tracks[(size_t) i].id.toString());
    };
    arrange.onClick = [this] { arrangeTracks(); };
    stageMap.setDescription ("Drag a seat or use arrow keys. Shift moves its section. Right-click overlapping seats to choose one.");
    resetSeat.onClick = [this]
    {
        if (auto* track = lookup (selectedTrack); editable && track != nullptr)
        {
            auto value = readSeat (track->state.getChildWithName (seatType), inferSection (*track));
            value.position = {};
            begin ("Reset seat");
            setSeat (*track, value);
            finish();
        }
    };
    toggleSend.onClick = [this] { toggleTrackAudio(); };
    pan.onChange = depth.onChange = trim.onChange = sections.onChange = [this] { editSeat(); };

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

SpatialWorkspace::~SpatialWorkspace() { micBankViewport.setViewedComponent (nullptr); }

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
    appliedSeating = -1;
    trackList.clear (dontSendNotification);
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
            next.push_back ({ track->itemID, track->getName(), data, v.isValid() });
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
            { return a.id == b.id && a.name == b.name; });
    if (listChanged)
    {
        trackList.clear (dontSendNotification);
        int id = 1;
        for (const auto& row : next) trackList.addItem (row.name, id++);
    }
    tracks = std::move (next);
    if (std::none_of (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; }))
        selectedTrack = tracks.empty() ? te::EditItemID() : tracks.front().id;
}

void SpatialWorkspace::syncStage()
{
    auto found = std::find_if (tracks.begin(), tracks.end(), [this] (const auto& row) { return row.id == selectedTrack; });
    auto* track = lookup (selectedTrack);
    const bool hasTrack = found != tracks.end() && track != nullptr;
    const bool enabled = hasTrack && hasRoomSend (*track);
    trackList.setSelectedItemIndex (hasTrack ? (int) (found - tracks.begin()) : -1, dontSendNotification);
    for (Component* component : std::initializer_list<Component*> { &sections, &pan, &depth, &trim, &resetSeat })
        component->setEnabled (editable && hasTrack);
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
    const auto signal = liveGain ? "Stage audio | Effective send " + String (*liveGain, 1) + " dB"
        : enabled ? String ("Stage audio | Room send bypassed")
                  : String ("Placement only | Enable stage audio to hear pan and room send");
    stageStatus.setText (edit == nullptr ? "Create or open a project to place instruments." : ! supported()
        ? "This project uses a newer spatial state version." : statusMessage.isNotEmpty() ? statusMessage : signal, dontSendNotification);
    stageStatus.setTooltip (stageStatus.getText());
    syncRoomAudioControls();
}

void SpatialWorkspace::syncMaps()
{
    std::vector<SpatialMap::Marker> markers;
    markers.reserve (tracks.size() + (size_t) microphones().getNumChildren());
    for (const auto& row : tracks)
        markers.push_back ({ row.id.toString(), row.name, row.seat.position, (int) row.seat.section, false, 0, Pattern::omni });
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
    replaceChild (track.state, track.state.getChildWithName (seatType),
        writeSeat (track.itemID.toString(), value), edit->getUndoManager());
    updateRoomSend (track, value.position, value.sendTrim);
}

void SpatialWorkspace::moveTrack (const String& key, Position point, bool group)
{
    if (! editable) return;
    const auto found = std::find_if (tracks.begin(), tracks.end(), [&] (const auto& row) { return row.id.toString() == key; });
    if (found == tracks.end()) return;
    auto delta = Position { point.x - found->seat.position.x, point.depth - found->seat.position.depth };
    if (group)
    {
        float left = -2, right = 2, front = -1, back = 1;
        for (const auto& row : tracks)
            if (row.seat.section == found->seat.section)
            {
                const auto p = row.seat.position;
                left = jmax (left, -1.0f - p.x); right = jmin (right, 1.0f - p.x);
                front = jmax (front, -p.depth); back = jmin (back, 1.0f - p.depth);
            }
        delta = { jlimit (left, right, delta.x), jlimit (front, back, delta.depth) };
    }
    if (delta.x == 0.0f && delta.depth == 0.0f) return;
    begin (group ? "Move stage section" : "Move stage seat");
    for (const auto& row : tracks)
        if (row.id == found->id || (group && row.seat.section == found->seat.section))
            if (auto* track = lookup (row.id))
            {
                auto value = row.seat;
                value.position = validPosition ({ value.position.x + delta.x, value.position.depth + delta.depth });
                setSeat (*track, value);
            }
    statusMessage.clear();
    finish();
}

void SpatialWorkspace::editSeat()
{
    auto* track = lookup (selectedTrack);
    if (! editable || track == nullptr || sections.getSelectedId() <= 0) return;
    begin ("Edit stage seat");
    setSeat (*track, { { pan.get(), depth.get() }, (Section) (sections.getSelectedId() - 1), trim.get() });
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
            if (auto* track = lookup (row.id))
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

bool SpatialWorkspace::verifyMoveForInterfaceReview (te::AudioTrack& track)
{
    const auto read = [&] { return readSeat (track.state.getChildWithName (seatType)); };
    const auto before = read();
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
    const auto moved = std::abs (after.position.x - target.x) < 0.0001f
        && std::abs (after.position.depth - target.depth) < 0.0001f && before != after;
    const bool undone = edit->getUndoManager().undo() && read() == before;
    const bool redone = edit->getUndoManager().redo() && read() == after;
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
        : "Active room · " + current, dontSendNotification);
    roomStatus.setTooltip (roomStatus.getText());
    roomDraftStatus.setText (statusMessage.isNotEmpty() ? statusMessage
        : roomDraftDirty ? "Draft room · Apply Room to hear these changes" : String(), dontSendNotification);
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
        : "Visual setup only · Microphone capture is unavailable."
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

void SpatialWorkspace::resized()
{
    const auto bounds = getLocalBounds();
    for (auto* page : { &stagePage, &roomPage, &micPage }) page->setBounds (bounds);
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
        toolbar (area, { { &seating, 180 }, { &arrangeScope, 155 }, { &arrange, 120 }, { &stageCheck, 161 } });
        place (area, stageStatus, 2 * m.line (12, 1));
        place (area, stageSignal, 2 * m.line (12, 1));
        mapAndInspector (stageMap, [&] (Rectangle<int>& region, bool assign)
        {
            const auto items = { ToolbarItem { &trackList, 147 }, ToolbarItem { &sections, 130 },
                                 ToolbarItem { &resetSeat, 96 }, ToolbarItem { &toggleSend, 171 } };
            if (assign) layoutToolbar (region, items);
            else region.removeFromTop (toolbarHeight (region.getWidth(), items));
            if (assign) layoutFields (region, { &pan, &depth, &trim });
            else region.removeFromTop (fieldRows (region.getWidth(), 3) * fieldHeight());
        });
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
