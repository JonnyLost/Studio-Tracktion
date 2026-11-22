// SPDX-License-Identifier: GPL-3.0-or-later
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <functional>
#include <limits>
#include <optional>
using namespace juce;
using namespace tracktion::literals;

#include "Utilities.h"
#include "Components.h"
#include "PluginWindow.h"
#include "MidiMonitor.h"
#include "PianoRoll.h"
#include "MusicalRuler.h"
#include "MusicalTransport.h"
#include "ComposerMidi.h"
#include "ComposerTracks.h"
#include "ComposerBounceUI.h"
#include "StudioUI.h"
#include "InstrumentBrowser.h"
#include "StudioMixing.h"
#include "StudioResources.h"
#include "ArticulationPalette.h"
#include "SpatialWorkspace.h"
#include "SpatialRouting.h"
#include "StudioEnvironments.h"
#include "SamplerEditor.h"
#include "SamplerReviewCapture.h"
#include "StudioSynth.h"
#include "InstrumentWorkspace.h"
#include "InstrumentPerformance.h"
#include "Seating/SeatingWorkspace.h"

class StudioPluginWindows final : public ExtendedUIBehaviour
{
public:
    std::function<bool(te::Plugin&)> openNative;

    std::unique_ptr<Component> createPluginWindow (te::PluginWindowState& state) override
    {
        if (auto* request = dynamic_cast<te::Plugin::WindowState*> (&state))
            if (openNative && openNative (request->plugin)) return {};
        return ExtendedUIBehaviour::createPluginWindow (state);
    }
};

static Rectangle<int> displayClientArea (const Component& window, const Displays::Display& display)
{
    auto area = display.userBounds.toNearestInt();
    if (auto* peer = window.getPeer())
        if (const auto frame = peer->getFrameSizeIfPresent())
            frame->subtractFrom (area);
    return area;
}

class MidiMonitorWindow final : public DocumentWindow
{
public:
    MidiMonitorWindow()
        : DocumentWindow ("Studio - MIDI Monitor", studio::colours::canvas, closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (770, 200, 2200, 1000);
    }

    std::function<void()> onClose;
    std::function<bool(const KeyPress&)> onAppearanceKey;
    bool keyPressed (const KeyPress& key) override
    {
        return (onAppearanceKey && onAppearanceKey (key)) || DocumentWindow::keyPressed (key);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }
};

class ArticulationWindow final : public DocumentWindow
{
public:
    explicit ArticulationWindow (studio::ArticulationPalette& paletteToShow)
        : DocumentWindow ("Studio - Articulations", studio::colours::canvas, closeButton),
          palette (paletteToShow)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (280, 210, 1800, 1200);
        viewport.setScrollBarsShown (true, false);
        viewport.setViewedComponent (&palette, false);
        setContentNonOwned (&viewport, false);
    }

    ~ArticulationWindow() override
    {
        viewport.setViewedComponent (nullptr);
        clearContentComponent();
    }

    void resized() override
    {
        DocumentWindow::resized();
        fitPalette();
    }

    void fitPalette()
    {
        const auto thickness = studio::appearance.scrollbar();
        viewport.setScrollBarThickness (thickness);
        auto width = viewport.getWidth();
        auto height = palette.minimumContentHeight (width);
        if (height > viewport.getHeight())
        {
            width = jmax (1, width - thickness);
            height = palette.minimumContentHeight (width);
        }
        palette.setSize (width, jmax (height, viewport.getHeight()));
    }

    std::function<void()> onClose;
    std::function<bool(const KeyPress&)> onAppearanceKey;
    bool keyPressed (const KeyPress& key) override
    {
        return (onAppearanceKey && onAppearanceKey (key)) || palette.keyPressed (key) || DocumentWindow::keyPressed (key);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    studio::ArticulationPalette& palette;
    Viewport viewport;
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
        resizePane (getSize() + studio::appearance.px (key == KeyPress::upKey ? 16 : -16));
        return true;
    }

private:
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    int initialSize = 0;
};

class AudioSettingsView final : public Viewport
{
public:
    explicit AudioSettingsView (AudioDeviceManager& manager)
        : selector (manager, 0, 512, 1, 512, false, false, true, true)
    {
        selector.setItemHeight (studio::appearance.control (24));
        setScrollBarThickness (studio::appearance.scrollbar());
        setScrollBarsShown (true, false);
        setViewedComponent (&selector, false);
    }

    ~AudioSettingsView() override { setViewedComponent (nullptr); }

    void resized() override
    {
        Viewport::resized();
        // AudioDeviceSelectorComponent lays out its controls and updates its
        // own height, including when the device or visible tabs change.
        selector.setSize (jmax (1, getWidth() - getScrollBarThickness()), selector.getHeight());
    }

private:
    AudioDeviceSelectorComponent selector;
};

class StudioView final : public Component, private Timer
{
public:
   #if STUDIO_SPATIAL_TESTS
    int runInterfaceReview (const File& output);
    int runInstrumentWorkspaceReview (const File& output);
    int runAdaptiveWorkspaceReview (const File& output);
    int runSeatingProjectReview (const File& output);
    int runUnifiedStageReview (const File& output);
   #endif

