// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "FilmScoringCore.h"
#include "FilmScoringState.h"
#include <JuceHeader.h>
#if JUCE_MAC
 #include <juce_video/juce_video.h>
#endif

namespace studio::film
{
// Film-scoring controls live on the JUCE message thread. No video or UI work
// happens inside Tracktion's audio callback.
class Workspace final : public juce::Component
{
public:
    Workspace()
    {
        for (auto* c : std::initializer_list<juce::Component*> { &importButton,
                         &floatButton, &ratePicker, &offsetEdit, &applyOffsetButton,
                         &addHitButton, &removeHitButton, &prevButton, &nextButton,
                         &goButton, &markerPicker, &cueEdit, &nameEdit, &notesEdit,
                         &applyHitButton, &quarterBeatsEdit, &timecode, &status, &timing })
            addAndMakeVisible (*c);
        addAndMakeVisible (monitorHost);
        timecode.setJustificationType (juce::Justification::centredLeft);
        timecode.setText ("00:00:00:00", juce::dontSendNotification);
        status.setText ("Load a picture reference (MOV, MP4, M4V). Its audio is muted.", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        timing.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        quarterBeatsEdit.setText ("16", juce::dontSendNotification);
        quarterBeatsEdit.setInputRestrictions (7, "0123456789.");
        quarterBeatsEdit.setTooltip ("Quarter-note beats between selected and next picture hit");
        offsetEdit.setTooltip ("Timeline frame position where picture source frame zero begins");
        offsetEdit.setInputRestrictions (18, "-0123456789");
        nameEdit.setTextToShowWhenEmpty ("Hit name", juce::Colours::grey);
        cueEdit.setTextToShowWhenEmpty ("Cue ID (e.g. 1M1)", juce::Colours::grey);
        notesEdit.setTextToShowWhenEmpty ("Spotting notes", juce::Colours::grey);
        markerPicker.setTextWhenNothingSelected ("Select a hit point");
        for (int i = 0; i < 6; ++i)
            ratePicker.addItem (rateName ((Rate) i), i + 1);
        ratePicker.setSelectedId (2, juce::dontSendNotification);

        importButton.onClick = [this] { choosePicture(); };
        floatButton.onClick = [this] { setFloating (! isFloating()); };
        ratePicker.onChange = [this]
        {
            if (! edit || ratePicker.getSelectedId() == 0) return;
            project.rate = (Rate) (ratePicker.getSelectedId() - 1);
            persist();
            refreshMarkerPicker();
            updateTimecode();
        };
        applyOffsetButton.onClick = [this]
        {
            if (! edit) return;
            project.pictureStartFrame = offsetEdit.getText().getLargeIntValue();
            persist();
            updateTimecode();
        };
        addHitButton.onClick = [this] { addHitAtPlayhead(); };
        removeHitButton.onClick = [this] { removeSelectedHit(); };
        prevButton.onClick = [this] { moveSelection (-1); };
        nextButton.onClick = [this] { moveSelection (1); };
        goButton.onClick = [this] { seekToSelectedHit(); };
        applyHitButton.onClick = [this] { applyHitChanges(); };
        quarterBeatsEdit.onReturnKey = [this] { updateTiming(); };
        markerPicker.onChange = [this]
        {
            const auto index = markerPicker.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (index, (int) sortedIds.size()))
                activeId = sortedIds[(size_t) index];
            else
                activeId.clear();
            populateHitFields();
        };

       #if JUCE_MAC
        monitorHost.addAndMakeVisible (video);
        video.setAudioVolume (0.0f);
        video.onErrorOccurred = [this] (const juce::String& error)
        {
            status.setText ("Video error: " + error, juce::dontSendNotification);
        };
       #else
        monitorHost.addAndMakeVisible (noVideo);
        noVideo.setJustificationType (juce::Justification::centred);
        noVideo.setText ("Picture playback is available on macOS.", juce::dontSendNotification);
       #endif
        setEnabledForProject (false);
    }

    ~Workspace() override
    {
        setFloating (false);
       #if JUCE_MAC
        video.closeVideo();
       #endif
        setEdit (nullptr);
    }

    void setEdit (tracktion::engine::Edit* next)
    {
        if (edit == next) return;
        setFloating (false);
        edit = next;
        project = edit ? decode (edit->state) : Project {};
        activeId.clear();
        ratePicker.setSelectedId ((int) project.rate + 1, juce::dontSendNotification);
        offsetEdit.setText (juce::String (project.pictureStartFrame), juce::dontSendNotification);
        setEnabledForProject (edit != nullptr);
        refreshMarkerPicker();
        loadSavedPicture();
    }

    int minimumContentHeight (int width) const
    {
        return juce::jmax (520, 290 + juce::jmin (370, juce::jmax (150, width * 9 / 23)));
    }

