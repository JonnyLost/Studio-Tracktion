// SPDX-License-Identifier: GPL-3.0-or-later
#include <JuceHeader.h>
#include <vector>
#include <functional>
using namespace juce;
using namespace tracktion::literals;

#include "Utilities.h"
#include "Components.h"
#include "PluginWindow.h"
#include "MidiMonitor.h"
#include "PianoRoll.h"
#include "MusicalRuler.h"

class MidiMonitorWindow final : public DocumentWindow
{
public:
    MidiMonitorWindow()
        : DocumentWindow ("Studio — MIDI Monitor", Colour (0xff172027), closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (770, 200, 2200, 1000);
    }

    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }
};

class WorkspaceDivider final : public Component
{
public:
    explicit WorkspaceDivider (bool horizontalDrag)
        : across (horizontalDrag)
    {
        setMouseCursor (across ? MouseCursor::LeftRightResizeCursor : MouseCursor::UpDownResizeCursor);
    }

    std::function<int()> getSize;
    std::function<void(int)> resizePane;

    void paint (Graphics& g) override
    {
        g.setColour (Colour (isMouseOverOrDragging() ? 0xffc5a56b : 0xff3c4954));
        auto bounds = getLocalBounds();
        if (across)
            g.fillRect (bounds.withSizeKeepingCentre (2, jmin (36, getHeight())));
        else
            g.fillRect (bounds.withSizeKeepingCentre (jmin (36, getWidth()), 2));
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseDown (const MouseEvent&) override { initialSize = getSize(); }
    void mouseDrag (const MouseEvent& e) override
    {
        resizePane (initialSize + (across ? e.getDistanceFromDragStartX() : -e.getDistanceFromDragStartY()));
    }

private:
    bool across;
    int initialSize = 0;
};

class StudioView final : public Component, private Timer
{
public:
    StudioView()
    {
        style.setColour (TextButton::buttonColourId, Colour (0xff303b45));
        style.setColour (TextButton::buttonOnColourId, Colour (0xff665333));
        style.setColour (TextButton::textColourOffId, Colour (0xffe6edf2));
        style.setColour (ComboBox::backgroundColourId, Colour (0xff29343e));
        style.setColour (ComboBox::outlineColourId, Colour (0xff4b5964));
        style.setColour (Slider::trackColourId, Colour (0xffc5a56b));
        setLookAndFeel (&style);
        setWantsKeyboardFocus (true);
        for (auto* control : { &newProject, &openProject, &saveProject, &saveAsProject,
                               &audioSettings, &plugins,
                               &rewind, &play, &record, &addMidi, &addAudio, &importAudio })
            addAndMakeVisible (*control);

        addAndMakeVisible (arrangementZoomOut);
        addAndMakeVisible (arrangementZoomIn);
        arrangementZoomOut.setTooltip ("Zoom out in the arrangement timeline");
        arrangementZoomIn.setTooltip ("Zoom in on the arrangement playhead");
        arrangementZoomOut.onClick = [this] { zoomArrangement (1.25); };
        arrangementZoomIn.onClick = [this] { zoomArrangement (1.0 / 1.25); };

        addAndMakeVisible (monitorButton);
        addAndMakeVisible (retryAriaButton);
        addAndMakeVisible (confirmAriaButton);
        addChildComponent (ariaReviewNotice);
        // Keep this notice to two short lines: the sidebar is narrow on a MacBook.
        ariaReviewNotice.setText ("ARIA CHECK\nPlay, then confirm to save.",
                                 dontSendNotification);
        ariaReviewNotice.setFont (FontOptions (12.0f, Font::bold));
        ariaReviewNotice.setColour (Label::textColourId, Colour (0xffffcb70));
        ariaReviewNotice.setJustificationType (Justification::centredLeft);
        ariaReviewNotice.setTooltip ("When reopening an ARIA project, play the instrument to check its samples, "
                                     "then click Confirm ARIA to unlock saving. Retry ARIA reloads the original AU state.");
        retryAriaButton.onClick = [this] { retryAriaRestoration(); };
        confirmAriaButton.onClick = [this]
        {
            if (! ariaReviewPending)
                return;
            ariaReviewPending = false;
            autoAriaRetryPending = false;
            writeRestoreLog ("User confirmed instrument state; project may now be saved.");
            message.setText ("ARIA state confirmed. Save is enabled.", dontSendNotification);
            resized();
        };
        addChildComponent (monitor);
        monitorButton.onClick = [this]
        {
            if (monitorWindow != nullptr && monitorWindow->isVisible())
            {
                monitorWindow->toFront (true);
                return;
            }
            showWorkspace (workspace == Workspace::midi ? Workspace::closed : Workspace::midi);
        };
        addAndMakeVisible (mixButton);
        mixButton.onClick = [this]
        {
            showWorkspace (workspace == Workspace::mix ? Workspace::closed : Workspace::mix);
        };
        addAndMakeVisible (pianoButton);
        pianoButton.onClick = [this]
        {
            if (piano.getClip() != nullptr)
                showWorkspace (workspace == Workspace::piano ? Workspace::closed : Workspace::piano);
            else
                message.setText ("Select a MIDI clip first, or create one with + MIDI Clip.", dontSendNotification);
        };
        addAndMakeVisible (closeWorkspace);
        closeWorkspace.onClick = [this] { showWorkspace (Workspace::closed); };

        addAndMakeVisible (sidebarDivider);
        sidebarDivider.getSize = [this] { return sidebar.getWidth(); };
        sidebarDivider.resizePane = [this] (int size)
        {
            sidebarWidth = jlimit (155, 280, size);
            resized();
        };
        addAndMakeVisible (workspaceDivider);
        workspaceDivider.getSize = [this] { return workspacePanel.getHeight(); };
        workspaceDivider.resizePane = [this] (int size)
        {
            workspaceHeight = jlimit (146, workspaceLimit, size);
            resized();
        };
        addAndMakeVisible (trackPicker);
        trackPicker.setTextWhenNothingSelected ("Select a track");
        trackPicker.setTitle ("Select a track");
        trackPicker.onChange = [this]
        {
            if (edit == nullptr)
                return;
            const auto tracks = te::getAudioTracks (*edit);
            const int index = trackPicker.getSelectedItemIndex();
            if (isPositiveAndBelow (index, tracks.size()))
                selection.selectOnly (tracks[index]);
        };
        monitor.onToggleDock = [this] { toggleMonitorDock(); };

        addAndMakeVisible (timeline);
        timeline.setScrollBarsShown (true, false);
        addAndMakeVisible (piano);
        piano.setVisible (false);
        piano.onMidiChanged = [this] { if (editor != nullptr) editor->repaint(); };
        for (auto* label : { &title, &position, &tempo, &mixerLabel, &message, &trackHeading, &trackDetails, &volumeLabel, &panLabel, &workspaceTitle })
            addAndMakeVisible (*label);
        title.setFont (FontOptions (23.0f, Font::bold));
        title.setText ("Studio 0.2", dontSendNotification);
        title.setColour (Label::textColourId, Colour (0xffedcc8d));
        position.setFont (FontOptions (19.0f, Font::bold));
        position.setJustificationType (Justification::centred);
        position.setText ("00:00.00", dontSendNotification);
        tempo.setJustificationType (Justification::centred);
        trackHeading.setText ("Tracks", dontSendNotification);
        volumeLabel.setText ("Volume", dontSendNotification);
        panLabel.setText ("Pan", dontSendNotification);
        zoom.setTitle ("Timeline zoom");
        addAndMakeVisible (zoom);
        zoom.addItem ("10 seconds", 1);
        zoom.addItem ("30 seconds", 2);
        zoom.addItem ("60 seconds", 3);
        zoom.addItem ("180 seconds", 4);
        zoom.addItem ("Fit project", 5);
        zoom.addItem ("Custom zoom", 6);
        zoom.setSelectedId (2);
        zoom.onChange = [this]
        {
            if (editor != nullptr)
            {
                if (zoom.getSelectedId() == 6)
                    return;
                constexpr double spans[] { 10, 30, 60, 180 };
                auto& view = editor->getEditViewState();
                view.viewX1 = 0s;
                const double seconds = zoom.getSelectedId() == 5
                    ? jmax (10.0, edit->getLength().inSeconds() * 1.05)
                    : spans[jlimit (1, 4, zoom.getSelectedId()) - 1];
                view.viewX2 = te::TimePosition::fromSeconds (seconds);
                repaint();
            }
        };

        setupSlider (volume, "Volume", -60, 6, 0.1, " dB");
        setupSlider (pan, "Pan", -1, 1, 0.01, {});
        volume.onValueChange = [this]
        {
            if (auto* track = selectedTrack())
                if (auto plugin = track->getVolumePlugin())
                    plugin->setVolumeDb (static_cast<float> (volume.getValue()));
        };
        pan.onValueChange = [this]
        {
            if (auto* track = selectedTrack())
                if (auto plugin = track->getVolumePlugin())
                    plugin->setPan (static_cast<float> (pan.getValue()));
        };

        newProject.onClick = [this] { chooseProject (true); };
        openProject.onClick = [this] { chooseProject (false); };
        saveProject.onClick = [this] { save(); };
        saveAsProject.onClick = [this] { chooseSaveAs(); };
        audioSettings.onClick = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        plugins.onClick = [this] { showPlugins(); };
        rewind.onClick = [this] { edit->getTransport().setPosition (0s); };
        play.onClick = [this]
        {
            if (edit->getTransport().isRecording())
                toggleRecordKeepingTake();
            else
                EngineHelpers::togglePlay (*edit);
        };
        record.onClick = [this] { toggleRecordKeepingTake(); };
        addMidi.onClick = [this] { addTrack ("Instrument"); };
        addAndMakeVisible (addMidiClip);
        addMidiClip.onClick = [this] { createMidiClip(); };
        addAudio.onClick = [this] { addTrack ("Audio"); };
        importAudio.onClick = [this] { chooseAudio(); };

        message.setText ("Create or open a project to begin.", dontSendNotification);
        setSize (1100, 650);
        showWorkspace (Workspace::closed);
        timerCallback();
        startTimerHz (24);
    }

