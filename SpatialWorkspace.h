// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SpatialState.h"
#include <functional>

namespace studio::spatial
{
class SpatialField final : public juce::Component
{
public:
    SpatialField (const char* title, double min, double max, double interval, const char* suffix = "");
    std::function<void()> onChange;
    void set (double);
    float get() const;
    void resized() override;
private:
    juce::Label label;
    juce::Slider number { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
};

class SpatialMap final : public juce::Component
{
public:
    struct Marker
    {
        juce::String key, name;
        Position point;
        int section = 0;
        bool microphone = false;
        float aim = 0.0f;
        Pattern pattern = Pattern::cardioid;
        bool operator== (const Marker&) const = default;
    };
    std::function<void(const juce::String&)> onSelect;
    std::function<void(const juce::String&, Position, bool wholeSection)> onMove;
    void display (std::vector<Marker>, juce::String selected, bool microphones);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
private:
    std::vector<Marker> markers;
    juce::String selected, dragged;
    Position preview, dragOrigin;
    bool micMode = false, moving = false;
    juce::Rectangle<float> plot() const;
    juce::Point<float> screen (Position) const;
    Position location (juce::Point<float>) const;
};

class RoomGraphic final : public juce::Component
{
public:
    void setRoom (Room);
    void paint (juce::Graphics&) override;
private:
    Room value;
};

class MicStrip;

class SpatialWorkspace final : public juce::Component
{
public:
    enum class View { stage, rooms, microphones };
    SpatialWorkspace();
    ~SpatialWorkspace() override;
    std::function<void(tracktion::EditItemID)> onSelectTrack;
    std::function<void()> onLayoutChange;
    void setEdit (tracktion::Edit*);
    void setView (View);
    void refresh (const juce::Array<tracktion::AudioTrack*>&, const juce::Array<tracktion::AudioTrack*>& selected,
                  bool canEdit, bool canRoute);
    void appearanceChanged();
    int minimumContentHeight (int width);
    void resized() override;

private:
    tracktion::Edit* edit = nullptr;
    View view = View::stage;
    bool editable = false, routable = false, mixer = false, roomDraftDirty = false;
    tracktion::EditItemID selectedTrack, lastArrangementSelection;
    int appliedSeating = -1;
    juce::Array<tracktion::EditItemID> arrangementSelection;
    struct TrackRow
    {
        tracktion::EditItemID id;
        juce::String name;
        Seat seat;
        bool placed;
        bool operator== (const TrackRow&) const = default;
    };
    std::vector<TrackRow> tracks;
    juce::String selectedMic, statusMessage, lastRoomStamp, lastMicStamp;
    juce::Component stagePage, roomPage, micPage;
    juce::Label stageStatus, roomStatus, micStatus;
    SpatialMap stageMap, micMap;
    RoomGraphic roomGraphic;

    juce::ComboBox seating, arrangeScope, trackList, sections;
    juce::TextButton arrange { "Auto Arrange" }, resetSeat { "Reset seat" }, toggleSend { "Enable stage audio" };
    SpatialField pan { "Stage pan", -1, 1, 0.01 }, depth { "Stage depth", 0, 1, 0.01 },
                 trim { "Send trim", -60, 0, 1, " dB" };

    juce::ComboBox roomList, wall, floor, ceiling;
    juce::TextEditor roomName;
    juce::TextButton applyRoom { "Apply Room" }, saveRoom { "Save copy" }, renameRoom { "Rename" },
                     deleteRoom { "Delete custom" }, bypassRoom { "Bypass return" }, resetRoom { "Reset return" };
    SpatialField widthField { "Width", 3, 80, 0.1, " m" }, lengthField { "Length", 3, 80, 0.1, " m" },
                 heightField { "Ceiling height", 2.2, 35, 0.1, " m" }, treatment { "Treatment", 0, 100, 1, "%" },
                 early { "Early reflections", 0, 100, 1, "%" }, late { "Late reverb", 0, 100, 1, "%" },
                 roomGain { "Room return", -60, 0, 1, " dB" };
    std::array<SpatialField, 6> decay;
    Room draft;
    juce::ValueTree currentRoomSnapshot;
    bool draftBypass = false;

    juce::ComboBox micList, patterns, stereo, setups;
    juce::TextEditor micName, setupName;
    juce::TextButton placement { "Placement" }, micMixer { "Mic Mixer" }, addMic { "Add mic" }, removeMic { "Remove mic" },
                     addStereo { "Place stereo pair" }, renameMic { "Rename" }, saveSetup { "Save setup" },
                     recallSetup { "Recall setup" }, removeSetup { "Delete setup" }, audition { "Mic Audition unavailable" },
                     micRoomBypass { "Bypass shared room" };
    SpatialField micX { "Left / right", -1, 1, 0.01 }, micDepth { "Front / rear", 0, 1, 0.01 },
                 micHeight { "Capsule height", 0.25, 20, 0.05, " m" }, azimuth { "Azimuth", -180, 180, 1, " deg" },
                 elevation { "Elevation", -90, 90, 1, " deg" }, spacing { "Stereo spacing", 0, 10, 0.01, " m" },
                 pairAngle { "Stereo angle", 0, 180, 1, " deg" };
    juce::Viewport micBankViewport;
    juce::Component micBank;
    std::vector<std::unique_ptr<MicStrip>> micStrips;

    juce::ValueTree scene() const;
    juce::ValueTree ensureChild (juce::ValueTree, const juce::Identifier&);
    juce::ValueTree requireScene();
    bool supported() const;
    void begin (const char* action);
    void finish();
    void setStatus (const juce::String&);
    tracktion::AudioTrack* lookup (tracktion::EditItemID) const;
    void updateTracks (const juce::Array<tracktion::AudioTrack*>&);
    void syncStage();
    void syncMaps();
    void moveTrack (const juce::String&, Position, bool);
    void setSeat (tracktion::AudioTrack&, Seat);
    void editSeat();
    void arrangeTracks();
    void toggleTrackAudio();
    juce::ValueTree library() const;
    juce::ValueTree presets() const;
    juce::ValueTree microphones() const;
    juce::ValueTree activeRoom() const;
    juce::ValueTree selectedMicState() const;
    void syncRooms();
    void setDraft (Room, const juce::String&);
    Room readDraft() const;
    void apply (Room, const juce::String&);
    void writeRoomPreset (bool rename, bool remove);
    void syncMicrophones();
    void updateMic (const juce::String& key, Mic);
    void editMic();
    void createMics (bool pair);
    void renameSelectedMic (bool remove);
    void manageSetup (bool recall, bool remove);
    void layoutChanged();
    int layoutWorkspace (int width, bool setBounds, int extra);
};
}
