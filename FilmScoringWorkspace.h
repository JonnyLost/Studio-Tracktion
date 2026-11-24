// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "FilmScoringCore.h"
#include "FilmScoringState.h"
#include "FilmScoringCueSheet.h"
#include "FilmPictureSync.h"
#include "FilmCueRender.h"
#include "FilmStemState.h"
#include <JuceHeader.h>
#if JUCE_MAC
 #include <juce_video/juce_video.h>
#endif

namespace studio::film
{
// Film-scoring controls live on the JUCE message thread. No video or UI work
// happens inside Tracktion's audio callback.
// Picture-source timeline that stays independent of musical tempo and offset.
class SpottingStrip final : public juce::Component
{
public:
    std::function<void (std::string)> onChooseHit;
    std::function<void (double)> onSeekPictureSeconds;

    void update (const Project* data, int64_t playheadFrame, double mediaDuration)
    {
        project = data;
        playhead = playheadFrame;
        lengthSeconds = juce::jmax (30.0, mediaDuration);
        if (project)
        {
            for (const auto& hit : project->spots)
                lengthSeconds = juce::jmax (lengthSeconds, frameSeconds (hit.pictureFrame, project->rate) + 3.0);
            lengthSeconds = juce::jmax (lengthSeconds, frameSeconds (playheadFrame, project->rate) + 3.0);
        }
        if (playhead != lastDrawn || lengthSeconds != lastLength)
        {
            lastDrawn = playhead;
            lastLength = lengthSeconds;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (2);
        g.setColour (juce::Colour (0xff222e3e));
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
        if (! project) return;
        area = area.reduced (10, 4);
        const int lineY = area.getBottom() - 13;
        g.setColour (juce::Colour (0xff607184));
        g.drawHorizontalLine (lineY, (float) area.getX(), (float) area.getRight());
        g.setFont (11.0f);
        g.setColour (juce::Colour (0xff9caaba));
        g.drawText ("PICTURE HITS  |  Click to seek", area.removeFromTop (15),
                    juce::Justification::centredLeft, false);
        for (int i = 0; i <= 8; ++i)
        {
            const auto x = area.getX() + area.getWidth() * i / 8;
            g.setColour (juce::Colour (0xff556272));
            g.drawVerticalLine (x, (float) lineY - 3, (float) lineY + 3);
        }
        for (const auto& hit : project->spots)
        {
            const auto x = xAt (frameSeconds (hit.pictureFrame, project->rate), area);
            g.setColour (juce::Colour (0xfff2ae58));
            g.fillEllipse ((float) x - 4, (float) lineY - 4, 8.0f, 8.0f);
            g.setColour (juce::Colour (0xffe8d6b4));
            g.drawFittedText (juce::String (hit.cue), x + 5, lineY - 19, 55, 15,
                              juce::Justification::centredLeft, 1);
        }
        const auto x = xAt (frameSeconds (playhead, project->rate), area);
        g.setColour (juce::Colour (0xfff06b6b));
        g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom() + 12);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! project) return;
        const auto area = getLocalBounds().reduced (12, 6).withTrimmedTop (15);
        if (area.getWidth() <= 0) return;
        for (const auto& hit : project->spots)
        {
            if (std::abs (event.x - xAt (frameSeconds (hit.pictureFrame, project->rate), area)) <= 9)
            {
                if (onChooseHit) onChooseHit (hit.id);
                return;
            }
        }
        const auto proportion = juce::jlimit (0.0, 1.0, double (event.x - area.getX()) / area.getWidth());
        if (onSeekPictureSeconds) onSeekPictureSeconds (lengthSeconds * proportion);
    }

private:
    int xAt (double seconds, juce::Rectangle<int> area) const
    {
        return area.getX() + juce::roundToInt (juce::jlimit (0.0, 1.0,
                                seconds / juce::jmax (0.001, lengthSeconds)) * area.getWidth());
    }

    const Project* project = nullptr;
    int64_t playhead = 0, lastDrawn = std::numeric_limits<int64_t>::min();
    double lengthSeconds = 30, lastLength = -1;
};