    ~StudioView() override
    {
        if (monitorWindow != nullptr)
            monitorWindow->clearContentComponent();
        monitor.setCaptureOpen (false);
        piano.setClip (nullptr);
        timeline.setViewedComponent (nullptr);
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void requestQuit()
    {
        if (busy)
            return;
        if (ariaReviewPending)
        {
            AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon,
                "Unverified ARIA restoration",
                "The saved project contains ARIA state that has not been confirmed as restored. "
                "Quit without saving? The original project and its recovery copy are preserved.",
                "Quit without saving", "Keep working", nullptr,
                ModalCallbackFunction::create ([] (int result)
                {
                    if (result != 0)
                        JUCEApplicationBase::quit();
                }));
            return;
        }
        save ([] (bool ok) { if (ok) JUCEApplicationBase::quit(); });
    }

    void paint (Graphics& graphics) override
    {
        graphics.fillAll (Colour (0xff151c24));
        graphics.setColour (Colour (0xff202a33));
        graphics.fillRect (topBar);
        graphics.fillRect (sidebar);
        graphics.fillRect (workspacePanel);
        graphics.setColour (Colour (0xff293540));
        graphics.fillRect (rulerRow);
        graphics.setColour (Colour (0xffaebdc9));
        graphics.drawText ("Tracks", rulerRow.withWidth (72).reduced (10, 0),
                           Justification::centredLeft);
        auto area = ruler;
        graphics.drawText ("Inserts", rulerRow.withLeft (area.getRight()).reduced (10, 0),
                           Justification::centredLeft);
        if (editor == nullptr)
        {
            graphics.setColour (Colour (0xffaebdc9));
            graphics.drawText ("Create or open a project to start arranging.", timeline.getBounds(),
                               Justification::centred);
        }
        if (editor == nullptr || area.isEmpty())
            return;

        auto& view = editor->getEditViewState();
        int lastLabelRight = area.getX() - 50;
        studio::forEachMusicalTick (edit->tempoSequence, view.viewX1.get(), view.viewX2.get(),
            [&] (const studio::MusicalTick& tick)
            {
                const int x = area.getX() + view.timeToX (tick.time, area.getWidth());
                if (x < area.getX() || x >= area.getRight())
                    return;
                const bool bar = tick.beatIndex == 0;
                graphics.setColour (bar ? Colour (0xffd3ba87) : Colour (0xff768c9a));
                graphics.drawVerticalLine (x, (float) (area.getBottom() - (bar ? 13 : 7)),
                                            (float) area.getBottom());
                if (bar && x >= lastLabelRight)
                {
                    graphics.drawText (String (tick.barIndex + 1), x + 4, area.getY(), 44,
                                       area.getHeight() - 5, Justification::centredLeft);
                    lastLabelRight = x + 42;
                }
            });
    }

