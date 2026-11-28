// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SpatialState.h"
#include <functional>

namespace studio::spatial
{
class SpatialField : public juce::Component
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

class SpatialMap final : public juce::Component, public juce::SettableTooltipClient,
                         public juce::DragAndDropTarget
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
        bool reference = false;
        bool operator== (const Marker&) const = default;
    };
    std::function<void(const juce::String&)> onSelect;
    std::function<void(const juce::String&, Position, bool wholeSection)> onMove;
    std::function<bool(const juce::String&)> onBeginLiveMove;
    std::function<void()> onEndLiveMove;
    std::function<juce::String(const SourceDetails&)> trackFromDrag;
    void setOverview (bool);
    void setZoomed (bool value) { zoomed = value; repaint(); }
    bool isZoomed() const { return zoomed; }
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void display (std::vector<Marker>, juce::String selected, bool microphones, Seating, bool physical = false);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
private:
    friend class SpatialWorkspace;
    std::vector<Marker> markers;
    juce::String selected, dragged;
    Position preview, dragOrigin;
    bool micMode = false, moving = false, liveMove = false, overview = false, dropTarget = false;
    bool zoomed = false, physicalStage = false;
    bool showTrackLabels = true; // Presentation-only; Stage coordinates and DSP are unchanged.
    Seating seating = Seating::concert;
    juce::Rectangle<float> plot() const;
    int legendHeight() const;
    void paintSeating (juce::Graphics&, juce::Rectangle<float>) const;
    void paintSemicircle (juce::Graphics&, juce::Rectangle<float>) const;
    bool containsStagePoint (juce::Point<float>) const;
    juce::Point<float> screen (Position, bool receiver = false) const;
    juce::Rectangle<float> physicalStageArea() const;
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

class SpatialWorkspace final : public juce::Component, private juce::ListBoxModel
{
public:
    enum class View { stage, rooms, microphones };
    SpatialWorkspace();
    ~SpatialWorkspace() override;
    std::function<void(tracktion::EditItemID)> onSelectTrack, onOpenRoomMixer;
    std::function<void()> onLayoutChange, onExpandStage;
    void setFloatAction (std::function<void()>);
   #if JUCE_MAC
    std::function<void()> onOpenSeating;
   #endif
    void setEdit (tracktion::Edit*);
    void setView (View);
    void setCompactStage (bool);
    void refresh (const juce::Array<tracktion::AudioTrack*>&, const juce::Array<tracktion::AudioTrack*>& selected,
                  bool canEdit, bool canRoute);
    void appearanceChanged();
    int minimumContentHeight (int width);
    void paint (juce::Graphics&) override;
    void resized() override;
   #if STUDIO_SPATIAL_TESTS
    void arrangeForInterfaceReview (Seating);
    bool verifyMoveForInterfaceReview (tracktion::AudioTrack&);
    bool verifyInspectorForInterfaceReview (tracktion::AudioTrack&);
    bool verifyDropForInterfaceReview (tracktion::AudioTrack&, Position);
    bool verifyDeletedTrackForInterfaceReview (tracktion::EditItemID);
   #endif

private:
    tracktion::Edit* edit = nullptr;
    View view = View::stage;
    bool editable = false, routable = false, mixer = false, roomDraftDirty = false, compactStage = false;
    tracktion::EditItemID selectedTrack, lastArrangementSelection;
    int appliedSeating = -1;
    bool liveSeatGesture = false;
    juce::Array<tracktion::EditItemID> arrangementSelection;
    struct TrackRow
    {
        tracktion::EditItemID id;
        juce::String name, instrument;
        Seat seat;
        bool placed;
        bool stageAudio;
        bool operator== (const TrackRow&) const = default;
    };
    std::vector<TrackRow> tracks;
    struct TrackListRow { Section section; int trackIndex; };
    std::vector<TrackListRow> trackListRows;
    juce::String dragSession;
    bool refreshingTrackList = false;
    int getNumRows() override;
    juce::String getNameForRow (int) override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;
    void returnKeyPressed (int) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    juce::String selectedMic, statusMessage, lastRoomStamp, lastMicStamp;
    struct StagePage final : juce::Component, juce::DragAndDropContainer {} stagePage;
    juce::Component roomPage, micPage, stageRoster, stageInspector, stageToolbar;
    juce::Label stageStatus, roomStatus, roomDraftStatus, micStatus, stageSignal, roomSignalLabel;
    juce::Label rosterTitle, selectedName, stageSummary;
    SpatialMap stageMap, micMap;
    RoomGraphic roomGraphic;

