cmake_minimum_required(VERSION 3.22)
project(Studio VERSION 0.2.0 LANGUAGES C CXX)

set(TRACKTION_ROOT "" CACHE PATH "Existing Tracktion Engine checkout with JUCE")
if(NOT EXISTS "${TRACKTION_ROOT}/modules/juce/CMakeLists.txt")
    message(FATAL_ERROR "Set TRACKTION_ROOT to your Tracktion Engine checkout with JUCE initialized.")
endif()

set(TE_ADD_EXAMPLES OFF CACHE BOOL "Build Tracktion examples" FORCE)
add_subdirectory("${TRACKTION_ROOT}" tracktion)

# Keep Studio's changes to the example editor in the build directory.
set(common "${TRACKTION_ROOT}/examples/common")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/RegionEditing.inc" "${CMAKE_CURRENT_SOURCE_DIR}/ArrangementLasso.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${common}/Components.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${common}/Components.h")
file(READ "${common}/Components.h" editor_header)
string(REPLACE "#pragma once" "#pragma once\n#include <functional>\n#include <utility>\n#include <vector>"
               editor_header "${editor_header}")
# The upstream example uses four spaces on some visually empty lines.
# Normalize those in the generated copy to keep the anchor legible.
string(REPLACE "    \n" "\n" editor_header "${editor_header}")
set(clip_header_anchor [=[    void mouseDown (const MouseEvent& e) override;

    te::Clip& getClip() { return *clip; }

protected:
    EditViewState& editViewState;
    te::Clip::Ptr clip;
};]=])
string(FIND "${editor_header}" "${clip_header_anchor}" clip_header_match)
if(clip_header_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example ClipComponent header has changed.")
endif()
string(REPLACE "${clip_header_anchor}" [=[    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseExit (const MouseEvent& e) override;

    te::Clip& getClip() { return *clip; }

protected:
    EditViewState& editViewState;
    te::Clip::Ptr clip;

private:
    enum class RegionGesture { none, move, trimLeft, trimRight };
    RegionGesture regionGesture = RegionGesture::none;
    std::vector<std::pair<te::Clip::Ptr, te::ClipPosition>> regionDragClips;
    std::unique_ptr<Component> regionDragPreview;
    bool regionDragStarted = false;
};]=] editor_header "${editor_header}")
set(track_header_anchor [=[    void resized() override;

private:
    void changeListenerCallback (ChangeBroadcaster*) override;]=])
string(FIND "${editor_header}" "${track_header_anchor}" track_header_match)
if(track_header_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example TrackComponent header has changed.")
endif()
string(REPLACE "${track_header_anchor}" [=[    void resized() override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    te::Track* getTrack() const { return track.get(); }

private:
    std::unique_ptr<Component> lasso;
    void changeListenerCallback (ChangeBroadcaster*) override;]=] editor_header "${editor_header}")
set(edit_view_accessor "    EditViewState& getEditViewState()   { return editViewState; }")
string(FIND "${editor_header}" "${edit_view_accessor}" edit_view_match)
if(edit_view_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example EditComponent accessor has changed.")
endif()
string(REPLACE "${edit_view_accessor}"
    "${edit_view_accessor}\n    int getVisibleTrackCount() const { return tracks.size(); }\n    void refreshStudioLayout() { resized(); repaint(); }"
    editor_header "${editor_header}")

# The track header is Studio's only per-track control area. The pinned
# example editor supplies the timeline and clips; callbacks stay in StudioView
# so effects, ARIA safety and destructive menus retain their existing checks.
set(studio_view_callbacks_anchor [=[    ValueTree state;
};]=])
string(FIND "${editor_header}" "${studio_view_callbacks_anchor}" studio_view_callbacks_match)
if(studio_view_callbacks_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example EditViewState callbacks anchor has changed.")
endif()
string(REPLACE "${studio_view_callbacks_anchor}" [=[    ValueTree state;

    std::function<void (te::EditItemID, Component&)> showStudioTrackFX;
    std::function<void (te::EditItemID, Component&)> showStudioTrackMenu;
};]=] editor_header "${editor_header}")
set(studio_track_header_controls_anchor [=[    TextButton armButton {"A"}, muteButton {"M"}, soloButton {"S"}, inputButton {"I"};]=])
string(FIND "${editor_header}" "${studio_track_header_controls_anchor}" studio_track_header_controls_match)
if(studio_track_header_controls_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example TrackHeader controls anchor has changed.")
endif()
string(REPLACE "${studio_track_header_controls_anchor}"
    [=[    TextButton armButton {"A"}, muteButton {"M"}, soloButton {"S"}, inputButton {"I"}, fxButton {"FX"};
    Slider miniVolume, miniPan;
    Label miniLevel;
    void updateMiniControls();
    void timerCallback() override;
    te::LevelMeasurer::Client miniMeterClient;
    juce::WeakReference<te::LevelMeasurer> miniMeter;]=]
    editor_header "${editor_header}")

# Studio's optional per-track mini controls use low-rate UI updates only.
string(REPLACE "class TrackHeaderComponent : public Component,"
               "class TrackHeaderComponent : public Component, private Timer," editor_header "${editor_header}")
file(CONFIGURE OUTPUT generated/Components.h CONTENT "${editor_header}" @ONLY)
file(READ "${common}/Components.cpp" editor)
set(midi_assignment [=[else if (res >= 100)
            {
                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
                    {
                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);

                        id++;
                    }
                }
            }]=])