    void paintOverChildren (Graphics& g) override
    {
        if (edit == nullptr || editor == nullptr || ruler.isEmpty())
            return;
        const auto area = Rectangle<int> (ruler.getX(), timeline.getY(),
                                          ruler.getWidth(), timeline.getHeight());
        if (area.isEmpty())
            return;
        g.saveState();
        g.reduceClipRegion (area);
        auto& view = editor->getEditViewState();
        studio::forEachMusicalTick (edit->tempoSequence, view.viewX1.get(), view.viewX2.get(),
            [&] (const studio::MusicalTick& tick)
            {
                const int x = area.getX() + view.timeToX (tick.time, area.getWidth());
                if (x < area.getX() || x >= area.getRight())
                    return;
                g.setColour (tick.beatIndex == 0 ? Colour (0x557d95a4) : Colour (0x223b5664));
                g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
            });
        g.restoreState();
    }

    void mouseDown (const MouseEvent& event) override
    {
        if (editor == nullptr || busy || edit->getTransport().isRecording() || ! ruler.contains (event.getPosition()))
            return;

        auto& view = editor->getEditViewState();
        edit->getTransport().setPosition (view.xToTime (event.x - ruler.getX(), ruler.getWidth()));
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey && play.isEnabled())
        {
            play.onClick();
            return true;
        }
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'S' || key.getKeyCode() == 's')
            && saveProject.isEnabled())
        {
            if (key.getModifiers().isShiftDown())
                chooseSaveAs();
            else
                save();
            return true;
        }
        return false;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        topBar = area.removeFromTop (88);
        auto files = topBar.reduced (12, 5).removeFromTop (34);
        for (auto* button : { &plugins, &audioSettings, &saveAsProject, &saveProject,
                              &openProject, &newProject })
            button->setBounds (files.removeFromRight (button == &plugins ? 116 : 94).reduced (3, 2));
        title.setBounds (files);

        auto controls = topBar.withTrimmedTop (44).reduced (12, 5);
        rewind.setBounds (controls.removeFromLeft (92).reduced (3, 2));
        play.setBounds (controls.removeFromLeft (70).reduced (3, 2));
        record.setBounds (controls.removeFromLeft (126).reduced (3, 2));
        position.setBounds (controls.removeFromLeft (110));
        tempo.setBounds (controls.removeFromLeft (93));
        monitorButton.setBounds (controls.removeFromRight (132).reduced (3, 2));
        mixButton.setBounds (controls.removeFromRight (74).reduced (3, 2));
        pianoButton.setBounds (controls.removeFromRight (98).reduced (3, 2));

        message.setBounds (area.removeFromBottom (26).reduced (10, 0));
        workspaceLimit = jmax (146, area.getHeight() - 180);
        workspacePanel = workspace == Workspace::closed ? Rectangle<int>()
                       : area.removeFromBottom (jlimit (146, workspaceLimit, workspaceHeight));
        auto panel = workspacePanel.reduced (10, 0);
        auto panelHeader = panel.removeFromTop (30);
        closeWorkspace.setBounds (panelHeader.removeFromRight (66).reduced (0, 3));
        workspaceTitle.setBounds (panelHeader);
        mixerLabel.setBounds (panel.removeFromTop (30));
        auto volumeRow = panel.removeFromTop (34);
        volumeLabel.setBounds (volumeRow.removeFromLeft (74));
        volume.setBounds (volumeRow.removeFromLeft (jmin (430, volumeRow.getWidth())));
        if (monitor.getParentComponent() == this && workspace == Workspace::midi)
            monitor.setBounds (workspacePanel.withTrimmedTop (30));
        if (workspace == Workspace::piano)
            piano.setBounds (workspacePanel.withTrimmedTop (30));
        auto panRow = panel.removeFromTop (34);
        panLabel.setBounds (panRow.removeFromLeft (74));
        pan.setBounds (panRow.removeFromLeft (jmin (430, panRow.getWidth())));
        workspaceDivider.setBounds (workspace == Workspace::closed ? Rectangle<int>() : area.removeFromBottom (7));

        sidebar = area.removeFromLeft (sidebarWidth);
        sidebarDivider.setBounds (area.removeFromLeft (7));
        auto left = sidebar.reduced (10, 6);
        trackHeading.setBounds (left.removeFromTop (27));
        trackPicker.setBounds (left.removeFromTop (32).reduced (0, 3));
        ariaReviewNotice.setVisible (ariaReviewPending);
        retryAriaButton.setVisible (ariaReviewPending);
        confirmAriaButton.setVisible (ariaReviewPending);
        if (ariaReviewPending)
        {
            ariaReviewNotice.setBounds (left.removeFromTop (42));
            retryAriaButton.setBounds (left.removeFromTop (28).reduced (0, 2));
            confirmAriaButton.setBounds (left.removeFromTop (28).reduced (0, 2));
        }
        else
        {
            ariaReviewNotice.setBounds ({});
            retryAriaButton.setBounds ({});
            confirmAriaButton.setBounds ({});
        }
        trackDetails.setBounds (left.removeFromTop (27));
        left.removeFromTop (6);
        for (auto* button : { &addMidi, &addAudio, &importAudio })
            button->setBounds (left.removeFromTop (31).reduced (0, 3));
        addMidiClip.setBounds (left.removeFromTop (31).reduced (0, 3));
        left.removeFromTop (7);
        zoom.setBounds (left.removeFromTop (31).reduced (0, 3));

        rulerRow = area.removeFromTop (26);
        timeline.setBounds (area);
        sizeEditor();
        ruler = rulerRow.withTrimmedLeft (150).withTrimmedRight (150 + timeline.getScrollBarThickness());
        arrangementZoomOut.setBounds (rulerRow.getX() + 73, rulerRow.getY() + 2, 34, 22);
        arrangementZoomIn.setBounds (rulerRow.getX() + 112, rulerRow.getY() + 2, 34, 22);
        repaint();
    }