class Workspace final : public juce::Component
{
public:
    Workspace()
    {
        for (auto* c : std::initializer_list<juce::Component*> { &importButton,
                         &floatButton, &ratePicker, &offsetEdit, &applyOffsetButton,
                         &addHitButton, &removeHitButton, &prevButton, &nextButton,
                         &goButton, &markerPicker, &cueEdit, &nameEdit, &notesEdit,
                         &applyHitButton, &quarterBeatsEdit, &timecode, &status, &timing,
                         &cuePicker, &newCueButton, &outCueButton, &gotoCueButton,
                         &cueIdEdit, &cueTitleEdit, &cueNotesEdit, &saveCueButton,
                         &deleteCueButton, &exportCueButton, &cueSection,
                         &renderMixButton, &renderTracksButton, &renderStemsButton,
                         &cancelRenderButton, &renderStatus, &stemSection,
                         &stemTrackPicker, &stemFamilyPicker, &syncStatus, &resyncButton })
            addAndMakeVisible (*c);
        addAndMakeVisible (monitorHost);
        addAndMakeVisible (strip);
        strip.onChooseHit = [this] (const std::string& id)
        {
            activeId = id;
            refreshMarkerPicker();
            seekToSelectedHit();
        };
        strip.onSeekPictureSeconds = [this] (double seconds)
        {
            if (! edit || edit->getTransport().isRecording()) return;
            if (const auto source = secondsToFrames (seconds, project.rate))
                edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (
                    juce::jmax (0.0, project.timelineSeconds (*source))));
        };
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
        cueSection.setText ("FILM CUES", juce::dontSendNotification);
        cueSection.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        cuePicker.setTextWhenNothingSelected ("Choose a cue");
        cueIdEdit.setTextToShowWhenEmpty ("Cue ID", juce::Colours::grey);
        cueTitleEdit.setTextToShowWhenEmpty ("Cue title", juce::Colours::grey);
        cueNotesEdit.setTextToShowWhenEmpty ("Cue notes", juce::Colours::grey);
        cueIdEdit.setTooltip ("Unique cue ID, such as 1M1 or 2M3");
        outCueButton.setTooltip ("Set the selected cue end at the picture playhead");
        exportCueButton.setTooltip ("Export a frame-anchored SMPTE cue sheet; no rendered audio");
        for (int i = 0; i < 6; ++i)
            ratePicker.addItem (rateName ((Rate) i), i + 1);
        ratePicker.setSelectedId (2, juce::dontSendNotification);

        importButton.onClick = [this] { choosePicture(); };
        floatButton.onClick = [this] { setFloating (! isFloating()); };
        ratePicker.onChange = [this]
        {
            if (! edit || ratePicker.getSelectedId() == 0) return;
            project.rate = (Rate) (ratePicker.getSelectedId() - 1);
            pictureSync.reset();
            persist();
            refreshMarkerPicker();
            refreshCuePicker();
            updateTimecode();
        };
        applyOffsetButton.onClick = [this]
        {
            if (! edit) return;
            project.pictureStartFrame = offsetEdit.getText().getLargeIntValue();
            pictureSync.reset();
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
        cuePicker.onChange = [this]
        {
            const int index = cuePicker.getSelectedItemIndex();
            activeCueId = juce::isPositiveAndBelow (index, (int) sortedCueIds.size())
                        ? sortedCueIds[(size_t) index] : std::string {};
            populateCueFields();
        };
        newCueButton.onClick = [this] { addCueAtPlayhead(); };
        outCueButton.onClick = [this] { markCueEnd(); };
        gotoCueButton.onClick = [this] { goToSelectedCue(); };
        saveCueButton.onClick = [this] { applyCueChanges(); };
        deleteCueButton.onClick = [this] { deleteSelectedCue(); };
        exportCueButton.onClick = [this] { exportCueSheet(); };
        renderMixButton.onClick = [this] { chooseCueDelivery (CueRenderSession::Mode::stereoMix); };
        renderTracksButton.onClick = [this] { chooseCueDelivery (CueRenderSession::Mode::individualTracks); };
        renderStemsButton.onClick = [this] { chooseCueDelivery (CueRenderSession::Mode::familyStems); };
        stemSection.setText ("ORCHESTRAL STEMS", juce::dontSendNotification);
        stemSection.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        stemTrackPicker.setTextWhenNothingSelected ("Select instrument track");
        stemTrackPicker.setTooltip ("Select a Studio track to assign to a stem family");
        for (int i = 0; i <= (int) StemFamily::other; ++i)
            stemFamilyPicker.addItem (stemFamilyName ((StemFamily) i), i + 1);
        stemFamilyPicker.onChange = [this] { saveSelectedStemFamily(); };
        stemTrackPicker.onChange = [this] { showSelectedStemFamily(); };
        renderStemsButton.setTooltip ("Export a WAV per assigned orchestral family. Unassigned tracks are excluded.");
        resyncButton.setTooltip ("Re-align the native picture player to the Studio transport immediately");
        resyncButton.onClick = [this] { pictureSync.reset(); tick(); };
        syncStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        syncStatus.setText ("Picture sync: waiting for media", juce::dontSendNotification);
        cancelRenderButton.onClick = [this] { cueRender.cancel(); };
        cueRender.onUpdate = [this] { renderStatus.setText (cueRender.description(), juce::dontSendNotification); };
        renderStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        renderStatus.setText ("Cue WAV: 24-bit, 48 kHz, exact range, no tail.", juce::dontSendNotification);
        renderMixButton.setTooltip ("Export the selected closed cue as a stereo WAV, through Studio's Tracktion engine");
        renderTracksButton.setTooltip ("Export one WAV per existing audio track, including routing dependencies where possible");

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
        // Members are destroyed after this body, in reverse declaration
        // order. Disable callbacks before child controls start destructing.
        cueRender.onUpdate = {};
        cueRender.clear();
        setFloating (false);
       #if JUCE_MAC
        video.closeVideo();
       #endif
        setEdit (nullptr);
    }