# Accept the local DemoRunner patch used during the Oxygen 25 test.
string(FIND "${editor}" "${midi_assignment}" match)
if(match EQUAL -1)
    string(REPLACE [=[                        if (id == res)
                        {
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);
                            if (EngineHelpers::trackHasInput (*at))
                                EngineHelpers::armTrack (*at, true);
                        }]=]
                   [=[                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);]=]
                   editor "${editor}")
endif()
string(FIND "${editor}" "${midi_assignment}" match)
if(match EQUAL -1)
    message(FATAL_ERROR "Tracktion's example MIDI routing differs from the Studio 0.1 baseline.")
endif()
string(REPLACE "${midi_assignment}" [=[else if (res >= 100)
            {
                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice)
                        continue;

                    if (id++ != res)
                        continue;

                    auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);
                    if (result.has_value())
                        instance->setRecordingEnabled (at->itemID, true);
                    else
                        at->edit.engine.getUIBehaviour().showWarningMessage (result.error());
                    break;
                }
            }]=] editor "${editor}")
# Normalize the refresh previously added to the local checkout.
set(refresh [=[            armButton.setEnabled (EngineHelpers::trackHasInput (*at));
            armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
        };
        armButton.onClick]=])
string(REPLACE "${refresh}" "        };\n        armButton.onClick" editor "${editor}")
string(REPLACE "        };\n        armButton.onClick" "${refresh}" editor "${editor}")
string(REPLACE "inputsState = track->edit.state.getChildWithName (te::IDs::INPUTDEVICES);"
               "inputsState = track->edit.inputDeviceState;" editor "${editor}")
# Style only the generated editor's drawing code. Track/clip geometry,
# hit testing, recording, and drag/trim still use the pinned implementation.
set(header_fill [=[    g.setColour (Colours::grey);
    g.fillRect (getLocalBounds().withTrimmedRight (2));

    if (editViewState.selectionManager.isSelected (track.get()))
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds().withTrimmedRight (-4), 2);
    }]=])
string(FIND "${editor}" "${header_fill}" header_fill_match)
if(header_fill_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example track header drawing has changed.")
endif()
string(REPLACE "${header_fill}" [=[    auto area = getLocalBounds().withTrimmedRight (1);
    const bool selected = editViewState.selectionManager.isSelected (track.get());
    g.setColour (selected ? studio::colours::selected : studio::colours::panel);
    g.fillRect (area);
    g.setColour (selected ? studio::colours::focus : track->getColour().interpolatedWith (studio::colours::action, 0.42f));
    g.fillRect (area.removeFromLeft (studio::appearance.px (3)));
    if (selected)
    {
        g.setColour (studio::colours::focus.withAlpha (0.38f));
        g.drawRect (area, 1);
    }]=] editor "${editor}")
string(REPLACE "g.setColour (Colours::grey);" "g.setColour (studio::colours::panel);" editor "${editor}")
string(REPLACE "g.fillAll (Colours::grey);" "g.fillAll (studio::colours::timeline);" editor "${editor}")
string(REPLACE "g.setColour (Colours::yellow);" "g.setColour (studio::colours::playhead);" editor "${editor}")
set(clip_fill [=[    g.fillAll (clip->getColour().withAlpha (0.5f));
    g.setColour (Colours::black);
    g.drawRect (getLocalBounds());

    if (editViewState.selectionManager.isSelected (clip.get()))
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds(), 2);
    }]=])
string(FIND "${editor}" "${clip_fill}" clip_fill_match)
if(clip_fill_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example clip drawing has changed.")
endif()
string(REPLACE "${clip_fill}" [=[    const auto area = getLocalBounds().reduced (1);
    const bool midi = dynamic_cast<const te::MidiClip*> (clip.get()) != nullptr;
    const auto base = midi ? studio::colours::midiClip : studio::colours::audioClip;
    const bool selected = editViewState.selectionManager.isSelected (clip.get());
    const auto fill = base.interpolatedWith (clip->getColour(), 0.16f);
    g.setColour (selected ? fill.brighter (0.13f) : fill);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (selected ? studio::colours::focus : studio::colours::divider);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, selected ? 1.25f : 0.75f);
    if (area.getWidth() >= studio::appearance.span (24))
    {
        const auto header = area.withHeight (studio::appearance.line (11, 2)).reduced (studio::appearance.px (5), 0);
        g.setFont (studio::uiFont (11.0f, Font::bold));
        g.setColour (studio::colours::text);
        g.drawFittedText (clip->getName(), header, Justification::centredLeft, 1, 1.0f);
    }]=] editor "${editor}")