    StudioView()
    {
        setLookAndFeel (&style);
        LookAndFeel::setDefaultLookAndFeel (&style);
        studio::spatial::registerPlugins (engine);
        studio::sampler::registerPlugin (engine);
        studio::synth::registerPlugin (engine);
        auto& prefs = engine.getPropertyStorage().getPropertiesFile();
        studio::appearance.set (prefs.getIntValue ("studio.interfaceScale", 100),
                                prefs.getIntValue ("studio.textScale", 100));
        workspaceHeight = studio::appearance.px (workspaceHeight);
        railViewport.setViewedComponent (&railContent, false);
        railViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (railViewport);
        workspaceViewport.setViewedComponent (&workspaceBody, false);
        workspaceViewport.setScrollBarsShown (true, false);
        addChildComponent (workspaceViewport);
        addAndMakeVisible (viewButton);
        addChildComponent (projectMenu);
        addChildComponent (trackMenu);
        addChildComponent (dockMenu);
        addAndMakeVisible (performanceMode);
        performanceMode.addItemList ({ "Performance", "Articulations only", "Workspace hidden" }, 1);
        performanceMode.setTitle ("Lower workspace presentation");
        performanceMode.setSelectedId (jlimit (1, 3, prefs.getIntValue ("studio.performanceMode", 1)), dontSendNotification);
        performanceMode.onChange = [this] { applyPerformanceMode(); };
        viewButton.onClick = [this] { showViewOptions(); };
        projectMenu.onClick = [this]
        {
            showCommandMenu (projectMenu, { &newProject, &openProject, &saveProject, &saveAsProject,
                                           &audioSettings, &plugins, &monitorButton, &importMidi, &exportMidi, &exportWav });
        };
        trackMenu.onClick = [this]
        {
            showCommandMenu (trackMenu, { &addMidi, &addSynthTrack, &addSamplerTrack, &addAudio, &addMidiClip, &importAudio, &regionActions });
        };
        dockMenu.onClick = [this]
        {
            if (workspaceLinks[0].isVisible() || collapsedDockLinks)
                showCommandMenu (dockMenu, { &compactButton, &splitButton, &expandedButton, &closeWorkspace,
                                             &workspaceLinks[0], &workspaceLinks[1] });
            else
                showCommandMenu (dockMenu, { &compactButton, &splitButton, &expandedButton, &closeWorkspace });
        };
        setWantsKeyboardFocus (true);
        for (auto* control : { &newProject, &openProject, &saveProject, &saveAsProject,
                               &audioSettings, &plugins, &addMidi, &addAudio, &importAudio })
            addAndMakeVisible (*control);
        for (auto* button : { &rewind, &play, &stop, &record, &cycleButton, &clickButton })
            addAndMakeVisible (*button);
        addChildComponent (performance);
        addChildComponent (articulationStrip);
        performance.onOpenEditor = [this] (te::Plugin& plugin)
        {
            if (! showNativeInstrument (plugin)) plugin.showWindowExplicitly();
        };
        articulationStrip.onManage = [this]
        {
            if (auto* native = articulation.nativeGroupInstrument(); native != nullptr && showNativeInstrument (*native))
            { if (auto* editor = instrumentWorkspace.getEditor()) editor->restoreView ("4"); return; }
            if (articulationWindow != nullptr) articulationWindow->toFront (true);
            else
            {
                if (dockMode == DockMode::compact) setDockMode (DockMode::split);
                showWorkspace (Workspace::articulations);
            }
        };
        cycleButton.onClick = [this] { music.toggleCycle(); timerCallback(); };
        clickButton.onClick = [this] { music.toggleClick(); timerCallback(); };
        tempo.onClick = [this] { music.promptTempoAtPlayhead(); };
        meter.onClick = [this] { music.promptMeterAtPlayhead(); };
        tempo.setTooltip ("Edit the tempo event at the playhead while stopped");
        meter.setTooltip ("Edit the time signature at the current bar while stopped");
        addAndMakeVisible (music);
        music.onTimelineChanged = [this] { repaint (rulerRow); };
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
        for (auto* indicator : { &resources.audio, &resources.ram, &resources.dropouts })
            addAndMakeVisible (*indicator);
        workspaceBody.addChildComponent (resources);
        workspaceBody.addChildComponent (instrumentWorkspace);
        static_cast<StudioPluginWindows&> (engine.getUIBehaviour()).openNative = [this] (te::Plugin& plug)
        {
            return showNativeInstrument (plug);
        };
        instrumentWorkspace.onLayout = [this] { resized(); };
        instrumentWorkspace.onExpand = [this] { setDockMode (DockMode::expanded); };
        instrumentWorkspace.onDock = [this]
        {
            if (dockMode == DockMode::compact) setDockMode (DockMode::split);
            showWorkspace (Workspace::instrumentEditor);
        };
        instrumentWorkspace.onPreferenceChange = [this] { rememberInstrumentWorkspace(); };
        workspaceBody.addChildComponent (articulation);
        workspaceBody.addChildComponent (spatialWorkspace);
        spatialWorkspace.onExpandStage = [this] { setDockMode (DockMode::split); };
       #if JUCE_MAC
        addChildComponent (seatingWorkspace);
        spatialWorkspace.onOpenSeating = [this]
        {
            setDockMode (DockMode::expanded);
            showWorkspace (Workspace::orchestraSeating);
        };
       #endif
        spatialWorkspace.onLayoutChange = [this] { resized(); };
        spatialWorkspace.onSelectTrack = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id)) selection.selectOnly (track);
        };
        articulation.onToggleDock = [this] { toggleArticulationDock(); };
        articulation.onStatus = [this] (const String& status) { message.setText (status, dontSendNotification); };
        articulation.onLayoutChange = [this]
        {
            if (articulationWindow != nullptr)
                articulationWindow->fitPalette();
            else
                resized();
        };
        spatialWorkspace.onOpenRoomMixer = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id))
            {
                selection.selectOnly (track);
                if (dockMode == DockMode::compact) dockMode = DockMode::split;
                showWorkspace (Workspace::mix);
            }
        };
        resources.onOpen = [this]
        {
            if (dockMode == DockMode::compact) dockMode = DockMode::split;
            showWorkspace (Workspace::resources);
        };
        addAndMakeVisible (retryAriaButton);
        addAndMakeVisible (confirmAriaButton);
        addChildComponent (ariaReviewNotice);
        ariaReviewNotice.setText ("ARIA CHECK  |  Play the restored instrument, then confirm to save.",
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
        workspaceBody.addChildComponent (monitor);
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
            railContent.addAndMakeVisible (*item.button);
            item.button->onClick = [this, destination = item.destination] { toggleWorkspace (destination); };
        }
        articulationsButton.onClick = [this]
        {
            if (articulationWindow != nullptr)
                articulationWindow->toFront (true);
            else
            {
                if (dockMode == DockMode::compact) dockMode = DockMode::split;
                toggleWorkspace (Workspace::articulations);
            }
        };
        railContent.addAndMakeVisible (moreButton);
        moreButton.onClick = [this] { showMoreWorkspaces(); };
        addAndMakeVisible (closeWorkspace);
        closeWorkspace.onClick = [this]
        {
            performanceMode.setSelectedId ((int) studio::PerformanceMode::hidden, dontSendNotification);
            applyPerformanceMode();
        };
        for (const auto& item : dockItems())
        {
            addAndMakeVisible (*item.button);
            item.button->onClick = [this, mode = item.mode] { setDockMode (mode); };
        }
        addChildComponent (workspaceSummary);
        workspaceBody.addChildComponent (browser);
        workspaceBody.addChildComponent (channelInspector);
        workspaceBody.addChildComponent (mixer);
        configureInstrumentWorkflow();
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
        addAndMakeVisible (workspaceDivider);
        workspaceDivider.getSize = [this] { return workspacePanel.getHeight(); };
        workspaceDivider.resizePane = [this] (int size)
        {
            const int nextHeight = jlimit (jmin (studio::appearance.px (minimumSplitHeight), splitLimit), splitLimit, size);
            if (dockMode != DockMode::split)
            {
                workspaceHeight = nextHeight;
                setDockMode (DockMode::split);
            }
            else if (workspaceHeight != nextHeight)
            {
                workspaceHeight = nextHeight;
                // Split is already selected: setDockMode intentionally skips
                // re-layout in that case, but every pointer drag must relayout.
                resized();
            }
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
        workspaceBody.addChildComponent (piano);
        piano.setVisible (false);
        piano.onMidiChanged = [this] { if (editor != nullptr) editor->repaint(); };
        piano.onNoteDrawn = [this] (te::MidiClip& clip, te::BeatPosition beat, int pitch)
        {
            if (! articulation.isShowing() && ! articulationStrip.isShowing()) return;
            articulation.refresh (workflowTrack(), &clip, canEditRegions());
            articulation.writeForDrawnNote (clip, beat, pitch);
            articulationStrip.refresh();
        };
        piano.onControllerLayout = [this] { resized(); };
        for (auto* label : { &title, &message, &trackDetails, &workspaceTitle })
            addAndMakeVisible (*label);
        for (auto* readout : { &bars, &position, &tempo, &meter })
        {
            addAndMakeVisible (*readout);
            readout->setValue ("-");
        }
        title.setFont (FontOptions (15.0f, Font::bold));
        for (auto* label : { &message, &trackDetails, &workspaceSummary })
        {
            label->setFont (FontOptions (13.0f));
            label->setMinimumHorizontalScale (1.0f);
        }
        title.setText ("Studio 0.2", dontSendNotification);
        workspaceTitle.setFont (FontOptions (15.0f, Font::bold));
        message.setColour (Label::textColourId, studio::colours::secondary);
        trackDetails.setColour (Label::textColourId, studio::colours::secondary);
        rewind.setTooltip ("Return to project start");
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

        newProject.onClick = [this] { chooseProject (true); };
        openProject.onClick = [this] { chooseProject (false); };
        saveProject.onClick = [this] { save(); };
        saveAsProject.onClick = [this] { chooseSaveAs(); };
        audioSettings.onClick = [this] { showAudioSettings(); };
        plugins.onClick = [this] { showPlugins(); };
        rewind.onClick = [this]
        {
            if (edit != nullptr && ! busy && studio::transportSettled (*edit))
            {
                piano.silencePreview();
                edit->getTransport().setPosition (0s);
            }
        };
        play.onClick = [this]
        {
            piano.silencePreview();
            if (edit != nullptr && ! edit->getTransport().isPlaying() && ! edit->getTransport().isRecording())
                EngineHelpers::togglePlay (*edit);
        };
        stop.setTooltip ("Stop playback or keep the current recording");
        stop.onClick = [this]
        {
            if (edit == nullptr) return;
            if (edit->getTransport().isRecording())
                toggleRecordKeepingTake();
            else if (edit->getTransport().isPlaying())
            {
                piano.silencePreview();
                EngineHelpers::togglePlay (*edit);
            }
        };
        record.onClick = [this] { toggleRecordKeepingTake(); };
        addMidi.onClick = [this]
        {
            PopupMenu menu;
            const Component::SafePointer<StudioView> safe (this);
            menu.addItem ("Add Instrument", [safe] { if (safe != nullptr) safe->addTrack ("Instrument"); });
            menu.addItem ("Add Synth", canInsertPlugin(), false, [safe]
            {
                if (safe != nullptr && safe->browser.loadSynth) safe->browser.loadSynth ("analog", true);
            });
            menu.addItem ("Add Sampler", canInsertPlugin(), false, [safe]
            {
                if (safe != nullptr && safe->browser.loadSampler) safe->browser.loadSampler (true);
            });
            menu.showMenuAsync (PopupMenu::Options().withTargetComponent (addMidi.isVisible() ? &addMidi : &trackMenu));
        };
        addSynthTrack.onClick = [this] { if (browser.loadSynth) browser.loadSynth ("analog", true); };
        addSamplerTrack.onClick = [this] { if (browser.loadSampler) browser.loadSampler (true); };
        addAndMakeVisible (addMidiClip);
        addMidiClip.onClick = [this] { createMidiClip(); };
        addAudio.onClick = [this] { addTrack ("Audio"); };
        importAudio.onClick = [this] { chooseAudio(); };
        importMidi.onClick = [this] { chooseMidiImport(); };
        exportMidi.onClick = [this] { chooseMidiExport(); };
        exportWav.onClick = [this] { chooseWavExport(); };

        message.setText ("Create or open a project to begin.", dontSendNotification);
        addChildComponent (environmentWelcome);
        environmentWelcome.onNewProject = [this] { chooseProject (true); };
        environmentWelcome.onOpenProject = [this] { chooseProject (false); };
        environmentWelcome.onContinue = [this] { grabKeyboardFocus(); };
        setSize (1024, 640);
        applyAppearance (studio::appearance.interfacePercent(), studio::appearance.textPercent(), false);
        showWorkspace (Workspace::closed);
        timerCallback();
        startTimerHz (24);
        if (environmentWelcome.showAtLaunch()) environmentWelcome.open (true);
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
        if (bounceWindow != nullptr)
            bounceWindow.deleteAndZero();
        bounce.reset();
        pendingPlugin.reset();
        rememberInstrumentWorkspace();
        performance.clear();
        instrumentWorkspace.clear();
        static_cast<StudioPluginWindows&> (engine.getUIBehaviour()).openNative = {};
        channelInspector.setEdit (nullptr);
        mixer.setEdit (nullptr);
        resources.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
       #if JUCE_MAC
        seatingWorkspace.setEdit (nullptr);
       #endif
        articulation.setContext (nullptr);
        if (articulationWindow != nullptr)
        {
            rememberArticulationWindow();
            articulationWindow->setLookAndFeel (nullptr);
        }
        articulationWindow.reset();
        workspaceViewport.setViewedComponent (nullptr);
        railViewport.setViewedComponent (nullptr);
        LookAndFeel::setDefaultLookAndFeel (nullptr);
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
        graphics.setColour (well);
        graphics.fillRoundedRectangle (musicalDisplay.toFloat(), (float) studio::appearance.px (6));
        graphics.setColour (divider);
        graphics.drawRoundedRectangle (musicalDisplay.toFloat().reduced (.5f), (float) studio::appearance.px (6), 1.0f);
        graphics.setColour (panel);
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
        const auto& m = studio::appearance;
        graphics.setFont (studio::uiFont (11.0f, Font::bold));
        graphics.drawText ("Tracks", rulerTrackLabel.reduced (m.px (5), 0),
                           Justification::centredLeft);
        auto area = ruler;
        graphics.drawText ("Inserts", rulerRow.withLeft (area.getRight()).reduced (m.px (8), 0),
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
        auto cycleLane = area.removeFromTop (m.control (18));
        graphics.setColour (timelineCycleColour);
        graphics.fillRect (cycleLane);
        graphics.saveState();
        graphics.reduceClipRegion (cycleLane);
        auto& transport = edit->getTransport();
        const auto range = cycleAnchor.has_value() ? cyclePreview : transport.getLoopRange();
        auto xAt = [&] (te::TimePosition time)
        {
            return cycleLane.getX() + view.timeToX (time, cycleLane.getWidth());
        };
        if (! range.isEmpty())
        {
            const int left = xAt (range.getStart()), right = xAt (range.getEnd());
            graphics.setColour (action.withAlpha (transport.looping || cycleAnchor.has_value() ? 0.48f : 0.17f));
            graphics.fillRect (cycleLane.withLeft (left).withRight (right));
            graphics.setColour (action);
            graphics.drawVerticalLine (left, (float) cycleLane.getY(), (float) cycleLane.getBottom());
            graphics.drawVerticalLine (right, (float) cycleLane.getY(), (float) cycleLane.getBottom());
        }
        int markerLabelRight = cycleLane.getX();
        for (const auto* cue : edit->getMarkerManager().getMarkers())
        {
            const int x = xAt (cue->getPosition().getStart());
            if (x < cycleLane.getX() || x >= cycleLane.getRight())
                continue;
            graphics.setColour (warning);
            graphics.fillRect (x, cycleLane.getY(), m.px (2), cycleLane.getHeight());
            if (x >= markerLabelRight)
            {
                const auto label = cue->getName();
                const int width = m.span (95);
                graphics.drawText (label, x + m.px (4), cycleLane.getY(), width,
                                   cycleLane.getHeight(), Justification::centredLeft, true);
                markerLabelRight = x + width;
            }
        }
        graphics.restoreState();
        int lastLabelRight = area.getX() - m.span (50);
        studio::forEachMusicalTick (edit->tempoSequence, view.viewX1.get(), view.viewX2.get(),
            [&] (const studio::MusicalTick& tick)
            {
                const int x = area.getX() + view.timeToX (tick.time, area.getWidth());
                if (x < area.getX() || x >= area.getRight())
                    return;
                const bool bar = tick.beatIndex == 0;
                graphics.setColour (bar ? action : secondary.withAlpha (0.5f));
                graphics.drawVerticalLine (x, (float) (area.getBottom() - m.px (bar ? 13 : 7)),
                                            (float) area.getBottom());
                if (bar && x >= lastLabelRight)
                {
                    graphics.drawText (String (tick.barIndex + 1), x + m.px (4), area.getY(), m.span (44),
                                       area.getHeight() - m.px (5), Justification::centredLeft);
                    lastLabelRight = x + m.span (42);
                }
            });
        graphics.saveState();
        graphics.reduceClipRegion (area);
        graphics.setColour (secondary);
        for (const auto* event : edit->tempoSequence.getTempos())
        {
            const auto time = event->getStartTime();
            if (time <= te::TimePosition() || time < view.viewX1.get() || time >= view.viewX2.get())
                continue;
            const int x = xAt (time);
            graphics.drawVerticalLine (x, (float) area.getY(), (float) area.getY() + m.px (5));
            graphics.drawText (String (event->getBpm(), 0) + " BPM", x + m.px (19), area.getY(),
                               m.span (56), area.getHeight() - m.px (5), Justification::centredLeft, true);
        }
        for (const auto* event : edit->tempoSequence.getTimeSigs())
        {
            const auto time = event->getPosition().getStart();
            if (time <= te::TimePosition() || time < view.viewX1.get() || time >= view.viewX2.get())
                continue;
            const int x = xAt (time);
            const bool hasTempo = edit->tempoSequence.getTempoAt (event->getStartBeat()).getStartBeat() == event->getStartBeat();
            graphics.drawVerticalLine (x, (float) area.getY(), (float) area.getY() + m.px (5));
            graphics.drawText (event->getStringTimeSig(), x + m.px (hasTempo ? 75 : 19), area.getY(),
                               m.span (42), area.getHeight() - m.px (5), Justification::centredLeft, true);
        }
        graphics.restoreState();
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
                g.setColour (studio::colours::grid.withAlpha (tick.beatIndex == 0 ? 0.66f : 0.24f));
                g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
            });
        g.restoreState();
    }

    te::TimePosition rulerTime (int x, bool snap) const
    {
        auto& view = editor->getEditViewState();
        const auto time = view.xToTime (jlimit (0, ruler.getWidth(), x - ruler.getX()), ruler.getWidth());
        return snap ? studio::snapMusicalTime (edit->tempoSequence, time, studio::regionSnapStep (*edit)) : time;
    }

    void mouseDown (const MouseEvent& event) override
    {
        if (editor == nullptr || ! canEditRegions() || ! ruler.contains (event.getPosition()))
            return;
        if (event.mods.isPopupMenu())
        {
            music.showTimelineMenu (rulerTime (event.x, false));
            return;
        }
        if (! event.mods.isLeftButtonDown())
            return;

        if (event.y < ruler.getY() + studio::appearance.control (18))
        {
            const auto range = edit->getTransport().getLoopRange();
            auto& view = editor->getEditViewState();
            const auto near = [&] (te::TimePosition time)
            {
                return std::abs (event.x - ruler.getX() - view.timeToX (time, ruler.getWidth())) <= studio::appearance.px (6);
            };
            if (! range.isEmpty() && near (range.getStart()))
                cycleAnchor = range.getEnd();
            else if (! range.isEmpty() && near (range.getEnd()))
                cycleAnchor = range.getStart();
            else
                cycleAnchor = rulerTime (event.x, ! event.mods.isAltDown());
            cyclePreview = range;
            return;
        }
        piano.silencePreview();
        edit->getTransport().setPosition (rulerTime (event.x, ! event.mods.isAltDown()));
    }

    void mouseDrag (const MouseEvent& event) override
    {
        if (cycleAnchor && ! busy && studio::transportSettled (*edit))
        {
            cyclePreview = te::TimeRange::between (*cycleAnchor, rulerTime (event.x, ! event.mods.isAltDown()));
            repaint (ruler);
        }
    }

    void mouseUp (const MouseEvent& event) override
    {
        if (! cycleAnchor)
            return;
        cycleAnchor.reset();
        if (event.mouseWasDraggedSinceMouseDown() && ! busy && ! studio::setCycle (*edit, cyclePreview))
            message.setText ("Drag a cycle at least 0.12 seconds long.", dontSendNotification);
        repaint (ruler);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (environmentWelcome.isVisible()) return environmentWelcome.keyPressed (key);
        if (handleAppearanceKey (key)
            || ((articulation.isShowing() || articulationStrip.isShowing()) && articulation.keyPressed (key))) return true;
        if (key == KeyPress::spaceKey && play.isEnabled())
        {
            if (edit != nullptr && (edit->getTransport().isPlaying() || edit->getTransport().isRecording()))
                stop.onClick();
            else play.onClick();
            return true;
        }
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'J' || key.getKeyCode() == 'j')
            && canEditRegions())
        {
            joinSelectedMidi();
            return true;
        }
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
            && canEditRegions())
        {
            if (key.getModifiers().isShiftDown())
                edit->redo();
            else
                edit->undo();
            repaint (rulerRow);
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
        const auto& m = studio::appearance;
        auto area = getLocalBounds();
        const int projectHeight = m.control (31, 12);
        const int transportHeight = jmax (m.control (44, 12), m.transportRowHeight() + m.px (7));
        topBar = area.removeFromTop (projectHeight + transportHeight + m.px (8));
        auto commands = topBar.withHeight (projectHeight).reduced (m.px (10), m.px (2));
        performanceMode.setBounds (commands.removeFromRight (m.span (163)).reduced (m.px (2), 0));
        viewButton.setBounds (commands.removeFromRight (m.span (63)).reduced (m.px (2), 0));
        const bool showCPU = commands.getWidth() > m.span (325);
        resources.audio.setVisible (showCPU);
        resources.ram.setVisible (false);
        resources.dropouts.setVisible (false);
        if (showCPU) resources.audio.setBounds (commands.removeFromRight (m.span (68)).reduced (m.px (2), 0));
        projectMenu.setVisible (true);
        projectMenu.setBounds (commands.removeFromRight (m.span (81)).reduced (m.px (2), 0));
        const bool showFiles = commands.getWidth() >= m.span (340);
        for (auto* button : { &newProject, &openProject })
        {
            button->setVisible (showFiles);
            if (showFiles) button->setBounds (commands.removeFromLeft (m.span (93)).reduced (m.px (2), 0));
        }
        for (auto* button : { &plugins, &audioSettings, &saveAsProject, &saveProject, &monitorButton })
            button->setVisible (false);
        title.setBounds (commands.reduced (m.px (8), 0));

        auto strip = topBar.withTrimmedTop (projectHeight + m.px (3)).reduced (m.px (10), m.px (2));
        const int barW = m.span (91), bpmW = m.span (58), meterW = m.span (53), menuW = m.span (76);
        const int gap = m.px (6);
        const int fixed = barW + bpmW + meterW + menuW + 4 * gap;
        const int key = jlimit (m.px (20), m.px (38), (strip.getWidth() - fixed) / 6);
        const bool showElapsed = fixed + 6 * key + m.span (97) <= strip.getWidth();
        const int readoutWidth = barW + bpmW + meterW + (showElapsed ? m.span (97) : 0);
        position.setVisible (showElapsed);
        auto row = strip.withSizeKeepingCentre (fixed + 6 * key + (showElapsed ? m.span (97) : 0), transportHeight);
        for (auto* button : { &rewind, &stop, &play, &record })
            button->setBounds (row.removeFromLeft (key).reduced (m.px (1), m.px (5)));
        row.removeFromLeft (gap);
        for (auto* button : { &cycleButton, &clickButton })
            button->setBounds (row.removeFromLeft (key).reduced (m.px (1), m.px (5)));
        row.removeFromLeft (gap);
        musicalDisplay = row.removeFromLeft (readoutWidth);
        auto lcd = musicalDisplay.reduced (gap / 2, m.px (2));
        bars.setBounds (lcd.removeFromLeft (barW - gap / 2));
        if (showElapsed) position.setBounds (lcd.removeFromLeft (m.span (97)));
        tempo.setBounds (lcd.removeFromLeft (bpmW));
        meter.setBounds (lcd);
        row.removeFromLeft (gap);
        music.setBounds (row.removeFromLeft (menuW).reduced (m.px (1), m.px (5)));

        message.setBounds (area.removeFromBottom (m.line (13, 5)).reduced (m.px (10), 0));
        ariaBanner = ariaReviewPending ? area.removeFromTop (m.control (34)) : Rectangle<int>();
        ariaReviewNotice.setVisible (ariaReviewPending);
        retryAriaButton.setVisible (ariaReviewPending);
        confirmAriaButton.setVisible (ariaReviewPending);
        auto bannerControls = ariaBanner.reduced (m.px (8), m.px (3));
        confirmAriaButton.setBounds (bannerControls.removeFromRight (m.span (114)).reduced (m.px (2), 0));
        retryAriaButton.setBounds (bannerControls.removeFromRight (m.span (90)).reduced (m.px (2), 0));
        ariaReviewNotice.setBounds (bannerControls);

        railBounds = area.removeFromLeft (m.span (getWidth() < m.px (1100) ? 76 : 80));
        railViewport.setBounds (railBounds);
        const int tileHeight = m.px (50) + m.line (11, 1) - m.px (14);
        railContent.setSize (railViewport.getMaximumVisibleWidth(), tileHeight * 7 + m.px (8));
        // Reserve the scrollbar only when the complete rail needs it.
        if (railContent.getHeight() > railBounds.getHeight())
            railContent.setSize (jmax (1, railBounds.getWidth() - railViewport.getScrollBarThickness()), railContent.getHeight());
        auto destinations = railContent.getLocalBounds().reduced (m.px (3), m.px (4));
        for (const auto& item : railItems())
            item.button->setBounds (destinations.removeFromTop (tileHeight).reduced (0, m.px (1)));
        moreButton.setBounds (destinations.removeFromTop (tileHeight).reduced (0, m.px (1)));

        layoutPerformance (area);
        layoutDock (area);
        arrangementTools = area.removeFromTop (m.control (29));
        auto tools = arrangementTools.reduced (m.px (5), m.px (2));
        const bool showAllTrackTools = tools.getWidth() >= m.span (828);
        trackPicker.setBounds (tools.removeFromLeft (m.span (145)).reduced (m.px (2), 0));
        zoom.setBounds (tools.removeFromRight (m.span (107)).reduced (m.px (2), 0));
        trackMenu.setVisible (! showAllTrackTools);
        if (trackMenu.isVisible())
            trackMenu.setBounds (tools.removeFromLeft (m.span (92)).reduced (m.px (2), 0));
        for (auto* button : { &addMidi, &addAudio, &addMidiClip, &importAudio, &regionActions })
        {
            button->setVisible (showAllTrackTools);
            if (showAllTrackTools)
                button->setBounds (tools.removeFromLeft (m.span (button == &addAudio ? 69 : 94)).reduced (m.px (2), 0));
        }
        trackDetails.setBounds (tools);

        rulerRow = area.removeFromTop (m.control (26) + m.control (18));
        timeline.setBounds (area);
        sizeEditor();
        ruler = rulerRow.withTrimmedLeft (m.trackHeader()).withTrimmedRight (m.trackFooter() + timeline.getScrollBarThickness());
        auto zoomButtons = rulerRow.withWidth (m.trackHeader()).reduced (m.px (3));
        const int zoomWidth = m.span (35);
        arrangementZoomIn.setBounds (zoomButtons.removeFromRight (zoomWidth).reduced (m.px (1)));
        arrangementZoomOut.setBounds (zoomButtons.removeFromRight (zoomWidth).reduced (m.px (1)));
        rulerTrackLabel = zoomButtons;
        environmentWelcome.setBounds (getLocalBounds());
        repaint();
    }

    void applyAppearance (int scale, int textSize, bool persist)
    {
        auto& m = studio::appearance;
        const auto oldScale = m.interfacePercent();
        m.set (scale, textSize);
        workspaceHeight = workspaceHeight * m.interfacePercent() / oldScale;
        if (persist)
        {
            auto& prefs = engine.getPropertyStorage().getPropertiesFile();
            prefs.setValue ("studio.interfaceScale", m.interfacePercent());
            prefs.setValue ("studio.textScale", m.textPercent());
            prefs.saveIfNeeded();
        }
        for (auto* view : { &timeline, &railViewport, &workspaceViewport })
            view->setScrollBarThickness (m.scrollbar());
        browser.appearanceChanged();
        channelInspector.appearanceChanged();
        mixer.appearanceChanged();
        piano.appearanceChanged();
        monitor.appearanceChanged();
        resources.appearanceChanged();
        spatialWorkspace.appearanceChanged();
       #if JUCE_MAC
        seatingWorkspace.appearanceChanged();
       #endif
        articulation.appearanceChanged();
        instrumentWorkspace.appearanceChanged();
        if (articulationWindow != nullptr) articulationWindow->fitPalette();
        fitFloatingMonitor();
        if (edit != nullptr)
            for (auto* track : te::getAudioTracks (*edit))
                for (auto* plugin : track->pluginList)
                    if (auto* window = dynamic_cast<PluginWindow*> (plugin->windowState->pluginWindow.get()))
                        if (auto* sampler = dynamic_cast<studio::sampler::Editor*> (window->getEditor()))
                            sampler->appearanceChanged();
        if (editor != nullptr) editor->refreshStudioLayout();
        resized();
        environmentWelcome.resized();
        environmentWelcome.repaint();
        repaint();
    }

    bool handleAppearanceKey (const KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (! mods.isCommandDown() || ! mods.isAltDown() || mods.isShiftDown()) return false;
        const auto& m = studio::appearance;
        const auto code = key.getKeyCode();
        const bool isIncrease = code == '=' || code == '+' || code == KeyPress::numberPadAdd;
        const bool isDecrease = code == '-' || code == KeyPress::numberPadSubtract;
        const bool isReset = code == '0' || code == KeyPress::numberPad0;
        if (! (isIncrease || isDecrease || isReset)) return false;
        // Resizing during a pointer gesture would move a clip or fader under
        // the pointer. Finish the current gesture before adjusting appearance.
        if (ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()) return true;
        if (isIncrease)
            applyAppearance (m.adjacentInterfacePercent (1), m.textPercent(), true);
        else if (isDecrease)
            applyAppearance (m.adjacentInterfacePercent (-1), m.textPercent(), true);
        else
            applyAppearance (100, m.textPercent(), true);
        return true;
    }

    void showViewOptions()
    {
        PopupMenu scale, textSize;
        const auto& metrics = studio::appearance;
        auto safe = SafePointer<StudioView> (this);
        for (auto percent : studio::AppearanceMetrics::interfaceOptions)
            scale.addItem (String (percent) + "%", true, metrics.interfacePercent() == percent,
                [safe, percent] { if (safe != nullptr) safe->applyAppearance (percent, studio::appearance.textPercent(), true); });
        static constexpr const char* labels[] { "Standard", "Large", "Extra Large" };
        for (size_t i = 0; i < studio::AppearanceMetrics::textOptions.size(); ++i)
        {
            const auto percent = studio::AppearanceMetrics::textOptions[i];
            textSize.addItem (labels[i], true, metrics.textPercent() == percent,
                [safe, percent] { if (safe != nullptr) safe->applyAppearance (studio::appearance.interfacePercent(), percent, true); });
        }
        PopupMenu menu;
        menu.addItem ("Studio Environments...", [safe]
        {
            if (safe != nullptr) safe->environmentWelcome.open (false);
        });
        menu.addSeparator();
        menu.addItem ("Resource Inspector", [safe] { if (safe != nullptr) safe->resources.onOpen(); });
        menu.addItem ("MIDI Monitor", [safe] { if (safe != nullptr) safe->monitorButton.onClick(); });
        menu.addSeparator();
        menu.addSubMenu ("Interface Scale", scale);
        menu.addSubMenu ("Text Size", textSize);
        menu.addSeparator();
        menu.addItem ("Increase Interface Scale", metrics.interfacePercent() < 175, false,
            [safe] { if (safe != nullptr) safe->applyAppearance (studio::appearance.adjacentInterfacePercent (1),
                                                                studio::appearance.textPercent(), true); });
        menu.addItem ("Decrease Interface Scale", metrics.interfacePercent() > 100, false,
            [safe] { if (safe != nullptr) safe->applyAppearance (studio::appearance.adjacentInterfacePercent (-1),
                                                                studio::appearance.textPercent(), true); });
        menu.addItem ("Reset Appearance", [safe] { if (safe != nullptr) safe->applyAppearance (100, 100, true); });
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&viewButton));
    }

    void showCommandMenu (TextButton& anchor, std::initializer_list<TextButton*> commands)
    {
        PopupMenu menu;
        for (auto* button : commands)
        {
            const SafePointer<TextButton> safe (button);
            menu.addItem (button->getButtonText(), button->isEnabled(), false, [safe]
            {
                if (safe != nullptr && safe->isEnabled() && safe->onClick) safe->onClick();
            });
        }
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
    }