    std::function<bool()> canRenderAudio;

    bool isRendering() const { return cueRender.isActive(); }

    void setEdit (tracktion::engine::Edit* next)
    {
        if (edit == next) return;
        ++editGeneration;
        cueRender.clear();
        setFloating (false);
        edit = next;
        pictureSync.reset();
        project = edit ? decode (edit->state) : Project {};
        stemAssignments = edit ? readStemAssignments (edit->state) : std::vector<StemAssignment> {};
        knownStemNode = edit ? edit->state.getChildWithName ("STUDIO_FILM_STEMS") : juce::ValueTree {};
        stemTrackNames.clear();
        liveStemTrackIDs.clear();
        knownScoringNode = edit ? edit->state.getChildWithName ("STUDIO_FILM_SCORING") : juce::ValueTree {};
        activeId.clear();
        ratePicker.setSelectedId ((int) project.rate + 1, juce::dontSendNotification);
        offsetEdit.setText (juce::String (project.pictureStartFrame), juce::dontSendNotification);
        setEnabledForProject (edit != nullptr);
        refreshMarkerPicker();
        activeCueId.clear();
        refreshCuePicker();
        refreshStemTracks();
        loadSavedPicture();
        strip.update (&project, 0, 0);
    }

    int minimumContentHeight (int width) const
    {
        if (width >= 600) return 392;
        return width >= 445 ? 640 : 980;
    }

    void tick()
    {
        if (! edit) return;
        if (cueRender.hasStarted())
            renderStatus.setText (cueRender.description(), juce::dontSendNotification);
        const bool rendering = cueRender.isActive();
        const bool canRender = ! rendering && canRenderAudio && canRenderAudio();
        renderMixButton.setEnabled (canRender);
        renderTracksButton.setEnabled (canRender);
        renderStemsButton.setEnabled (canRender);
        stemTrackPicker.setEnabled (! rendering);
        stemFamilyPicker.setEnabled (! rendering);
        cancelRenderButton.setEnabled (rendering);
        refreshStemTracks();
        const auto newStemNode = edit->state.getChildWithName ("STUDIO_FILM_STEMS");
        if (newStemNode != knownStemNode)
        {
            stemAssignments = readStemAssignments (edit->state);
            knownStemNode = newStemNode;
            showSelectedStemFamily();
        }
        // Undo/Redo swaps the versioned ValueTree subtree. Refresh our
        // message-thread snapshot before any future scoring edits can overwrite
        // an undone operation with stale local cue/spot data.
        const auto current = edit->state.getChildWithName ("STUDIO_FILM_SCORING");
        if (current != knownScoringNode)
        {
            const auto oldMedia = project.picturePath;
            project = decode (edit->state);
            knownScoringNode = current;
            ratePicker.setSelectedId ((int) project.rate + 1, juce::dontSendNotification);
            offsetEdit.setText (juce::String (project.pictureStartFrame), juce::dontSendNotification);
            refreshMarkerPicker();
            refreshCuePicker();
            pictureSync.reset();
            if (oldMedia != project.picturePath) loadSavedPicture();
            strip.repaint();
        }
        updateTimecode();
        const auto nowPicture = project.pictureFrameAt (edit->getTransport().getPosition().inSeconds());
       #if JUCE_MAC
        strip.update (&project, nowPicture.value_or (0), video.isVideoOpen() ? video.getVideoDuration() : 0);
       #else
        strip.update (&project, nowPicture.value_or (0), 0);
       #endif
       #if JUCE_MAC
        PreviewObservation observed;
        observed.timelineSeconds = edit->getTransport().getPosition().inSeconds();
        observed.videoOpen = video.isVideoOpen();
        observed.visible = isShowing() || isFloating();
        observed.transportPlaying = edit->getTransport().isPlaying();
        observed.videoPlaying = observed.videoOpen && video.isPlaying();
        if (observed.videoOpen)
        {
            observed.videoSeconds = video.getPlayPosition();
            observed.videoDurationSeconds = video.getVideoDuration();
        }
        observed.tickMillis = (uint64_t) juce::Time::getMillisecondCounter();
        const auto command = pictureSync.update (project, observed);
        if (command.stop) video.stop();
        if (command.seek) video.setPlayPosition (command.targetSeconds);
        if (command.play) video.play();
        syncStatus.setText (observed.videoOpen && observed.visible
            ? (command.stop && ! observed.transportPlaying ? "Picture sync: paused"
               : command.discontinuity ? "Picture sync: re-aligning"
               : "Picture drift: " + juce::String (command.driftFrames, 1) + " frames")
            : "Picture sync: waiting for media", juce::dontSendNotification);
       #endif
    }

