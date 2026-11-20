// SPDX-License-Identifier: GPL-3.0-or-later
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <functional>
#include <limits>
using namespace juce;
using namespace tracktion::literals;

#include "Utilities.h"
#include "Components.h"
#include "PluginWindow.h"
#include "MidiMonitor.h"
#include "PianoRoll.h"
#include "MusicalRuler.h"
#include "StudioUI.h"

class MidiMonitorWindow final : public DocumentWindow
{
public:
    MidiMonitorWindow()
        : DocumentWindow ("Studio — MIDI Monitor", studio::colours::canvas, closeButton)
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
    WorkspaceDivider()
    {
        setMouseCursor (MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (true);
        setTitle ("Resize lower workspace");
        setDescription ("Press Up or Down to resize the lower workspace.");
    }

    std::function<int()> getSize;
    std::function<void(int)> resizePane;

    void paint (Graphics& g) override
    {
        g.fillAll (studio::colours::divider);
        g.setColour (hasKeyboardFocus (false) || isMouseOverOrDragging()
                         ? studio::colours::focus : studio::colours::secondary);
        for (int offset : { -10, -5, 0, 5, 10 })
            g.fillEllipse ((float) (getWidth() / 2 + offset - 1), (float) (getHeight() / 2 - 1), 2.0f, 2.0f);
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseDown (const MouseEvent&) override { initialSize = getSize(); }
    void mouseDrag (const MouseEvent& e) override
    {
        resizePane (initialSize - e.getDistanceFromDragStartY());
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key != KeyPress::upKey && key != KeyPress::downKey)
            return false;
        resizePane (getSize() + (key == KeyPress::upKey ? 16 : -16));
        return true;
    }

private:
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    int initialSize = 0;
};

class StudioView final : public Component, private Timer
{
public:
    StudioView()
    {
        setLookAndFeel (&style);
        setWantsKeyboardFocus (true);
        for (auto* control : { &newProject, &openProject, &saveProject, &saveAsProject,
                               &audioSettings, &plugins,
                               &rewind, &play, &record, &addMidi, &addAudio, &importAudio })
            addAndMakeVisible (*control);
        addAndMakeVisible (regionActions);
        regionActions.setTooltip ("Split, duplicate, copy or paste selected arrangement regions");
        regionActions.onClick = [this] { showRegionActions(); };

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
        ariaReviewNotice.setText ("ARIA CHECK  ·  Play the restored instrument, then confirm to save.",
                                 dontSendNotification);
        ariaReviewNotice.setFont (FontOptions (12.0f, Font::bold));
        ariaReviewNotice.setColour (Label::textColourId, studio::colours::warning);
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
        for (const auto& item : railItems())
        {
            addAndMakeVisible (*item.button);
            item.button->onClick = [this, destination = item.destination] { toggleWorkspace (destination); };
        }
        addAndMakeVisible (moreButton);
        moreButton.onClick = [this] { showMoreWorkspaces(); };
        addAndMakeVisible (closeWorkspace);
        closeWorkspace.onClick = [this] { showWorkspace (Workspace::closed); };
        for (const auto& item : dockItems())
        {
            addAndMakeVisible (*item.button);
            item.button->onClick = [this, mode = item.mode] { setDockMode (mode); };
        }
        addChildComponent (workspaceSummary);
        addChildComponent (placeholder);
        for (size_t index = 0; index < workspaceLinks.size(); ++index)
        {
            auto& link = workspaceLinks[index];
            addChildComponent (link);
            link.onClick = [this, index]
            {
                showWorkspace (contextLinks()[index].destination);
                if (workspaceLinks[index].isVisible())
                    workspaceLinks[index].grabKeyboardFocus();
                else
                    moreButton.grabKeyboardFocus();
            };
        }
        workspaceSummary.setColour (Label::textColourId, studio::colours::secondary);
        placeholder.setColour (Label::textColourId, studio::colours::secondary);
        placeholder.setJustificationType (Justification::centred);
        addAndMakeVisible (workspaceDivider);
        workspaceDivider.getSize = [this] { return workspacePanel.getHeight(); };
        workspaceDivider.resizePane = [this] (int size)
        {
            workspaceHeight = jlimit (minimumSplitHeight, splitLimit, size);
            setDockMode (DockMode::split);
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
        for (auto* label : { &title, &mixerLabel, &message, &trackDetails, &volumeLabel, &panLabel, &workspaceTitle })
            addAndMakeVisible (*label);
        for (auto* readout : { &bars, &position, &tempo, &meter })
        {
            addAndMakeVisible (*readout);
            readout->setValue ("—");
        }
        title.setFont (FontOptions (15.0f, Font::bold));
        title.setText ("Studio 0.2", dontSendNotification);
        workspaceTitle.setFont (FontOptions (15.0f, Font::bold));
        message.setColour (Label::textColourId, studio::colours::secondary);
        trackDetails.setColour (Label::textColourId, studio::colours::secondary);
        record.setColour (TextButton::textColourOffId, studio::colours::record);
        record.setColour (TextButton::buttonOnColourId, studio::colours::record.darker (0.55f));
        rewind.setTooltip ("Return to project start");
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
        setSize (1024, 640);
        showWorkspace (Workspace::closed);
        timerCallback();
        startTimerHz (24);
    }

    ~StudioView() override
    {
        if (monitorWindow != nullptr)
        {
            monitorWindow->clearContentComponent();
            monitorWindow->setLookAndFeel (nullptr);
        }
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
        using namespace studio::colours;
        graphics.fillAll (canvas);
        graphics.setColour (panel);
        graphics.fillRect (topBar);
        graphics.fillRect (workspacePanel);
        graphics.fillRect (arrangementTools);
        graphics.setColour (rail);
        graphics.fillRect (railBounds);
        graphics.setColour (studio::colours::timeline);
        graphics.fillRect (timeline.getBounds());
        graphics.setColour (control);
        graphics.fillRect (rulerRow);
        if (ariaReviewPending)
        {
            graphics.setColour (Colour (0xff332b20));
            graphics.fillRect (ariaBanner);
        }
        graphics.setColour (divider);
        graphics.drawHorizontalLine (topBar.getBottom() - 1, 0.0f, (float) getWidth());
        graphics.drawVerticalLine (railBounds.getRight() - 1, (float) railBounds.getY(), (float) railBounds.getBottom());
        graphics.setColour (secondary);
        graphics.setFont (FontOptions (11.0f, Font::bold));
        graphics.drawText ("Tracks", rulerRow.withWidth (72).reduced (10, 0),
                           Justification::centredLeft);
        auto area = ruler;
        graphics.drawText ("Inserts", rulerRow.withLeft (area.getRight()).reduced (10, 0),
                           Justification::centredLeft);
        if (editor == nullptr)
        {
            graphics.setColour (secondary);
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
                graphics.setColour (bar ? text : secondary.withAlpha (0.5f));
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
                g.setColour (studio::colours::secondary.withAlpha (tick.beatIndex == 0 ? 0.22f : 0.08f));
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
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
            && canEditRegions())
        {
            if (key.getModifiers().isShiftDown())
                edit->redo();
            else
                edit->undo();
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
        auto files = topBar.reduced (10, 3).removeFromTop (32);
        for (auto* button : { &plugins, &audioSettings, &saveAsProject, &saveProject,
                              &openProject, &newProject })
            button->setBounds (files.removeFromRight (button == &plugins ? 110 : 88).reduced (2, 2));
        title.setBounds (files);

        auto controls = topBar.withTrimmedTop (37).reduced (10, 6);
        rewind.setBounds (controls.removeFromLeft (62).reduced (2, 5));
        play.setBounds (controls.removeFromLeft (64).reduced (2, 5));
        record.setBounds (controls.removeFromLeft (115).reduced (2, 5));
        controls.removeFromLeft (8);
        bars.setBounds (controls.removeFromLeft (90).reduced (3, 0));
        position.setBounds (controls.removeFromLeft (101).reduced (3, 0));
        tempo.setBounds (controls.removeFromLeft (67).reduced (3, 0));
        meter.setBounds (controls.removeFromLeft (57).reduced (3, 0));
        monitorButton.setBounds (controls.removeFromRight (124).reduced (2, 5));

        message.setBounds (area.removeFromBottom (22).reduced (10, 0));
        ariaBanner = ariaReviewPending ? area.removeFromTop (32) : Rectangle<int>();
        ariaReviewNotice.setVisible (ariaReviewPending);
        retryAriaButton.setVisible (ariaReviewPending);
        confirmAriaButton.setVisible (ariaReviewPending);
        auto bannerControls = ariaBanner.reduced (8, 3);
        confirmAriaButton.setBounds (bannerControls.removeFromRight (114).reduced (2, 1));
        retryAriaButton.setBounds (bannerControls.removeFromRight (90).reduced (2, 1));
        ariaReviewNotice.setBounds (bannerControls);

        railBounds = area.removeFromLeft (getWidth() < 1100 ? 66 : 74);
        auto rail = railBounds.reduced (4, 6);
        const int tileHeight = getHeight() < 700 ? 48 : 54;
        for (const auto& item : railItems())
            item.button->setBounds (rail.removeFromTop (tileHeight).reduced (0, 1));
        moreButton.setBounds (rail.removeFromTop (tileHeight).reduced (0, 1));

        layoutDock (area);
        arrangementTools = area.removeFromTop (32);
        auto tools = arrangementTools.reduced (5, 3);
        trackPicker.setBounds (tools.removeFromLeft (140).reduced (2, 0));
        for (auto* button : { &addMidi, &addAudio, &addMidiClip, &importAudio, &regionActions })
            button->setBounds (tools.removeFromLeft (button == &addAudio ? 69 : 94).reduced (2, 0));
        zoom.setBounds (tools.removeFromRight (107).reduced (2, 0));
        trackDetails.setBounds (tools);

        rulerRow = area.removeFromTop (26);
        timeline.setBounds (area);
        sizeEditor();
        ruler = rulerRow.withTrimmedLeft (150).withTrimmedRight (150 + timeline.getScrollBarThickness());
        arrangementZoomOut.setBounds (rulerRow.getX() + 73, rulerRow.getY() + 2, 34, 22);
        arrangementZoomIn.setBounds (rulerRow.getX() + 112, rulerRow.getY() + 2, 34, 22);
        repaint();
    }

private:
    enum class Workspace
    {
        closed, instruments, channel, stage, articulations, piano, mix, midi,
        rooms, microphones, synth, sampler, effects, templates, picture
    };
    enum class DockMode { compact, split, expanded };

    studio::StudioLookAndFeel style;
    Workspace workspace = Workspace::closed;
    DockMode dockMode = DockMode::split;
    WorkspaceDivider workspaceDivider;
    static constexpr int minimumSplitHeight = 170;
    int workspaceHeight = 270, splitLimit = minimumSplitHeight;
    Rectangle<int> topBar, railBounds, ariaBanner, arrangementTools, workspacePanel, rulerRow;
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

    struct RegionClipboardItem
    {
        te::ClipCopy copy;
        double relativeStartSeconds = 0.0;
        int relativeTrack = 0;
    };
    std::vector<RegionClipboardItem> regionClipboard;
    int clipboardBaseTrack = 0;


    Label title, mixerLabel, message, trackDetails, volumeLabel, panLabel, workspaceTitle,
          workspaceSummary, placeholder, ariaReviewNotice;
    studio::TransportReadout bars { "Bars / Beats" }, position { "Time" }, tempo { "BPM" }, meter { "Meter" };
    ComboBox zoom, trackPicker;
    StringArray trackNames;
    TextButton arrangementZoomOut { "-" }, arrangementZoomIn { "+" };
    studio::RailButton instrumentsButton { "Instruments", studio::RailIcon::instruments },
                       channelButton { "Channel", studio::RailIcon::channel },
                       stageButton { "Stage", studio::RailIcon::stage },
                       articulationsButton { "Articulations", studio::RailIcon::articulations },
                       pianoButton { "Piano Roll", studio::RailIcon::piano },
                       mixButton { "Mixer", studio::RailIcon::mixer },
                       moreButton { "More", studio::RailIcon::more };
    TextButton compactButton { "Compact" }, splitButton { "Split" },
               expandedButton { "Expanded" }, closeWorkspace { "Hide" };
    std::array<TextButton, 2> workspaceLinks;
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
               regionActions { "Edit Regions" },
               rewind { "Start" }, play { "Play" }, record { "Record" },
               addMidi { "+ Instrument" }, addAudio { "+ Audio" }, importAudio { "Import Audio" },
               addMidiClip { "+ MIDI Clip" };

    struct RailItem { Workspace destination; studio::RailButton* button; };
    struct DockItem { DockMode mode; TextButton* button; };
    struct WorkspaceEntry { Workspace destination; const char* name; };

    std::array<RailItem, 6> railItems()
    {
        return {{ { Workspace::instruments, &instrumentsButton }, { Workspace::channel, &channelButton },
                  { Workspace::stage, &stageButton }, { Workspace::articulations, &articulationsButton },
                  { Workspace::piano, &pianoButton }, { Workspace::mix, &mixButton } }};
    }

    std::array<DockItem, 3> dockItems()
    {
        return {{ { DockMode::compact, &compactButton }, { DockMode::split, &splitButton },
                  { DockMode::expanded, &expandedButton } }};
    }

    static constexpr WorkspaceEntry extraWorkspaces[] {
        { Workspace::rooms, "Rooms" }, { Workspace::microphones, "Virtual Microphones" },
        { Workspace::synth, "Studio Synth" }, { Workspace::sampler, "Studio Sampler" },
        { Workspace::effects, "Effects" }, { Workspace::templates, "Template Manager" },
        { Workspace::picture, "Picture" }
    };

    std::array<WorkspaceEntry, 2> contextLinks() const
    {
        switch (workspace)
        {
            case Workspace::stage:       return {{ { Workspace::rooms, "Rooms" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::rooms:       return {{ { Workspace::stage, "Stage" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::microphones: return {{ { Workspace::stage, "Stage" }, { Workspace::rooms, "Rooms" } }};
            case Workspace::channel:     return {{ { Workspace::synth, "Synth" }, { Workspace::sampler, "Sampler" } }};
            case Workspace::synth:
            case Workspace::sampler:     return {{ { Workspace::channel, "Channel" }, { Workspace::instruments, "Instruments" } }};
            default:                    return {{ { Workspace::closed, "" }, { Workspace::closed, "" } }};
        }
    }

    void layoutDock (Rectangle<int>& arrangement)
    {
        const int available = arrangement.getHeight();
        splitLimit = jmax (minimumSplitHeight, available - 168);
        int panelHeight = 0;
        if (workspace != Workspace::closed)
        {
            if (dockMode == DockMode::compact)
                panelHeight = 65;
            else if (dockMode == DockMode::expanded)
                panelHeight = jmax (minimumSplitHeight, available - 122);
            else
                panelHeight = jlimit (minimumSplitHeight, splitLimit, workspaceHeight);
        }
        workspacePanel = arrangement.removeFromBottom (panelHeight);
        workspaceDivider.setBounds (panelHeight > 0 ? arrangement.removeFromBottom (9) : Rectangle<int>());
        auto body = workspacePanel;
        auto header = body.removeFromTop (34).reduced (9, 4);
        closeWorkspace.setBounds (header.removeFromRight (52).reduced (2, 0));
        expandedButton.setBounds (header.removeFromRight (82).reduced (2, 0));
        splitButton.setBounds (header.removeFromRight (48).reduced (2, 0));
        compactButton.setBounds (header.removeFromRight (76).reduced (2, 0));
        for (auto it = workspaceLinks.rbegin(); it != workspaceLinks.rend(); ++it)
            if (it->isVisible())
                it->setBounds (header.removeFromRight (94).reduced (2, 0));
        workspaceTitle.setBounds (header);
        workspaceSummary.setBounds (body.reduced (9, 1));
        placeholder.setBounds (body.reduced (12, 0));
        if (piano.isVisible())
            piano.setBounds (body);
        if (monitor.getParentComponent() == this && monitor.isVisible())
            monitor.setBounds (body);

        auto mixerBounds = body.reduced (10, 0);
        mixerLabel.setBounds (mixerBounds.removeFromTop (25));
        auto volumeRow = mixerBounds.removeFromTop (33);
        volumeLabel.setBounds (volumeRow.removeFromLeft (64));
        volume.setBounds (volumeRow.removeFromLeft (jmin (430, volumeRow.getWidth())));
        auto panRow = mixerBounds.removeFromTop (33);
        panLabel.setBounds (panRow.removeFromLeft (64));
        pan.setBounds (panRow.removeFromLeft (jmin (430, panRow.getWidth())));
    }

    void toggleWorkspace (Workspace destination)
    {
        showWorkspace (workspace == destination ? Workspace::closed : destination);
    }

    void showMoreWorkspaces()
    {
        PopupMenu menu;
        int id = 1;
        for (const auto& entry : extraWorkspaces)
            menu.addItem (id++, entry.name, true, workspace == entry.destination);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&moreButton),
                            [safe = Component::SafePointer<StudioView> (this)] (int choice)
                            {
                                if (safe != nullptr && choice > 0)
                                    safe->toggleWorkspace (extraWorkspaces[choice - 1].destination);
                            });
    }

    void setDockMode (DockMode mode)
    {
        dockMode = mode;
        syncWorkspace();
    }

    void showWorkspace (Workspace next)
    {
        if (next == Workspace::piano && dockMode == DockMode::compact)
            dockMode = DockMode::split;
        workspace = next;
        syncWorkspace();
    }

    void syncWorkspace()
    {
        const bool open = workspace != Workspace::closed;
        const bool compact = dockMode == DockMode::compact;
        const bool mixing = workspace == Workspace::mix;
        const bool monitoring = workspace == Workspace::midi;
        const bool editingMidi = workspace == Workspace::piano;
        const bool planned = open && ! (mixing || monitoring || editingMidi);
        String name;
        bool inPrimaryRail = false;
        for (const auto& item : railItems())
        {
            const bool selected = workspace == item.destination;
            item.button->setToggleState (selected, dontSendNotification);
            if (selected)
            {
                name = item.button->getButtonText();
                inPrimaryRail = true;
            }
        }
        for (const auto& entry : extraWorkspaces)
            if (workspace == entry.destination)
                name = entry.name;
        moreButton.setToggleState (planned && ! inPrimaryRail, dontSendNotification);
        if (monitoring)
            name = "MIDI Monitor";
        if (workspace == Workspace::instruments)
            name = "Instrument Browser";
        if (workspace == Workspace::stage)
            name = "Orchestral Stage";

        for (Component* component : std::initializer_list<Component*> {
                 &mixerLabel, &volumeLabel, &panLabel, &volume, &pan })
            component->setVisible (mixing && ! compact);
        for (const auto& item : dockItems())
        {
            item.button->setVisible (open);
            item.button->setToggleState (dockMode == item.mode, dontSendNotification);
        }
        const auto links = contextLinks();
        for (size_t index = 0; index < links.size(); ++index)
        {
            workspaceLinks[index].setVisible (links[index].destination != Workspace::closed);
            workspaceLinks[index].setButtonText (links[index].name);
        }
        closeWorkspace.setVisible (open);
        workspaceTitle.setVisible (open);
        workspaceDivider.setVisible (open);
        workspaceTitle.setText (name, dontSendNotification);
        workspaceSummary.setVisible (open && compact);
        workspaceSummary.setText (planned ? "Planned workspace" : "Choose Split or Expanded to show controls.",
                                  dontSendNotification);
        placeholder.setVisible (planned && ! compact);
        placeholder.setText (workspace == Workspace::instruments
                                 ? "Instrument Browser is planned. Use a track’s Inserts + to load an installed instrument."
                                 : workspace == Workspace::channel
                                     ? "Channel is planned. Use the track header for input and arm, or Mixer for volume and pan."
                                     : "Planned workspace",
                             dontSendNotification);
        if (! editingMidi || compact)
            piano.silencePreview();
        piano.setVisible (editingMidi && ! compact);
        monitorButton.setToggleState (monitoring, dontSendNotification);
        if (monitor.getParentComponent() == this)
        {
            monitor.setVisible (monitoring && ! compact);
            monitor.setCaptureOpen (monitoring);
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
            if (dockMode == DockMode::compact)
                dockMode = DockMode::split;
            showWorkspace (Workspace::midi);
        }
        else
        {
            if (monitorWindow == nullptr)
            {
                monitorWindow = std::make_unique<MidiMonitorWindow>();
                monitorWindow->setLookAndFeel (&style);
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

    void copySelectedRegions()
    {
        if (edit == nullptr)
            return;
        auto clips = selection.getItemsOfType<te::Clip>();
        if (clips.isEmpty())
        {
            message.setText ("Select one or more regions in the arrangement first.", dontSendNotification);
            return;
        }
        regionClipboard.clear();
        const auto tracks = te::getAudioTracks (*edit);
        double earliest = std::numeric_limits<double>::max();
        int firstTrack = tracks.size();
        for (auto* clip : clips)
        {
            earliest = jmin (earliest, clip->getPosition().getStart().inSeconds());
            const int index = tracks.indexOf (dynamic_cast<te::AudioTrack*> (clip->getTrack()));
            if (index >= 0)
                firstTrack = jmin (firstTrack, index);
        }
        if (firstTrack < 0 || firstTrack >= tracks.size())
            return;
        std::vector<RegionClipboardItem> copied;
        copied.reserve ((size_t) clips.size());
        for (auto* clip : clips)
        {
            const int index = tracks.indexOf (dynamic_cast<te::AudioTrack*> (clip->getTrack()));
            if (index < 0)
                continue;
            copied.push_back ({ te::ClipCopy::fromClip (*clip),
                                clip->getPosition().getStart().inSeconds() - earliest,
                                index - firstTrack });
        }
        regionClipboard = std::move (copied);
        clipboardBaseTrack = firstTrack;
        message.setText (String (regionClipboard.size()) + " region(s) copied. Select a destination track and seek, then Paste.",
                         dontSendNotification);
    }

    void pasteRegions()
    {
        if (edit == nullptr || regionClipboard.empty())
            return;
        const auto tracks = te::getAudioTracks (*edit);
        int baseTrack = clipboardBaseTrack;
        if (auto* chosen = selectedTrack())
            baseTrack = tracks.indexOf (chosen);
        if (! isPositiveAndBelow (baseTrack, tracks.size()))
            return;

        const double start = edit->getTransport().getPosition().inSeconds();
        edit->getUndoManager().beginNewTransaction ("Paste regions");
        int pasted = 0;
        te::Clip::Ptr last;
        for (const auto& item : regionClipboard)
        {
            const int index = baseTrack + item.relativeTrack;
            if (! isPositiveAndBelow (index, tracks.size()))
                continue;
            if (auto* inserted = te::insertClipCopy (*tracks[index], item.copy.withNewItemID (*edit)))
            {
                inserted->setStart (te::TimePosition::fromSeconds (start + item.relativeStartSeconds), false, true);
                last = inserted;
                ++pasted;
            }
        }
        if (last != nullptr)
            selection.selectOnly (last.get());
        message.setText (String (pasted) + " region(s) pasted at the playhead.", dontSendNotification);
    }

    void performRegionAction (int action)
    {
        if (edit == nullptr || busy || edit->getTransport().isRecording())
            return;
        if (action == 3)
        {
            copySelectedRegions();
            return;
        }
        if (action == 4)
        {
            pasteRegions();
            return;
        }
        if (action == 5)
        {
            const int selectedCount = selection.getItemsOfType<te::Clip>().size();
            copySelectedRegions();
            if ((int) regionClipboard.size() != selectedCount)
                return; // Never delete a region if its clipboard copy failed.
        }
        auto clips = selection.getItemsOfType<te::Clip>();
        if (clips.isEmpty())
            return;
        // The owning track's split and ClipCopy insert APIs retain clip
        // state, source offsets, channel data and AU/plugin parameters.
        edit->getUndoManager().beginNewTransaction (action == 1 ? "Split regions"
                                                   : action == 2 ? "Duplicate regions"
                                                   : action == 5 ? "Cut regions" : "Delete regions");
        int count = 0;
        te::Clip::Ptr last;
        for (auto* clip : clips)
        {
            if (auto* track = clip->getClipTrack())
            {
                if (action == 1)
                {
                    const auto pos = edit->getTransport().getPosition();
                    if (pos <= clip->getPosition().getStart() || pos >= clip->getPosition().getEnd())
                        continue;
                    if (auto* right = track->splitClip (*clip, pos))
                    {
                        last = right;
                        ++count;
                    }
                }
                else if (action == 2)
                {
                    const auto nextStart = clip->getPosition().getEnd();
                    if (auto* duplicated = te::insertClipCopy (*track,
                            te::ClipCopy::fromClip (*clip).withNewItemID (*edit)))
                    {
                        duplicated->setStart (nextStart, false, true);
                        last = duplicated;
                        ++count;
                    }
                }
                else if (action == 5 || action == 6)
                {
                    clip->removeFromParent();
                    ++count;
                }
            }
        }
        if (action == 5 || action == 6)
        {
            piano.setClip (nullptr);
            selection.deselectAll();
        }
        else if (last != nullptr)
            selection.selectOnly (last.get());
        message.setText (String (count) + (action == 1 ? " region(s) split."
                                   : action == 2 ? " region(s) duplicated."
                                   : action == 5 ? " region(s) cut." : " region(s) deleted."),
                         dontSendNotification);
    }

    void showRegionActions()
    {
        PopupMenu menu;
        const bool selected = selection.getItemsOfType<te::Clip>().size() > 0;
        menu.addItem (1, "Split selected at playhead", selected);
        menu.addItem (2, "Duplicate selected", selected);
        menu.addSeparator();
        menu.addItem (3, "Copy selected regions", selected);
        menu.addItem (4, "Paste at playhead", ! regionClipboard.empty());
        menu.addItem (5, "Cut selected regions", selected);
        menu.addItem (6, "Delete selected regions", selected);
        menu.addSeparator();
        menu.addItem (7, "Undo", edit != nullptr && edit->getUndoManager().canUndo());
        menu.addItem (8, "Redo", edit != nullptr && edit->getUndoManager().canRedo());
        Component::SafePointer<StudioView> safe (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&regionActions),
                            [safe] (int choice)
                            {
                                if (safe == nullptr || choice == 0 || ! safe->canEditRegions())
                                    return;
                                if (choice == 7)
                                    safe->edit->undo();
                                else if (choice == 8)
                                    safe->edit->redo();
                                else
                                    safe->performRegionAction (choice);
                            });
    }

    bool canEditRegions() const
    {
        return edit != nullptr && ! busy && ! edit->getTransport().isRecording();
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
        regionActions.setEnabled (ready && ! recording);
        for (auto* button : { &saveProject, &saveAsProject, &rewind, &addMidi,
                              &addAudio, &importAudio, &addMidiClip })
            button->setEnabled (ready && ! recording);
        // ARIA review protects Save and the original AU state, not MIDI editing.
        // A loaded project must remain editable while its ARIA slots are checked.
        piano.setEditingEnabled (ready && ! recording);
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
            record.setToggleState (recording, dontSendNotification);
            const auto now = transport.getPosition();
            piano.setTransportPosition (now);
            const auto centiseconds = static_cast<int64> (juce::jmax (0.0, now.inSeconds()) * 100.0);
            position.setValue (String (centiseconds / 6000).paddedLeft ('0', 2) + ":"
                               + String (centiseconds / 100 % 60).paddedLeft ('0', 2) + "."
                               + String (centiseconds % 100).paddedLeft ('0', 2));
            const auto& sequence = edit->tempoSequence;
            const auto musical = sequence.toBarsAndBeats (now);
            const auto& signature = sequence.getTimeSigAt (now);
            bars.setValue (String (musical.bars + 1) + "." + String (musical.getWholeBeats() + 1));
            tempo.setValue (String (sequence.getBpmAt (now), 1));
            meter.setValue (String (signature.numerator.get()) + "/" + String (signature.denominator.get()));
            sizeEditor();
        }

        const auto selectedMidiClips = selection.getItemsOfType<te::MidiClip>();
        if (! selectedMidiClips.isEmpty())
        {
            // Use the earliest region as the active edit target so the
            // other selected clips line up to its right in the piano roll.
            auto* primary = selectedMidiClips.getFirst();
            for (auto* candidate : selectedMidiClips)
                if (candidate->getPosition().getStart() < primary->getPosition().getStart())
                    primary = candidate;
            if (piano.getClip() != primary)
            {
                piano.setClip (primary);
                workspaceHeight = 360;
                showWorkspace (Workspace::piano);
            }
            piano.setComparisonClips (selectedMidiClips);
        }
        else
            piano.setComparisonClips ({});

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
        regionClipboard.clear();
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
        Window() : DocumentWindow ("Studio", studio::colours::canvas, DocumentWindow::allButtons)
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