private:
    using Workspace = studio::Workspace;
    using DockMode = studio::DockMode;

    studio::StudioLookAndFeel style;
    Workspace workspace = Workspace::closed;
    studio::WorkspaceScroll workspaceScroll;
    DockMode dockMode = DockMode::split;
    WorkspaceDivider workspaceDivider;
    static constexpr int minimumSplitHeight = 170;
    int workspaceHeight = 270, splitLimit = minimumSplitHeight;
    Rectangle<int> topBar, musicalDisplay, railBounds, ariaBanner, arrangementTools, workspacePanel, rulerRow, rulerTrackLabel;
    Component railContent, workspaceBody;
    TooltipWindow tooltips { this, 650 };
    Viewport railViewport, workspaceViewport;
    bool collapsedDockLinks = false;
    TextButton viewButton { "View..." }, projectMenu { "Project..." }, trackMenu { "Tracks..." }, dockMenu { "Layout..." };
    te::Engine engine { "Studio 0.1", std::make_unique<StudioPluginWindows>(), nullptr };
    studio::StudioEnvironmentWelcome environmentWelcome { engine.getPropertyStorage().getPropertiesFile() };
    ComboBox performanceMode;
    studio::InstrumentPerformance performance;
    studio::InstrumentWorkspace instrumentWorkspace { engine.getPropertyStorage().getPropertiesFile() };
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


    Label title, message, trackDetails, workspaceTitle,
          workspaceSummary, ariaReviewNotice;
    TextButton importMidi { "Import MIDI..." }, exportMidi { "Export MIDI..." }, exportWav { "Export WAV..." };
    std::unique_ptr<studio::bounce::Job> bounce;
    double bounceProgress = 0;
    SafePointer<AlertWindow> bounceWindow;
    studio::MusicalTransport music;
    const Colour timelineCycleColour { 0xff242c36 };
    std::optional<te::TimePosition> cycleAnchor;
    te::TimeRange cyclePreview, displayedCycle;
    bool displayedLooping = false;
    std::size_t displayedTempoHash = 0;
    studio::TransportReadout bars { "Bars / Beats" }, position { "Elapsed" }, tempo { "BPM", true }, meter { "Meter", true };
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
    studio::TrackActions trackActions;
    studio::InstrumentBrowser browser { engine.getPluginManager().knownPluginList };
    studio::ChannelInspector channelInspector { trackActions };
    studio::StudioMixer mixer { trackActions };
    studio::StudioResources resources { engine.getDeviceManager() };
    studio::spatial::SpatialWorkspace spatialWorkspace;
   #if JUCE_MAC
    studio::seating::Workspace seatingWorkspace;
   #endif
    studio::ArticulationPalette articulation { engine.getPropertyStorage().getPropertiesFile() };
    studio::ArticulationStrip articulationStrip { articulation };
    std::unique_ptr<ArticulationWindow> articulationWindow;

    struct PendingPlugin
    {
        te::Plugin::Ptr plugin;
        te::EditItemID destination;
        bool addTrack = false, instrument = false;
        uint32 startedAt = 0;
    };

    std::optional<PendingPlugin> pendingPlugin;
    uint64 projectGeneration = 0;
    unsigned workspaceTicks = 0;
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
               addSynthTrack { "Add Synth" }, addSamplerTrack { "Add Sampler" },
               addMidi { "+ Instrument" }, addAudio { "+ Audio" }, importAudio { "Import Audio" },
               addMidiClip { "+ MIDI Clip" };

    using TransportIcon = studio::TransportIcon;
    studio::TransportButton rewind { "Return to start", TransportIcon::start }, play { "Play", TransportIcon::play },
                            stop { "Stop", TransportIcon::stop }, record { "Record", TransportIcon::record },
                            cycleButton { "Toggle cycle playback", TransportIcon::cycle },
                            clickButton { "Toggle metronome", TransportIcon::click };

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
        { Workspace::midi, "MIDI Monitor" }, { Workspace::resources, "Resource Inspector" },
        { Workspace::instrumentEditor, "Instrument Editor" },
       #if JUCE_MAC
        { Workspace::orchestraSeating, "Arrangement Reference" },
       #endif
    };

    std::array<WorkspaceEntry, 2> contextLinks() const
    {
        switch (workspace)
        {
            case Workspace::stage:       return {{ { Workspace::rooms, "Rooms" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::rooms:       return {{ { Workspace::stage, "Stage" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::microphones: return {{ { Workspace::stage, "Stage" }, { Workspace::rooms, "Rooms" } }};
            case Workspace::instrumentEditor: return {{ { Workspace::instruments, "Browser" }, { Workspace::channel, "Channel" } }};
            case Workspace::orchestraSeating: return {{ { Workspace::stage, "Stage" }, { Workspace::rooms, "Rooms" } }};
            case Workspace::channel:     return {{ { Workspace::instruments, "Instruments" }, { Workspace::stage, "Stage" } }};
            default:                    return {{ { Workspace::closed, "" }, { Workspace::closed, "" } }};
        }
    }

    void applyPerformanceMode()
    {
        auto& preferences = engine.getPropertyStorage().getPropertiesFile();
        preferences.setValue ("studio.performanceMode", performanceMode.getSelectedId());
        preferences.saveIfNeeded();
        if (performanceMode.getSelectedId() == (int) studio::PerformanceMode::hidden)
            showWorkspace (Workspace::closed);
        else
            resized();
    }

    void layoutPerformance (Rectangle<int>& area)
    {
        const auto& m = studio::appearance;
        const auto mode = (studio::PerformanceMode) performanceMode.getSelectedId();
        const bool visible = edit != nullptr && mode != studio::PerformanceMode::hidden;
        // The full mapping editor already displays these same actions.
        const bool showStrip = visible && workspace != Workspace::articulations;
        articulationStrip.setVisible (showStrip);
        if (showStrip) articulationStrip.setBounds (area.removeFromBottom (studio::ArticulationStrip::height()));
        int panelHeight = jmin (studio::InstrumentPerformance::height(), getHeight() * 26 / 100);
        if (workspace != Workspace::closed && dockMode != DockMode::compact)
        {
            // Keep room for the ruler and an interactive editor at large appearance scales.
            const auto reserved = m.control (32) + m.control (26) + m.px (9 + 28)
                                 + m.control (34) + m.control (24);
            panelHeight = jmin (panelHeight, area.getHeight() - reserved);
        }
        const int usableHeight = m.control (28, 12) + m.line (11, 2) + m.line (12, 2) + m.px (33);
        performance.setVisible (visible && mode == studio::PerformanceMode::full && panelHeight >= usableHeight);
        if (performance.isVisible())
            performance.setBounds (area.removeFromBottom (panelHeight));
    }

    void layoutDock (Rectangle<int>& arrangement)
    {
        const auto& m = studio::appearance;
        const int available = arrangement.getHeight();
        const int headerHeight = m.control (34);
        const int dividerHeight = m.px (12);
        const auto room = m.dockRoom (available, workspaceHeight, dockMode);
        splitLimit = room.splitLimit;
        const int panelHeight = workspace == Workspace::closed ? 0 : room.panelHeight;
        workspacePanel = arrangement.removeFromBottom (panelHeight);
        workspaceDivider.setBounds (panelHeight > 0 ? arrangement.removeFromBottom (dividerHeight) : Rectangle<int>());
        auto body = workspacePanel;
        auto header = body.removeFromTop (headerHeight).reduced (m.px (9), m.px (3));
        const bool hasLinks = contextLinks()[0].destination != Workspace::closed;
        const bool collapse = header.getWidth() < m.span (hasLinks ? 676 : 470);
        collapsedDockLinks = hasLinks && collapse;
        dockMenu.setVisible (panelHeight > 0 && collapse);
        for (const auto& item : dockItems())
            item.button->setVisible (panelHeight > 0 && ! collapse);
        closeWorkspace.setVisible (panelHeight > 0 && ! collapse);
        if (dockMenu.isVisible())
            dockMenu.setBounds (header.removeFromRight (m.span (96)));
        if (! collapse)
        {
            closeWorkspace.setBounds (header.removeFromRight (m.span (54)).reduced (m.px (2), 0));
            expandedButton.setBounds (header.removeFromRight (m.span (85)).reduced (m.px (2), 0));
            splitButton.setBounds (header.removeFromRight (m.span (48)).reduced (m.px (2), 0));
            compactButton.setBounds (header.removeFromRight (m.span (76)).reduced (m.px (2), 0));
        }
        for (auto it = workspaceLinks.rbegin(); it != workspaceLinks.rend(); ++it)
        {
            it->setVisible (panelHeight > 0 && hasLinks && ! collapse);
            if (it->isVisible())
                it->setBounds (header.removeFromRight (m.span (94)).reduced (m.px (2), 0));
        }
        workspaceTitle.setBounds (header);
        workspaceSummary.setBounds (body.reduced (m.px (9), m.px (1)));
       #if JUCE_MAC
        seatingWorkspace.setBounds (body);
        updateSeatingVisibility();
       #endif

        const bool dockedMonitor = monitor.getParentComponent() == &workspaceBody && monitor.isVisible();
        const auto minimumContentHeight = [this, dockedMonitor] (int width)
        {
            if (instrumentWorkspace.isVisible()) return instrumentWorkspace.minimumContentHeight();
            if (browser.isVisible()) return browser.minimumContentHeight (width);
            if (channelInspector.isVisible()) return channelInspector.minimumContentHeight (width);
            if (mixer.isVisible()) return mixer.minimumContentHeight();
            if (piano.isVisible()) return piano.minimumContentHeight (width);
            if (resources.isVisible()) return resources.minimumContentHeight (width);
            if (spatialWorkspace.isVisible()) return spatialWorkspace.minimumContentHeight (width);
            if (articulationWindow == nullptr && articulation.isVisible()) return articulation.minimumContentHeight (width);
            return dockedMonitor ? monitor.minimumContentHeight (width) : 0;
        };
        auto width = body.getWidth();
        auto height = minimumContentHeight (width);
        workspaceViewport.setVisible (height > 0 && ! body.isEmpty());
        workspaceViewport.setBounds (body);
        if (height > body.getHeight())
        {
            width = jmax (1, width - workspaceViewport.getScrollBarThickness());
            height = minimumContentHeight (width);
        }
        workspaceBody.setSize (width, jmax (height, body.getHeight()));
        for (Component* item : std::initializer_list<Component*> { &browser, &channelInspector, &mixer, &piano, &resources, &spatialWorkspace, &instrumentWorkspace })
            if (item->isVisible()) item->setBounds (workspaceBody.getLocalBounds());
        if (dockedMonitor) monitor.setBounds (workspaceBody.getLocalBounds());
        if (articulationWindow == nullptr && articulation.isVisible()) articulation.setBounds (workspaceBody.getLocalBounds());
    }

   #if JUCE_MAC
    void updateSeatingVisibility()
    {
        seatingWorkspace.setVisible (workspace == Workspace::orchestraSeating && dockMode != DockMode::compact
                                     && ! seatingWorkspace.getBounds().isEmpty() && ! environmentWelcome.isVisible());
    }
   #endif

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
        if (dockMode == mode) return;
        rememberWorkspaceScroll();
        dockMode = mode;
        syncWorkspace();
        restoreWorkspaceScroll();
    }

    void rememberWorkspaceScroll()
    {
        if (workspaceViewport.isVisible()) workspaceScroll.store (workspace, workspaceViewport.getViewPositionY());
    }

    void restoreWorkspaceScroll()
    {
        if (workspaceViewport.isVisible()) workspaceViewport.setViewPosition (0, workspaceScroll.position (workspace));
    }

    void showWorkspace (Workspace next)
    {
        const bool changing = workspace != next;
        if (changing) rememberWorkspaceScroll();
        if ((next == Workspace::piano || ((next == Workspace::instrumentEditor || next == Workspace::orchestraSeating) && changing))
            && dockMode == DockMode::compact)
            dockMode = DockMode::split;
        workspace = next;
        syncWorkspace();
        if (changing) restoreWorkspaceScroll();
    }

    void syncWorkspace()
    {
        const bool open = workspace != Workspace::closed;
        const bool compact = dockMode == DockMode::compact;
        const bool mixing = workspace == Workspace::mix;
        const bool monitoring = workspace == Workspace::midi;
        const bool editingMidi = workspace == Workspace::piano;
        const bool browsing = workspace == Workspace::instruments;
        const bool inspecting = workspace == Workspace::channel;
        const bool checkingResources = workspace == Workspace::resources;
        const bool showingArticulation = workspace == Workspace::articulations;
        const bool showingSpatial = workspace == Workspace::stage || workspace == Workspace::rooms
                                 || workspace == Workspace::microphones;
        String name;
        bool inPrimaryRail = false;
        for (const auto& item : railItems())
        {
            const bool selected = workspace == item.destination
                || (item.destination == Workspace::articulations && articulationWindow != nullptr);
            item.button->setToggleState (selected, dontSendNotification);
            if (workspace == item.destination)
            {
                name = item.button->getButtonText();
                inPrimaryRail = true;
            }
        }
        for (const auto& entry : extraWorkspaces)
            if (workspace == entry.destination)
                name = entry.name;
        moreButton.setToggleState (open && ! inPrimaryRail, dontSendNotification);
        if (monitoring)
            name = "MIDI Monitor";
        if (workspace == Workspace::instruments)
            name = "Instrument Browser";
        if (workspace == Workspace::stage)
            name = "Orchestral Stage";

        instrumentWorkspace.setVisible (workspace == Workspace::instrumentEditor && ! compact);
        browser.setVisible (browsing && ! compact);
        channelInspector.setVisible (inspecting && ! compact);
        resources.setVisible (checkingResources && ! compact);
        spatialWorkspace.setVisible (showingSpatial && (! compact || workspace == Workspace::stage));
        if (showingSpatial)
        {
            using View = studio::spatial::SpatialWorkspace::View;
            const auto view = workspace == Workspace::stage ? View::stage : workspace == Workspace::rooms ? View::rooms : View::microphones;
            spatialWorkspace.setView (view);
            spatialWorkspace.setCompactStage (compact && view == View::stage);
        }
        articulation.setVisible (articulationWindow != nullptr || (showingArticulation && ! compact));
        mixer.setVisible (mixing && ! compact);
        if (! mixer.isVisible()) mixer.pauseMeters();
        workspaceTicks = 1; // Refresh the newly shown workspace on the next host tick.
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
        workspaceSummary.setVisible (open && compact && workspace != Workspace::stage);
        workspaceSummary.setText ("Choose Split or Expanded to show controls.", dontSendNotification);
        if (! editingMidi || compact)
            piano.silencePreview();
        piano.setVisible (editingMidi && ! compact);
        monitorButton.setToggleState (monitoring, dontSendNotification);
        if (monitor.getParentComponent() == &workspaceBody)
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
            workspaceHeight = studio::appearance.px (360);
            showWorkspace (Workspace::piano);
            message.setText ("Draw notes in the piano roll, then Save to keep your edits.", dontSendNotification);
        }
    }

    void fitFloatingMonitor()
    {
        if (monitorWindow == nullptr || monitor.getParentComponent() != monitorWindow.get())
            return;
        const int height = monitor.minimumContentHeight (monitor.getWidth()) + studio::appearance.px (40);
        monitorWindow->setResizeLimits (770, jmax (200, height), 2200, 1000);
        auto bounds = monitorWindow->getBounds();
        bounds.setHeight (jmax (bounds.getHeight(), height));
        if (const auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
            bounds = bounds.constrainedWithin (displayClientArea (*monitorWindow, *display));
        monitorWindow->setBounds (bounds);
    }

    void toggleMonitorDock()
    {
        if (monitorWindow != nullptr && monitorWindow->isVisible())
        {
            monitorWindow->setVisible (false);
            monitorWindow->clearContentComponent();
            workspaceBody.addAndMakeVisible (monitor);
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
                monitorWindow->onAppearanceKey = [this] (const KeyPress& key) { return handleAppearanceKey (key); };
                monitorWindow->onClose = [this] { toggleMonitorDock(); };
                monitorWindow->centreWithSize (860, 350);
            }
            monitorWindow->setContentNonOwned (&monitor, false);
            monitor.setVisible (true);
            monitor.setFloating (true);
            fitFloatingMonitor();
            monitorWindow->setVisible (true);
            monitorWindow->toFront (true);
            showWorkspace (Workspace::closed);
        }
    }

    void rememberArticulationWindow()
    {
        auto& prefs = engine.getPropertyStorage().getPropertiesFile();
        prefs.setValue ("studio.articulationWindow", articulationWindow->getWindowStateAsString());
        prefs.saveIfNeeded();
    }

    void toggleArticulationDock()
    {
        if (articulationWindow != nullptr)
        {
            rememberArticulationWindow();
            // Close may originate in the window's own close-button handler.
            // Keep the window alive until that callback has unwound.
            MessageManager::callAsync ([safe = SafePointer<StudioView> (this)]
            {
                if (! safe || safe->articulationWindow == nullptr) return;
                safe->articulationWindow.reset();
                safe->workspaceBody.addAndMakeVisible (safe->articulation);
                safe->articulation.setFloating (false);
                if (safe->dockMode == DockMode::compact) safe->dockMode = DockMode::split;
                safe->showWorkspace (Workspace::articulations);
            });
            return;
        }

        articulationWindow = std::make_unique<ArticulationWindow> (articulation);
        articulationWindow->setLookAndFeel (&style);
        articulationWindow->onClose = [this] { toggleArticulationDock(); };
        articulationWindow->onAppearanceKey = [this] (const KeyPress& key) { return handleAppearanceKey (key); };
        auto& prefs = engine.getPropertyStorage().getPropertiesFile();
        if (! articulationWindow->restoreWindowStateFromString (prefs.getValue ("studio.articulationWindow")))
            articulationWindow->centreAroundComponent (this, studio::appearance.px (520), studio::appearance.px (300));
        auto bounds = articulationWindow->getBounds();
        if (const auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
            articulationWindow->setBounds (bounds.constrainedWithin (displayClientArea (*articulationWindow, *display)));
        articulation.setFloating (true);
        articulationWindow->setVisible (true);
        showWorkspace (Workspace::closed);
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
                                     (studio::appearance.trackHeight() + studio::appearance.trackGap()) * editor->getVisibleTrackCount());
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

    void joinSelectedMidi()
    {
        const auto midi = selection.getItemsOfType<te::MidiClip>();
        if (midi.size() != selection.getItemsOfType<te::Clip>().size())
        {
            message.setText ("Select MIDI regions only to join them.", dontSendNotification);
            return;
        }
        te::MidiClip::Ptr merged;
        const auto result = studio::midi::join (midi, merged);
        if (result.wasOk()) selection.selectOnly (merged.get());
        message.setText (result.wasOk() ? "Joined MIDI regions." : result.getErrorMessage(), dontSendNotification);
    }

    void populateTrackEditMenu (PopupMenu& parent, te::AudioTrack& track)
    {
        const auto id = track.itemID;
        PopupMenu tracks;
        const auto general = studio::tracks::restriction (track, false);
        const auto destructive = studio::tracks::restriction (track, true);
        tracks.addItem ("Rename...", general.isEmpty(), false, trackMenuAction (id,
            [] (StudioView& view, te::AudioTrack& target) { view.renameTrack (target); }));
        for (const auto direction : { -1, 1 })
            tracks.addItem (direction < 0 ? "Move up" : "Move down", studio::tracks::canMove (track, direction),
                false, trackMenuAction (id, [direction] (StudioView&, te::AudioTrack& target)
                {
                    studio::tracks::move (target, direction);
                }));
        tracks.addItem ("Duplicate (muted)", destructive.isEmpty(), false, trackMenuAction (id,
            [] (StudioView& view, te::AudioTrack& target)
            {
                if (auto copy = studio::tracks::duplicate (target))
                {
                    view.selection.selectOnly (copy.get());
                    view.message.setText ("Duplicated track. Unmute the copy when ready.", dontSendNotification);
                }
                else
                    view.message.setText ("Could not duplicate the track.", dontSendNotification);
            }));
        tracks.addItem ("Delete track", destructive.isEmpty(), false, trackMenuAction (id,
            [] (StudioView& view, te::AudioTrack& target)
            {
                view.piano.setClip (nullptr);
                studio::tracks::remove (target);
                view.message.setText ("Deleted track. Use Undo to restore it.", dontSendNotification);
            }));
        if (destructive.isNotEmpty())
            tracks.addSectionHeader (destructive);
        parent.addSubMenu ("Track · " + track.getName(), tracks);
    }

    void renameTrack (te::AudioTrack& track)
    {
        auto* window = new AlertWindow ("Rename track", {}, MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", track.getName(), "Track name");
        window->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        const SafePointer<StudioView> safe (this);
        const auto generation = projectGeneration;
        const auto id = track.itemID;
        window->enterModalState (true, ModalCallbackFunction::create ([safe, generation, id, window] (int result)
        {
            if (result != 1 || safe == nullptr || safe->projectGeneration != generation || ! safe->canTransferMidi())
                return;
            auto* current = safe->trackById (id);
            const auto name = window->getTextEditorContents ("name").trim();
            if (current != nullptr && ! studio::tracks::rename (*current, name))
                safe->message.setText ("Enter a name for the track.", dontSendNotification);
        }), true);
    }

    void showRegionActions()
    {
        PopupMenu menu;
        const bool selected = selection.getItemsOfType<te::Clip>().size() > 0;
        if (auto* track = workflowTrack())
            populateTrackEditMenu (menu, *track);
        if (edit != nullptr)
        {
            PopupMenu snap;
            const auto division = studio::regionSnapStep (*edit);
            for (const auto& [label, step] : std::initializer_list<std::pair<const char*, double>> {
                   { "Off", 0 }, { "Beat", 1 }, { "Half beat", 0.5 },
                   { "Quarter beat", 0.25 }, { "Beat triplet", 1.0 / 3.0 } })
            {
                const SafePointer<StudioView> safe (this);
                const auto generation = projectGeneration;
                snap.addItem (label, true, step == division, [safe, generation, step]
                {
                    if (safe != nullptr && safe->projectGeneration == generation && safe->canEditRegions())
                        safe->edit->state.setProperty (studio::regionSnapProperty, step, nullptr);
                });
            }
            menu.addSubMenu ("Arrangement snap", snap);
        }
        menu.addItem (1, "Split selected at playhead", selected);
        menu.addItem (2, "Duplicate selected", selected);
        menu.addSeparator();
        menu.addItem (3, "Copy selected regions", selected);
        menu.addItem (4, "Paste at playhead", ! regionClipboard.empty());
        menu.addItem (5, "Cut selected regions", selected);
        menu.addItem (6, "Delete selected regions", selected);
        menu.addItem (9, "Join selected MIDI regions", selection.getItemsOfType<te::MidiClip>().size() >= 2
                      && selection.getItemsOfType<te::MidiClip>().size() == selection.getItemsOfType<te::Clip>().size()
                      && edit != nullptr && ! edit->getTransport().isPlaying());
        menu.addSeparator();
        menu.addItem (7, "Undo", edit != nullptr && edit->getUndoManager().canUndo());
        menu.addItem (8, "Redo", edit != nullptr && edit->getUndoManager().canRedo());
        Component::SafePointer<StudioView> safe (this);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (regionActions.isVisible() ? &regionActions : &trackMenu),
                            [safe] (int choice)
                            {
                                if (safe == nullptr || choice == 0 || ! safe->canEditRegions())
                                    return;
                                if (choice == 7)
                                    safe->edit->undo();
                                else if (choice == 8)
                                    safe->edit->redo();
                                else if (choice == 9)
                                    safe->joinSelectedMidi();
                                else
                                    safe->performRegionAction (choice);
                                safe->repaint (safe->rulerRow);
                            });
    }

    bool canEditRegions() const
    {
        return edit != nullptr && ! busy && studio::transportSettled (*edit) && ! recentlyStoppedRecording();
    }

    void timerCallback() override
    {
       #if JUCE_MAC
        updateSeatingVisibility();
       #endif
        monitorButton.setButtonText (monitor.isActive() ? "* MIDI Monitor" : "MIDI Monitor");
        monitorButton.setColour (TextButton::textColourOffId,
                                 monitor.isActive() ? Colour (0xff8edec0) : Colours::white);
        // Tracktion creates AU instances during project setup. The successful
        // manual retry proves that resubmitting the *on-disk* state after loading
        // can restore ARIA. Defer one automatic attempt until instances are
        // available and have had a short settling interval (not audio callback).
        maybeAutoRestoreAria();
        completePendingPlugin();
        updateWavExport();
        const bool ready = edit != nullptr && ! busy;
        const bool recording = edit != nullptr && edit->getTransport().isRecording();
        for (auto* button : { &newProject, &openProject, &audioSettings, &plugins })
            button->setEnabled (! busy && ! recording);
        regionActions.setEnabled (ready && ! recording);
        for (auto* button : { &saveProject, &saveAsProject, &addMidi,
                              &addAudio, &importAudio, &addMidiClip })
            button->setEnabled (ready && ! recording);
        addSynthTrack.setEnabled (canInsertPlugin());
        addSamplerTrack.setEnabled (canInsertPlugin());
        // ARIA review protects Save and the original AU state, not MIDI editing.
        // A loaded project must remain editable while its ARIA slots are checked.
        piano.setEditingEnabled (ready && ! recording);
        const bool playing = edit != nullptr && edit->getTransport().isPlaying();
        const bool transportReady = ready && studio::transportSettled (*edit) && ! recentlyStoppedRecording();
        music.refresh (transportReady);
        cycleButton.setEnabled (transportReady);
        clickButton.setEnabled (transportReady);
        tempo.setEnabled (transportReady && ! playing);
        meter.setEnabled (transportReady && ! playing);
        cycleButton.setToggleState (edit != nullptr && edit->getTransport().looping, dontSendNotification);
        clickButton.setToggleState (edit != nullptr && edit->clickTrackEnabled, dontSendNotification);
        importMidi.setEnabled (transportReady && ! playing);
        exportMidi.setEnabled (transportReady && ! playing);
        exportWav.setEnabled (transportReady && ! playing && ! ariaReviewPending);
        rewind.setEnabled (transportReady);
        play.setEnabled (transportReady || (ready && recording));
        play.setToggleState (playing && ! recording, dontSendNotification);
        stop.setEnabled (ready && (playing || recording));
        record.setEnabled (ready && (recording || (transportReady && ! edit->getTransport().looping)));
        record.setToggleState (recording, dontSendNotification);
        zoom.setEnabled (ready);
        arrangementZoomOut.setEnabled (ready);
        arrangementZoomIn.setEnabled (ready);
        retryAriaButton.setEnabled (ready && ! recording && ariaReviewPending);
        confirmAriaButton.setEnabled (ready && ! recording && ariaReviewPending && ! autoAriaRetryPending);
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            record.setTooltip (recording ? "Stop recording and keep the take"
                                         : transport.looping ? "Disable cycle playback to record a linear take"
                                         : "Start recording");
            const auto now = transport.getPosition();
            piano.setTransportPosition (now);
            const auto centiseconds = static_cast<int64> (juce::jmax (0.0, now.inSeconds()) * 100.0);
            const auto elapsed = String (centiseconds / 6000).paddedLeft ('0', 2) + ":"
                               + String (centiseconds / 100 % 60).paddedLeft ('0', 2) + "."
                               + String (centiseconds % 100).paddedLeft ('0', 2);
            position.setValue (elapsed);
            if (! position.isVisible()) bars.setTooltip ("Elapsed " + elapsed);
            const auto& sequence = edit->tempoSequence;
            const auto tempoHash = sequence.getInternalSequence().hash();
            if (displayedCycle != transport.getLoopRange() || displayedLooping != transport.looping.get()
                || displayedTempoHash != tempoHash)
            {
                displayedCycle = transport.getLoopRange();
                displayedLooping = transport.looping.get();
                displayedTempoHash = tempoHash;
                repaint (rulerRow);
                repaint (timeline.getBounds());
                piano.repaint();
            }
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
                if (workspace != Workspace::closed && workspace != Workspace::articulations && dockMode != DockMode::compact)
                    showWorkspace (Workspace::piano);
            }
            piano.setComparisonClips (selectedMidiClips);
        }
        else
            piano.setComparisonClips ({});

        auto* track = selectedTrack();
        StringArray names;
        int selectedIndex = -1;
        const auto tracks = edit != nullptr ? te::getAudioTracks (*edit) : juce::Array<te::AudioTrack*>();
        if (edit != nullptr)
            for (auto* item : tracks)
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
        trackDetails.setColour (Label::textColourId, recording ? studio::colours::record : studio::colours::secondary);
        trackDetails.setText (recording ? String ("Recording") : track == nullptr ? String (trackNames.size()) + " tracks"
            : EngineHelpers::isTrackArmed (*track) ? "Record armed"
            : EngineHelpers::trackHasInput (*track) ? "Input assigned" : "No input assigned", dontSendNotification);

        if ((++workspaceTicks & 1u) == 0)
        {
            instrumentWorkspace.updateSelection (workflowTrack(), tracks,
                workspace == Workspace::instrumentEditor || instrumentWorkspace.isFloating());
            refreshInstrumentWorkflow (tracks, selectedMidiClips.size() == 1 ? selectedMidiClips.getFirst() : nullptr);
            auto* context = workflowTrack();
            if (performance.isVisible()) performance.refresh (context, transportReady);
            if (articulation.isShowing() || articulationStrip.isVisible())
            {
                articulation.refresh (context, selectedMidiClips.size() == 1 ? selectedMidiClips.getFirst() : nullptr, ready);
                if (articulationStrip.isVisible()) articulationStrip.refresh();
            }
            if (spatialWorkspace.isShowing())
            {
                auto selectedTracks = selection.getItemsOfType<te::AudioTrack>();
                if (selectedTracks.isEmpty())
                    if (auto* context = workflowTrack()) selectedTracks.add (context);
                spatialWorkspace.refresh (tracks, selectedTracks, ready && ! recording,
                    ready && ! recording && ! ariaReviewPending && ! edit->getTransport().isPlaying());
            }
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
        else if (transport.looping)
            message.setText ("Disable cycle playback to record a linear take.", dontSendNotification);
        else if (canEditRegions())
        {
            const auto inputs = edit->getAllInputDevices();
            const bool replaces = std::any_of (inputs.begin(), inputs.end(), [] (const auto* input)
            {
                const auto* midi = dynamic_cast<const te::MidiInputDevice*> (&input->owner);
                return input->isRecordingActive() && midi != nullptr && midi->replaceExistingClips;
            });
            if (! replaces)
            {
                transport.record (false);
                return;
            }
            const SafePointer<StudioView> safe (this);
            const auto generation = projectGeneration;
            AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon, "Replace MIDI regions?",
                "An armed MIDI input uses Replace mode. Recording can remove existing regions under the take.",
                "Record in Replace mode", "Cancel", this,
                ModalCallbackFunction::create ([safe, generation] (int result)
                {
                    if (result == 1 && safe != nullptr && safe->projectGeneration == generation
                        && safe->canEditRegions() && ! safe->edit->getTransport().looping)
                        safe->edit->getTransport().record (false);
                }));
        }
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

    bool recentlyStoppedRecording() const
    {
        return lastRecordingStopRequestedAt != 0
            && Time::getMillisecondCounter() - lastRecordingStopRequestedAt < 400;
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
        if (! transport.isRecording() && ! transport.isStopping()
            && ! transport.isRecordingStopping() && ! recentlyStoppedRecording())
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
                                    rememberInstrumentWorkspace();
                                    instrumentWorkspace.relocateProject (newFile);
                                    rememberInstrumentWorkspace();
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

        pendingPlugin.reset();
        auto next = [&]
        {
            const ScopedValueSetter<bool> loading (busy, true);
            return create ? te::createEmptyEdit (engine, file) : te::loadEditFromFile (engine, file);
        }();
        if (next == nullptr)
            return;

        if (edit != nullptr)
            edit->getTransport().stop (false, false);
        piano.setClip (nullptr);
        regionClipboard.clear();
        rememberInstrumentWorkspace();
        instrumentWorkspace.setProject (file);
        dockMode = instrumentWorkspace.rememberedLayout();
        showWorkspace (Workspace::closed);
        workspaceScroll = {};
        selection.deselectAll();
        timeline.setViewedComponent (nullptr);
        editor.reset();
        browser.setStatus ({});
        channelInspector.setEdit (nullptr);
        mixer.setEdit (nullptr);
        resources.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
        articulation.setContext (nullptr);
        performance.clear();
       #if JUCE_MAC
        seatingWorkspace.setEdit (nullptr);
       #endif
        ++projectGeneration;
        cycleAnchor.reset();
        edit = std::move (next);
        music.setEdit (edit.get());
        studio::spatial::prepareRoomPlayback (*edit);
        channelInspector.setEdit (edit.get());
        resources.setEdit (edit.get());
        spatialWorkspace.setEdit (edit.get());
       #if JUCE_MAC
        seatingWorkspace.setEdit (edit.get());
       #endif
        articulation.setContext (edit.get());
        mixer.setEdit (edit.get());
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
        const auto lastInstrument = instrumentWorkspace.rememberedInstrument();
        if (! create && lastInstrument.isValid())
            if (auto savedPlugin = te::findPluginForID (*edit, lastInstrument); savedPlugin != nullptr)
                if (showNativeInstrument (*savedPlugin))
                {
                    const bool floating = instrumentWorkspace.rememberedFloating();
                    setDockMode (instrumentWorkspace.rememberedLayout());
                    if (floating) instrumentWorkspace.setFloating (true);
                }
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

    bool canTransferMidi() const
    {
        return canEditRegions() && ! edit->getTransport().isPlaying();
    }

    void chooseMidiImport()
    {
        if (! canTransferMidi()) return;
        const auto generation = projectGeneration;
        const auto destination = workflowTrack() != nullptr ? workflowTrack()->itemID : te::EditItemID();
        chooser = std::make_unique<FileChooser> ("Import Standard MIDI File", projectFile.getParentDirectory(), "*.mid;*.midi");
        const SafePointer<StudioView> safe (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe, generation, destination] (const FileChooser& dialog)
            {
                if (safe == nullptr || safe->projectGeneration != generation || ! safe->canTransferMidi()
                    || dialog.getResult() == File())
                    return;
                auto smf = std::make_shared<studio::midi::Smf>();
                if (auto result = studio::midi::read (dialog.getResult(), *smf); result.failed())
                {
                    safe->message.setText (result.getErrorMessage(), dontSendNotification);
                    return;
                }
                safe->showMidiImportOptions (std::move (smf), destination);
            });
    }

    void showMidiImportOptions (std::shared_ptr<const studio::midi::Smf> smf, te::EditItemID selected)
    {
        auto* window = new AlertWindow ("Import MIDI", "Choose where the MIDI parts and tempo map go.",
                                        MessageBoxIconType::NoIcon, this);
        StringArray destinations { "New tracks in this project" };
        if (auto* track = trackById (selected); track != nullptr && ! studio::spatial::isRoomReturn (*track))
            destinations.add ("Selected track: " + track->getName());
        window->addComboBox ("tracks", destinations, "Track placement");
        window->addComboBox ("tempo", { "Keep project tempo and meter (at playhead)",
                                          "Replace tempo and meter from file (at project start)" }, "Tempo map");
        window->addButton ("Import", 1, KeyPress (KeyPress::returnKey));
        window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        const SafePointer<StudioView> safe (this);
        const auto generation = projectGeneration;
        window->enterModalState (true, ModalCallbackFunction::create ([safe, generation, window, smf, selected] (int answer)
        {
            if (answer != 1 || safe == nullptr || safe->projectGeneration != generation || ! safe->canTransferMidi())
                return;
            const bool intoTrack = window->getComboBoxComponent ("tracks")->getSelectedId() == 2;
            auto* track = intoTrack ? safe->trackById (selected) : nullptr;
            if (intoTrack && track == nullptr)
            {
                safe->message.setText ("The selected destination is no longer available.", dontSendNotification);
                return;
            }
            const bool useFileTempo = window->getComboBoxComponent ("tempo")->getSelectedId() == 2;
            auto& project = *safe->edit;
            const auto result = studio::midi::import (project, *smf, project.getTransport().getPosition(), track, useFileTempo);
            safe->message.setText (result.wasOk() ? "Imported MIDI. Load instruments on any new tracks to hear them."
                                                 : result.getErrorMessage(), dontSendNotification);
            safe->repaint();
        }), true);
    }

    void chooseMidiExport()
    {
        if (! canTransferMidi()) return;
        auto clips = selection.getItemsOfType<te::MidiClip>();
        if (clips.isEmpty())
            for (auto* track : selection.getItemsOfType<te::AudioTrack>())
                for (auto* clip : track->getClips())
                    if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                        clips.add (midi);
        if (clips.isEmpty())
        {
            message.setText ("Select MIDI regions or tracks containing MIDI regions.", dontSendNotification);
            return;
        }
        Array<te::EditItemID> ids;
        for (const auto* clip : clips) ids.add (clip->itemID);
        const auto generation = projectGeneration;
        chooser = std::make_unique<FileChooser> ("Export selected MIDI", projectFile.withFileExtension (".mid"), "*.mid");
        const SafePointer<StudioView> safe (this);
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
            [safe, generation, ids] (const FileChooser& dialog)
            {
                if (safe == nullptr || safe->projectGeneration != generation || ! safe->canTransferMidi()
                    || dialog.getResult() == File()) return;
                Array<te::MidiClip*> sources;
                for (const auto id : ids)
                    if (auto* clip = dynamic_cast<te::MidiClip*> (te::findClipForID (*safe->edit, id)))
                        sources.add (clip);
                const auto result = studio::midi::write (*safe->edit, sources, dialog.getResult().withFileExtension (".mid"));
                safe->message.setText (result.wasOk() ? "Exported selected MIDI." : result.getErrorMessage(), dontSendNotification);
            });
    }

    void chooseWavExport()
    {
        if (! canTransferMidi() || ariaReviewPending) return;
        Array<te::EditItemID> ids;
        for (const auto* track : selection.getItemsOfType<te::AudioTrack>())
            ids.add (track->itemID);
        for (const auto* clip : selection.getItemsOfType<te::Clip>())
            if (const auto* track = clip->getClipTrack())
                ids.addIfNotAlreadyThere (track->itemID);
        const SafePointer<StudioView> safe (this);
        const auto generation = projectGeneration;
        studio::bounce::showOptions (*this, *edit, ids, [safe, generation] (Result result, studio::bounce::Options options)
        {
            if (safe == nullptr || generation != safe->projectGeneration || ! safe->canTransferMidi()
                || safe->ariaReviewPending)
                return;
            if (result.failed())
                safe->message.setText (result.getErrorMessage(), dontSendNotification);
            else
                safe->chooseWavDestination (std::move (options));
        });
    }

    void chooseWavDestination (studio::bounce::Options options)
    {
        const auto generation = projectGeneration;
        const SafePointer<StudioView> safe (this);
        chooser = std::make_unique<FileChooser> (options.stems ? "Choose a folder for WAV stems" : "Export WAV mix",
            options.stems ? projectFile.getParentDirectory() : projectFile.withFileExtension (".wav"),
            options.stems ? "*" : "*.wav");
        const auto flags = options.stems ? FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories
                                         : FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles;
        chooser->launchAsync (flags, [safe, generation, options = std::move (options)] (const FileChooser& dialog) mutable
        {
            if (safe == nullptr || safe->projectGeneration != generation || dialog.getResult() == File()
                || ! safe->canTransferMidi() || safe->ariaReviewPending)
                return;
            options.destination = options.stems ? dialog.getResult() : dialog.getResult().withFileExtension (".wav");
            safe->startWavExport (options);
        });
    }

    void startWavExport (const studio::bounce::Options& options)
    {
        piano.silencePreview();
        busy = true;
        if (auto result = studio::bounce::Job::create (*edit, options, bounce); result.failed())
        {
            busy = false;
            message.setText (result.getErrorMessage(), dontSendNotification);
            return;
        }
        bounceProgress = 0;
        auto* window = new AlertWindow ("Exporting WAV", "Rendering " + String (bounce->fileCount()) + " file(s).",
                                        MessageBoxIconType::NoIcon, this);
        bounceWindow = window;
        window->addProgressBarComponent (bounceProgress);
        window->addButton ("Cancel", 1, KeyPress (KeyPress::escapeKey));
        const SafePointer<StudioView> safe (this);
        window->enterModalState (true, ModalCallbackFunction::create ([safe] (int answer)
        {
            if (answer == 1 && safe != nullptr && safe->bounce != nullptr)
                safe->bounce->cancel();
        }), true);
        bounce->start();
    }

    void updateWavExport()
    {
        if (bounce == nullptr) return;
        busy = true;
        bounceProgress = bounce->progress();
        if (! bounce->isFinished()) return;
        const auto result = bounce->publish();
        message.setText (result.wasOk() ? "Exported " + String (bounce->fileCount()) + " WAV file(s)." : result.getErrorMessage(),
                         dontSendNotification);
        if (bounceWindow != nullptr)
            bounceWindow->exitModalState (0);
        bounce.reset();
        busy = false;
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

    te::AudioTrack* trackById (te::EditItemID id) const
    {
        return edit != nullptr && id.isValid() ? te::findAudioTrackForID (*edit, id) : nullptr;
    }

    te::AudioTrack* workflowTrack()
    {
        te::AudioTrack* target = nullptr;
        for (int i = 0; i < selection.getNumObjectsSelected(); ++i)
        {
            auto* item = selection.getSelectedObject (i);
            auto* track = dynamic_cast<te::AudioTrack*> (item);
            if (auto* clip = dynamic_cast<te::Clip*> (item))
                track = dynamic_cast<te::AudioTrack*> (clip->getClipTrack());
            else if (auto* plugin = dynamic_cast<te::Plugin*> (item))
                track = dynamic_cast<te::AudioTrack*> (plugin->getOwnerTrack());
            if (track == nullptr || (target != nullptr && target != track)) return nullptr;
            target = track;
        }
        return target;
    }

    void rememberInstrumentWorkspace()
    {
        instrumentWorkspace.saveSession (workspace == Workspace::instrumentEditor || instrumentWorkspace.isFloating(), dockMode);
    }

    bool showNativeInstrument (te::Plugin& plug)
    {
        if (edit == nullptr || &plug.edit != edit.get() || ! studio::isNativeInstrument (plug))
            return false;
        auto* track = dynamic_cast<te::AudioTrack*> (plug.getOwnerTrack());
        if (track == nullptr || ! instrumentWorkspace.open (plug, *track)) return false;
        if (workflowTrack() != track) selection.selectOnly (track);
        if (dockMode == DockMode::compact) setDockMode (DockMode::split);
        showWorkspace (Workspace::instrumentEditor);
        return true;
    }

    void configureInstrumentWorkflow()
    {
        browser.openPluginManager = [this] { if (! busy && (edit == nullptr || canEditRegions())) showPlugins(); };
        browser.loadInstrument = [this] (const PluginDescription& plugin, bool addNew)
        {
            auto* target = workflowTrack();
            startExternalPlugin (plugin, target != nullptr ? target->itemID : te::EditItemID {}, addNew, true);
        };
        browser.openSampler = [this]
        {
            if (! busy)
                if (auto* track = workflowTrack())
                    if (auto* sampler = dynamic_cast<studio::sampler::Plugin*> (studio::findInstrument (*track)))
                        showNativeInstrument (*sampler);
        };
        browser.openSynth = [this]
        {
            if (! busy)
                if (auto* track = workflowTrack())
                    if (auto* synth = dynamic_cast<studio::synth::Plugin*> (studio::findInstrument (*track)))
                        showNativeInstrument (*synth);
        };
        browser.loadSampler = [this] (bool addNew)
        {
            if (! canInsertPlugin()) return;
            auto* target = workflowTrack();
            if (! addNew)
                if (auto reason = instrumentTrackRestriction (target); reason.isNotEmpty())
                {
                    pluginStatus (reason);
                    return;
                }
            auto plugin = edit->getPluginCache().createNewPlugin (studio::sampler::Plugin::xmlTypeName, {});
            commitPlugin (plugin, target != nullptr ? target->itemID : te::EditItemID {}, addNew, true);
        };
        browser.loadSynth = [this] (String family, bool addNew)
        {
            if (! canInsertPlugin()) return;
            auto* target = workflowTrack();
            if (! addNew)
                if (const auto reason = instrumentTrackRestriction (target); reason.isNotEmpty())
                {
                    pluginStatus (reason);
                    return;
                }
            auto plugin = edit->getPluginCache().createNewPlugin (studio::synth::Plugin::xmlTypeName, {});
            auto* synth = dynamic_cast<studio::synth::Plugin*> (plugin.get());
            if (synth == nullptr)
            {
                pluginStatus ("Could not create Studio Synth.");
                return;
            }
            const auto wanted = studio::synth::parseFamily (family);
            const auto& catalog = studio::synth::catalog().sounds;
            const auto first = std::find_if (catalog.begin(), catalog.end(), [wanted] (const auto& sound) { return sound.family == wanted; });
            if (first == catalog.end())
            {
                pluginStatus ("No Studio Synth presets were found for that family.");
                return;
            }
            if (auto result = synth->selectFactory (*first); result.failed())
            {
                pluginStatus (result.getErrorMessage());
                return;
            }
            commitPlugin (plugin, target != nullptr ? target->itemID : te::EditItemID {}, addNew, true);
        };
        trackActions.canChangeMix = [this] { return edit != nullptr && ! busy; };
        trackActions.selectTrack = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id))
                selection.selectOnly (track);
        };
        trackActions.showMenu = [this] (te::EditItemID id, studio::TrackMenu kind, Component& anchor)
        {
            showTrackMenu (id, kind, anchor);
        };
        trackActions.openInstrument = [this] (te::EditItemID id)
        {
            if (! busy)
                if (auto* track = trackById (id))
                    if (auto* instrument = studio::findInstrument (*track); instrument != nullptr && ! showNativeInstrument (*instrument))
                        instrument->showWindowExplicitly();
        };
        trackActions.toggleTrackState = [this] (te::EditItemID id, bool solo)
        {
            if (! busy)
                if (auto* track = trackById (id))
                {
                    if (solo) track->setSolo (! track->isSolo (false));
                    else track->setMute (! track->isMuted (false));
                }
        };
        trackActions.toggleArm = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id); track != nullptr && canEditRegions() && ! track->isFrozen (te::Track::anyFreeze))
            {
                auto inputs = edit->getAllInputDevices();
                const auto assigned = [id] (te::InputDeviceInstance* i) { return i->getTargets().contains (id); };
                const bool armed = std::any_of (inputs.begin(), inputs.end(), [&] (auto* i)
                {
                    return assigned (i) && i->isRecordingEnabled (id);
                });
                for (auto* input : inputs)
                    if (assigned (input))
                        input->setRecordingEnabled (id, ! armed);
            }
        };
        trackActions.showStage = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id))
            {
                selection.selectOnly (track);
                if (dockMode == DockMode::compact) dockMode = DockMode::split;
                showWorkspace (Workspace::stage);
            }
        };
        trackActions.toggleMonitoring = [this] (te::EditItemID id)
        {
            if (auto* track = trackById (id); track != nullptr && canEditRegions() && ! track->isFrozen (te::Track::anyFreeze))
            {
                const auto inputs = edit->getAllInputDevices();
                using Mode = te::InputDevice::MonitorMode;
                const bool enabled = std::any_of (inputs.begin(), inputs.end(), [id] (auto* i)
                {
                    return i->getTargets().contains (id) && i->getInputDevice().getMonitorMode() == Mode::on;
                });
                for (auto* input : inputs)
                    if (input->getTargets().contains (id))
                        if (auto& device = input->getInputDevice(); device.getMonitorMode() != Mode::automatic)
                            device.setMonitorMode (enabled ? Mode::off : Mode::on);
            }
        };
    }

    void refreshInstrumentWorkflow (const Array<te::AudioTrack*>& tracks, const te::MidiClip* selectedClip)
    {
        if (dockMode == DockMode::compact || (workspace != Workspace::instruments
             && workspace != Workspace::channel && workspace != Workspace::mix))
            return;

        auto* target = workflowTrack();
        if (workspace == Workspace::instruments)
        {
            const auto restriction = edit == nullptr ? "Open or create a project to load an instrument."
                : ariaReviewPending ? "Complete ARIA verification before adding plug-ins."
                : pendingPlugin ? "An Audio Unit is loading."
                : canEditRegions() ? instrumentTrackRestriction (target) : "Finish the current task or stop recording.";
            const bool hasSampler = target != nullptr && ! busy
                && dynamic_cast<studio::sampler::Plugin*> (studio::findInstrument (*target)) != nullptr;
            const bool hasSynth = target != nullptr && ! busy
                && dynamic_cast<studio::synth::Plugin*> (studio::findInstrument (*target)) != nullptr;
            browser.setLoadContext (canInsertPlugin(), target != nullptr ? target->getName() : String(), restriction, hasSampler, hasSynth);
        }
        else if (workspace == Workspace::channel)
        {
            const auto recovery = ariaReviewPending
                ? (autoAriaRetryPending ? "ARIA recovery | Automatic restore pending"
                                        : "ARIA recovery | Verify sound, then Confirm ARIA below transport")
                : "ARIA recovery | No verification pending";
            channelInspector.refresh (target, edit != nullptr && ! busy, canEditRegions(), recovery,
                selectedClip != nullptr && selectedClip->getAudioTrack() == target ? selectedClip : nullptr);
        }
        else
            mixer.refresh (tracks, target, edit != nullptr && ! busy, canEditRegions());
    }

    bool canInsertPlugin() const
    {
        return canEditRegions() && ! ariaReviewPending && ! pendingPlugin.has_value();
    }

    String instrumentTrackRestriction (te::AudioTrack* track) const
    {
        if (track == nullptr) return "Select an instrument track or use Add as New Track.";
        if (track->isFrozen (te::Track::anyFreeze)) return "The selected track is frozen.";
        if (studio::findInstrument (*track) != nullptr) return "This track already hosts an instrument. Use Add as New Track.";
        for (auto* clip : track->getClips())
            if (dynamic_cast<te::AudioClipBase*> (clip) != nullptr)
                return "The selected track contains audio. Use Add as New Track.";
        for (auto* input : track->edit.getAllInputDevices())
            if (input->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
                && input->getTargets().contains (track->itemID))
                return "The selected track has an audio input. Use Add as New Track.";
        return {};
    }

    void pluginStatus (const String& status)
    {
        browser.setStatus (status);
        message.setText (status, dontSendNotification);
    }

    void startExternalPlugin (PluginDescription description, te::EditItemID target, bool addNew, bool instrument)
    {
        if (! canInsertPlugin())
        {
            pluginStatus (ariaReviewPending ? "Complete ARIA verification before adding plug-ins."
                                            : "Stop recording or wait for the current plug-in to finish.");
            return;
        }
        if (! addNew)
        {
            auto* track = trackById (target);
            const auto restriction = instrument ? instrumentTrackRestriction (track)
                : (track == nullptr || track->isFrozen (te::Track::anyFreeze) ? "Choose an unfrozen track." : "");
            if (restriction.isNotEmpty())
            {
                pluginStatus (restriction);
                return;
            }
        }
        if (description.pluginFormatName != "AudioUnit"
            || engine.getPluginManager().knownPluginList.getTypeForIdentifierString (description.createIdentifierString()) == nullptr)
        {
            pluginStatus ("The Audio Unit is no longer in the host catalog. Scan for plug-ins again.");
            return;
        }

        pluginStatus ("Loading " + description.name + "...");
        {
            const ScopedValueSetter<bool> loading (busy, true);
            auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
            if (plugin == nullptr)
            {
                pluginStatus ("Could not create " + description.name + ".");
                return;
            }
            pendingPlugin = PendingPlugin { plugin, target, addNew, instrument, Time::getMillisecondCounter() };
            plugin->initialiseFully();
        }
        completePendingPlugin();
    }

    void completePendingPlugin()
    {
        if (! pendingPlugin) return;
        if (edit == nullptr || edit->getTransport().isRecording() || ariaReviewPending)
        {
            pendingPlugin.reset();
            pluginStatus ("Plug-in load cancelled before changing the project.");
            return;
        }
        if (busy) return;
        auto& plugin = static_cast<te::ExternalPlugin&> (*pendingPlugin->plugin);
        if (plugin.isInitialisingAsync())
        {
            if (Time::getMillisecondCounter() - pendingPlugin->startedAt < 15000u) return;
            pendingPlugin.reset();
            pluginStatus ("The Audio Unit did not finish loading. The project was not changed.");
            return;
        }
        auto requested = std::move (*pendingPlugin);
        pendingPlugin.reset();
        if (plugin.getAudioPluginInstance() == nullptr)
        {
            pluginStatus (plugin.getName() + " could not be loaded. " + plugin.getLoadError());
            return;
        }
        commitPlugin (requested.plugin, requested.destination, requested.addTrack, requested.instrument);
    }

    void commitPlugin (const te::Plugin::Ptr& plugin, te::EditItemID target, bool addNew, bool instrument)
    {
        if (! canInsertPlugin() || plugin == nullptr) return;
        auto* track = trackById (target);
        if (! addNew && (track == nullptr || track->isFrozen (te::Track::anyFreeze)
                        || (instrument && instrumentTrackRestriction (track).isNotEmpty())))
        {
            pluginStatus ("The original destination is no longer available for this plug-in.");
            return;
        }
        auto& history = edit->getUndoManager();
        history.beginNewTransaction (addNew ? "Add instrument track" : instrument ? "Load instrument" : "Add insert effect");
        te::AudioTrack::Ptr newTrack;
        if (addNew)
        {
            newTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
            if (newTrack == nullptr)
            {
                pluginStatus ("Could not create a new instrument track.");
                return;
            }
            track = newTrack.get();
            track->setName (plugin->getName());
        }

        const auto* volume = track->getVolumePlugin();
        // The instrument comes first. Effects go before the existing track fader.
        const int index = instrument ? 0 : volume != nullptr ? track->pluginList.indexOf (volume) : -1;
        track->pluginList.insertPlugin (plugin, index, nullptr);
        if (track->pluginList.indexOf (plugin.get()) < 0)
        {
            if (newTrack != nullptr) edit->deleteTrack (newTrack.get());
            pluginStatus ("Could not insert " + plugin->getName() + ".");
            return;
        }
        if (newTrack != nullptr) selection.selectOnly (track);
        auto status = "Loaded " + plugin->getName() + " on " + track->getName() + ".";
        if (instrument && plugin->getName().containsIgnoreCase ("ARIA Player"))
            status << " Choose the patch in Channel > Open Instrument.";
        pluginStatus (status);
        if (instrument) showNativeInstrument (*plugin);
    }

    // Menu callbacks re-resolve the original track and project after the popup closes.
    std::function<void()> trackMenuAction (te::EditItemID id, std::function<void(StudioView&, te::AudioTrack&)> action)
    {
        return [safe = Component::SafePointer<StudioView> (this), id, generation = projectGeneration,
                action = std::move (action)]
        {
            if (safe == nullptr || generation != safe->projectGeneration || ! safe->canEditRegions()) return;
            if (auto* track = safe->trackById (id); track != nullptr && ! track->isFrozen (te::Track::anyFreeze))
                action (*safe, *track);
        };
    }

    void showTrackMenu (te::EditItemID id, studio::TrackMenu kind, Component& anchor)
    {
        auto* track = trackById (id);
        if (track == nullptr || ! canEditRegions() || track->isFrozen (te::Track::anyFreeze)) return;
        PopupMenu menu;
        if (kind == studio::TrackMenu::inserts)
            populateInsertMenu (menu, *track);
        else if (kind == studio::TrackMenu::output)
            populateOutputMenu (menu, *track);
        else
            populateInputMenu (menu, *track, kind == studio::TrackMenu::midiInput);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
    }

    void populateInputMenu (PopupMenu& menu, te::AudioTrack& track, bool midi)
    {
        const auto type = midi ? te::InputDevice::physicalMidiDevice : te::InputDevice::waveDevice;
        const auto id = track.itemID;
        menu.addSectionHeader (midi ? "Physical MIDI inputs" : "Audio inputs");
        bool found = false;
        for (auto* instance : edit->getAllInputDevices())
        {
            auto& device = instance->getInputDevice();
            if (device.getDeviceType() != type || ! device.isEnabled()) continue;
            found = true;
            const auto deviceId = device.getDeviceID();
            const auto name = device.getName();
            const auto targets = instance->getTargets();
            const bool assigned = targets.contains (id);
            const auto label = assigned ? "Disconnect " + name
                : ! targets.isEmpty() ? "Move " + name + " to this track" : "Assign " + name;
            menu.addItem (label, true, assigned, trackMenuAction (id,
                [deviceId, type, assigned, midi] (StudioView& studio, te::AudioTrack& target)
                {
                    for (auto* in : target.edit.getAllInputDevices())
                    {
                        auto& current = in->getInputDevice();
                        if (current.getDeviceType() != type || current.getDeviceID() != deviceId || ! current.isEnabled())
                            continue;
                        if (in->getTargets().contains (target.itemID) != assigned) return;
                        auto& undo = target.edit.getUndoManager();
                        undo.beginNewTransaction (assigned ? "Disconnect track input" : "Assign track input");
                        if (assigned)
                        {
                            const auto result = in->removeTarget (target.itemID, &undo);
                            if (result.failed()) studio.pluginStatus (result.getErrorMessage());
                        }
                        else if (auto result = in->setTarget (target.itemID, true, &undo, 0))
                        {
                            if (midi) in->setRecordingEnabled (target.itemID, true);
                        }
                        else
                            studio.pluginStatus (result.error());
                        return;
                    }
                }));
            if (assigned && midi)
                if (auto* midiDevice = dynamic_cast<te::MidiInputDevice*> (&device))
                {
                    PopupMenu channels;
                    for (int channel = 1; channel <= 16; ++channel)
                        channels.addItem ("Channel " + String (channel), true, midiDevice->isChannelAllowed (channel),
                            trackMenuAction (id, [deviceId, channel] (StudioView&, te::AudioTrack& target)
                            {
                                for (auto* in : target.edit.getAllInputDevices())
                                    if (in->getTargets().contains (target.itemID)
                                        && in->getInputDevice().getDeviceID() == deviceId)
                                        if (auto* device = dynamic_cast<te::MidiInputDevice*> (&in->getInputDevice()))
                                        {
                                            device->setChannelAllowed (channel, ! device->isChannelAllowed (channel));
                                            return;
                                        }
                            }));
                    menu.addSubMenu (name + " MIDI channels (device-wide)", channels);
                }
        }
        if (! found) menu.addItem (1, "No enabled devices", false);
    }

    void populateOutputMenu (PopupMenu& menu, te::AudioTrack& track)
    {
        const auto id = track.itemID;
        auto& output = track.getOutput();
        menu.addSectionHeader ("Current output | " + studio::outputLabel (track));
        menu.addItem ("Default Audio (Master)", true, output.usesDefaultAudioOut(),
            trackMenuAction (id, [] (StudioView&, te::AudioTrack& target)
            {
                target.edit.getUndoManager().beginNewTransaction ("Change track output");
                target.getOutput().setOutputToDefaultDevice (false);
            }));
        auto& devices = engine.getDeviceManager();
        if (auto* midi = devices.getDefaultMidiOutDevice(); midi != nullptr && midi->isEnabled())
            menu.addItem ("Default MIDI", true, output.usesDefaultMIDIOut(),
                trackMenuAction (id, [] (StudioView& studio, te::AudioTrack& target)
                {
                    auto* device = studio.engine.getDeviceManager().getDefaultMidiOutDevice();
                    if (device == nullptr || ! device->isEnabled()) return;
                    target.edit.getUndoManager().beginNewTransaction ("Change track output");
                    target.getOutput().setOutputToDefaultDevice (true);
                }));
        for (int i = 0; i < devices.getNumOutputDevices(); ++i)
            if (auto* device = devices.getOutputDeviceAt (i); device != nullptr && device->isEnabled())
            {
                const auto deviceId = device->getDeviceID();
                menu.addItem (device->getName(), true, output.getOutputDeviceID() == deviceId,
                    trackMenuAction (id, [deviceId] (StudioView& studio, te::AudioTrack& target)
                    {
                        auto* current = studio.engine.getDeviceManager().findOutputDeviceForID (deviceId);
                        if (current == nullptr || ! current->isEnabled()) return;
                        target.edit.getUndoManager().beginNewTransaction ("Change track output");
                        target.getOutput().setOutputToDeviceID (deviceId);
                    }));
            }
    }

    void populateInsertMenu (PopupMenu& menu, te::AudioTrack& track)
    {
        const auto id = track.itemID;
        menu.addSectionHeader ("Insert chain | " + track.getName());
        bool any = false;
        for (auto* plugin : track.pluginList.getPlugins())
        {
            if (plugin == track.getVolumePlugin() || plugin == track.getLevelMeterPlugin()) continue;
            any = true;
            const auto pluginId = plugin->itemID;
            menu.addItem ("Open " + plugin->getName(), trackMenuAction (id,
                [pluginId] (StudioView& studio, te::AudioTrack& target)
                {
                    for (auto* current : target.pluginList.getPlugins())
                        if (current->itemID == pluginId)
                        {
                            if (! studio.showNativeInstrument (*current)) current->showWindowExplicitly();
                            break;
                        }
                }));
        }
        if (! any) menu.addItem (10, "No instruments or effects", false);
        PopupMenu effects;
        const std::pair<const char*, const char*> builtIns[] {
            { "Equaliser", te::EqualiserPlugin::xmlTypeName },
            { "Compressor", te::CompressorPlugin::xmlTypeName },
            { "Delay", te::DelayPlugin::xmlTypeName }
        };
        for (const auto& [label, type] : builtIns)
            effects.addItem (label, trackMenuAction (id, [type] (StudioView& studio, te::AudioTrack& target)
            {
                if (! studio.canInsertPlugin()) return;
                studio.commitPlugin (target.edit.getPluginCache().createNewPlugin (type, {}), target.itemID, false, false);
            }));
        for (const auto& description : engine.getPluginManager().knownPluginList.getTypes())
            if (description.pluginFormatName == "AudioUnit" && ! description.isInstrument
                && ! description.name.containsIgnoreCase ("ARIA Player"))
                effects.addItem (description.name, trackMenuAction (id,
                    [description] (StudioView& studio, te::AudioTrack& target)
                    {
                        studio.startExternalPlugin (description, target.itemID, false, false);
                    }));
        menu.addSubMenu ("Add Effect", effects, canInsertPlugin());
    }

    void showAudioSettings()
    {
        DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = studio::colours::canvas;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.content.setOwned (new AudioSettingsView (engine.getDeviceManager().deviceManager));
        options.content->setSize (jmin (getWidth() - 32, studio::appearance.span (420)),
                                  jmin (getHeight() - 60, studio::appearance.px (580)));
        options.launchAsync();
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
        auto scanner = std::make_unique<PluginListComponent> (manager.pluginFormatManager,
            manager.knownPluginList, engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
            std::addressof (engine.getPropertyStorage().getPropertiesFile()), true);
        scanner->getTableListBox().setRowHeight (studio::appearance.line (14, 7));
        scanner->getTableListBox().setHeaderHeight (studio::appearance.control (24, 12));
        scanner->getTableListBox().getViewport()->setScrollBarThickness (studio::appearance.scrollbar());
        options.content.setOwned (scanner.release());
        options.content->setSize (jmin (getWidth() - 32, studio::appearance.px (760)),
                                  jmin (getHeight() - 60, studio::appearance.px (480)));
        options.launchAsync();
    }
};