string(REPLACE "g.setColour (Colours::red);" "g.setColour (studio::colours::focus.withAlpha (0.52f));" editor "${editor}")
string(REPLACE "0, getWidth(), 0, getHeight(), 0);" "0, getWidth(), studio::appearance.line (11, 2), jmax (1, getHeight() - studio::appearance.line (11, 2)), 0);" editor "${editor}")
string(REPLACE "Colours::black.withAlpha (0.5f)," "studio::colours::winds.withAlpha (0.76f)," editor "${editor}")
string(REPLACE "double y = (1.0 - double (n->getNoteNumber()) / 127.0) * getHeight();" [=[const auto top = studio::appearance.line (11, 2);
                double y = top + (1.0 - double (n->getNoteNumber()) / 127.0) * jmax (0, getHeight() - top - 2);]=] editor "${editor}")
string(REPLACE "armButton.setColour (TextButton::buttonOnColourId, Colours::red);"
               [=[armButton.setColour (TextButton::buttonOnColourId, studio::colours::record.darker (0.48f));
    armButton.setTooltip ("Arm track for recording");
    armButton.setTitle ("Record arm");
    inputButton.setTooltip ("Choose audio or MIDI input");
    inputButton.setTitle ("Track input");
    muteButton.setTooltip ("Mute track");
    muteButton.setTitle ("Mute");
    soloButton.setTooltip ("Solo track");
    soloButton.setTitle ("Solo");
    trackName.setFont (FontOptions (12.0f, Font::bold));
    trackName.setMinimumHorizontalScale (1.0f);]=] editor "${editor}")
string(REPLACE "muteButton.setColour (TextButton::buttonOnColourId, Colours::red);" "muteButton.setColour (TextButton::buttonOnColourId, studio::colours::warning.darker (0.7f));" editor "${editor}")
string(REPLACE "soloButton.setColour (TextButton::buttonOnColourId, Colours::green);" "soloButton.setColour (TextButton::buttonOnColourId, studio::colours::selected.brighter (0.28f));" editor "${editor}")
set(track_header_geometry [=[    auto r = getLocalBounds().reduced (4);
    trackName.setBounds (r.removeFromTop (r.getHeight() / 2));

    int w = r.getHeight();]=])