    juce::TextButton floatStage { "Float Stage" };
    juce::ComboBox seating, arrangeScope, sections;
    juce::ListBox trackList { "Project tracks", this };
   #if JUCE_MAC
    juce::TextButton seatingDesigner { "Arrangement Reference" };
   #endif
    juce::TextButton arrange { "Auto Arrange" }, resetSeat { "Reset seat" }, toggleSend { "Enable stage audio" },
                     stageCheck { "Room Check +12 dB" }, stageOptions { "Stage controls" }, sourceOptions { "Source controls" },
                     atlasButton { "Atlas & layouts" }, placeSelected { "Place on Stage" }, openStage { "Open Stage" };
    SpatialField pan { "Stage pan", -1, 1, 0.01 }, depth { "Stage depth", 0, 1, 0.01 },
                 trim { "Send trim", -60, 0, 1, " dB" };

    juce::ComboBox roomList, wall, floor, ceiling;
    juce::TextEditor roomName;
    juce::TextButton applyRoom { "Apply Room" }, saveRoom { "Save copy" }, renameRoom { "Rename" },
                     deleteRoom { "Delete custom" }, bypassRoom { "Bypass return" }, resetRoom { "Reset draft" },
                     roomCheck { "Room Check +12 dB" }, roomOptions { "Advanced reverb" }, roomMixer { "Show return in Mixer" };
    SpatialField widthField { "Width", 3, 80, 0.1, " m" }, lengthField { "Length", 3, 80, 0.1, " m" },
                 heightField { "Ceiling height", 2.2, 35, 0.1, " m" }, treatment { "Treatment", 0, 100, 1, "%" },
                 early { "Early reflections", 0, 100, 1, "%" }, late { "Late reverb", 0, 100, 1, "%" },
                 roomGain { "Room return", -60, 0, 1, " dB" }, preDelay { "Pre-delay", 0, 100, 1, " ms" };
    std::array<SpatialField, 6> decay;
    Room draft;
    juce::ValueTree currentRoomSnapshot;
    bool draftBypass = false;

    juce::ComboBox micList, patterns, stereo, setups;
    juce::TextEditor micName, setupName;
    juce::TextButton placement { "Placement" }, micMixer { "Mic Mixer" }, addMic { "Add mic" }, removeMic { "Remove mic" },
                     addStereo { "Place stereo pair" }, renameMic { "Rename" }, saveSetup { "Save setup" },
                     recallSetup { "Recall setup" }, removeSetup { "Delete setup" },
                     micRoomBypass { "Bypass shared room" }, monitorMics { "Monitor microphones" };
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
    void placeSelectedTrack();
    void setSeat (tracktion::AudioTrack&, Seat);
    enum class SeatField { family, pan, depth, trim };
    void editSeat (SeatField);
    void arrangeTracks();
    void toggleTrackAudio();
    void showStageOptions();
    void showSourceOptions();
    void showRoomOptions();
    void showAtlas();
    void manageLayout (bool recall);
    juce::ValueTree library() const;
    juce::ValueTree presets() const;
    juce::ValueTree microphones() const;
    juce::ValueTree activeRoom() const;
    juce::ValueTree selectedMicState() const;
    void syncRooms();
    void syncRoomAudioControls();
    void toggleRoomCheck();
    void toggleRoomBypass();
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

class StageWindow final : public juce::DocumentWindow
{
public:
    StageWindow();
    SpatialWorkspace& workspace();
    void appearanceChanged();
    void closeButtonPressed() override { setVisible (false); }
private:
    class Content;
};

}