#if STUDIO_SPATIAL_TESTS
static bool hasArrangementRegion (const Component& component)
{
    if (dynamic_cast<const ClipComponent*> (&component) != nullptr) return true;
    for (const auto* child : component.getChildren())
        if (hasArrangementRegion (*child)) return true;
    return false;
}

#include "tests/AdaptiveWorkspaceReview.inc"

int StudioView::runUnifiedStageReview (const File& output)
{
    using namespace studio::spatial;
    stopTimer();
    environmentWelcome.dismiss();
    applyAppearance (100, 100, false);
    setSize (1280, 800);
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Unified Stage review failed: %s\n", reason);
        return 1;
    };
    const auto folder = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Studio Stage Review", {}, false);
    if (folder.createDirectory().failed()) return fail ("create fixture directory");
    const ScopeGuard removeFixtures { [folder] { folder.deleteRecursively(); } };
    const auto fixture = folder.getChildFile ("Chamber Session.tracktionedit");
    loadProject (fixture, true);
    if (! edit || ! browser.loadSynth || ! browser.loadSampler) return fail ("prepare instrument project");
    browser.loadSynth ("analog", true);
    auto* synth = dynamic_cast<studio::synth::Plugin*> (instrumentWorkspace.getPlugin());
    auto* legacy = synth != nullptr ? dynamic_cast<te::AudioTrack*> (synth->getOwnerTrack()) : nullptr;
    browser.loadSampler (true);
    auto* sampler = dynamic_cast<studio::sampler::Plugin*> (instrumentWorkspace.getPlugin());
    auto* unplaced = sampler != nullptr ? dynamic_cast<te::AudioTrack*> (sampler->getOwnerTrack()) : nullptr;
    if (! legacy || ! unplaced || legacy == unplaced) return fail ("create distinct Synth and Sampler tracks");
    legacy->setName ("Flute");
    unplaced->setName ("Flute");
    const auto legacyID = legacy->itemID, unplacedID = unplaced->itemID;
    const auto archived = String (R"({"version":1,"name":"Legacy Reference","seats":[]})");
    if (! studio::seating::write (*edit, archived, "Save reference")) return fail ("prepare reference data");
    const Seat savedSeat { { -0.639f, 0.389f }, Section::woodwinds, -18 };
    legacy->state.addChild (writeSeat (legacyID.toString(), savedSeat), -1, nullptr);
    legacy->getOutput().setOutputToDefaultDevice (false);
    if (setRoomSend (*legacy, true, savedSeat.position, savedSeat.sendTrim, factoryRooms[0].room).failed())
        return fail ("prepare legacy room send");
    const auto saveToDisk = [&] { return te::EditFileOperations (*edit).writeToFile (fixture, false); };
    if (! saveToDisk()) return fail ("save legacy fixture");
    loadProject (fixture, false);
    legacy = trackById (legacyID);
    unplaced = trackById (unplacedID);
    const auto read = [] (te::AudioTrack& track) { return readSeat (track.state.getChildWithName (seatType)); };
    if (! legacy || ! unplaced || read (*legacy) != savedSeat || ! hasRoomSend (*legacy)
        || unplaced->state.getChildWithName (seatType).isValid() || studio::seating::read (edit->state) != archived)
        return fail ("reopen legacy placements, routing and reference without automatic conversion");

    setDockMode (DockMode::expanded);
    showWorkspace (Workspace::stage);
    const auto refresh = [&] (te::AudioTrack* selection)
    {
        spatialWorkspace.refresh (te::getAudioTracks (*edit), selection != nullptr ? Array<te::AudioTrack*> { selection } : Array<te::AudioTrack*>(), true, true);
    };
    refresh (legacy);
    if (! spatialWorkspace.verifyMoveForInterfaceReview (*legacy)) return fail ("drag updates pan and existing send, including Undo and Redo");
    if (! spatialWorkspace.verifyInspectorForInterfaceReview (*legacy)) return fail ("trim updates the send without rounding the saved pan or depth");
    const auto moved = read (*legacy);
    if (! spatialWorkspace.verifyDropForInterfaceReview (*unplaced, { 0.48f, 0.50f }) || read (*legacy) != moved)
        return fail ("roster drop places the selected duplicate-named track without enabling audio");
    const auto placed = read (*unplaced);
    if (! edit->getUndoManager().undo() || unplaced->state.getChildWithName (seatType).isValid()
        || ! edit->getUndoManager().redo() || read (*unplaced) != placed)
        return fail ("undo and redo initial placement");
    if (selectedTrack() != unplaced) return fail ("Stage selection reaches the arrangement");
    unplaced->setName ("Clarinet");
    if (auto clip = legacy->insertMIDIClip ("Opening phrase", { {}, te::TimePosition::fromSeconds (4) }, nullptr))
        clip->getSequence().addNote (69, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 90, 0, nullptr);

    // Add other orchestral families through the same roster drop path for review.
    for (const auto& [name, position] : { std::pair { "Violin I", Position { -0.73f, 0.14f } },
                                        std::pair { "Violin II", Position { -0.32f, 0.15f } },
                                        std::pair { "Cellos", Position { 0.51f, 0.15f } },
                                        std::pair { "Horns", Position { -0.34f, 0.66f } },
                                        std::pair { "Timpani", Position { 0.13f, 0.83f } } })
    {
        auto* track = addTrack ("Instrument");
        if (! track) return fail ("create review track");
        track->setName (name);
        refresh (track);
        if (! spatialWorkspace.verifyDropForInterfaceReview (*track, position)) return fail ("place review track");
    }
    auto* waiting = addTrack ("Instrument");
    if (! waiting) return fail ("create an unplaced review track");
    waiting->setName ("Concert Piano");
    selection.selectOnly (legacy);
    edit->dispatchPendingUpdatesSynchronously();
    for (int i = 0; i < 12 && editor->getVisibleTrackCount() < te::getAudioTracks (*edit).size(); ++i)
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    timerCallback();
    refresh (legacy);
    if (output != File() && output.createDirectory().failed()) return fail ("create screenshot directory");
    const auto capture = [&] (const char* name)
    {
        if (output == File()) return true;
        FileOutputStream file (output.getChildFile (name));
        return file.openedOk() && file.setPosition (0) && file.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds()), file);
    };
    for (const auto& [mode, filename] : { std::pair { DockMode::compact, "stage-compact.png" },
                                         std::pair { DockMode::split, "stage-split.png" },
                                         std::pair { DockMode::expanded, "stage-expanded.png" } })
    {
        workspaceHeight = 410;
        setDockMode (mode);
        if (! spatialWorkspace.isVisible() || ! workspaceViewport.isVisible() || ! workspaceDivider.isVisible() || ! capture (filename))
            return fail ("present the same acoustic Stage in all three workspace modes");
    }

    if (! saveToDisk()) return fail ("save updated project");
    loadProject (fixture, false);
    legacy = trackById (legacyID);
    unplaced = trackById (unplacedID);
    if (! legacy || ! unplaced || read (*legacy) != moved || read (*unplaced) != placed || ! hasRoomSend (*legacy)
        || hasRoomSend (*unplaced) || studio::seating::read (edit->state) != archived || roomReturnTrack (*edit) == nullptr)
        return fail ("restore positions, acoustic send and the unmodified reference");
    showWorkspace (Workspace::stage);
    refresh (unplaced);
    selection.deselectAll();
    edit->deleteTrack (unplaced);
    refresh (legacy);
    if (! spatialWorkspace.verifyDeletedTrackForInterfaceReview (unplacedID) || read (*legacy) != moved)
        return fail ("track deletion removes its marker and rejects stale drops");
    std::puts ("Unified Stage placement, routing, Undo/Redo, deletion, and project compatibility passed.");
    return 0;
}