string(FIND "${editor}" "${track_header_geometry}" track_header_geometry_match)
if(track_header_geometry_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example track header geometry has changed.")
endif()
string(REPLACE "${track_header_geometry}" [=[    const auto& m = studio::appearance;
    auto r = getLocalBounds().reduced (m.px (7), m.px (3));
    trackName.setBounds (r.removeFromTop (m.line (12, 2)));

    const bool showMini = (bool) track->state.getProperty (Identifier ("studioHeaderVolume"), false)
                       || (bool) track->state.getProperty (Identifier ("studioHeaderPan"), false)
                       || (bool) track->state.getProperty (Identifier ("studioHeaderMeter"), false);
    if (showMini) r.removeFromBottom (m.px (14));
    int w = jmin (jmin (r.getHeight(), m.control (23, 11)), (r.getWidth() - 6) / 5);]=] editor "${editor}")

set(studio_fx_bounds_anchor [=[    soloButton.setBounds (r.removeFromLeft (w));
    r.removeFromLeft (2);]=])
string(FIND "${editor}" "${studio_fx_bounds_anchor}" studio_fx_bounds_match)
if(studio_fx_bounds_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example TrackHeader FX position anchor has changed.")
endif()
string(REPLACE "${studio_fx_bounds_anchor}" [=[    soloButton.setBounds (r.removeFromLeft (w));
    r.removeFromLeft (2);
    fxButton.setBounds (r.removeFromLeft (w));
    const bool showVolume = miniVolume.isVisible(), showPan = miniPan.isVisible(), showLevel = miniLevel.isVisible();
    auto extra = getLocalBounds().reduced (m.px (7), m.px (2)).removeFromBottom (m.px (14));
    const int remaining = extra.getWidth() - (showLevel ? m.px (20) : 0);
    miniVolume.setBounds (showVolume ? extra.removeFromLeft (remaining / ((showPan ? 2 : 1))) : Rectangle<int>());
    miniPan.setBounds (showPan ? extra.removeFromLeft (remaining / (showVolume ? 2 : 1)) : Rectangle<int>());
    miniLevel.setBounds (showLevel ? extra : Rectangle<int>());]=] editor "${editor}")

# Keep the ruler and generated arrangement in the same logical-point geometry.
set(track_geometry [=[    const int trackHeight = 50, trackGap = 2;
    const int headerWidth = editViewState.showHeaders ? 150 : 0;
    const int footerWidth = editViewState.showFooters ? 150 : 0;]=])
string(FIND "${editor}" "${track_geometry}" track_geometry_match)
if(track_geometry_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example arrangement geometry has changed.")
endif()
string(REPLACE "${track_geometry}" [=[    const auto& metrics = studio::appearance;
    const int trackHeight = metrics.trackHeight(), trackGap = metrics.trackGap();
    const int headerWidth = editViewState.showHeaders ? metrics.trackHeader() : 0;
    const int footerWidth = editViewState.showFooters ? metrics.trackFooter() : 0;]=]
    editor "${editor}")

set(footer_geometry [=[    auto r = getLocalBounds().reduced (4);
    const int cx = 21;

    addButton.setBounds (r.removeFromLeft (cx).withSizeKeepingCentre (cx, cx));
    r.removeFromLeft (6);

    for (auto p : plugins)
    {
        p->setBounds (r.removeFromLeft (cx).withSizeKeepingCentre (cx, cx));
        r.removeFromLeft (2);
    }]=])
string(FIND "${editor}" "${footer_geometry}" footer_geometry_match)
if(footer_geometry_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example insert-button geometry has changed.")
endif()
string(REPLACE "${footer_geometry}" [=[    const auto& metrics = studio::appearance;
    auto available = getLocalBounds().reduced (metrics.px (3));
    const int size = metrics.control (21, 12);
    auto row = available.removeFromTop (size);
    auto place = [&] (Component& component)
    {
        if (row.getWidth() < size)
            row = available.removeFromTop (size);
        component.setBounds (row.getHeight() >= size && row.getWidth() >= size
            ? row.removeFromLeft (size).reduced (metrics.px (1)) : juce::Rectangle<int>());
    };
    place (addButton);
    for (auto* plugin : plugins)
        place (*plugin);]=] editor "${editor}")

# The Tracktion example's right-hand plugin initials are replaced by a
# single FX control on the left. StudioView owns the popup action so all
# plug-in/undo/ARIA restrictions still apply.
set(studio_track_buttons_anchor [=[    Helpers::addAndMakeVisible (*this, { &trackName, &armButton, &muteButton, &soloButton, &inputButton });]=])
string(FIND "${editor}" "${studio_track_buttons_anchor}" studio_track_buttons_match)
if(studio_track_buttons_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example TrackHeader button initialization has changed.")
endif()
string(REPLACE "${studio_track_buttons_anchor}" [=[    Helpers::addAndMakeVisible (*this, { &trackName, &armButton, &muteButton, &soloButton, &inputButton, &fxButton });
    fxButton.setTooltip ("Instrument and insert effects");
    fxButton.setTitle ("Track instrument and insert effects");
    fxButton.setVisible (dynamic_cast<te::AudioTrack*> (track.get()) != nullptr);
    fxButton.onClick = [this]
    {
        if (editViewState.showStudioTrackFX)
            editViewState.showStudioTrackFX (track->itemID, fxButton);
    };
    trackName.setInterceptsMouseClicks (false, false);
    for (auto* knob : { &miniVolume, &miniPan })
    {
        addAndMakeVisible (*knob);
        knob->setSliderStyle (Slider::LinearHorizontal);
        knob->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    }
    miniVolume.setRange (-100.0, 6.0, 0.1);
    miniVolume.setDoubleClickReturnValue (true, 0.0);
    miniVolume.setTooltip ("Track volume. Double-click for 0 dB.");
    miniPan.setRange (-1.0, 1.0, 0.01);
    miniPan.setDoubleClickReturnValue (true, 0.0);
    miniPan.setTooltip ("Track pan. Double-click to centre.");
    miniVolume.onValueChange = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            if (auto p = at->getVolumePlugin()) p->setVolumeDb ((float) miniVolume.getValue());
    };
    miniPan.onValueChange = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            if (auto p = at->getVolumePlugin()) p->setPan ((float) miniPan.getValue());
    };
    addAndMakeVisible (miniLevel);
    miniLevel.setJustificationType (Justification::centred);
    miniLevel.setTooltip ("Track audio level activity");
    updateMiniControls();]=] editor "${editor}")

set(studio_track_mouse_anchor [=[void TrackHeaderComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (track.get());
}]=])
string(FIND "${editor}" "${studio_track_mouse_anchor}" studio_track_mouse_match)
if(studio_track_mouse_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example track-header mouse callback has changed.")
endif()
string(REPLACE "${studio_track_mouse_anchor}" [=[void TrackHeaderComponent::mouseDown (const MouseEvent& event)
{
    editViewState.selectionManager.selectOnly (track.get());
    if (event.mods.isPopupMenu() && editViewState.showStudioTrackMenu)
        editViewState.showStudioTrackMenu (track->itemID, *this);
}

void TrackHeaderComponent::updateMiniControls()
{
    miniVolume.setVisible ((bool) track->state.getProperty (Identifier ("studioHeaderVolume"), false));
    miniPan.setVisible ((bool) track->state.getProperty (Identifier ("studioHeaderPan"), false));
    miniLevel.setVisible ((bool) track->state.getProperty (Identifier ("studioHeaderMeter"), false));
    if (miniVolume.isVisible() || miniPan.isVisible() || miniLevel.isVisible()) startTimerHz (10);
    else stopTimer();
    resized();
}

void TrackHeaderComponent::timerCallback()
{
    auto* at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr) return;
    if (auto p = at->getVolumePlugin())
    {
        if (miniVolume.isVisible() && ! miniVolume.isMouseButtonDown())
            miniVolume.setValue (p->getVolumeDb(), dontSendNotification);
        if (miniPan.isVisible() && ! miniPan.isMouseButtonDown())
            miniPan.setValue (p->getPan(), dontSendNotification);
    }
    te::LevelMeasurer* next = nullptr;
    if (miniLevel.isVisible())
        for (auto* plugin : at->pluginList)
            if (auto* level = dynamic_cast<te::LevelMeterPlugin*> (plugin))
                next = &level->measurer;
    if (next != miniMeter.get())
    {
        if (auto* previous = miniMeter.get()) previous->removeClient (miniMeterClient);
        miniMeter = next;
        if (next != nullptr) next->addClient (miniMeterClient);
    }
    if (next != nullptr)
    {
        const float db = miniMeterClient.getAndClearAudioLevel (0).dB;
        miniLevel.setText (db > -8 ? "▮▮" : db > -28 ? "▮" : "·", dontSendNotification);
    }
}]=] editor "${editor}")

# Keep the arrangement mini-controls attached to the exact final channel
# gain/pan plug-in used by Mixer and Inspector, not a different insert.
set(studio_header_mixer_access [=[TrackHeaderComponent::TrackHeaderComponent (EditViewState& evs, te::Track::Ptr t)]=])
string(FIND "${editor}" "${studio_header_mixer_access}" studio_header_mixer_match)
if(studio_header_mixer_match EQUAL -1)
    message(FATAL_ERROR "Tracktion header mini-control gain anchor missing.")
endif()
string(REPLACE "${studio_header_mixer_access}" [=[static te::VolumeAndPanPlugin* studioHeaderMixPlugin (te::AudioTrack& track)
{
    for (int i = track.pluginList.size(); --i >= 0;)
        if (auto* p = dynamic_cast<te::VolumeAndPanPlugin*> (track.pluginList[i]))
            return p;
    return nullptr;
}

TrackHeaderComponent::TrackHeaderComponent (EditViewState& evs, te::Track::Ptr t)]=] editor "${editor}")
string(REPLACE "at->getVolumePlugin()" "studioHeaderMixPlugin (*at)" editor "${editor}")
set(studio_header_destructor [=[TrackHeaderComponent::~TrackHeaderComponent()
{
    track->state.removeListener (this);
}]=])
string(FIND "${editor}" "${studio_header_destructor}" studio_header_destructor_match)
if(studio_header_destructor_match EQUAL -1)
    message(FATAL_ERROR "Tracktion header mini-meter destructor anchor missing.")
endif()
string(REPLACE "${studio_header_destructor}" [=[TrackHeaderComponent::~TrackHeaderComponent()
{
    stopTimer();
    if (auto* meter = miniMeter.get()) meter->removeClient (miniMeterClient);
    track->state.removeListener (this);
}]=] editor "${editor}")

# Keep renamed tracks in sync without recreating the arrangement row.
set(studio_track_name_anchor [=[    if (te::TrackList::isTrack (v))
    {
        if (i == te::IDs::mute)]=])
string(FIND "${editor}" "${studio_track_name_anchor}" studio_track_name_match)
if(studio_track_name_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example TrackHeader property listener has changed.")
endif()
string(REPLACE "${studio_track_name_anchor}" [=[    if (te::TrackList::isTrack (v))
    {
        if (i == te::IDs::name)
        {
            trackName.setText (track->getName(), dontSendNotification);
            trackName.setTooltip (track->getName());
        }
        else if (i == Identifier ("studioHeaderVolume") || i == Identifier ("studioHeaderPan")
              || i == Identifier ("studioHeaderMeter"))
            updateMiniControls();
        else if (i == te::IDs::mute)]=] editor "${editor}")

string(PREPEND editor "#include \"StudioUI.h\"\n#include \"MusicalRuler.h\"\n")
# The stock example always replaces clip selection on click. Keep Shift-click
# additive/toggle selection without modifying the Tracktion checkout itself.
set(single_clip_selection [=[void ClipComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (clip.get());
}]=])
string(FIND "${editor}" "${single_clip_selection}" selection_match)
if(selection_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example clip-selection implementation has changed.")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/RegionEditing.inc" region_editing)
string(REPLACE "${single_clip_selection}" "${region_editing}" editor "${editor}")
set(track_mouse_down [=[void TrackComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (track.get());
}]=])
string(FIND "${editor}" "${track_mouse_down}" track_mouse_down_match)
if(track_mouse_down_match EQUAL -1)
    message(FATAL_ERROR "Tracktion example track-selection implementation has changed.")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ArrangementLasso.inc" arrangement_lasso)
string(REPLACE "${track_mouse_down}" "${arrangement_lasso}" editor "${editor}")
file(CONFIGURE OUTPUT generated/Components.cpp CONTENT "${editor}" @ONLY)

set(studio_synth_families Analog FM Wavetable Additive Granular Physical)
add_library(StudioNativeSynth STATIC SynthEngine.cpp SynthDSP/src/StudioSharedPCM.cpp)
foreach(family IN LISTS studio_synth_families)
    target_sources(StudioNativeSynth PRIVATE "SynthDSP/src/Studio${family}DSP.cpp")
endforeach()
target_include_directories(StudioNativeSynth PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/SynthDSP/include")
target_compile_features(StudioNativeSynth PUBLIC cxx_std_20)
juce_add_binary_data(StudioSynthData
    HEADER_NAME StudioSynthData.h NAMESPACE StudioSynthData
    SOURCES SynthDSP/data/StudioSynthFactory.json)

set(STUDIO_ICON_FILE "" CACHE FILEPATH "Approved 1024 x 1024 Studio icon PNG")
set(studio_icon_args)
if(APPLE)
    if(NOT STUDIO_ICON_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/Studio.png")
        set(STUDIO_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/Studio.png")
    endif()
    if(STUDIO_ICON_FILE)
        list(APPEND studio_icon_args ICON_BIG "${STUDIO_ICON_FILE}")
    endif()
endif()
juce_add_gui_app(Studio
    ${studio_icon_args}
    COMPANY_NAME "Studio"
    BUNDLE_ID "org.studioproject.tracktion"
    PRODUCT_NAME "Studio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Studio uses the selected audio input to record audio tracks."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE)
juce_add_binary_data(StudioEnvironmentsAssets
    NAMESPACE StudioEnvironmentData
    HEADER_NAME StudioEnvironmentData.h
    SOURCES
    assets/environments/BlueHourStudy.jpg
    assets/environments/MidnightScoring.jpg
    assets/environments/OrchestralStage.jpg
    assets/environments/ComposerDesk.jpg
    assets/environments/ModernStudio.jpg)
juce_generate_juce_header(Studio)
target_sources(Studio PRIVATE Studio.cpp MusicalTransport.cpp MusicalTransport.h ComposerMidi.cpp ComposerMidi.h ComposerFiles.h ComposerBounce.cpp ComposerBounce.h ComposerBounceUI.cpp ComposerBounceUI.h ComposerTracks.cpp ComposerTracks.h AppearanceMetrics.h StudioUI.h InstrumentBrowser.cpp InstrumentBrowser.h StudioMixing.cpp StudioMixing.h AtelierInspectorPanel.cpp AtelierInspectorPanel.h MidiMonitor.cpp MidiMonitor.h PianoRoll.cpp PianoRoll.h MusicalRuler.h StudioResources.cpp StudioResources.h MidiControllerLanes.cpp MidiControllerLanes.h MidiExpressionValues.h ArticulationPalette.cpp ArticulationPalette.h ArticulationMappings.h ResourceSample.h
    SamplerEngine.cpp StudioSampler.cpp SamplerEditor.cpp
    StudioSynth.cpp SynthEditor.cpp InstrumentWorkspace.cpp InstrumentPerformance.cpp AtelierInstrumentEditor.cpp AtelierInstrumentEditor.h
    SpatialModel.h SpatialState.h SpatialReverb.cpp SpatialReverb.h SpatialRouting.cpp SpatialRouting.h
    SpatialCanvas.cpp SpatialWorkspace.cpp SpatialWorkspace.h StagePresentation.h
    StudioEnvironments.cpp StudioEnvironments.h EnvironmentSelection.h
    FilmScoringWorkspace.h FilmScoringCore.h FilmScoringState.h
    FilmPictureSync.h FilmScoringCueSheet.h FilmCueDelivery.h FilmCueRender.h
    FilmStemDelivery.h FilmStemState.h)
target_include_directories(Studio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated" "${common}")
target_compile_features(Studio PRIVATE cxx_std_20)
target_compile_definitions(Studio PRIVATE
    JUCE_PLUGINHOST_AU=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(Studio PRIVATE
    StudioNativeSynth StudioSynthData StudioEnvironmentsAssets
    tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
    juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils)
if(APPLE)
    target_compile_options(Studio PRIVATE "$<$<NOT:$<COMPILE_LANGUAGE:Swift>>:-fno-aligned-allocation>")
    set_target_properties(Studio PROPERTIES LINKER_LANGUAGE CXX)
    set(CMAKE_Swift_LANGUAGE_VERSION 5)
    enable_language(Swift)
    set(studio_seating_model Seating/SeatingModel.swift Seating/SeatingCatalog.swift)
    add_library(StudioSeatingUI STATIC ${studio_seating_model} Seating/SeatingEditor.swift Seating/SeatingBridge.swift)
    set_target_properties(StudioSeatingUI PROPERTIES Swift_LANGUAGE_VERSION 5)
    target_sources(Studio PRIVATE Seating/SeatingWorkspace.cpp)
    target_link_libraries(Studio PRIVATE StudioSeatingUI juce::juce_video "-framework SwiftUI" "-framework AppKit")
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(Studio PROPERTIES XCODE_ATTRIBUTE_ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES YES)
        target_link_directories(Studio PRIVATE "$(TOOLCHAIN_DIR)/usr/lib/swift/macosx" "$(SDKROOT)/usr/lib/swift")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Studio PRIVATE atomic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(StudioEnvironmentTests tests/EnvironmentSelectionTests.cpp)
    target_compile_features(StudioEnvironmentTests PRIVATE cxx_std_20)
    add_test(NAME StudioEnvironmentSelection COMMAND StudioEnvironmentTests)

    add_executable(StudioFilmScoringTests tests/FilmScoringTests.cpp)
    target_compile_features(StudioFilmScoringTests PRIVATE cxx_std_20)
    target_compile_options(StudioFilmScoringTests PRIVATE -UNDEBUG)
    target_compile_definitions(StudioFilmScoringTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(StudioFilmScoringTests PRIVATE juce::juce_data_structures)
    add_test(NAME StudioFilmScoringCore COMMAND StudioFilmScoringTests)

    add_executable(StudioFilmCueSheetTests tests/FilmCueSheetTests.cpp)
    target_compile_features(StudioFilmCueSheetTests PRIVATE cxx_std_20)
    target_compile_options(StudioFilmCueSheetTests PRIVATE -UNDEBUG)
    add_test(NAME StudioFilmCueSheet COMMAND StudioFilmCueSheetTests)

    add_executable(StudioFilmPictureSyncTests tests/FilmPictureSyncTests.cpp)
    target_compile_features(StudioFilmPictureSyncTests PRIVATE cxx_std_20)
    target_compile_options(StudioFilmPictureSyncTests PRIVATE -UNDEBUG)
    add_test(NAME StudioFilmPictureSync COMMAND StudioFilmPictureSyncTests)

    add_executable(StudioFilmCueDeliveryTests tests/FilmCueDeliveryTests.cpp)
    target_compile_features(StudioFilmCueDeliveryTests PRIVATE cxx_std_20)
    target_compile_options(StudioFilmCueDeliveryTests PRIVATE -UNDEBUG)
    add_test(NAME StudioFilmCueDelivery COMMAND StudioFilmCueDeliveryTests)

    add_executable(StudioFilmStemDeliveryTests tests/FilmStemDeliveryTests.cpp)
    target_compile_features(StudioFilmStemDeliveryTests PRIVATE cxx_std_20)
    target_compile_options(StudioFilmStemDeliveryTests PRIVATE -UNDEBUG)
    add_test(NAME StudioFilmStemDelivery COMMAND StudioFilmStemDeliveryTests)

    add_executable(StudioSynthBench tests/SynthBench.cpp SamplerEngine.cpp)
    target_link_libraries(StudioSynthBench PRIVATE StudioNativeSynth)
    add_custom_target(StudioSynthValidation DEPENDS StudioSynthBench)
    foreach(family IN LISTS studio_synth_families)
        set(target "StudioSynth${family}Original")
        add_executable("${target}" "tests/synth-original/Studio${family}DSP.cpp")
        target_link_libraries("${target}" PRIVATE StudioNativeSynth)
        target_compile_options("${target}" PRIVATE -UNDEBUG)
        add_dependencies(StudioSynthValidation "${target}")
        add_test(NAME "${target}" COMMAND "${target}")
    endforeach()
    add_executable(StudioMidiFocusPolicyTests tests/MidiFocusPolicyTests.cpp)
    target_compile_features(StudioMidiFocusPolicyTests PRIVATE cxx_std_20)
    target_include_directories(StudioMidiFocusPolicyTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME StudioMidiFocusPolicy COMMAND StudioMidiFocusPolicyTests)

    add_executable(StudioMilestoneTests tests/MilestoneMidiTests.cpp)
    target_compile_features(StudioMilestoneTests PRIVATE cxx_std_20)
    target_compile_definitions(StudioMilestoneTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(StudioMilestoneTests PRIVATE juce::juce_core)
    add_test(NAME StudioMilestoneMidi COMMAND StudioMilestoneTests)

    add_executable(StudioSpatialTests tests/SpatialTests.cpp SpatialReverb.cpp)
    target_compile_features(StudioSpatialTests PRIVATE cxx_std_20)
    target_compile_definitions(StudioSpatialTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(StudioSpatialTests PRIVATE juce::juce_data_structures)
    add_test(NAME StudioSpatialModel COMMAND StudioSpatialTests)

    add_executable(StudioAcousticComparison tools/AcousticComparison.cpp SpatialReverb.cpp)
    target_include_directories(StudioAcousticComparison PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(StudioAcousticComparison PRIVATE cxx_std_20)
    add_test(NAME StudioAcousticQuality COMMAND StudioAcousticComparison --verify)
    set_tests_properties(StudioAcousticQuality PROPERTIES TIMEOUT 90)

    add_executable(StudioSamplerMappingTests tests/SamplerMappingTests.cpp SamplerEngine.cpp)
    target_compile_features(StudioSamplerMappingTests PRIVATE cxx_std_20)
    add_test(NAME StudioSamplerMapping COMMAND StudioSamplerMappingTests)

    add_executable(StudioSamplerBench tests/SamplerBench.cpp SamplerEngine.cpp)
    target_compile_features(StudioSamplerBench PRIVATE cxx_std_20)

    add_executable(StudioSamplerArticulationTests tests/SamplerArticulationTests.cpp SamplerEngine.cpp)
    target_compile_features(StudioSamplerArticulationTests PRIVATE cxx_std_20)
    add_test(NAME StudioSamplerArticulations COMMAND StudioSamplerArticulationTests)

    add_executable(StudioSamplerEngineTests tests/SamplerEngineTests.cpp SamplerEngine.cpp)
    target_compile_features(StudioSamplerEngineTests PRIVATE cxx_std_20)
    add_test(NAME StudioSamplerEngine COMMAND StudioSamplerEngineTests)

    target_sources(Studio PRIVATE tests/SpatialRoutingTest.cpp tests/SamplerIntegrationTest.cpp tests/SynthIntegrationTest.cpp tests/ComposerTransportTest.cpp tests/ComposerMidiTest.cpp tests/ComposerBounceTest.cpp)
    add_test(NAME StudioComposerMidi COMMAND $<TARGET_FILE:Studio> --studio-test-composer-midi)
    set_tests_properties(StudioComposerMidi PROPERTIES TIMEOUT 90)
    add_test(NAME StudioComposerBounce COMMAND $<TARGET_FILE:Studio> --studio-test-composer-bounce)
    set_tests_properties(StudioComposerBounce PROPERTIES TIMEOUT 120)
    add_test(NAME StudioComposerTransport COMMAND $<TARGET_FILE:Studio> --studio-test-composer-transport)
    set_tests_properties(StudioComposerTransport PROPERTIES TIMEOUT 90)
    add_test(NAME StudioSynthIntegration COMMAND $<TARGET_FILE:Studio> --studio-test-synth)
    set_tests_properties(StudioSynthIntegration PROPERTIES TIMEOUT 120)
    add_test(NAME StudioSamplerIntegration COMMAND $<TARGET_FILE:Studio> --studio-test-sampler)
    set_tests_properties(StudioSamplerIntegration PROPERTIES TIMEOUT 90)
    target_compile_definitions(Studio PRIVATE STUDIO_SPATIAL_TESTS=1)
    add_test(NAME StudioSpatialRouting COMMAND $<TARGET_FILE:Studio> --studio-test-spatial-routing)
    set_tests_properties(StudioSpatialRouting PROPERTIES TIMEOUT 45)
    add_test(NAME StudioAdaptiveWorkspace COMMAND $<TARGET_FILE:Studio> --studio-adaptive-workspace-review)
    set_tests_properties(StudioAdaptiveWorkspace PROPERTIES TIMEOUT 150)
    add_test(NAME StudioInstrumentWorkspace COMMAND $<TARGET_FILE:Studio> --studio-instrument-workspace-review)
    set_tests_properties(StudioInstrumentWorkspace PROPERTIES TIMEOUT 120)
    add_test(NAME StudioInterfaceReview COMMAND $<TARGET_FILE:Studio> --studio-interface-review)
    set_tests_properties(StudioInterfaceReview PROPERTIES TIMEOUT 120)
    add_test(NAME StudioFilmScoringWorkspace COMMAND $<TARGET_FILE:Studio> --studio-test-film-scoring-workspace)
    set_tests_properties(StudioFilmScoringWorkspace PROPERTIES TIMEOUT 90)
    add_test(NAME StudioSeatingProject COMMAND $<TARGET_FILE:Studio> --studio-test-seating-project)
    set_tests_properties(StudioSeatingProject PROPERTIES TIMEOUT 60)
    add_test(NAME StudioUnifiedStage COMMAND $<TARGET_FILE:Studio> --studio-test-unified-stage)
    set_tests_properties(StudioUnifiedStage PROPERTIES TIMEOUT 90)
    if(APPLE)
        target_compile_definitions(StudioSeatingUI PRIVATE STUDIO_SEATING_TESTS)
        add_executable(StudioSeatingModelTests ${studio_seating_model} tests/SeatingModelTests.swift)
        set_target_properties(StudioSeatingModelTests PROPERTIES Swift_LANGUAGE_VERSION 5)
        add_test(NAME StudioSeatingModel COMMAND $<TARGET_FILE:StudioSeatingModelTests>)
    endif()
endif()
