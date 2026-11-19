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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${common}/Components.cpp")
configure_file("${common}/Components.h" generated/Components.h COPYONLY)
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
target_sources(Studio PRIVATE Studio.cpp MidiMonitor.cpp MidiMonitor.h)
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
