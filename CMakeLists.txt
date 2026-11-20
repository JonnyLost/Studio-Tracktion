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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/RegionEditing.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${common}/Components.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${common}/Components.h")
file(READ "${common}/Components.h" editor_header)
string(REPLACE "#pragma once" "#pragma once\n#include <utility>\n#include <vector>"
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
    te::Track* getTrack() const { return track.get(); }

private:
    void changeListenerCallback (ChangeBroadcaster*) override;]=] editor_header "${editor_header}")
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
# Give the generated arrangement editor the same palette as the Studio window.
string(REPLACE "g.setColour (Colours::grey);" "g.setColour (Colour (0xff303d48));" editor "${editor}")
string(REPLACE "g.setColour (Colours::red);" "g.setColour (Colour (0xffc5a56b));" editor "${editor}")
string(REPLACE "g.fillAll (Colours::grey);" "g.fillAll (Colour (0xff24303b));" editor "${editor}")
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
file(CONFIGURE OUTPUT generated/Components.cpp CONTENT "${editor}" @ONLY)

juce_add_gui_app(Studio
    COMPANY_NAME "Studio"
    BUNDLE_ID "org.studioproject.tracktion"
    PRODUCT_NAME "Studio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Studio uses the selected audio input to record audio tracks."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE)
juce_generate_juce_header(Studio)
target_sources(Studio PRIVATE Studio.cpp MidiMonitor.cpp MidiMonitor.h PianoRoll.cpp PianoRoll.h MusicalRuler.h)
target_include_directories(Studio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${common}")
target_compile_features(Studio PRIVATE cxx_std_20)
target_compile_definitions(Studio PRIVATE
    JUCE_PLUGINHOST_AU=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(Studio PRIVATE
    tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
    juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils)
if(APPLE)
    target_compile_options(Studio PRIVATE -fno-aligned-allocation)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Studio PRIVATE atomic)
endif()