int StudioView::runSeatingProjectReview (const File& output)
{
    stopTimer();
    environmentWelcome.dismiss();
    applyAppearance (100, 100, false);
    setSize (1360, 800);
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Orchestral seating review failed: %s\n", reason);
        return 1;
    };
    TemporaryFile fixture (".tracktionedit");
    loadProject (fixture.getFile(), true);
    if (! edit || ! studio::seating::read (edit->state).isEmpty()) return fail ("new project starts with no seating layout");
    auto tracks = te::getAudioTracks (*edit);
    if (tracks.isEmpty()) return fail ("create an audio track for the isolation test");
    const auto originalTrack = tracks.getFirst()->state.createCopy();
    auto& undo = edit->getUndoManager();
    if (studio::seating::write (*edit, "{\"version\":2,\"seats\":[]}", "Invalid layout"))
        return fail ("reject a future layout version on write");

   #if JUCE_MAC
    showWorkspace (Workspace::stage);
    if (! spatialWorkspace.onOpenSeating) return fail ("Stage opens the Seating Designer");
    spatialWorkspace.onOpenSeating();
    if (! seatingWorkspace.isVisible() || ! workspaceDivider.isVisible()) return fail ("seat editor uses the Studio workspace");
    if (output != File() && output.createDirectory().failed()) return fail ("make screenshot directory");
    const auto screenshot = [&] (const char* name) { return output == File() ? File() : output.getChildFile (name); };
    if (! seatingWorkspace.exercise (0, screenshot ("modern-symphony.png")) || ! seatingWorkspace.exercise (1, {}))
        return fail ("SwiftUI model uses project callbacks for preset, insertion, section and coordinate edits");
   #else
    ignoreUnused (output);
    if (! studio::seating::write (*edit, "{\"version\":1,\"name\":\"Orchestra & Choir\",\"seats\":[]}", "Change seating"))
        return fail ("commit seating data to the Tracktion edit");
   #endif

    const auto stored = studio::seating::read (edit->state);
    if (stored.isEmpty() || ! edit->hasChangedSinceSaved()) return fail ("seating changes mark the project dirty");
    if (! tracks.getFirst()->state.isEquivalentTo (originalTrack)) return fail ("seating leaves track and routing properties intact");
    if (! undo.undo() || studio::seating::read (edit->state) == stored || ! undo.redo() || studio::seating::read (edit->state) != stored)
        return fail ("seating actions participate in project undo and redo");
    const auto generation = projectGeneration;
    if (! te::EditFileOperations (*edit).writeToFile (fixture.getFile(), false)) return fail ("save the seating project");
    loadProject (fixture.getFile(), false);
    if (projectGeneration != generation + 1 || studio::seating::read (edit->state) != stored)
        return fail ("reopen the saved layout through Tracktion XML persistence");
   #if JUCE_MAC
    spatialWorkspace.onOpenSeating();
    if (! seatingWorkspace.exercise (2, screenshot ("restored-seating.png")))
        return fail ("SwiftUI restores the placement, section and coordinates");
    showWorkspace (Workspace::stage);
    if (seatingWorkspace.isVisible()) return fail ("leaving Seating hides the native view");
   #endif
    std::puts ("Orchestral seating project, routing isolation, undo and restoration passed.");
    return 0;
}