    void tick()
    {
        if (! edit) return;
        updateTimecode();
       #if JUCE_MAC
        if (! video.isVideoOpen()) return;
        if (! isShowing() && ! isFloating())
        {
            if (video.isPlaying()) video.stop();
            return;
        }
        const auto now = edit->getTransport().getPosition().inSeconds();
        const auto frame = project.pictureFrameAt (now);
        if (! frame) return;
        const auto sourceSeconds = frameSeconds (*frame, project.rate);
        const auto duration = video.getVideoDuration();
        if (sourceSeconds < 0 || (duration > 0 && sourceSeconds >= duration))
        {
            if (video.isPlaying()) video.stop();
            return;
        }
        const bool playing = edit->getTransport().isPlaying();
        if (playing != video.isPlaying())
        {
            if (playing) video.play();
            else video.stop();
        }
        // Seek only when position error is meaningful, and throttle correction:
        // AVPlayer seeks are asynchronous and repeated seeks cause stuttering.
        const double tolerance = playing ? 0.22 : 0.055;
        const auto elapsedMs = juce::Time::getMillisecondCounter() - lastSeekMs;
        if (std::abs (video.getPlayPosition() - sourceSeconds) > tolerance && elapsedMs > 350)
        {
            video.setPlayPosition (sourceSeconds);
            lastSeekMs = juce::Time::getMillisecondCounter();
        }
       #endif
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto row = area.removeFromTop (30);
        importButton.setBounds (row.removeFromLeft (108));
        row.removeFromLeft (8);
        floatButton.setBounds (row.removeFromLeft (72));
        row.removeFromLeft (10);
        timecode.setBounds (row.removeFromLeft (130));
        ratePicker.setBounds (row.removeFromLeft (155));
        row.removeFromLeft (8);
        offsetEdit.setBounds (row.removeFromLeft (96));
        applyOffsetButton.setBounds (row.removeFromLeft (88));
        area.removeFromTop (5);
        const auto monitorHeight = juce::jmin (370, juce::jmax (160, area.getWidth() * 9 / 23));
        monitorHost.setBounds (area.removeFromTop (monitorHeight));
       #if JUCE_MAC
        if (! isFloating()) video.setBounds (monitorHost.getLocalBounds());
       #else
        noVideo.setBounds (monitorHost.getLocalBounds());
       #endif
        area.removeFromTop (3);
        status.setBounds (area.removeFromTop (26));
        area.removeFromTop (5);
        row = area.removeFromTop (30);
        addHitButton.setBounds (row.removeFromLeft (115));
        row.removeFromLeft (4);
        removeHitButton.setBounds (row.removeFromLeft (65));
        row.removeFromLeft (8);
        markerPicker.setBounds (row.removeFromLeft (juce::jmax (140, row.getWidth() - 210)));
        prevButton.setBounds (row.removeFromLeft (35));
        nextButton.setBounds (row.removeFromLeft (35));
        goButton.setBounds (row.removeFromLeft (55));
        area.removeFromTop (4);
        row = area.removeFromTop (30);
        cueEdit.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (5);
        nameEdit.setBounds (row.removeFromLeft (juce::jmax (150, row.getWidth() / 2)));
        row.removeFromLeft (5);
        notesEdit.setBounds (row.removeFromLeft (juce::jmax (120, row.getWidth() - 90)));
        applyHitButton.setBounds (row.removeFromLeft (85));
        area.removeFromTop (5);
        row = area.removeFromTop (30);
        quarterBeatsEdit.setBounds (row.removeFromLeft (55));
        timing.setBounds (row);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff151a23));
        auto r = monitorHost.getBounds().toFloat();
        g.setColour (juce::Colour (0xff0c1016));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff364352));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    bool isFloating() const { return floating != nullptr; }