    void resized() override
    {
        // The docked picture editor is a three-column console. Only the
        // separate native PictureWindow is independently resizable.
        auto area = getLocalBounds().reduced (8);
        const int gutter = 9;
        juce::Rectangle<int> left, right, centre;
        if (getWidth() >= 600)
        {
            const int sideWidth = juce::jlimit (205, 290, area.getWidth() / 4);
            left = area.removeFromLeft (sideWidth);
            area.removeFromLeft (gutter);
            right = area.removeFromRight (sideWidth);
            area.removeFromRight (gutter);
            centre = area;
        }
        else
        {
            centre = area.removeFromTop (240);
            area.removeFromTop (gutter);
            if (getWidth() >= 445)
            {
                left = area.removeFromLeft ((area.getWidth() - gutter) / 2);
                area.removeFromLeft (gutter);
            }
            else
            {
                left = area.removeFromTop (340);
                area.removeFromTop (gutter);
            }
            right = area;
        }

        const auto line = [] (juce::Rectangle<int>& panel, int height = 27)
        {
            auto result = panel.removeFromTop (height);
            panel.removeFromTop (4);
            return result;
        };
        const auto pair = [] (juce::Rectangle<int> row, juce::Component& first,
                              juce::Component& second, int firstWidth)
        {
            first.setBounds (row.removeFromLeft (juce::jmin (firstWidth, row.getWidth())));
            row.removeFromLeft (4);
            second.setBounds (row);
        };

        pair (line (left), importButton, floatButton, 112);
        pair (line (left), timecode, ratePicker, 118);
        pair (line (left), offsetEdit, applyOffsetButton, 115);
        pair (line (left), addHitButton, removeHitButton, 110);
        auto row = line (left);
        auto navigation = row.removeFromRight (108);
        markerPicker.setBounds (row);
        prevButton.setBounds (navigation.removeFromLeft (34));
        nextButton.setBounds (navigation.removeFromLeft (34));
        goButton.setBounds (navigation);
        pair (line (left), cueEdit, nameEdit, 84);
        notesEdit.setBounds (line (left));
        pair (line (left), applyHitButton, quarterBeatsEdit, 108);
        timing.setBounds (line (left, 38));
        pair (line (left), resyncButton, syncStatus, 82);

        const auto rightLine = [] (juce::Rectangle<int>& panel, int height = 23)
        {
            auto result = panel.removeFromTop (height);
            panel.removeFromTop (2);
            return result;
        };
        auto cueHeader = rightLine (right);
        cueSection.setBounds (cueHeader.removeFromLeft (juce::jmax (0, cueHeader.getWidth() - 104)));
        exportCueButton.setBounds (cueHeader);
        pair (rightLine (right), newCueButton, outCueButton, 98);
        pair (rightLine (right), cuePicker, gotoCueButton, juce::jmax (0, right.getWidth() - 78));
        pair (rightLine (right), cueIdEdit, cueTitleEdit, 85);
        cueNotesEdit.setBounds (rightLine (right));
        pair (rightLine (right), saveCueButton, deleteCueButton, 84);
        stemSection.setBounds (rightLine (right, 22));
        stemTrackPicker.setBounds (rightLine (right));
        stemFamilyPicker.setBounds (rightLine (right));
        renderMixButton.setBounds (rightLine (right));
        renderTracksButton.setBounds (rightLine (right));
        renderStemsButton.setBounds (rightLine (right));
        cancelRenderButton.setBounds (rightLine (right));
        renderStatus.setBounds (rightLine (right, 34));

        // Reserve the picture-hit ruler directly under the centered monitor.
        // A 16:9 surface prevents native AVKit video from covering dock controls.
        const int monitorHeight = juce::jmax (1, juce::jmin (centre.getHeight() - 99,
                                                       centre.getWidth() * 9 / 16));
        auto monitorArea = centre.removeFromTop (monitorHeight);
        const int monitorWidth = juce::jmin (monitorArea.getWidth(), monitorHeight * 16 / 9);
        monitorHost.setBounds (monitorArea.withSizeKeepingCentre (monitorWidth, monitorHeight));
       #if JUCE_MAC
        fitDockedVideo();
       #else
        noVideo.setBounds (monitorHost.getLocalBounds());
       #endif
        centre.removeFromTop (4);
        status.setBounds (line (centre, 23));
        strip.setBounds (centre.removeFromTop (juce::jmin (65, centre.getHeight())));
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

    void setDockedVideoViewport (juce::Rectangle<int> screenBounds)
    {
       #if JUCE_MAC
        dockedVideoViewport = screenBounds;
        fitDockedVideo();
       #else
        juce::ignoreUnused (screenBounds);
       #endif
    }

    bool isFloating() const
    {
       #if JUCE_MAC
        return floating != nullptr;
       #else
        return false;
       #endif
    }
    // The docked macOS AVKit view is a native child surface. JUCE tooltips
    // painted behind it become invisible; tell the host where that surface is.
    // Floating video has its own window and must not reserve an avoidance area.
    juce::Rectangle<int> dockedVideoScreenBounds() const
    {
       #if JUCE_MAC
        // The native child surface can occlude popups even before media loads.
        if (isShowing() && ! isFloating() && video.isVisible())
            return video.getScreenBounds();
       #endif
        return {};
    }

   #if JUCE_MAC && STUDIO_SPATIAL_TESTS
    bool testImportAndResync (const juce::File& file)
    {
        loadPicture (file, true);
        if (! video.isVideoOpen() || ! isShowing()) return false;
        for (int i = 0; i < 150 && ! (video.getVideoDuration() > 1.0); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        if (! (video.getVideoDuration() > 1.0) || video.getAudioVolume() != 0.0f)
            return false;
        setFloating (true);
        if (! isFloating() || ! video.isShowing()) return false;
        setFloating (false);
        edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (
            project.timelineSeconds (24)));
        resyncButton.onClick();
        for (int i = 0; i < 60; ++i)
        {
            if (std::abs (video.getPlayPosition() - frameSeconds (24, project.rate)) < 0.06)
                return true;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        return false;
    }
   #endif

    size_t spotCount() const { return project.spots.size(); }
    const Project& snapshot() const { return project; }

private:
   #if JUCE_MAC
    void fitDockedVideo()
    {
        if (isFloating() || video.getParentComponent() != &monitorHost)
            return;

        // AVKit's native view is parented to the window's native peer, so
        // JUCE's viewport alone cannot clip it while the dock scrolls.
        const auto visible = monitorHost.getLocalArea (nullptr, dockedVideoViewport)
                                         .getIntersection (monitorHost.getLocalBounds());
        video.setVisible (! visible.isEmpty());
        video.setBounds (visible);
    }

    juce::Rectangle<int> dockedVideoViewport;
   #endif

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
        {
            store (edit->state, project, &edit->getUndoManager());
            knownScoringNode = edit->state.getChildWithName ("STUDIO_FILM_SCORING");
        }
        strip.repaint();
    }

    void setEnabledForProject (bool enabled)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &importButton, &ratePicker, &offsetEdit,
                                   &applyOffsetButton, &addHitButton, &removeHitButton, &prevButton,
                                   &nextButton, &goButton, &markerPicker, &cueEdit, &nameEdit,
                                   &notesEdit, &applyHitButton, &quarterBeatsEdit,
                                   &cuePicker, &newCueButton, &outCueButton, &gotoCueButton,
                                   &cueIdEdit, &cueTitleEdit, &cueNotesEdit, &saveCueButton,
                                   &deleteCueButton, &exportCueButton, &renderMixButton,
                                   &renderTracksButton, &renderStemsButton,
                                   &stemTrackPicker, &stemFamilyPicker, &resyncButton })
            c->setEnabled (enabled);
        cancelRenderButton.setEnabled (enabled && cueRender.isActive());
    }

    void refreshStemTracks()
    {
        juce::StringArray names;
        std::vector<std::string> ids;
        if (edit)
            for (auto* track : tracktion::engine::getAudioTracks (*edit))
            {
                names.add (track->getName());
                ids.push_back (track->itemID.toString().toStdString());
            }
        if (names == stemTrackNames && ids == liveStemTrackIDs) return;
        std::string previous;
        const auto selected = stemTrackPicker.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (selected, (int) liveStemTrackIDs.size()))
            previous = liveStemTrackIDs[(size_t) selected];
        stemTrackNames = names;
        liveStemTrackIDs = std::move (ids);
        stemTrackPicker.clear (juce::dontSendNotification);
        int selectedId = 0;
        for (int i = 0; i < names.size(); ++i)
        {
            stemTrackPicker.addItem (names[i], i + 1);
            if (liveStemTrackIDs[(size_t) i] == previous)
                selectedId = i + 1;
        }
        if (! selectedId && ! liveStemTrackIDs.empty()) selectedId = 1;
        stemTrackPicker.setSelectedId (selectedId, juce::dontSendNotification);
        showSelectedStemFamily();
    }

    void showSelectedStemFamily()
    {
        const int index = stemTrackPicker.getSelectedItemIndex();
        const auto family = juce::isPositiveAndBelow (index, (int) liveStemTrackIDs.size())
            ? familyForTrack (stemAssignments, liveStemTrackIDs[(size_t) index])
            : StemFamily::unassigned;
        stemFamilyPicker.setSelectedId ((int) family + 1, juce::dontSendNotification);
    }

    void saveSelectedStemFamily()
    {
        if (! edit || cueRender.isActive()) return;
        const int index = stemTrackPicker.getSelectedItemIndex();
        const int familyCode = stemFamilyPicker.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, (int) liveStemTrackIDs.size())
            || ! validStemFamily (familyCode))
            return;
        if (! setStemFamily (stemAssignments, liveStemTrackIDs[(size_t) index],
                             (StemFamily) familyCode)) return;
        saveStemAssignments (edit->state, stemAssignments, &edit->getUndoManager());
        knownStemNode = edit->state.getChildWithName ("STUDIO_FILM_STEMS");
        status.setText ("Stem family assigned; saved with this project.", juce::dontSendNotification);
    }

    void choosePicture()
    {
        if (! edit) return;
        chooser = std::make_unique<juce::FileChooser> ("Choose a picture reference",
                juce::File::getSpecialLocation (juce::File::userMoviesDirectory), "*.mov;*.mp4;*.m4v");
        const auto version = editGeneration;
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<Workspace> (this), version] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->edit == nullptr || safe->editGeneration != version) return;
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
        pictureSync.reset();
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
            edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (
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
                    const auto a = edit->tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (
                        project.timelineSeconds (first->pictureFrame))).inBeats();
                    const auto b = edit->tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (
                        project.timelineSeconds (next->pictureFrame))).inBeats();
                    detail += "  |  Current " + juce::String (b - a, 2) + " beats";
                }
                timing.setText (detail, juce::dontSendNotification);
            }
            return;
        }
        timing.setText ("No later hit point in this cue", juce::dontSendNotification);
    }

    void refreshCuePicker()
    {
        sortedCueIds.clear();
        for (const auto& cue : project.cues) sortedCueIds.push_back (cue.id);
        std::sort (sortedCueIds.begin(), sortedCueIds.end(), [this] (const auto& lhs, const auto& rhs)
        {
            const auto* a = project.findCue (lhs);
            const auto* b = project.findCue (rhs);
            return a->startPictureFrame == b->startPictureFrame
                 ? lhs < rhs : a->startPictureFrame < b->startPictureFrame;
        });
        cuePicker.clear (juce::dontSendNotification);
        int selection = 0;
        for (size_t i = 0; i < sortedCueIds.size(); ++i)
        {
            const auto* cue = project.findCue (sortedCueIds[i]);
            if (! cue) continue;
            const juce::String label = juce::String (cue->id) + "  " + juce::String (cue->title)
                + "  [" + juce::String (cueTimecode (cue->startPictureFrame, project.rate))
                + " - " + (cue->endPictureFrame
                            ? juce::String (cueTimecode (*cue->endPictureFrame, project.rate)) : "open") + "]";
            cuePicker.addItem (label, (int) i + 1);
            if (cue->id == activeCueId) selection = (int) i + 1;
        }
        cuePicker.setSelectedId (selection, juce::dontSendNotification);
        populateCueFields();
    }

    void populateCueFields()
    {
        const auto* cue = project.findCue (activeCueId);
        cueIdEdit.setText (cue ? juce::String (cue->id) : "", juce::dontSendNotification);
        cueTitleEdit.setText (cue ? juce::String (cue->title) : "", juce::dontSendNotification);
        cueNotesEdit.setText (cue ? juce::String (cue->notes) : "", juce::dontSendNotification);
    }

    void addCueAtPlayhead()
    {
        if (! edit || edit->getTransport().isRecording()) return;
        const auto frame = project.pictureFrameAt (edit->getTransport().getPosition().inSeconds());
        if (! frame || *frame < 0)
        {
            status.setText ("Move the playhead inside picture time to add a cue.", juce::dontSendNotification);
            return;
        }
        int counter = 1;
        std::string id;
        do { id = "1M" + std::to_string (counter++); } while (project.findCue (id) != nullptr);
        if (! project.addCue ({ id, "New Cue", "", *frame, std::nullopt })) return;
        activeCueId = id;
        persist();
        refreshCuePicker();
        status.setText ("Created " + juce::String (id) + ". Mark End when the cue finishes.",
                        juce::dontSendNotification);
    }

    void markCueEnd()
    {
        if (! edit || edit->getTransport().isRecording()) return;
        const auto* original = project.findCue (activeCueId);
        const auto frame = project.pictureFrameAt (edit->getTransport().getPosition().inSeconds());
        if (! original || ! frame || *frame <= original->startPictureFrame)
        {
            status.setText ("Choose a cue and seek after its start to mark an end.",
                            juce::dontSendNotification);
            return;
        }
        auto updated = *original;
        updated.endPictureFrame = *frame;
        if (project.updateCue (activeCueId, std::move (updated)))
        {
            persist();
            refreshCuePicker();
        }
    }

    void applyCueChanges()
    {
        if (! edit || edit->getTransport().isRecording()) return;
        const auto* original = project.findCue (activeCueId);
        if (! original) return;
        auto updated = *original;
        updated.id = cueIdEdit.getText().trim().toStdString();
        updated.title = cueTitleEdit.getText().toStdString();
        updated.notes = cueNotesEdit.getText().toStdString();
        if (! project.updateCue (activeCueId, updated))
        {
            status.setText ("Invalid cue ID or duplicate cue number.", juce::dontSendNotification);
            return;
        }
        activeCueId = updated.id;
        persist();
        refreshCuePicker();
    }

    void deleteSelectedCue()
    {
        if (! edit || edit->getTransport().isRecording() || ! project.removeCue (activeCueId)) return;
        activeCueId.clear();
        persist();
        refreshCuePicker();
    }

    void goToSelectedCue()
    {
        if (! edit || edit->getTransport().isRecording()) return;
        if (const auto* cue = project.findCue (activeCueId))
            edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (
                juce::jmax (0.0, project.timelineSeconds (cue->startPictureFrame))));
    }

    void chooseCueDelivery (CueRenderSession::Mode mode)
    {
        if (! edit || cueRender.isActive())
            return;
        if (! canRenderAudio || ! canRenderAudio())
        {
            status.setText ("Stop transport and finish any pending ARIA review or audio export.",
                            juce::dontSendNotification);
            return;
        }
        const auto* cue = project.findCue (activeCueId);
        if (! cue || ! deliveryRange (project, *cue))
        {
            status.setText ("Choose a cue with a valid start and marked end.", juce::dontSendNotification);
            return;
        }
        const auto version = editGeneration;
        const auto pictureSnapshot = project;
        const auto selectedCue = *cue;
        const auto stemSnapshot = stemAssignments;
        deliveryChooser = std::make_unique<juce::FileChooser> ("Choose a destination for cue WAV delivery",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        deliveryChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<Workspace> (this), version,
             pictureSnapshot, selectedCue, stemSnapshot, mode] (const juce::FileChooser& dialog)
            {
                if (safe == nullptr) return;
                if (safe->edit == nullptr || version != safe->editGeneration)
                {
                    safe->status.setText ("Project changed. Cue render cancelled.", juce::dontSendNotification);
                    return;
                }
                const auto destination = dialog.getResult();
                if (destination == juce::File()) return;
                if (! safe->canRenderAudio || ! safe->canRenderAudio())
                {
                    safe->status.setText ("Studio is busy. Cue render cancelled.", juce::dontSendNotification);
                    return;
                }
                const auto result = safe->cueRender.start (*safe->edit, pictureSnapshot,
                                                            selectedCue, destination, mode, stemSnapshot);
                if (result.failed())
                    safe->status.setText ("Cue render unavailable: " + result.getErrorMessage(),
                                         juce::dontSendNotification);
                else
                    safe->status.setText ("Rendering cue " + juce::String (selectedCue.id)
                                         + " into a new folder.", juce::dontSendNotification);
            });
    }

    void exportCueSheet()
    {
        if (! edit || project.cues.empty())
        {
            status.setText ("Add a cue before exporting a cue sheet.", juce::dontSendNotification);
            return;
        }
        // Capture a snapshot before the asynchronous save dialog opens. A user
        // can change projects or edit cues while the native file panel is open.
        const auto csv = cueSheetCSV (project);
        cueExportChooser = std::make_unique<juce::FileChooser> ("Export picture cue sheet",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Studio Cue Sheet.csv"),
            "*.csv");
        cueExportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe = juce::Component::SafePointer<Workspace> (this), csv] (const juce::FileChooser& dialog)
            {
                if (safe == nullptr) return;
                auto file = dialog.getResult();
                if (file == juce::File()) return;
                file = file.withFileExtension (".csv");
                auto output = file.createOutputStream();
                const bool ok = output && output->openedOk() && output->setPosition (0)
                             && output->truncate().wasOk() && output->write (csv.data(), csv.size());
                safe->status.setText (ok ? "Cue sheet exported to " + file.getFileName()
                                       : "Unable to write cue sheet.",
                                      juce::dontSendNotification);
            });
    }

    void setFloating (bool wantFloat)
    {
       #if JUCE_MAC
        if (wantFloat == isFloating()) return;
        if (wantFloat)
        {
            floating = std::make_unique<PictureWindow>();
            floating->onDock = [safe = juce::Component::SafePointer<Workspace> (this)]
            {
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr) safe->setFloating (false);
                });
            };
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
            fitDockedVideo();
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
   #else
    juce::Label noVideo;
   #endif

    tracktion::engine::Edit* edit = nullptr;
    Project project;
    juce::ValueTree knownScoringNode, knownStemNode;
    std::vector<StemAssignment> stemAssignments;
    juce::StringArray stemTrackNames;
    std::vector<std::string> liveStemTrackIDs;
    std::string activeId;
    std::vector<std::string> sortedIds, sortedCueIds;
    std::string activeCueId;
    PictureSyncController pictureSync;
    CueRenderSession cueRender;
    uint64_t editGeneration = 0;
    std::unique_ptr<juce::FileChooser> chooser, cueExportChooser, deliveryChooser;
    juce::Component monitorHost;
    SpottingStrip strip;
    juce::Label timecode, status, timing, syncStatus;
    juce::TextButton importButton { "Import Picture" }, floatButton { "Float" };
    juce::ComboBox ratePicker;
    juce::TextEditor offsetEdit, cueEdit, nameEdit, notesEdit, quarterBeatsEdit;
    juce::TextButton applyOffsetButton { "Set Offset" }, addHitButton { "Add Hit" },
                     removeHitButton { "Remove" }, prevButton { "<" }, nextButton { ">" },
                     goButton { "Go To" }, applyHitButton { "Apply Hit" };
    juce::ComboBox markerPicker, cuePicker, stemTrackPicker, stemFamilyPicker;
    juce::Label cueSection, stemSection;
    juce::TextEditor cueIdEdit, cueTitleEdit, cueNotesEdit;
    juce::TextButton newCueButton { "+ Cue" }, outCueButton { "Mark End" },
                     gotoCueButton { "Go Cue" }, saveCueButton { "Apply" },
                     deleteCueButton { "Delete Cue" }, exportCueButton { "Export CSV" };
    juce::TextButton renderMixButton { "Render Cue WAV" },
                     renderTracksButton { "Render Track WAVs" },
                     renderStemsButton { "Render Family Stems" },
                     cancelRenderButton { "Cancel Render" }, resyncButton { "Resync" };
    juce::Label renderStatus;
};
} // namespace studio::film