private:
    enum class Workspace { closed, mix, midi, piano };

    LookAndFeel_V4 style;
    Workspace workspace = Workspace::closed;
    WorkspaceDivider sidebarDivider { true }, workspaceDivider { false };
    int sidebarWidth = 184, workspaceHeight = 210, workspaceLimit = 210;
    Rectangle<int> topBar, sidebar, workspacePanel, rulerRow;
    te::Engine engine { "Studio 0.1", std::make_unique<ExtendedUIBehaviour>(), nullptr };
    te::SelectionManager selection { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editor;
    Viewport timeline;
    std::unique_ptr<FileChooser> chooser;
    File projectFile;
    Rectangle<int> ruler;
    bool busy = false;
    bool ariaReviewPending = false;
    // One bounded, message-thread-only attempt per project load. The original AU
    // states remain immutable and Save remains locked until manual verification.
    bool autoAriaRetryPending = false;
    uint32 autoAriaRetryScheduledAt = 0;
    uint32 lastRecordingStopRequestedAt = 0;
    File restoreLogFile;

    // Immutable snapshots taken from the on-disk XML BEFORE Tracktion loads the Edit.
    // Never obtain retry data from ExternalPlugin::state after instantiation:
    // an empty AU may already have overwritten that state in memory.
    struct OriginalAriaState
    {
        String pluginId;
        String encodedState;
        String programNumber;
        size_t decodedBytes = 0;
    };
    std::vector<OriginalAriaState> originalAriaStates;


    Label title, position, tempo, mixerLabel, message, trackHeading, trackDetails,
          volumeLabel, panLabel, workspaceTitle, ariaReviewNotice;
    ComboBox zoom, trackPicker;
    StringArray trackNames;
    TextButton arrangementZoomOut { "-" }, arrangementZoomIn { "+" };
    TextButton mixButton { "Mixer" }, pianoButton { "Piano Roll" }, closeWorkspace { "Hide" };
    Slider volume, pan;
    StudioMidiMonitor monitor;
    StudioPianoRoll piano;
    std::unique_ptr<MidiMonitorWindow> monitorWindow;
    TextButton monitorButton { "MIDI Monitor" };
    TextButton retryAriaButton { "Retry ARIA" };
    TextButton confirmAriaButton { "Confirm ARIA" };
    TextButton newProject { "New Project" }, openProject { "Open Project" },
               saveProject { "Save" }, saveAsProject { "Save As" },
               audioSettings { "Audio Settings" }, plugins { "Plugin Manager" },
               rewind { "To Start" }, play { "Play" }, record { "Record" },
               addMidi { "+ Instrument" }, addAudio { "+ Audio" }, importAudio { "Import Audio" },
               addMidiClip { "+ MIDI Clip" };

    void showWorkspace (Workspace next)
    {
        workspace = next;
        const bool open = next != Workspace::closed;
        const bool mixing = next == Workspace::mix;
        for (Component* component : std::initializer_list<Component*> {
                 &mixerLabel, &volumeLabel, &panLabel, &volume, &pan })
            component->setVisible (mixing);
        closeWorkspace.setVisible (open);
        workspaceTitle.setVisible (open);
        workspaceDivider.setVisible (open);
        workspaceTitle.setText (mixing ? "Selected Track Mixer" : "MIDI Monitor", dontSendNotification);
        if (next == Workspace::piano)
            workspaceTitle.setText ("MIDI Piano Roll", dontSendNotification);
        mixButton.setToggleState (mixing, dontSendNotification);
        pianoButton.setToggleState (next == Workspace::piano, dontSendNotification);
        piano.setVisible (next == Workspace::piano);
        monitorButton.setToggleState (next == Workspace::midi, dontSendNotification);
        if (monitor.getParentComponent() == this)
        {
            monitor.setVisible (next == Workspace::midi);
            monitor.setCaptureOpen (next == Workspace::midi);
        }
        resized();
    }

    void createMidiClip()
    {
        if (edit == nullptr || busy || edit->getTransport().isRecording())
            return;
        auto* track = selectedTrack();
        if (track == nullptr)
            track = addTrack ("Instrument");
        if (track == nullptr)
            return;

        const auto start = edit->getTransport().getPosition();
        const auto startBeat = edit->tempoSequence.toBeats (start);
        const auto end = edit->tempoSequence.toTime (startBeat + te::BeatDuration::fromBeats (16.0));
        if (auto clip = track->insertMIDIClip ("MIDI Clip", { start, end }, &selection))
        {
            piano.setClip (clip);
            workspaceHeight = 360;
            showWorkspace (Workspace::piano);
            message.setText ("Draw notes in the piano roll, then Save to keep your edits.", dontSendNotification);
        }
    }

    void toggleMonitorDock()
    {
        if (monitorWindow != nullptr && monitorWindow->isVisible())
        {
            monitorWindow->setVisible (false);
            monitorWindow->clearContentComponent();
            addAndMakeVisible (monitor);
            monitor.setFloating (false);
            showWorkspace (Workspace::midi);
        }
        else
        {
            if (monitorWindow == nullptr)
            {
                monitorWindow = std::make_unique<MidiMonitorWindow>();
                monitorWindow->onClose = [this] { toggleMonitorDock(); };
                monitorWindow->centreWithSize (860, 350);
            }
            monitorWindow->setContentNonOwned (&monitor, false);
            monitor.setVisible (true);
            monitor.setFloating (true);
            monitorWindow->setVisible (true);
            monitorWindow->toFront (true);
            showWorkspace (Workspace::closed);
        }
    }

    void setupSlider (Slider& slider, const String& name, double min, double max, double interval, const String& suffix)
    {
        slider.setName (name);
        slider.setTooltip (name);
        slider.setRange (min, max, interval);
        slider.setTextValueSuffix (suffix);
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::TextBoxRight, false, 78, 24);
        addAndMakeVisible (slider);
    }

    te::AudioTrack* selectedTrack()
    {
        return dynamic_cast<te::AudioTrack*> (selection.getSelectedObject (0));
    }

    void sizeEditor()
    {
        if (editor != nullptr)
        {
            const int width = jmax (1, timeline.getMaximumVisibleWidth() - timeline.getScrollBarThickness());
            const int height = jmax (timeline.getMaximumVisibleHeight(),
                                     52 * (int) te::getAudioTracks (*edit).size());
            if (editor->getWidth() != width || editor->getHeight() != height)
                editor->setSize (width, height);
        }
    }

    void zoomArrangement (double scale)
    {
        if (edit == nullptr || editor == nullptr)
            return;

        auto& view = editor->getEditViewState();
        const double start = view.viewX1.get().inSeconds();
        const double end = view.viewX2.get().inSeconds();
        const double oldSpan = jmax (0.001, end - start);
        const double newSpan = jlimit (1.0, 3600.0, oldSpan * scale);
        double anchor = edit->getTransport().getPosition().inSeconds();
        if (anchor < start || anchor > end)
            anchor = (start + end) / 2.0;

        const double anchorRatio = (anchor - start) / oldSpan;
        const double nextStart = jmax (0.0, anchor - anchorRatio * newSpan);
        view.viewX1 = te::TimePosition::fromSeconds (nextStart);
        view.viewX2 = te::TimePosition::fromSeconds (nextStart + newSpan);
        zoom.setSelectedId (6, dontSendNotification);
        repaint();
    }

    void timerCallback() override
    {
        monitorButton.setButtonText (monitor.isActive() ? "●  MIDI Monitor" : "MIDI Monitor");
        monitorButton.setColour (TextButton::textColourOffId,
                                 monitor.isActive() ? Colour (0xff8edec0) : Colours::white);
        // Tracktion creates AU instances during project setup. The successful
        // manual retry proves that resubmitting the *on-disk* state after loading
        // can restore ARIA. Defer one automatic attempt until instances are
        // available and have had a short settling interval (not audio callback).
        maybeAutoRestoreAria();
        const bool ready = edit != nullptr && ! busy;
        const bool recording = edit != nullptr && edit->getTransport().isRecording();
        for (auto* button : { &newProject, &openProject, &audioSettings, &plugins })
            button->setEnabled (! busy && ! recording);
        for (auto* button : { &saveProject, &saveAsProject, &rewind, &addMidi,
                              &addAudio, &importAudio, &addMidiClip })
            button->setEnabled (ready && ! recording);
        // ARIA review protects Save and the original AU state, not MIDI editing.
        // A loaded project must remain editable while its ARIA slots are checked.
        piano.setEditingEnabled (ready && ! recording);
        pianoButton.setEnabled (ready && piano.getClip() != nullptr);
        play.setEnabled (ready);
        record.setEnabled (ready);
        zoom.setEnabled (ready);
        arrangementZoomOut.setEnabled (ready);
        arrangementZoomIn.setEnabled (ready);
        retryAriaButton.setEnabled (ready && ! recording && ariaReviewPending);
        confirmAriaButton.setEnabled (ready && ! recording && ariaReviewPending && ! autoAriaRetryPending);
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            play.setButtonText (transport.isPlaying() ? "Stop" : "Play");
            record.setButtonText (recording ? "Stop Recording" : "Record");
            const auto now = transport.getPosition();
            piano.setTransportPosition (now);
            const auto centiseconds = static_cast<int64> (juce::jmax (0.0, now.inSeconds()) * 100.0);
            position.setText (String (centiseconds / 6000).paddedLeft ('0', 2) + ":"
                                  + String (centiseconds / 100 % 60).paddedLeft ('0', 2) + "."
                                  + String (centiseconds % 100).paddedLeft ('0', 2), dontSendNotification);
            tempo.setText (String (edit->tempoSequence.getBpmAt (now), 1) + " BPM", dontSendNotification);
            sizeEditor();
        }

        if (auto* selectedClip = dynamic_cast<te::MidiClip*> (selection.getSelectedObject (0)))
            if (piano.getClip() != selectedClip)
            {
                piano.setClip (selectedClip);
                workspaceHeight = 360;
                showWorkspace (Workspace::piano);
            }

        auto* track = selectedTrack();
        StringArray names;
        int selectedIndex = -1;
        if (edit != nullptr)
            for (auto* item : te::getAudioTracks (*edit))
            {
                if (item == track)
                    selectedIndex = names.size();
                names.add (item->getName());
            }
        if (names != trackNames)
        {
            trackPicker.clear (dontSendNotification);
            trackPicker.addItemList (names, 1);
            trackNames = std::move (names);
        }
        trackPicker.setEnabled (ready && ! trackNames.isEmpty());
        trackPicker.setSelectedItemIndex (selectedIndex, dontSendNotification);
        trackDetails.setText (track == nullptr ? String (trackNames.size()) + " tracks"
            : EngineHelpers::isTrackArmed (*track) ? "Record armed"
            : EngineHelpers::trackHasInput (*track) ? "Input assigned" : "No input assigned", dontSendNotification);

        auto plugin = track != nullptr ? track->getVolumePlugin() : nullptr;
        volume.setEnabled (ready && plugin != nullptr);
        pan.setEnabled (ready && plugin != nullptr);
        mixerLabel.setText (track != nullptr ? track->getName() + " mix" : "Select a track to mix", dontSendNotification);
        if (plugin != nullptr)
        {
            if (! volume.isMouseButtonDown())
                volume.setValue (plugin->getVolumeDb(), dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (plugin->getPan(), dontSendNotification);
        }
    }

    // Diagnostics intentionally contain metadata only, never AU state bytes or sample paths.
    void writeRestoreLog (const String& detail)
    {
        if (restoreLogFile == File())
        {
            auto folder = File::getSpecialLocation (File::userApplicationDataDirectory)
                              .getChildFile ("Studio");
            folder.createDirectory();
            restoreLogFile = folder.getChildFile ("aria-restore.log");
        }
        restoreLogFile.appendText (Time::getCurrentTime().toISO8601 (true) + " | " + detail + "\n");
    }

    static bool isAriaPlugin (te::ExternalPlugin& plugin)
    {
        return plugin.getName().containsIgnoreCase ("ARIA Player");
    }

    // Read directly from the disk XML, not from a live Tracktion plugin.
    // The latter can already contain the AU's freshly initialized empty state.
    static std::vector<OriginalAriaState> readOriginalAriaStates (const XmlElement& root)
    {
        std::vector<OriginalAriaState> result;
        std::function<void(const XmlElement&)> visit = [&] (const XmlElement& element)
        {
            if (element.hasTagName ("PLUGIN")
                && element.getStringAttribute ("name").containsIgnoreCase ("ARIA Player"))
            {
                auto encoded = element.getStringAttribute ("state");
                MemoryBlock decoded;
                if (encoded.isNotEmpty() && decoded.fromBase64Encoding (encoded)
                    && decoded.getSize() > 0)
                {
                    result.push_back ({ element.getStringAttribute ("id"),
                                        encoded,
                                        element.getStringAttribute ("programNum"),
                                        decoded.getSize() });
                }
            }
            forEachXmlChildElement (element, child)
                visit (*child);
        };
        visit (root);
        return result;
    }

    static File ariaBackupFor (const File& file)
    {
        return file.getSiblingFile (file.getFileNameWithoutExtension()
                         + ".pre-aria-restore.tracktionedit");
    }

    void prepareAriaRecoveryOnLoad (bool backupAvailable)
    {
        ariaReviewPending = ! originalAriaStates.empty();
        if (! ariaReviewPending)
            return;

        for (const auto& saved : originalAriaStates)
            writeRestoreLog ("Captured original on-disk ARIA state before load: plugin id="
                             + saved.pluginId + "; bytes=" + String (saved.decodedBytes));
        writeRestoreLog ("Original on-disk ARIA states=" + String (originalAriaStates.size())
                         + "; backup " + (backupAvailable ? "available" : "FAILED")
                         + "; Save locked pending verification.");
        if (! backupAvailable)
            message.setText ("WARNING: could not back up ARIA project. Saving is blocked.",
                             dontSendNotification);
    }

    void maybeAutoRestoreAria()
    {
        if (! autoAriaRetryPending || ! ariaReviewPending || edit == nullptr || busy)
            return;

        if (edit->getTransport().isPlaying() || edit->getTransport().isRecording())
            return;

        const auto elapsed = Time::getMillisecondCounter() - autoAriaRetryScheduledAt;
        if (elapsed < 1000)
            return;

        // Wait for all matching ARIA instances. This is a bounded readiness
        // check, not a blind repeating state injection.
        int readyCount = 0;
        for (const auto& saved : originalAriaStates)
        {
            bool foundReady = false;
            for (auto* track : te::getAudioTracks (*edit))
                for (auto* plugin : track->pluginList)
                    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                        if (isAriaPlugin (*external)
                            && external->state.getProperty ("id").toString() == saved.pluginId)
                        {
                            external->initialiseFully();
                            foundReady = ! external->isInitialisingAsync()
                                         && external->getAudioPluginInstance() != nullptr;
                            break;
                        }
            if (foundReady)
                ++readyCount;
        }

        if (readyCount != static_cast<int> (originalAriaStates.size()))
        {
            if (elapsed >= 10000)
            {
                autoAriaRetryPending = false;
                writeRestoreLog ("Automatic ARIA retry timed out: ready=" + String (readyCount)
                                 + "/" + String (originalAriaStates.size())
                                 + "; manual Retry ARIA remains available.");
                message.setText ("ARIA not ready automatically. Use Retry ARIA; do not save until verified.",
                                 dontSendNotification);
            }
            return;
        }

        autoAriaRetryPending = false;  // Exactly one attempt for this project load.
        writeRestoreLog ("Automatic ARIA retry: all " + String (readyCount)
                         + " plugin(s) instantiated; using original on-disk state.");
        retryAriaRestoration (true);
    }

    void retryAriaRestoration (bool automatic = false)
    {
        if (! automatic)
            autoAriaRetryPending = false;
        if (edit == nullptr || ! ariaReviewPending || edit->getTransport().isPlaying()
            || edit->getTransport().isRecording())
        {
            message.setText ("Stop playback before retrying ARIA state.", dontSendNotification);
            return;
        }

        int attempted = 0, unavailable = 0, unmatched = 0;
        for (const auto& original : originalAriaStates)
        {
            te::ExternalPlugin* target = nullptr;
            String trackName;
            for (auto* track : te::getAudioTracks (*edit))
                for (auto plugin : track->pluginList)
                    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                        if (isAriaPlugin (*external)
                            && external->state.getProperty ("id").toString() == original.pluginId)
                        {
                            target = external;
                            trackName = track->getName();
                            break;
                        }

            if (target == nullptr)
            {
                ++unmatched;
                writeRestoreLog ("Retry skipped: cannot match ARIA plugin id=" + original.pluginId);
                continue;
            }

            target->initialiseFully();
            if (target->getAudioPluginInstance() == nullptr)
            {
                ++unavailable;
                writeRestoreLog ("Retry skipped " + trackName + ": instance unavailable; error="
                                 + target->getLoadError());
                continue;
            }

            // Temporary tree contains the true original bytes from disk, *not*
            // target->state, which Tracktion/ARIA may have modified on load.
            ValueTree originalState ("PLUGIN");
            originalState.setProperty ("state", original.encodedState, nullptr);
            if (original.programNumber.isNotEmpty())
                originalState.setProperty ("programNum", original.programNumber.getIntValue(), nullptr);

            MemoryBlock before;
            target->getPluginStateFromTree (before);
            writeRestoreLog ("Retry original ARIA " + trackName + ": disk bytes="
                             + String (original.decodedBytes) + "; live tree bytes="
                             + String (before.getSize()) + "; plugin id=" + original.pluginId);
            target->restorePluginStateFromValueTree (originalState);
            ++attempted;
            writeRestoreLog ("Original disk state submitted to " + trackName
                             + "; check actual instrument slot and sound; submission is not proof of success.");
        }
        message.setText ((automatic ? "Automatic ARIA restoration sent to " : "Retried original ARIA state for ")
                         + String (attempted) + " instance(s); unavailable=" + String (unavailable)
                         + ", unmatched=" + String (unmatched)
                         + ". Check instrument and sound before Confirm ARIA.", dontSendNotification);
    }

    void toggleRecordKeepingTake()
    {
        if (edit == nullptr)
            return;
        // A held piano-roll audition must release before record is engaged,
        // so no preview note or trailing note-off leaks into a new take.
        piano.silencePreview();
        auto& transport = edit->getTransport();
        if (transport.isRecording())
        {
            lastRecordingStopRequestedAt = Time::getMillisecondCounter();
            transport.stop (false, false); // false: retain the take
        }
        else
            transport.record (false);
    }

    bool canWriteProject()
    {
        if (! ariaReviewPending)
            return true;

        const String reason = "Saving is paused to protect the original ARIA instrument state.\n\n"
                              "Open ARIA and check that the instrument plays, then click Confirm ARIA "
                              "in the left sidebar. You can edit MIDI while checking, but Save and "
                              "Save As remain protected until confirmation.";
        message.setText ("Save blocked: verify ARIA playback, then click Confirm ARIA.",
                         dontSendNotification);
        writeRestoreLog ("Save / Save As blocked: ARIA restoration not yet confirmed.");
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon,
                                          "Confirm ARIA before saving", reason, "OK");
        return false;
    }

    // Tracktion's transport may still be finalising the take after Stop is
    // requested. Do not serialize the Edit until that work has finished.
    void writeWhenRecordingFinished (te::Edit* expectedEdit, int attempts,
                                     std::function<void()> write,
                                     std::function<void()> failed)
    {
        if (edit.get() != expectedEdit)
        {
            busy = false;
            failed();
            return;
        }

        const auto& transport = edit->getTransport();
        const bool recentlyStopped = lastRecordingStopRequestedAt != 0
            && Time::getMillisecondCounter() - lastRecordingStopRequestedAt < 400;
        if (! transport.isRecording() && ! transport.isStopping()
            && ! transport.isRecordingStopping() && ! recentlyStopped)
        {
            write();
            return;
        }

        if (attempts >= 100)
        {
            busy = false;
            message.setText ("Could not finish the MIDI recording in time. Project was not overwritten.",
                             dontSendNotification);
            failed();
            return;
        }

        message.setText ("Finishing the MIDI take before saving...", dontSendNotification);
        Component::SafePointer<StudioView> safe (this);
        Timer::callAfterDelay (100,
            [safe, expectedEdit, attempts, write = std::move (write), failed = std::move (failed)]() mutable
            {
                if (safe != nullptr)
                    safe->writeWhenRecordingFinished (expectedEdit, attempts + 1,
                                                       std::move (write), std::move (failed));
            });
    }

    void save (std::function<void (bool)> after = {})
    {
        if (edit == nullptr)
        {
            if (after) after (true);
            return;
        }

        if (busy || ! canWriteProject())
        {
            if (after) after (false);
            return;
        }
        if (edit->getTransport().isRecording())
            toggleRecordKeepingTake();
        busy = true;
        auto onFailure = [after] { if (after) after (false); };
        writeWhenRecordingFinished (edit.get(), 0,
            [this, after = std::move (after)] () mutable
            {
                te::EditFileOperations (*edit).save (true, true, false,
                    [this, after = std::move (after)] (bool ok)
                    {
                        busy = false;
                        message.setText (ok ? "Saved " + projectFile.getFileName()
                                            : "Project save failed. Try Save As with a new filename.",
                                         dontSendNotification);
                        if (after)
                            MessageManager::callAsync ([after, ok] { after (ok); });
                    });
            }, std::move (onFailure));
    }

    void chooseSaveAs()
    {
        if (edit == nullptr || busy || ! canWriteProject())
            return;
        auto target = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension()
                                                  + " Copy.tracktionedit");
        chooser = std::make_unique<FileChooser> ("Save a copy of this Studio project", target,
                                                 "*.tracktionedit");
        busy = true;
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
            [this] (const FileChooser& dialog)
            {
                busy = false;
                auto newFile = dialog.getResult();
                if (newFile == File())
                    return;
                newFile = newFile.withFileExtension (".tracktionedit");
                if (newFile == projectFile || newFile.exists())
                {
                    message.setText ("Save As needs a different, unused filename. Nothing was overwritten.",
                                     dontSendNotification);
                    return;
                }
                if (edit == nullptr || ! canWriteProject())
                    return;

                if (edit->getTransport().isRecording())
                    toggleRecordKeepingTake();
                busy = true;
                writeWhenRecordingFinished (edit.get(), 0,
                    [this, newFile]
                    {
                        te::EditFileOperations (*edit).saveAs (newFile, false,
                            [this, newFile] (bool ok)
                            {
                                busy = false;
                                if (ok)
                                {
                                    projectFile = newFile;
                                    edit->editFileRetriever = [newFile] { return newFile; };
                                    title.setText ("Studio 0.2  /  " + newFile.getFileNameWithoutExtension(),
                                                   dontSendNotification);
                                }
                                message.setText (ok ? "Saved new copy: " + newFile.getFullPathName()
                                                    : "Save As failed. Original project is unchanged.",
                                                 dontSendNotification);
                            });
                    }, [this] {});
            });
    }

    void chooseProject (bool create)
    {
        save ([this, create] (bool ok)
        {
            if (! ok)
                return;
            chooser = std::make_unique<FileChooser> (create ? "New Studio project" : "Open Studio project",
                            File::getSpecialLocation (File::userDocumentsDirectory), "*.tracktionedit");
            busy = true;
            const auto flags = FileBrowserComponent::canSelectFiles
                             | (create ? FileBrowserComponent::saveMode : FileBrowserComponent::openMode);
            chooser->launchAsync (flags, [this, create] (const FileChooser& dialog)
            {
                busy = false;
                auto file = dialog.getResult();
                if (file == File())
                    return;
                if (create)
                    file = file.withFileExtension (".tracktionedit");
                if (create && file.exists())
                {
                    message.setText ("Choose a new project filename.", dontSendNotification);
                    return;
                }
                loadProject (file, create);
            });
        });
    }

    void loadProject (const File& file, bool create)
    {
        std::vector<OriginalAriaState> pendingOriginalStates;
        bool backupAvailable = true;
        if (! create)
        {
            auto xml = XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("EDIT"))
            {
                message.setText ("Could not read this Studio project.", dontSendNotification);
                return;
            }
            pendingOriginalStates = readOriginalAriaStates (*xml);
            if (! pendingOriginalStates.empty())
            {
                const auto backup = ariaBackupFor (file);
                // Existing backup is never overwritten; copy BEFORE load or
                // plugin state flushing has any chance to touch the project.
                backupAvailable = backup.existsAsFile() || file.copyFileTo (backup);
            }
        }

        auto next = create ? te::createEmptyEdit (engine, file) : te::loadEditFromFile (engine, file);
        if (next == nullptr)
            return;

        if (edit != nullptr)
            edit->getTransport().stop (false, false);
        piano.setClip (nullptr);
        showWorkspace (Workspace::closed);
        selection.deselectAll();
        timeline.setViewedComponent (nullptr);
        editor.reset();
        edit = std::move (next);
        projectFile = file;
        lastRecordingStopRequestedAt = 0;
        ariaReviewPending = false;
        autoAriaRetryPending = false;
        originalAriaStates = std::move (pendingOriginalStates);
        if (! create)
            prepareAriaRecoveryOnLoad (backupAvailable);
        edit->editFileRetriever = [file] { return file; };
        edit->playInStopEnabled = true;

        auto& devices = engine.getDeviceManager();
        for (auto& midi : devices.getMidiInDevices())
        {
            midi->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            midi->setEnabled (true);
        }
        for (int i = 0; i < devices.getNumWaveInDevices(); ++i)
            if (auto input = devices.getWaveInDevice (i))
            {
                input->setMonitorMode (te::InputDevice::MonitorMode::off);
                input->setEnabled (true);
            }

        edit->getTransport().ensureContextAllocated();
        if (create && te::getAudioTracks (*edit).isEmpty())
            addTrack ("Instrument");
        edit->restartPlayback();

        editor = std::make_unique<EditComponent> (*edit, selection);
        auto& view = editor->getEditViewState();
        view.showFooters = true;
        view.showMidiDevices = true;
        view.showWaveDevices = true;
        timeline.setViewedComponent (editor.get(), false);
        zoom.onChange();
        sizeEditor();
        title.setText ("Studio 0.2  /  " + file.getFileNameWithoutExtension(), dontSendNotification);
        message.setText ("I selects input. A arms recording. + at right adds an instrument or effect.", dontSendNotification);
        if (create)
            save();
        else if (ariaReviewPending)
        {
            autoAriaRetryScheduledAt = Time::getMillisecondCounter();
            autoAriaRetryPending = true;
            message.setText ("ARIA original state protected. Automatic restore queued; check instrument and sound.",
                             dontSendNotification);
            writeRestoreLog ("Automatic restore queued for " + String (originalAriaStates.size())
                             + " original AU state(s); Save remains locked until user confirmation.");
        }
        resized();
    }

    te::AudioTrack* addTrack (const String& kind)
    {
        auto index = te::getAudioTracks (*edit).size();
        if (auto* track = EngineHelpers::getOrInsertAudioTrackAt (*edit, index))
        {
            track->setName (kind + " " + String (index + 1));
            selection.selectOnly (track);
            sizeEditor();
            return track;
        }
        return nullptr;
    }

    void chooseAudio()
    {
        chooser = std::make_unique<FileChooser> ("Import audio",
                           projectFile.getParentDirectory(),
                           engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
        busy = true;
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [this] (const FileChooser& dialog)
            {
                busy = false;
                auto file = dialog.getResult();
                if (! file.existsAsFile())
                    return;

                te::AudioFile media (engine, file);
                if (! media.isValid())
                {
                    message.setText ("Could not read this audio file.", dontSendNotification);
                    return;
                }
                if (auto* track = addTrack ("Audio"))
                {
                    auto start = edit->getTransport().getPosition();
                    auto end = start + te::TimeDuration::fromSeconds (media.getLength());
                    if (track->insertWaveClip (file.getFileNameWithoutExtension(), file, { { start, end }, {} }, false))
                        message.setText ("Imported " + file.getFileName() + " at the playhead. Keep the source file in place.",
                                         dontSendNotification);
                    else
                        message.setText ("Could not insert the audio clip.", dontSendNotification);
                }
            });
    }

    void showPlugins()
    {
        auto& manager = engine.getPluginManager();
        DialogWindow::LaunchOptions options;
        options.dialogTitle = "Studio Plugin Manager";
        options.dialogBackgroundColour = Colour (0xff172027);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.content.setOwned (new PluginListComponent (manager.pluginFormatManager,
            manager.knownPluginList, engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
            std::addressof (engine.getPropertyStorage().getPropertiesFile()), true));
        options.content->setSize (760, 480);
        options.launchAsync();
    }
};

class StudioApplication final : public JUCEApplication
{
    class Window final : public DocumentWindow
    {
    public:
        Window() : DocumentWindow ("Studio", Colour (0xff172027), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setResizeLimits (920, 610, 2600, 1800);
            setContentOwned (new StudioView(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

public:
    const String getApplicationName() override { return "Studio"; }
    const String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            static_cast<StudioView*> (window->getContentComponent())->requestQuit();
    }

private:
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (StudioApplication)