int StudioView::runInstrumentWorkspaceReview (const File& output)
{
    stopTimer();
    environmentWelcome.dismiss();
    applyAppearance (100, 100, false);
    setSize (1280, 800);
    const auto fail = [] (const char* why)
    {
        std::fprintf (stderr, "Instrument workspace review failed: %s\n", why);
        return 1;
    };
    const auto folder = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Studio instruments review", {}, false);
    if (folder.createDirectory().failed()) return fail ("create fixture directory");
    const ScopeGuard removeFixtures { [folder] { folder.deleteRecursively(); } };
    auto& devices = engine.getDeviceManager();
    devices.setCpuLimitBeforeMuting (1000);
    auto& hosted = devices.getHostedAudioDeviceInterface();
    hosted.initialise ({ .sampleRate = 48000, .blockSize = 256, .inputChannels = 0, .outputChannels = 2 });
    hosted.prepareToPlay (48000, 256);
    devices.dispatchPendingUpdates();
    const ScopeGuard resetDevice { [&]
    {
        instrumentWorkspace.clear();
        piano.setClip (nullptr);
        selection.deselectAll();
        timeline.setViewedComponent (nullptr);
        editor.reset();
        mixer.setEdit (nullptr);
        resources.setEdit (nullptr);
        channelInspector.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
        articulation.setContext (nullptr);
        edit.reset();
        devices.deviceManager.closeAudioDevice();
        devices.removeHostedAudioDeviceInterface();
    } };
    const auto session = folder.getChildFile ("Instrument Session.tracktionedit");
    loadProject (session, true);
    if (! edit || ! browser.loadSynth || ! browser.loadSampler) return fail ("prepare instrument project");
    browser.loadSynth ("analog", true);
    auto* synth = dynamic_cast<studio::synth::Plugin*> (instrumentWorkspace.getPlugin());
    if (workspace != Workspace::instrumentEditor || synth == nullptr || instrumentWorkspace.isFloating()
        || synth->windowState->pluginWindow != nullptr)
        return fail ("Synth must open docked through the normal instrument creation workflow");

    auto snapshot = [&] (Component& component, const String& name)
    {
        if (output == File()) return true;
        if (&component == this)
        {
            edit->dispatchPendingUpdatesSynchronously();
            for (int i = 0; editor != nullptr && i < 12 && editor->getVisibleTrackCount() < te::getAudioTracks (*edit).size(); ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (50);
            timerCallback();
        }
        if (output.createDirectory().failed()) return false;
        const auto originalWidth = component.getWidth(), originalHeight = component.getHeight();
        if (name.startsWith ("sampler")) studio::sampler::captureNativeScreen (component, output.getChildFile ("native-" + name + ".png"));
        if (&component == this && (component.getWidth() != originalWidth || component.getHeight() != originalHeight))
            component.setSize (originalWidth, originalHeight);
        FileOutputStream file (output.getChildFile (name + ".png"));
        return file.openedOk() && file.setPosition (0) && file.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds()), file);
    };
    auto* synthEditor = instrumentWorkspace.getEditor();
    setDockMode (DockMode::expanded);
    if (! snapshot (*this, "synth-docked")) return fail ("capture Synth Sound");
    if (! studio::synth::verifyPresetBrowser (*synthEditor, folder.getChildFile ("Workspace User Test.studiosynth")))
        return fail ("factory filters, preset selection, unsaved changes, user preset and Advanced editing");
    synthEditor->restoreView ("0,1");
    if (! snapshot (*this, "synth-preset-browser")) return fail ("capture preset browser");
    synthEditor->restoreView ("1,0");

    AudioBuffer<float> audio (2, 256);
    MidiBuffer midi;
    edit->dispatchPendingUpdatesSynchronously();
    edit->getTransport().ensureContextAllocated (true);
    auto period = [&]
    {
        float level = 0;
        for (int b = 0; b < 20; ++b)
        {
            audio.clear();
            midi.clear();
            hosted.processBlock (audio, midi);
            level = jmax (level, audio.getMagnitude (0, audio.getNumSamples()));
        }
        return level;
    };
    synth->preview (48, 108);
    if (period() < .001f) return fail ("Synth audition through the track signal path");
    instrumentWorkspace.setFloating (true);
    if (! instrumentWorkspace.isFloating() || instrumentWorkspace.getEditor() != synthEditor
        || instrumentWorkspace.getPlugin() != synth || period() < .001f)
        return fail ("Float replaced the editor, instrument, or sounding voice");
    if (! snapshot (*synthEditor->getTopLevelComponent(), "synth-floating")) return fail ("capture floating Synth");
    instrumentWorkspace.setFloating (false);
    if (instrumentWorkspace.isFloating() || workspace != Workspace::instrumentEditor
        || instrumentWorkspace.getEditor() != synthEditor || period() < .001f)
        return fail ("Dock replaced the editor or interrupted playback");
    synth->preview (48, 0);

    const auto synthTrack = synth->getOwnerTrack()->itemID;
    const auto synthId = synth->itemID;
    const auto savedFactory = synth->factoryID();
    browser.loadSampler (true);
    auto* sampler = dynamic_cast<studio::sampler::Plugin*> (instrumentWorkspace.getPlugin());
    if (sampler == nullptr || instrumentWorkspace.isFloating()) return fail ("Sampler must open docked");
    const auto samplerTrack = sampler->getOwnerTrack()->itemID;
    const auto samplerID = sampler->itemID;
    const auto makeSample = [&] (const char* name, float frequency)
    {
        const auto sample = folder.getChildFile (name);
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> stream = sample.createOutputStream();
        auto wave = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        if (! wave) return false;
        AudioBuffer<float> signal (1, 96000);
        for (int t = 0; t < signal.getNumSamples(); ++t)
        {
            const float second = t / 48000.0f;
            const float phase = MathConstants<float>::twoPi * frequency * second;
            const float envelope = jmin (1.0f, second * 380) * std::exp (-second * 1.85f);
            signal.setSample (0, t, envelope * (.46f * std::sin (phase) + .20f * std::sin (phase * 2.0f)));
        }
        if (! wave->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples())) return false;
        wave.reset();
        return sampler->loadSample (sample).wasOk();
    };
    if (! makeSample ("Studio Keys C4.wav", 261.626f)) return fail ("load sampler fixture");
    instrumentWorkspace.getEditor()->restoreView ("0");
    edit->dispatchPendingUpdatesSynchronously();
    edit->getTransport().ensureContextAllocated (true);
    sampler->preview (1, 1, 60, 100);
    if (period() < .001f) return fail ("Sampler audition after opening its editor");
    if (! snapshot (*this, "sampler-docked")) return fail ("capture docked Sampler");
    instrumentWorkspace.setFloating (true);
    if (period() < .001f || ! snapshot (*instrumentWorkspace.getEditor()->getTopLevelComponent(), "sampler-floating"))
        return fail ("floating Sampler must keep sounding");
    instrumentWorkspace.setFloating (false);
    sampler->preview (1, 1, 60, 0);
    if (! makeSample ("Studio Keys C5.wav", 523.252f)) return fail ("load second sampler zone");
    auto lowZone = sampler->instrument().zones[0].mapping;
    lowZone.high = 65;
    lowZone.loop = true;
    lowZone.loopStart = 15000;
    lowZone.loopEnd = 62000;
    sampler->editZone (0, lowZone);
    auto highZone = sampler->instrument().zones[1].mapping;
    highZone.root = 72;
    highZone.low = 66;
    sampler->editZone (1, highZone);
    instrumentWorkspace.getEditor()->restoreView ("1");
    if (! snapshot (*this, "sampler-mapping")) return fail ("capture mapping");
    instrumentWorkspace.getEditor()->restoreView ("2");
    if (! snapshot (*this, "sampler-loops")) return fail ("capture loops");

    // Build real sampler articulations, then compose while following violin/horn tracks.
    auto violinModel = sampler->instrument(); violinModel.name = "Synthetic Violin"; violinModel.groups.clear();
    violinModel.zones.push_back (violinModel.zones[0]);
    const StringArray violinNames { "Sustain", "Staccato", "Pizzicato" };
    for (int i = 0; i < 3; ++i)
    {
        studio::sampler::InstrumentGroup g { i, violinNames[i] };
        g.playback.id = i; g.playback.trigger = studio::sampler::GroupTrigger::keyswitch; g.playback.key = 24 + i;
        violinModel.groups.push_back (g);
        auto& z = violinModel.zones[static_cast<std::size_t> (i)]; z.folder = i;
        z.mapping.low = 48; z.mapping.high = 84; z.mapping.oneShot = i != 0;
        if (i) { z.mapping.loop = false; z.mapping.end = i == 1 ? 12000 : 8000; }
    }
    sampler->commitImport (violinModel); sampler->getOwnerTrack()->setName ("Violin");
    auto hornTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (! hornTrack) return fail ("create French horn track");
    hornTrack->setName ("French Horn"); hornTrack->getOutput().setOutputToDefaultDevice (false);
    auto hornPlugin = edit->getPluginCache().createNewPlugin (studio::sampler::Plugin::xmlTypeName, {});
    hornTrack->pluginList.insertPlugin (hornPlugin, 0, nullptr);
    auto* hornSampler = dynamic_cast<studio::sampler::Plugin*> (hornPlugin.get());
    if (! hornSampler) return fail ("create native horn sampler");
    auto hornModel = violinModel; hornModel.name = "Synthetic French Horn";
    const StringArray hornNames { "Sustain", "Marcato", "Stopped" };
    for (int i = 0; i < 3; ++i) { hornModel.groups[static_cast<std::size_t> (i)].name = hornNames[i]; hornModel.groups[static_cast<std::size_t> (i)].playback.key = 36 + i; }
    hornSampler->commitImport (hornModel);
    edit->dispatchPendingUpdatesSynchronously(); edit->getTransport().ensureContextAllocated (true);
    auto* violinTrack = dynamic_cast<te::AudioTrack*> (sampler->getOwnerTrack());
    auto phrase = violinTrack->insertMIDIClip ("Violin articulations", { 0_tp, 2_tp }, nullptr);
    if (! phrase) return fail ("create violin phrase");
    const auto phraseId = phrase->itemID;
    phrase->setMidiChannel (te::MidiChannel (1));
    showWorkspace (Workspace::closed); selection.selectOnly (violinTrack);
    articulation.refresh (violinTrack, phrase.get(), true);
    if (! articulation.selectReviewMapping ("Pizzicato")) return fail ("native violin mappings follow selected track");
    period();
    if (sampler->activeKeyswitch (1) != 26) return fail ("strip must deliver the verified violin keyswitch to the audio engine");
    articulation.writeForDrawnNote (*phrase, te::BeatPosition::fromBeats (.5), 60);
    phrase->getSequence().addNote (60, te::BeatPosition::fromBeats (.5), te::BeatDuration::fromBeats (.25), 100, 0, nullptr);
    bool hasSwitch = false;
    for (const auto* n : phrase->getSequence().getNotes()) if (n->getNoteNumber() == 26) hasSwitch = true;
    if (! hasSwitch) return fail ("Piano Roll workflow inserts verified native keyswitch");
    if (! snapshot (articulationStrip, "sampler-violin-strip-closeup") || ! snapshot (*this, "sampler-violin-articulation-strip")) return fail ("capture native violin strip");
    articulationStrip.onManage();
    if (instrumentWorkspace.getPlugin() != sampler || instrumentWorkspace.getEditor()->viewState() != "4") return fail ("strip Mappings opens native group assignments");
    if (! snapshot (*this, "sampler-native-group-editor")) return fail ("capture articulation assignments");
    showWorkspace (Workspace::closed);
    edit->dispatchPendingUpdatesSynchronously();
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    edit->getTransport().ensureContextAllocated (true);
    sampler->reset();
    period();
    if (sampler->activeKeyswitch (1) != -1) return fail ("reset clears the live keyswitch before clip playback");
    edit->getTransport().setPosition (0_tp); edit->getTransport().play (false);
    float phrasePeak = 0;
    for (int i = 0; i < 30 && (phrasePeak < .001f || sampler->activeKeyswitch (1) != 26); ++i)
        phrasePeak = jmax (phrasePeak, period());
    if (phrasePeak < .001f || sampler->activeKeyswitch (1) != 26) return fail ("clip playback applies its stored keyswitch and sounds");
    if (! edit->getTransport().isPlaying() || ! articulation.selectReviewMapping ("Sustain")) return fail ("select a new native articulation during playback");
    period();
    if (sampler->activeKeyswitch (1) != 24) return fail ("live articulation reaches the engine during playback");
    edit->getTransport().setPosition (1.5_tp, 1.5_tp);
    period();
    if (sampler->resourceSnapshot().activeVoices.value_or (1) != 0) return fail ("seeking beyond the phrase clears native sampler voices");
    edit->getTransport().stop (false, false);
    period();
    if (sampler->activeKeyswitch (1) != -1 || sampler->resourceSnapshot().activeVoices.value_or (1) != 0)
        return fail ("stopping clears the sampler's latched articulation and voices");
    if (! articulation.selectReviewMapping ("Pizzicato")) return fail ("restore the selected violin articulation for track switching");
    selection.selectOnly (hornTrack.get()); articulation.refresh (hornTrack.get(), nullptr, true);
    if (! articulation.selectReviewMapping ("Marcato") || articulation.reviewActiveName() != "Marcato"
        || ! snapshot (articulationStrip, "sampler-horn-strip-closeup") || ! snapshot (*this, "sampler-horn-articulation-strip")) return fail ("horn strip follows actual horn instrument");
    period();
    if (hornSampler->activeKeyswitch (1) != 37) return fail ("strip must deliver the verified horn keyswitch to the audio engine");
    hornSampler->preview (1, 1, 60, 100);
    if (period() < .001f) return fail ("horn articulation must audition");
    hornSampler->preview (1, 1, 60, 0); hornSampler->reset(); period();
    selection.selectOnly (violinTrack); articulation.refresh (violinTrack, phrase.get(), true);
    if (articulation.reviewActiveName() != "Pizzicato") return fail ("violin articulation retained after following horn");
    showWorkspace (Workspace::instrumentEditor); instrumentWorkspace.updateSelection (violinTrack, te::getAudioTracks (*edit), true);
    instrumentWorkspace.getEditor()->restoreView ("2");
    phrase = nullptr; hornPlugin = nullptr; hornTrack = nullptr;

    for (const auto scale : studio::AppearanceMetrics::interfaceOptions)
        for (const auto text : studio::AppearanceMetrics::textOptions)
        {
            setSize (1024, 640);
            applyAppearance (scale, text, false);
            setDockMode (DockMode::split);
            if (! workspaceViewport.isVisible() || workspaceDivider.getBounds().isEmpty()
                || instrumentWorkspace.getEditor()->getHeight() < instrumentWorkspace.getEditor()->minimumDockHeight())
                return fail ("Instrument editor must remain scrollable at each appearance setting");
        }

    selection.selectOnly (trackById (synthTrack));
    instrumentWorkspace.updateSelection (trackById (synthTrack), te::getAudioTracks (*edit), true);
    if (instrumentWorkspace.getPlugin() != synth || instrumentWorkspace.getEditor()->viewState() != "1,0"
        || synth->factoryID() != savedFactory)
        return fail ("selecting another track must recover its instrument and editor page");
    selection.selectOnly (trackById (samplerTrack));
    instrumentWorkspace.updateSelection (trackById (samplerTrack), te::getAudioTracks (*edit), true);
    if (instrumentWorkspace.getPlugin() != sampler || instrumentWorkspace.getEditor()->viewState() != "2"
        || sampler->instrument().zones.empty())
        return fail ("track switching must retain sampler source and loop page");

    applyAppearance (100, 100, false);
    setDockMode (DockMode::expanded);
    instrumentWorkspace.setFloating (true);
    auto* floatingEditor = instrumentWorkspace.getEditor();
    auto* floatingWindow = floatingEditor->getTopLevelComponent();
    floatingWindow->setSize (440, 340);
    applyAppearance (175, 130, false);
    if (floatingEditor->getHeight() < floatingEditor->minimumDockHeight())
        return fail ("floating editor must scroll in a compact window with enlarged text");
    applyAppearance (100, 100, false);
    if (! te::EditFileOperations (*edit).writeToFile (session, false)) return fail ("save native instruments");
    const auto generation = projectGeneration;
    loadProject (session, false);
    auto* restored = dynamic_cast<studio::sampler::Plugin*> (instrumentWorkspace.getPlugin());
    if (projectGeneration != generation + 1 || restored == nullptr || restored->itemID != samplerID
        || restored->instrument().zones.size() != 3 || restored->instrument().groups.size() != 3
        || restored->instrument().groups[2].playback.key != 26 || ! restored->instrument().zones[0].mapping.loop
        || ! instrumentWorkspace.isFloating() || dockMode != DockMode::expanded
        || instrumentWorkspace.getEditor()->viewState() != "2" || te::findPluginForID (*edit, synthId) == nullptr)
        return fail ("reopen with the selected editor, layout, page and both instruments intact");
    auto* restoredPhrase = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, phraseId));
    if (restoredPhrase == nullptr || restoredPhrase->getMidiChannel().getChannelNumber() != 1)
        return fail ("reopened project retains the channel-1 phrase");
    const auto restoredNotes = restoredPhrase->getSequence().getNotes();
    if (std::none_of (restoredNotes.begin(), restoredNotes.end(), [] (const auto* note) { return note->getNoteNumber() == 26; }))
        return fail ("reopened phrase retains the composed keyswitch");
    setSize (1280, 800);
    instrumentWorkspace.setFloating (false);
    if (! snapshot (*this, "sampler-reopened-1280x800")) return fail ("capture reopened sampler");
    showWorkspace (Workspace::closed);
    if (! performance.isVisible() || ! snapshot (*this, "sampler-performance-1280x800")) return fail ("compact Performance without detailed editor");
    restored->deleteFromParent();
    instrumentWorkspace.updateSelection (selectedTrack(), te::getAudioTracks (*edit), true);
    if (instrumentWorkspace.getPlugin() != nullptr || instrumentWorkspace.isFloating())
        return fail ("deleting an instrument must discard its editor and floating window");
    loadProject (folder.getChildFile ("Another Project.tracktionedit"), true);
    if (instrumentWorkspace.getEditor() != nullptr || instrumentWorkspace.isFloating())
        return fail ("switching projects must clear the editor");
    std::puts ("Native instrument workspace, playback, preset browser, appearance and restoration passed.");
    return 0;
}

