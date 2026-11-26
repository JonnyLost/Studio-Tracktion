// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"

namespace studio::help
{
enum class Topic { play, sound, mapping, groups, waveform, advanced, library, builder };
struct Article { const char* title; const char* body; };
inline Article article(Topic topic)
{
    switch(topic)
    {
        case Topic::play:return {"Playing instruments",
            "Open loads a saved instrument or a supported library file. New Instrument creates one from audio recordings.\n\n"
            "Play your MIDI keyboard or the on-screen keys. Choose an articulation and select it to change the playing technique, if that instrument has mapped articulations. Not every instrument includes these.\n\n"
            "Volume here changes the instrument itself, not the track mixer. Use Sound to shape the sound. Build contains the detailed editing tools.\n\n"
            "If recordings are missing, Find missing sample opens the original recording location chooser. Keep the original audio available when saving your preset."};
        case Topic::sound:return {"Shaping the sound",
            "Attack is how quickly the sound begins. Decay is how quickly it settles. Sustain is the level while a note is held. Release is how long it fades after you let go.\n\n"
            "For imported instruments, the envelope may belong to the selected recording; the Sound page tells you which scope you are editing. Volume and Tune affect the whole instrument.\n\n"
            "Use the keyboard to hear your changes. The waveform shows the selected recording. The instrument does not need to be rebuilt to change these controls."};
        case Topic::mapping:return {"Keyboard mapping",
            "A sample is a recording. A zone is the range of notes and playing strengths that trigger that recording. Root note is the original pitch (C4 = MIDI 60). Lowest and highest key define its playable range.\n\n"
            "Velocity is how hard a MIDI note is played, from 1 to 127. Separate velocity ranges let quiet and loud recordings respond naturally. Overlap is sometimes intentional.\n\n"
            "Add Sample adds recordings; Replace Sample keeps the selected mapping and changes its audio. Removing a zone does not delete the original recording. Use Undo for mapping edits."};
        case Topic::groups:return {"Groups and articulations",
            "Groups organize recordings that belong together. A keyswitch is a MIDI note that selects a playing technique; a controller condition responds to a MIDI control value. Normal groups play without a switching condition.\n\n"
            "On note release plays a group when you release the key or sustain pedal. Mute and Solo control which groups are heard.\n\n"
            "Round robin is separate: recordings in the same nonzero alternate-take group cycle when their key and velocity ranges match. It is configured in Advanced."};
        case Topic::waveform:return {"Editing recordings and loops",
            "Sample start and end choose the part of the recording that plays. A loop repeats a section for sustained notes; one-shot plays to the end. Use a suitable steady part of a recording for a sustain loop.\n\n"
            "Loop crossfade blends the end back into the beginning to reduce clicks. The controls are in sample frames, not musical beats. Audition lets you hear the selected recording.\n\n"
            "These edits affect playback only. Studio does not overwrite your source recording."};
        case Topic::advanced:return {"Advanced sampler settings",
            "Maximum voices limits simultaneous sample playback. Leave it at the default unless you need more polyphony. More voices can require more CPU.\n\n"
            "Fine tune changes the selected recording's pitch in cents; Tune changes the entire instrument in semitones. Pitch-bend range determines how far the MIDI pitch wheel bends.\n\n"
            "Round-robin group zero layers recordings; matching nonzero groups cycle alternate recordings. Memory and voice meters are diagnostic information, not settings you need to manage when composing."};
        case Topic::library:return {"Finding and managing instruments",
            "Choose a family, search by name or filter to Favorites. Select a native instrument and Load as new track. Double-click or Return performs the selected row's available action.\n\n"
            "Source instruments need preparation first. Choose Prepare instrument, check the compatibility report, then load. Some proprietary formats have limited support; a catalog entry is not proof that it can be converted.\n\n"
            "Manage library opens folder scanning, batch conversion, artwork and repair. Removing a folder does not delete recordings. If a drive is offline, reconnect it and rescan. Repair sample paths creates a separate native preset copy; compare proposed replacements."};
        case Topic::builder:return {"Creating an instrument",
            "Choose recordings or a folder. Name the instrument and choose Pitched or Drum / percussion kit.\n\n"
            "For pitched multisamples, filenames such as Cello_C4.wav identify the root note. Cello_C4_v1-63.wav adds a velocity range. Studio also reads supported file root metadata. It does not detect acoustic pitch. A single unknown recording uses the root you choose.\n\n"
            "Drum kits map recordings to successive keys, starting at the first drum key. Alternate takes cycle matching pitched recordings. Existing embedded loops must be valid; Studio will not invent a loop.\n\n"
            "Preview shows the proposed ranges. Create replaces the current sampler instrument only after you approve. Save the result as a native preset. Cancel leaves the existing instrument unchanged."};
    }
    return {"Studio help","Choose a task to begin."};
}
class Panel final:public juce::Component
{
public:
    explicit Panel(Article a)
    {
        using namespace juce;
        setLookAndFeel(&look);
        addAndMakeVisible(text);addAndMakeVisible(close);
        text.setMultiLine(true);text.setReadOnly(true);text.setScrollbarsShown(true);
        text.setTitle(a.title);text.setFont(uiFont(15));text.setText(a.body,false);
        close.setTitle("Close help");close.onClick=[this]{if(auto* d=findParentComponentOfClass<DialogWindow>())d->exitModalState(0);};
        setSize(560,430);
    }
    ~Panel() override{setLookAndFeel(nullptr);}
    void resized() override
    {
        auto bounds=getLocalBounds().reduced(appearance.px(12));
        close.setBounds(bounds.removeFromBottom(appearance.control(32)).removeFromRight(appearance.span(100)));
        bounds.removeFromBottom(appearance.px(8));text.setBounds(bounds);
    }
private:
    StudioLookAndFeel look;
    juce::TextEditor text;
    juce::TextButton close{"Close"};
};
inline juce::DialogWindow* show(juce::Component& parent,Topic topic)
{
    auto a=article(topic);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle=a.title;options.dialogBackgroundColour=colours::panel;
    options.useNativeTitleBar=true;options.resizable=true;options.componentToCentreAround=&parent;
    options.content.setOwned(new Panel(a));return options.launchAsync();
}
}