private:
    static juce::String rateName (Rate rate)
    {
        switch (rate)
        {
            case Rate::fps23976: return "23.976 NDF";
            case Rate::fps24: return "24 fps";
            case Rate::fps25: return "25 fps";
            case Rate::fps2997NDF: return "29.97 NDF";
            case Rate::fps2997DF: return "29.97 DF";
            case Rate::fps30: return "30 fps";
        }
        return "24 fps";
    }

    static juce::String labelFor (int64_t pictureFrame, Rate rate)
    {
        const auto tc = fromFrames (pictureFrame, rate);
        const auto hh = juce::String (tc.hours).paddedLeft ('0', 2);
        const auto mm = juce::String (tc.minutes).paddedLeft ('0', 2);
        const auto ss = juce::String (tc.seconds).paddedLeft ('0', 2);
        const auto ff = juce::String (tc.frames).paddedLeft ('0', 2);
        return (tc.negative ? "-" : "") + hh + ":" + mm + ":" + ss
             + (info (rate).drop ? ";" : ":") + ff;
    }

    void persist()
    {
        if (edit)
            store (edit->state, project, &edit->getUndoManager());
    }

    void setEnabledForProject (bool enabled)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &importButton, &ratePicker, &offsetEdit,
                                   &applyOffsetButton, &addHitButton, &removeHitButton, &prevButton,
                                   &nextButton, &goButton, &markerPicker, &cueEdit, &nameEdit,
                                   &notesEdit, &applyHitButton, &quarterBeatsEdit })
            c->setEnabled (enabled);
    }

    void choosePicture()
    {
        if (! edit) return;
        chooser = std::make_unique<juce::FileChooser> ("Choose a picture reference",
                juce::File::getSpecialLocation (juce::File::userMoviesDirectory), "*.mov;*.mp4;*.m4v");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<Workspace> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file.existsAsFile()) safe->loadPicture (file, true);
            });
    }

    void loadSavedPicture()
    {
       #if JUCE_MAC
        video.stop();
        video.closeVideo();
       #endif
        if (project.picturePath.empty())
        {
            status.setText ("No picture reference. Import MOV, MP4, or M4V.", juce::dontSendNotification);
            return;
        }
        const juce::File file (juce::String (project.picturePath));
        if (file.existsAsFile()) loadPicture (file, false);
        else status.setText ("Missing picture: " + file.getFullPathName() + " — relink with Import Picture.",
                             juce::dontSendNotification);
    }

    void loadPicture (const juce::File& file, bool savePath)
    {
       #if JUCE_MAC
        video.stop();
        video.closeVideo();
        const auto result = video.load (file);
        if (result.failed())
        {
            status.setText ("Unable to open picture: " + result.getErrorMessage(), juce::dontSendNotification);
            return;
        }
        video.setAudioVolume (0.0f);
        lastSeekMs = 0;
       #else
        status.setText ("Video preview requires macOS.", juce::dontSendNotification);
        if (savePath) return;
       #endif
        if (savePath)
        {
            project.picturePath = file.getFullPathName().toStdString();
            persist();
        }
        status.setText ("Picture: " + file.getFileName() + " — muted reference; transport follows Studio.",
                        juce::dontSendNotification);
        tick();
        resized();
    }

    void updateTimecode()
    {
        if (! edit) { timecode.setText ("00:00:00:00", juce::dontSendNotification); return; }
        const auto now = edit->getTransport().getPosition().inSeconds();
        const auto frame = project.pictureFrameAt (now);
        timecode.setText (frame ? labelFor (*frame, project.rate) : "--:--:--:--", juce::dontSendNotification);
    }

    void refreshMarkerPicker()
    {
        sortedIds.clear();
        for (const auto& spot : project.spots) sortedIds.push_back (spot.id);
        std::sort (sortedIds.begin(), sortedIds.end(), [this] (const auto& lhs, const auto& rhs)
        {
            return project.find (lhs)->pictureFrame < project.find (rhs)->pictureFrame;
        });
        markerPicker.clear (juce::dontSendNotification);
        int selected = 0;
        for (size_t i = 0; i < sortedIds.size(); ++i)
        {
            const auto* spot = project.find (sortedIds[i]);
            markerPicker.addItem (juce::String (spot->cue) + "  " + juce::String (spot->name)
                                  + "  [" + labelFor (spot->pictureFrame, project.rate) + "]", (int) i + 1);
            if (sortedIds[i] == activeId) selected = (int) i + 1;
        }
        markerPicker.setSelectedId (selected, juce::dontSendNotification);
        populateHitFields();
    }

    void populateHitFields()
    {
        const auto* hit = project.find (activeId);
        cueEdit.setText (hit ? juce::String (hit->cue) : "", juce::dontSendNotification);
        nameEdit.setText (hit ? juce::String (hit->name) : "", juce::dontSendNotification);
        notesEdit.setText (hit ? juce::String (hit->notes) : "", juce::dontSendNotification);
        updateTiming();
    }

    void addHitAtPlayhead()
    {
        if (! edit) return;
        const auto frame = project.pictureFrameAt (edit->getTransport().getPosition().inSeconds());
        if (! frame || *frame < 0)
        {
            status.setText ("Place the playhead within picture time before adding a hit.", juce::dontSendNotification);
            return;
        }
        const auto id = juce::Uuid().toString().toStdString();
        const juce::String number (project.spots.size() + 1);
        if (! project.addSpot ({ id, "1M1", ("Hit " + number).toStdString(), "", *frame })) return;
        activeId = id;
        persist();
        refreshMarkerPicker();
    }

    void removeSelectedHit()
    {
        if (! edit || activeId.empty()) return;
        project.spots.erase (std::remove_if (project.spots.begin(), project.spots.end(),
                           [this] (const auto& s) { return s.id == activeId; }), project.spots.end());
        activeId.clear();
        persist();
        refreshMarkerPicker();
    }

    void applyHitChanges()
    {
        if (! edit || activeId.empty()) return;
        for (auto& spot : project.spots)
            if (spot.id == activeId)
            {
                spot.cue = cueEdit.getText().toStdString();
                spot.name = nameEdit.getText().toStdString();
                spot.notes = notesEdit.getText().toStdString();
                persist();
                refreshMarkerPicker();
                return;
            }
    }

    void seekToSelectedHit()
    {
        if (! edit || edit->getTransport().isRecording()) return;
        if (const auto* spot = project.find (activeId))
            edit->getTransport().setPosition (tracktion::engine::TimePosition::fromSeconds (
                    juce::jmax (0.0, project.timelineSeconds (spot->pictureFrame))));
    }

    void moveSelection (int direction)
    {
        if (sortedIds.empty()) return;
        auto it = std::find (sortedIds.begin(), sortedIds.end(), activeId);
        const int index = it == sortedIds.end() ? 0
                        : juce::jlimit (0, (int) sortedIds.size() - 1,
                                       (int) std::distance (sortedIds.begin(), it) + direction);
        activeId = sortedIds[(size_t) index];
        refreshMarkerPicker();
        seekToSelectedHit();
    }

    void updateTiming()
    {
        timing.setText ("Quarter-note beats to next hit", juce::dontSendNotification);
        const auto* first = project.find (activeId);
        if (! first) return;
        for (const auto& id : sortedIds)
        {
            const auto* next = project.find (id);
            if (! next || next->pictureFrame <= first->pictureFrame) continue;
            const auto duration = frameSeconds (next->pictureFrame - first->pictureFrame, project.rate);
            const auto beats = quarterBeatsEdit.getText().getDoubleValue();
            if (const auto bpm = suggestedBpm (duration, beats))
            {
                juce::String detail = "To next hit: " + juce::String (duration, 2)
                    + "s  |  Suggested " + juce::String (*bpm, 2) + " BPM";
                if (edit)
                {
                    const auto a = edit->tempoSequence.toBeats (tracktion::engine::TimePosition::fromSeconds (
                        project.timelineSeconds (first->pictureFrame))).inBeats();
                    const auto b = edit->tempoSequence.toBeats (tracktion::engine::TimePosition::fromSeconds (
                        project.timelineSeconds (next->pictureFrame))).inBeats();
                    detail += "  |  Current " + juce::String (b - a, 2) + " beats";
                }
                timing.setText (detail, juce::dontSendNotification);
            }
            return;
        }
        timing.setText ("No later hit point in this cue", juce::dontSendNotification);
    }

    void setFloating (bool wantFloat)
    {
       #if JUCE_MAC
        if (wantFloat == isFloating()) return;
        if (wantFloat)
        {
            floating = std::make_unique<PictureWindow>();
            floating->onDock = [this] { setFloating (false); };
            floating->setContentNonOwned (&video, false);
            floating->centreWithSize (760, 470);
            floating->setVisible (true);
            floatButton.setButtonText ("Dock");
        }
        else
        {
            floating->clearContentComponent();
            floating.reset();
            monitorHost.addAndMakeVisible (video);
            video.setBounds (monitorHost.getLocalBounds());
            floatButton.setButtonText ("Float");
        }
       #else
        juce::ignoreUnused (wantFloat);
       #endif
    }

   #if JUCE_MAC
    class PictureWindow final : public juce::DocumentWindow
    {
    public:
        PictureWindow() : DocumentWindow ("Studio — Picture Monitor", juce::Colours::black, closeButton)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setResizeLimits (400, 250, 2400, 1600);
        }
        std::function<void()> onDock;
        void closeButtonPressed() override { if (onDock) onDock(); }
    };
    juce::VideoComponent video { false };
    std::unique_ptr<PictureWindow> floating;
    uint32_t lastSeekMs = 0;
   #else
    juce::Label noVideo;
   #endif

    tracktion::engine::Edit* edit = nullptr;
    Project project;
    std::string activeId;
    std::vector<std::string> sortedIds;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component monitorHost;
    juce::Label timecode, status, timing;
    juce::TextButton importButton { "Import Picture" }, floatButton { "Float" };
    juce::ComboBox ratePicker;
    juce::TextEditor offsetEdit, cueEdit, nameEdit, notesEdit, quarterBeatsEdit;
    juce::TextButton applyOffsetButton { "Set Offset" }, addHitButton { "Add Hit" },
                     removeHitButton { "Remove" }, prevButton { "<" }, nextButton { ">" },
                     goButton { "Go To" }, applyHitButton { "Apply Hit" };
    juce::ComboBox markerPicker;
};
} // namespace studio::film