int StudioView::runInterfaceReview (const File& output)
{
    stopTimer();
    environmentWelcome.dismiss();
    const ScopedValueSetter<studio::AppearanceMetrics> appearanceScope (studio::appearance, studio::appearance);
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Studio interface review failed: %s\n", reason);
        return 1;
    };
    TemporaryFile fixture (".tracktionedit");
    loadProject (fixture.getFile(), true);
    if (edit == nullptr || editor == nullptr) return fail ("create temporary project");
    constexpr std::array names { "First Violins", "Violas", "Cellos", "Flute", "Clarinet", "French Horn", "Timpani", "Piano" };
    auto existing = te::getAudioTracks (*edit);
    for (size_t i = 0; i < names.size(); ++i)
    {
        auto* track = i < (size_t) existing.size() ? existing[(int) i] : addTrack ("Instrument");
        if (track == nullptr) return fail ("create fixture tracks");
        track->setName (names[i]);
        if (auto clip = track->insertMIDIClip (i < 3 ? "Opening theme" : "Orchestral cue",
                { te::TimePosition::fromSeconds ((double) i * 0.6), te::TimePosition::fromSeconds ((double) i * 0.6 + 5.0) },
                nullptr))
            for (int note = 0; note < 8; ++note)
                clip->getSequence().addNote (60 + (note * 3) % 12 + (int) i,
                    te::BeatPosition::fromBeats (0.75 * note), te::BeatDuration::fromBeats (0.5),
                    (uint8) (60 + 6 * note), 0, nullptr);
    }
    edit->dispatchPendingUpdatesSynchronously();
    auto tracks = te::getAudioTracks (*edit);
    title.setText ("Studio 0.2  /  Chamber Session", dontSendNotification);
    message.setText ("Chamber Session", dontSendNotification);
    selection.selectOnly (tracks[0]);
    // The production editor builds its rows and regions on the message loop.
    // Wait for those updates so review images show the actual arrangement.
    const auto arrangementReady = [&] { return editor->getVisibleTrackCount() >= tracks.size() && hasArrangementRegion (*editor); };
    for (int attempt = 0; attempt < 12 && ! arrangementReady(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    if (! arrangementReady()) return fail ("arrangement rows and regions did not load");
    timerCallback();
    for (auto scale : studio::AppearanceMetrics::interfaceOptions)
        for (auto text : studio::AppearanceMetrics::textOptions)
        {
            applyAppearance (scale, text, false);
            for (const auto size : { Point<int> (1024, 640), Point<int> (1280, 800), Point<int> (1600, 960) })
            {
                setSize (size.x, size.y);
                auto previous = Rectangle<int>();
                for (auto* key : { &rewind, &stop, &play, &record })
                {
                    if (key->getBounds().isEmpty() || ! topBar.contains (key->getBounds())
                        || previous.intersects (key->getBounds()))
                        return fail ("transport controls overlap or clip");
                    previous = key->getBounds();
                }
                showWorkspace (Workspace::mix);
                setDockMode (DockMode::compact);
                if (! workspaceSummary.isVisible() || mixer.isVisible()) return fail ("compact Mixer");
                setDockMode (DockMode::split);
                if (! workspaceViewport.isVisible() || ! mixer.isVisible() || ruler.getWidth() < 200)
                    return fail ("split arrangement and Mixer");
                // Exercise actual pointer and keyboard resizing. Merely
                // selecting Split does not catch a divider that stops moving
                // once Split is already the active mode.
                const int minDock = jmin (studio::appearance.px (minimumSplitHeight), splitLimit);
                if (splitLimit - minDock > studio::appearance.px (32))
                {
                    workspaceHeight = minDock + (splitLimit - minDock) / 2;
                    resized();
                    const int beforeDrag = workspacePanel.getHeight();
                    const int distance = jmin (studio::appearance.px (24), splitLimit - beforeDrag);
                    const auto start = workspaceDivider.getLocalBounds().getCentre().toFloat();
                    const auto end = start.translated (0.0f, (float) -distance);
                    const auto time = Time::getCurrentTime();
                    const auto source = Desktop::getInstance().getMainMouseSource();
                    const auto event = [&] (Point<float> point, int flags, bool moved)
                    {
                        return MouseEvent (source, point, ModifierKeys (flags), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           &workspaceDivider, &workspaceDivider, time, start, time, 1, moved);
                    };
                    workspaceDivider.mouseDown (event (start, ModifierKeys::leftButtonModifier, false));
                    workspaceDivider.mouseDrag (event (end, ModifierKeys::leftButtonModifier, true));
                    workspaceDivider.mouseUp (event (end, 0, true));
                    if (workspacePanel.getHeight() != beforeDrag + distance)
                        return fail ("Split divider pointer drag does not resize");
                    const int afterDrag = workspacePanel.getHeight();
                    if (! workspaceDivider.keyPressed (KeyPress (KeyPress::downKey)) || workspacePanel.getHeight() >= afterDrag)
                        return fail ("Split divider keyboard resize does not resize");
                }
                setDockMode (DockMode::expanded);
                if (workspaceViewport.getHeight() == 0 || mixer.getWidth() <= 2 * studio::appearance.mixerStripWidth (mixer.getWidth()))
                    return fail ("expanded mixer width");
                showWorkspace (Workspace::closed);
                if (workspaceViewport.isVisible()) return fail ("hide workspace");
            }
        }
    applyAppearance (100, 100, false);
    setSize (1280, 800);
    setDockMode (DockMode::expanded);
    showWorkspace (Workspace::mix);
    trackActions.selectTrack (tracks[1]->itemID);
    if (selectedTrack() != tracks[1]) return fail ("Mixer and arrangement selection");
    mixer.refresh (tracks, selectedTrack(), true, true);

    const bool writeImages = output != File();
    if (writeImages && output.createDirectory().failed()) return fail ("create screenshot directory");
    const auto capture = [&] (const char* name, Component* component = nullptr)
    {
        if (! writeImages) return true;
        auto& content = component != nullptr ? *component : *this;
        const auto image = content.createComponentSnapshot (content.getLocalBounds());
        FileOutputStream stream (output.getChildFile (name));
        return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (image, stream);
    };
    if (! capture ("mixer-1280x800.png")) return fail ("write Mixer screenshot");

    const auto seconds = [] (double value) { return te::TimePosition::fromSeconds (value); };
    if (! studio::setTempoEvent (*edit, te::BeatPosition::fromBeats (8), 94)
        || ! studio::setMeterEvent (*edit, te::BeatPosition::fromBeats (8), 3, 4)
        || studio::addCueMarker (*edit, seconds (4), "Bridge") == nullptr
        || ! studio::setCycle (*edit, { seconds (2), seconds (8) }))
        return fail ("set up Composer timeline review");
    showWorkspace (Workspace::closed);
    zoom.setSelectedId (1, sendNotificationSync);
    timerCallback();
    if (! capture ("composer-arrangement-1280x800.png")) return fail ("write Composer arrangement screenshot");
    if (writeImages)
    {
        studio::bounce::showOptions (*this, *edit, { tracks[1]->itemID }, [] (Result, studio::bounce::Options) {});
        auto* dialog = Component::getCurrentlyModalComponent();
        if (dialog == nullptr || ! capture ("composer-wav-options.png", dialog)) return fail ("write WAV options screenshot");
        dialog->exitModalState (0);
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    environmentWelcome.selectForReview (1);
    environmentWelcome.open (false);
    if (! environmentWelcome.essentialControlsFit()) return fail ("Welcome controls at 1280 x 800");
    if (! capture ("studio-welcome-1280x800.png")) return fail ("write Welcome screenshot");
    environmentWelcome.dismiss();

    showWorkspace (Workspace::stage);
    spatialWorkspace.refresh (tracks, { tracks[1] }, true, true);
    using studio::spatial::Seating;
    for (const auto [layout, filename] : { std::pair { Seating::concert, "stage-concert-1280x800.png" },
                                          std::pair { Seating::chamber, "stage-chamber-1280x800.png" },
                                          std::pair { Seating::scoring, "stage-scoring-1280x800.png" } })
    {
        spatialWorkspace.arrangeForInterfaceReview (layout);
        if (! capture (filename)) return fail ("write Stage screenshot");
    }
    if (! spatialWorkspace.verifyMoveForInterfaceReview (*tracks[1])) return fail ("Stage seat move, undo and redo");
    auto* clip = dynamic_cast<te::MidiClip*> (tracks[0]->getClips()[0]);
    if (clip == nullptr) return fail ("fixture MIDI clip");
    selection.selectOnly (clip);
    piano.setClip (clip);
    setDockMode (DockMode::split);
    showWorkspace (Workspace::piano);
    setSize (1024, 640);
    if (! capture ("piano-arrangement-1024x640.png")) return fail ("write Piano Roll screenshot");
    applyAppearance (175, 130, false);
    if (! capture ("piano-arrangement-1024x640-largest-text.png")) return fail ("write scaled Piano Roll screenshot");
    environmentWelcome.open (false);
    if (! environmentWelcome.essentialControlsFit()) return fail ("Welcome controls at 1024 x 640 largest text");
    if (! capture ("studio-welcome-1024x640-largest-text.png")) return fail ("write scaled Welcome screenshot");
    environmentWelcome.dismiss();

    {
        const ScopedValueSetter<bool> reviewRequired (ariaReviewPending, true);
        timerCallback();
        chooseWavExport();
        if (exportWav.isEnabled() || Component::getCurrentlyModalComponent() != nullptr)
            return fail ("unconfirmed ARIA must block export before opening destination controls");
    }
    const auto original = studio::spatial::readSeat (tracks[1]->state.getChildWithName (studio::spatial::seatType));
    const auto beforeReopen = projectGeneration;
    save();
    loadProject (fixture.getFile(), false);
    tracks = te::getAudioTracks (*edit);
    if (projectGeneration != beforeReopen + 1 || tracks.size() < 2 || tracks[0]->getClips().isEmpty()
        || studio::spatial::readSeat (tracks[1]->state.getChildWithName (studio::spatial::seatType)) != original)
        return fail ("restore project with MIDI and Stage positions");
    std::puts ("Studio interface review passed at 45 size and appearance combinations.");
    return 0;
}

namespace studio::spatial { int runSpatialRoutingTest(); }
namespace studio { int runComposerTransportTest(); int runComposerMidiTest(); int runComposerBounceTest (const File&); }
#endif

class StudioApplication final : public JUCEApplication
{
    class Window final : public DocumentWindow
    {
    public:
        Window() : DocumentWindow ("Studio", studio::colours::canvas, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setResizeLimits (920, 500, 2600, 1800);
            setContentOwned (new StudioView(), true);
            if (const auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = displayClientArea (*this, *display);
                setBounds (getBounds().withCentre (area.getCentre()).constrainedWithin (area));
            }
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

public:
    const String getApplicationName() override { return "Studio"; }
    const String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const String& commandLine) override
    {
       #if STUDIO_SPATIAL_TESTS
        if (commandLine == "--studio-test-unified-stage")
        {
            StudioView view;
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_UNIFIED_STAGE_SCREENSHOTS", {});
            setApplicationReturnValue (view.runUnifiedStageReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-test-seating-project")
        {
           #if JUCE_MAC
            Window reviewWindow;
            auto& view = *static_cast<StudioView*> (reviewWindow.getContentComponent());
           #else
            StudioView view;
           #endif
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_SEATING_SCREENSHOTS", {});
            setApplicationReturnValue (view.runSeatingProjectReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-test-composer-bounce")
        {
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_COMPOSER_AUDIO", {});
            setApplicationReturnValue (studio::runComposerBounceTest (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-test-composer-midi")
        {
            setApplicationReturnValue (studio::runComposerMidiTest());
            quit();
            return;
        }
        if (commandLine == "--studio-test-composer-transport")
        {
            setApplicationReturnValue (studio::runComposerTransportTest());
            quit();
            return;
        }
        if (commandLine == "--studio-test-spatial-routing")
        {
            setApplicationReturnValue (studio::spatial::runSpatialRoutingTest());
            quit();
            return;
        }
        if (commandLine == "--studio-test-synth")
        {
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_SYNTH_SCREENSHOTS", {});
            setApplicationReturnValue (studio::synth::runSynthIntegrationTest (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-test-sampler")
        {
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_SAMPLER_SCREENSHOTS", {});
            setApplicationReturnValue (studio::sampler::runSamplerIntegrationTest (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-adaptive-workspace-review")
        {
            Window reviewWindow;
            auto& view = *static_cast<StudioView*> (reviewWindow.getContentComponent());
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_M9_SCREENSHOTS", {});
            setApplicationReturnValue (view.runAdaptiveWorkspaceReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-instrument-workspace-review")
        {
            StudioView view;
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_INSTRUMENT_SCREENSHOTS", {});
            setApplicationReturnValue (view.runInstrumentWorkspaceReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
        if (commandLine == "--studio-interface-review")
        {
            StudioView view;
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_INTERFACE_SCREENSHOTS", {});
            setApplicationReturnValue (view.runInterfaceReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
       #else
        ignoreUnused (commandLine);
       #endif
        window = std::make_unique<Window>();
    }
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
