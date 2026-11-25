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
#include "MidiPerformanceFocus.h"
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
#include "AtelierInstrumentEditor.h"
#include "InstrumentPerformance.h"
#include "Seating/SeatingWorkspace.h"
#include "FilmScoringWorkspace.h"

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

    // The Instrument Editor has a deliberate, fixed Split height. Other
    // lower workspaces continue to use the same draggable/keyboard divider.
    void setResizeLocked (bool shouldLock)
    {
        if (locked == shouldLock) return;
        locked = shouldLock;
        setMouseCursor (locked ? MouseCursor::NormalCursor : MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (! locked);
        if (locked) giveAwayKeyboardFocus();
        setTitle (locked ? "Instrument workspace divider" : "Resize lower workspace");
        setDescription (locked ? "Instrument workspace height is fixed."
                               : "Press Up or Down to resize the lower workspace.");
        repaint();
    }
    bool isResizeLocked() const { return locked; }

    void paint (Graphics& g) override
    {
        g.fillAll (studio::colours::divider);
        if (locked) return; // A divider line, not a misleading drag handle.
        g.setColour (hasKeyboardFocus (false) || isMouseOverOrDragging()
                         ? studio::colours::focus : studio::colours::secondary);
        for (int offset : { -10, -5, 0, 5, 10 })
            g.fillEllipse ((float) (getWidth() / 2 + offset - 1), (float) (getHeight() / 2 - 1), 2.0f, 2.0f);
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseDown (const MouseEvent&) override
    {
        if (! locked) initialSize = getSize();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! locked) resizePane (initialSize - e.getDistanceFromDragStartY());
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (locked || (key != KeyPress::upKey && key != KeyPress::downKey))
            return false;
        resizePane (getSize() + studio::appearance.px (key == KeyPress::upKey ? 16 : -16));
        return true;
    }

private:
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    int initialSize = 0;
    bool locked = false;
};

class AudioSettingsView final : public Viewport
{
public:
    explicit AudioSettingsView (AudioDeviceManager& manager)
        : selector (manager, 0, 512, 2, 512, false, false, true, true)
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

// Searchable reference for controls already implemented in Studio.
// This is display-only: it does not register or alter keyboard commands.
class StudioCommandReference final : public Component
{
public:
    StudioCommandReference()
    {
        addAndMakeVisible (search);
        addAndMakeVisible (results);
        search.setTextToShowWhenEmpty ("Search commands and shortcuts", studio::colours::secondary);
        search.setColour (TextEditor::backgroundColourId, studio::colours::panel);
        search.onTextChange = [this] { refreshList(); };
        results.setMultiLine (true);
        results.setReadOnly (true);
        results.setScrollbarsShown (true);
        results.setColour (TextEditor::backgroundColourId, studio::colours::canvas);
        refreshList();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        search.setBounds (area.removeFromTop (34));
        area.removeFromTop (10);
        results.setBounds (area);
    }

private:
    TextEditor search, results;
    void refreshList()
    {
        const auto query = search.getText().trim();
        StringArray lines;
        // Only claim shortcuts implemented in StudioView::keyPressed or
        // enabled by the Piano Roll. Other commands are listed without keys.
        for (const auto& item : std::initializer_list<const char*> {
            "Transport  |  Space  |  Play / Stop",
            "File  |  Command-S  |  Save Project",
            "File  |  Shift-Command-S  |  Save As",
            "Edit  |  Command-Z  |  Undo",
            "Edit  |  Shift-Command-Z  |  Redo",
            "MIDI  |  Command-J  |  Join Selected MIDI Regions",
            "Piano Roll  |  Delete  |  Delete Selected Notes",
            "Piano Roll  |  Escape  |  Clear Note Selection",
            "Transport  |  Return to Start",
            "Record  |  Capture Recent MIDI",
            "Track  |  Add Instrument / Synth / Sampler / Audio",
            "Mix  |  Selected Track Inspector",
            "View  |  Transport Layout and Interface Scale",
            "Window  |  Mixer / Stage / Piano Roll"
        })
            if (query.isEmpty() || String (item).containsIgnoreCase (query))
                lines.add (item);
        results.setText (lines.isEmpty() ? "No matching commands." : lines.joinIntoString ("\n\n"),
                         dontSendNotification);
    }
};

// A compact landing window for existing Studio settings; it does not replace
// or reconfigure the established audio and plug-in systems.
class StudioSettingsHub final : public Component
{
public:
    std::function<void()> openAudio, openPlugins, openMidi, restoreAppearance;
    StudioSettingsHub()
    {
        for (auto* item : { &heading, &detail, &audioInfo, &pluginInfo, &midiInfo, &visualInfo })
            addAndMakeVisible (*item);
        for (auto* button : { &audio, &plugins, &midi, &appearance })
            addAndMakeVisible (*button);
        heading.setText ("Studio Settings", dontSendNotification);
        heading.setFont (FontOptions (18.0f, Font::bold));
        detail.setText ("Existing preferences and diagnostics", dontSendNotification);
        audioInfo.setText ("Audio output, input and stereo channels", dontSendNotification);
        pluginInfo.setText ("Audio Unit inventory and scanning", dontSendNotification);
        midiInfo.setText ("MIDI devices and activity", dontSendNotification);
        visualInfo.setText ("Restore interface and text sizing", dontSendNotification);
        for (auto* label : { &detail, &audioInfo, &pluginInfo, &midiInfo, &visualInfo })
            label->setColour (Label::textColourId, studio::colours::secondary);
        audio.onClick = [this] { if (openAudio) openAudio(); };
        plugins.onClick = [this] { if (openPlugins) openPlugins(); };
        midi.onClick = [this] { if (openMidi) openMidi(); };
        appearance.onClick = [this] { if (restoreAppearance) restoreAppearance(); };
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        heading.setBounds (area.removeFromTop (32));
        detail.setBounds (area.removeFromTop (34));
        area.removeFromTop (8);
        const auto place = [&] (TextButton& button, Label& label)
        {
            auto row = area.removeFromTop (54);
            button.setBounds (row.removeFromLeft (152).reduced (2, 8));
            label.setBounds (row.reduced (10, 6));
        };
        place (audio, audioInfo);
        place (plugins, pluginInfo);
        place (midi, midiInfo);
        place (appearance, visualInfo);
    }
private:
    Label heading, detail, audioInfo, pluginInfo, midiInfo, visualInfo;
    TextButton audio { "Audio..." }, plugins { "Plug-ins..." },
               midi { "MIDI Monitor..." }, appearance { "Reset Appearance" };
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
    int runFilmScoringReview (const File& output);
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
        followSelectedMidi = prefs.getBoolValue ("studio.midiFollowSelection", true);
        inspectorOpen = prefs.getBoolValue ("studio.inspectorOpen", false);
        showRoomReturnTrack = prefs.getBoolValue ("studio.showRoomReturnTrack", false);
        inspectorWidth = prefs.getIntValue ("studio.inspectorWidth", 375);
        showTransportCycle = prefs.getBoolValue ("studio.transportCycle", true);
        showTransportClick = prefs.getBoolValue ("studio.transportClick", true);
        showTransportElapsed = prefs.getBoolValue ("studio.transportElapsed", true);
        showTransportTempo = prefs.getBoolValue ("studio.transportTempo", true);
        showTransportMeter = prefs.getBoolValue ("studio.transportMeter", true);
        workspaceHeight = studio::appearance.px (workspaceHeight);
        railViewport.setViewedComponent (&railContent, false);
        railViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (railViewport);
        workspaceViewport.setViewedComponent (&workspaceBody, false);
        workspaceViewport.setScrollBarsShown (true, false);
        addChildComponent (workspaceViewport);
        addAndMakeVisible (viewButton);
        addAndMakeVisible (inspectorButton);
        inspectorButton.setTooltip ("Toggle the selected-track Inspector on the right");
        inspectorButton.setTitle ("Selected-track Inspector");
        inspectorButton.onClick = [this] { setInspectorOpen (! inspectorOpen); };
        addChildComponent (inspectorDrawer);
        inspectorDrawer.onClose = [this] { setInspectorOpen (false); };
        inspectorDrawer.onWidthChanged = [this] (int width)
        {
            inspectorWidth = jlimit (studio::appearance.span (205),
                                     jmin (studio::appearance.span (510), jmax (studio::appearance.span (205), getWidth() - studio::appearance.span (220))),
                                     width);
            engine.getPropertyStorage().getPropertiesFile().setValue ("studio.inspectorWidth", inspectorWidth);
            resized();
        };
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
                                           &audioSettings, &plugins, &importMidi, &exportMidi, &exportWav });
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
            // A repeated Detailed Editor click closes the same native editor.
            // Keep the instrument and playback running; this is only a view toggle.
            if (studio::isNativeInstrument (plugin) && instrumentWorkspace.getPlugin() == &plugin)
            {
                if (instrumentWorkspace.isFloating())
                {
                    instrumentWorkspace.setFloating (false);
                    showWorkspace (Workspace::closed);
                    return;
                }
                if (workspace == Workspace::instrumentEditor)
                {
                    showWorkspace (Workspace::closed);
                    return;
                }
            }
            if (! showNativeInstrument (plugin) && ! showAtelierInstrument (plugin))
                plugin.showWindowExplicitly();
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
        addAndMakeVisible (transportOptions);
        addAndMakeVisible (flashbackButton);
        flashbackButton.setTooltip ("Capture recently played MIDI from the selected track's assigned input, even if Record wasn't pressed");
        flashbackButton.onClick = [this] { captureFlashbackMidi(); };
        transportOptions.setTooltip ("Customize the visible transport controls and LCD readouts");
        transportOptions.onClick = [this] { showTransportOptions(); };
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
        workspaceBody.addChildComponent (filmWorkspace);
        workspaceViewport.onScroll = [this] { fitDockedPicture(); };
        filmWorkspace.canRenderAudio = [this] { return canStartAudioExport() && bounce == nullptr; };
        workspaceBody.addChildComponent (instrumentWorkspace);
        static_cast<StudioPluginWindows&> (engine.getUIBehaviour()).openNative = [this] (te::Plugin& plug)
        {
            return showNativeInstrument (plug);
        };
        instrumentWorkspace.onLayout = [this] { resized(); };
        instrumentWorkspace.onExpand = [this] { setDockMode (DockMode::expanded); };
        instrumentWorkspace.onDock = [this]
        {
            if (instrumentWorkspace.isAtelier())
            {
                // The full Audio Unit window returns to the performance
                // Inspector, never steals the lower production workspace.
                if (workspace == Workspace::instrumentEditor)
                    showWorkspace (Workspace::closed);
                setInspectorOpen (true);
                return;
            }
            if (dockMode == DockMode::compact) setDockMode (DockMode::split);
            showWorkspace (Workspace::instrumentEditor);
        };
        instrumentWorkspace.onPreferenceChange = [this] { rememberInstrumentWorkspace(); };
        instrumentWorkspace.onArticulations = [this] (te::EditItemID id)
        {
            if (auto* target = trackById (id))
            {
                selection.selectOnly (target);
                if (dockMode == DockMode::compact) setDockMode (DockMode::split);
                showWorkspace (Workspace::articulations);
            }
        };
        instrumentWorkspace.onStage = [this] (te::EditItemID id)
        {
            if (auto* target = trackById (id))
            {
                selection.selectOnly (target);
                if (dockMode == DockMode::compact) setDockMode (DockMode::split);
                showWorkspace (Workspace::stage);
            }
        };
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
        // The rail contains real composer destinations only. Stage subviews
        // live in its workspace header; diagnostics live under View.
        // Reserve the remaining rail space for actual future composing tools.
        stageButton.setTooltip ("Orchestral Stage | Rooms, Virtual Microphones and Arrangement Reference");
        instrumentsButton.setTooltip ("Instrument browser | Open the selected instrument from its own editor");
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
                if (contextLinks()[index].destination == Workspace::channel)
                {
                    setInspectorOpen (true);
                    return;
                }
                showWorkspace (contextLinks()[index].destination);
                if (workspaceLinks[index].isVisible())
                    workspaceLinks[index].grabKeyboardFocus();
                else
                    viewButton.grabKeyboardFocus();
            };
        }
        workspaceSummary.setColour (Label::textColourId, studio::colours::secondary);
        addAndMakeVisible (workspaceDivider);
        workspaceDivider.getSize = [this] { return workspacePanel.getHeight(); };
        workspaceDivider.resizePane = [this] (int size)
        {
            if (isWorkspaceDividerLocked())
                return;
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
        addAndMakeVisible (trackOptions);
        trackOptions.setTooltip ("Rename, duplicate, move or delete the selected track");
        trackOptions.onClick = [this]
        {
            PopupMenu menu;
            if (auto* track = workflowTrack())
                populateTrackEditMenu (menu, *track);
            else
                menu.addSectionHeader ("Select a track in the arrangement first");
            menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&trackOptions));
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
        setInspectorOpen (inspectorOpen, false);
        ensureStereoOutputGroup();
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
        if (edit != nullptr) midiFocus.release (edit.get());
        rememberInstrumentWorkspace();
        performance.clear();
        instrumentWorkspace.clear();
        static_cast<StudioPluginWindows&> (engine.getUIBehaviour()).openNative = {};
        channelInspector.setEdit (nullptr);
        inspectorDrawer.setEdit (nullptr);
        mixer.setEdit (nullptr);
        resources.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
        filmWorkspace.setEdit (nullptr);
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
        workspaceViewport.onScroll = {};
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
        // The musical guide grid is a paint-over-children layer. Exclude the
        // right Inspector or it visibly draws through its fully opaque panel.
        if (inspectorOpen && inspectorDrawer.isVisible())
            g.excludeClipRegion (inspectorDrawer.getBounds());
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
       #if JUCE_MAC
        performanceMode.setVisible (false);
        viewButton.setVisible (false);
        projectMenu.setVisible (false);
       #else
        performanceMode.setVisible (true);
        viewButton.setVisible (true);
        performanceMode.setBounds (commands.removeFromRight (m.span (163)).reduced (m.px (2), 0));
       #endif
        inspectorButton.setBounds (commands.removeFromRight (m.span (88)).reduced (m.px (2), 0));
       #if ! JUCE_MAC
        viewButton.setBounds (commands.removeFromRight (m.span (63)).reduced (m.px (2), 0));
       #endif
        const bool showCPU = commands.getWidth() > m.span (325);
        resources.audio.setVisible (showCPU);
        resources.ram.setVisible (false);
        resources.dropouts.setVisible (false);
        if (showCPU) resources.audio.setBounds (commands.removeFromRight (m.span (68)).reduced (m.px (2), 0));
       #if ! JUCE_MAC
        projectMenu.setVisible (true);
        projectMenu.setBounds (commands.removeFromRight (m.span (81)).reduced (m.px (2), 0));
       #endif
       #if JUCE_MAC
        const bool showFiles = false; // New/Open live in the native File menu.
       #else
        const bool showFiles = commands.getWidth() >= m.span (340);
       #endif
        for (auto* button : { &newProject, &openProject })
        {
            button->setVisible (showFiles);
            if (showFiles) button->setBounds (commands.removeFromLeft (m.span (93)).reduced (m.px (2), 0));
        }
        for (auto* button : { &plugins, &audioSettings, &saveAsProject, &saveProject, &monitorButton })
            button->setVisible (false);
        title.setBounds (commands.reduced (m.px (8), 0));

        auto strip = topBar.withTrimmedTop (projectHeight + m.px (3)).reduced (m.px (10), m.px (2));
        const int gap = m.px (6);
        const int barW = m.span (91), bpmW = m.span (58), meterW = m.span (53);
        const int menuW = m.span (76), flashbackW = m.span (98), elapsedW = m.span (97);
       #if JUCE_MAC
        const int customizeW = 0; // View menu owns transport customization.
       #else
        const int customizeW = m.span (95);
       #endif
        // Responsive transport: playback and record remain reachable at
        // 1024-wide MacBook sizes even with large accessibility text. Hidden
        // controls retain their persisted preferences and native menu actions.
        const int availableWidth = strip.getWidth();
        const bool showCycle = showTransportCycle && availableWidth >= m.span (740);
        const bool showClick = showTransportClick && availableWidth >= m.span (780);
        const bool showTempo = showTransportTempo && availableWidth >= m.span (580);
        const bool showMeter = showTransportMeter && availableWidth >= m.span (670);
        const bool showMusicMenu = availableWidth >= m.span (610);
        const bool showFlashback = availableWidth >= m.span (830);
        const bool showCustomize = customizeW > 0 && availableWidth >= m.span (925);
        const int optionalButtons = (showCycle ? 1 : 0) + (showClick ? 1 : 0);
        const int readoutBase = barW + (showTempo ? bpmW : 0) + (showMeter ? meterW : 0);
        const int secondaryWidth = (showMusicMenu ? menuW + gap : 0)
                                 + (showFlashback ? flashbackW + gap : 0)
                                 + (showCustomize ? customizeW : 0);
        const int fixed = readoutBase + secondaryWidth + 2 * gap;
        const int buttonCount = 4 + optionalButtons;
        const int key = jlimit (m.px (20), m.px (38),
            (availableWidth - fixed - (showTransportElapsed ? elapsedW : 0)) / buttonCount);
        const bool showElapsed = showTransportElapsed
            && fixed + buttonCount * key + elapsedW <= availableWidth;
        position.setVisible (showElapsed);
        tempo.setVisible (showTempo);
        meter.setVisible (showMeter);
        cycleButton.setVisible (showCycle);
        clickButton.setVisible (showClick);
        music.setVisible (showMusicMenu);
        flashbackButton.setVisible (showFlashback);
        const int readoutWidth = readoutBase + (showElapsed ? elapsedW : 0);
        const int totalWidth = fixed + buttonCount * key + (showElapsed ? elapsedW : 0);
        auto row = strip.withSizeKeepingCentre (jmin (availableWidth, totalWidth), transportHeight);
        for (auto* button : { &rewind, &stop, &play, &record })
            button->setBounds (row.removeFromLeft (key).reduced (m.px (1), m.px (5)));
        row.removeFromLeft (gap);
        for (auto* button : { &cycleButton, &clickButton })
            button->setBounds (button->isVisible()
                ? row.removeFromLeft (key).reduced (m.px (1), m.px (5))
                : Rectangle<int>());
        row.removeFromLeft (gap);
        musicalDisplay = row.removeFromLeft (readoutWidth);
        auto lcd = musicalDisplay.reduced (gap / 2, m.px (2));
        bars.setBounds (lcd.removeFromLeft (barW - gap / 2));
        position.setBounds (showElapsed ? lcd.removeFromLeft (elapsedW) : Rectangle<int>());
        tempo.setBounds (showTransportTempo ? lcd.removeFromLeft (bpmW) : Rectangle<int>());
        meter.setBounds (showMeter ? lcd : Rectangle<int>());
        if (showMusicMenu)
        {
            row.removeFromLeft (gap);
            music.setBounds (row.removeFromLeft (menuW).reduced (m.px (1), m.px (5)));
        }
        else music.setBounds ({});
        if (showFlashback)
        {
            row.removeFromLeft (gap);
            flashbackButton.setBounds (row.removeFromLeft (flashbackW).reduced (m.px (1), m.px (5)));
        }
        else flashbackButton.setBounds ({});
       #if JUCE_MAC
        transportOptions.setVisible (false);
       #else
        transportOptions.setVisible (showCustomize);
        transportOptions.setBounds (showCustomize
            ? row.removeFromLeft (customizeW).reduced (m.px (1), m.px (5))
            : Rectangle<int>());
       #endif

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
        railContent.setSize (railViewport.getMaximumVisibleWidth(),
                             tileHeight * (int) railItems().size() + m.px (8));
        // Reserve the scrollbar only when the complete rail needs it.
        if (railContent.getHeight() > railBounds.getHeight())
            railContent.setSize (jmax (1, railBounds.getWidth() - railViewport.getScrollBarThickness()), railContent.getHeight());
        auto destinations = railContent.getLocalBounds().reduced (m.px (3), m.px (4));
        for (const auto& item : railItems())
            item.button->setBounds (destinations.removeFromTop (tileHeight).reduced (0, m.px (1)));

        // The right Inspector owns an actual column, never an overlay. Both
        // arrangement and lower production workspace receive the same
        // reduced width before any layout or ruler calculations.
        const bool performanceInspector = inspectorOpen && workflowTrack() != nullptr
            && dynamic_cast<te::ExternalPlugin*> (studio::findInstrument (*workflowTrack())) != nullptr;
        const int minInspector = m.span (performanceInspector ? 365 : 205);
        const int visibleInspectorWidth = jlimit (minInspector,
            jmax (minInspector, jmin (m.span (510), area.getWidth() - m.span (265))),
            jmax (inspectorWidth, minInspector));
        const auto inspectorRegion = inspectorOpen ? area.removeFromRight (visibleInspectorWidth)
                                                   : Rectangle<int>();
        layoutPerformance (area);
        layoutDock (area);
        arrangementTools = area.removeFromTop (m.control (29));
        auto tools = arrangementTools.reduced (m.px (5), m.px (2));
       #if JUCE_MAC
        // Track creation/import commands now live in the native Track/File
        // menus. Keep only selection, region actions and zoom above the ruler.
        const bool showAllTrackTools = false;
       #else
        const bool showAllTrackTools = tools.getWidth() >= m.span (828);
       #endif
        trackOptions.setBounds (tools.removeFromLeft (m.span (90)).reduced (m.px (2), 0));
        zoom.setBounds (tools.removeFromRight (m.span (107)).reduced (m.px (2), 0));
        const int zoomButtonWidth = m.span (32);
        arrangementZoomIn.setBounds (tools.removeFromRight (zoomButtonWidth).reduced (m.px (1), 0));
        arrangementZoomOut.setBounds (tools.removeFromRight (zoomButtonWidth).reduced (m.px (1), 0));
        trackMenu.setVisible (! showAllTrackTools);
        if (trackMenu.isVisible())
            trackMenu.setBounds (tools.removeFromLeft (m.span (92)).reduced (m.px (2), 0));
        for (auto* button : { &addMidi, &addAudio, &addMidiClip, &importAudio })
        {
            button->setVisible (showAllTrackTools);
            if (showAllTrackTools)
                button->setBounds (tools.removeFromLeft (m.span (button == &addAudio ? 69 : 94)).reduced (m.px (2), 0));
        }
        // Region editing is used frequently, so retain one discoverable
        // entry point at narrow as well as full MacBook widths.
        regionActions.setVisible (true);
        regionActions.setBounds (tools.removeFromLeft (m.span (100)).reduced (m.px (2), 0));
        trackDetails.setBounds (tools);

        rulerRow = area.removeFromTop (m.control (26) + m.control (18));
        timeline.setBounds (area);
        sizeEditor();
        // Right-side Tracktion plug-in initials are disabled; the musical
        // ruler must use the same content width as the arrangement clips.
        const auto footerWidth = editor != nullptr && editor->getEditViewState().showFooters
            ? m.trackFooter() : 0;
        ruler = rulerRow.withTrimmedLeft (m.trackHeader()).withTrimmedRight (footerWidth + timeline.getScrollBarThickness());
        rulerTrackLabel = rulerRow.withWidth (m.trackHeader()).reduced (m.px (3));
        // The Inspector starts below the transport and runs beside the full
        // arrangement and lower workspace. Nothing is painted underneath it.
        inspectorDrawer.setBounds (inspectorRegion);
        inspectorDrawer.setVisible (inspectorOpen);
        if (inspectorOpen) inspectorDrawer.toFront (false);
        environmentWelcome.setBounds (getLocalBounds());
        if (environmentWelcome.isVisible()) environmentWelcome.toFront (false);
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
        for (Viewport* view : std::initializer_list<Viewport*> { &timeline, &railViewport, &workspaceViewport })
            view->setScrollBarThickness (m.scrollbar());
        browser.appearanceChanged();
        channelInspector.appearanceChanged();
        inspectorDrawer.appearanceChanged();
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

    void setRoomReturnTrackVisible (bool show, bool persist = true)
    {
        showRoomReturnTrack = show;
        if (editor != nullptr)
        {
            // Only rebuild Tracktion's arrangement widgets. This never hides
            // the actual aux return, changes the Edit, or affects playback.
            editor->setStudioRoomReturnVisible (show);
            sizeEditor();
        }
        if (! show && edit != nullptr && selectedTrack() != nullptr
            && studio::spatial::isRoomReturn (*selectedTrack()))
            selection.deselectAll(); // Don't leave an invisible selected row.
        if (persist)
        {
            auto& prefs = engine.getPropertyStorage().getPropertiesFile();
            prefs.setValue ("studio.showRoomReturnTrack", show);
            prefs.saveIfNeeded();
        }
    }

    void setInspectorOpen (bool value, bool persist = true)
    {
        if (! value) inspectorDrawer.suspendAtelier();
        inspectorOpen = value;
        if (value && workspace == Workspace::channel)
            showWorkspace (Workspace::closed); // Don't display duplicate Channel controls.
        inspectorButton.setToggleState (value, dontSendNotification);
        inspectorButton.setTooltip (value ? "Hide the selected-track Inspector" : "Show the selected-track Inspector");
        if (persist)
        {
            auto& prefs = engine.getPropertyStorage().getPropertiesFile();
            prefs.setValue ("studio.inspectorOpen", value);
            prefs.saveIfNeeded();
        }
        resized();
    }

    void captureFlashbackMidi()
    {
        if (edit == nullptr || busy || ariaReviewPending || ! studio::transportSettled (*edit))
            return;
        if (edit->getTransport().isPlaying() || edit->getTransport().isRecording())
        {
            message.setText ("Stop playback before using Capture MIDI.", dontSendNotification);
            return;
        }
        auto* target = workflowTrack();
        if (target == nullptr || target->isFrozen (te::Track::anyFreeze))
        {
            message.setText ("Select an unfrozen instrument track before Capture MIDI.", dontSendNotification);
            return;
        }
        auto* context = edit->getTransport().getCurrentPlaybackContext();
        if (context == nullptr)
        {
            message.setText ("Audio/MIDI playback context isn't ready.", dontSendNotification);
            return;
        }

        // Use the input device's existing bounded retrospective MIDI buffer.
        // Do not open a competing CoreMIDI port or capture unrelated tracks.
        int assigned = 0;
        Array<te::Clip*> created;
        for (auto* input : edit->getAllInputDevices())
        {
            if (! input->getTargets().contains (target->itemID)
                || input->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice)
                continue;
            if (input->getTargets().size() != 1)
            {
                message.setText ("Capture MIDI requires an input assigned only to the selected track.",
                                 dontSendNotification);
                return;
            }
            ++assigned;
        }
        if (assigned == 0)
        {
            message.setText ("Assign a physical MIDI input to this track using its I button first.",
                             dontSendNotification);
            return;
        }
        edit->getUndoManager().beginNewTransaction ("Capture MIDI");
        for (auto* input : edit->getAllInputDevices())
            if (input->getTargets().contains (target->itemID)
                && input->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
                created.addArray (input->applyRetrospectiveRecord (false));

        if (created.isEmpty())
        {
            message.setText ("No recent MIDI notes were captured. Play the keyboard and try again.",
                             dontSendNotification);
            return;
        }
        selection.selectOnly (created.getFirst());
        sizeEditor();
        message.setText ("Captured " + String (created.size()) + " MIDI clip(s) on "
                            + target->getName() + ". Save your project or Undo to remove them.",
                         dontSendNotification);
    }

    void showTransportOptions()
    {
        PopupMenu menu;
        menu.addSectionHeader ("Transport controls");
        const auto addToggle = [this, &menu] (const String& name, bool current,
                                             bool StudioView::* member, const char* key)
        {
            const auto safe = Component::SafePointer<StudioView> (this);
            menu.addItem (name, true, current, [safe, member, key]
            {
                if (safe == nullptr) return;
                safe.getComponent()->*member = !(safe.getComponent()->*member);
                auto& prefs = safe->engine.getPropertyStorage().getPropertiesFile();
                prefs.setValue (key, safe.getComponent()->*member);
                prefs.saveIfNeeded();
                safe->resized();
            });
        };
        addToggle ("Cycle", showTransportCycle, &StudioView::showTransportCycle, "studio.transportCycle");
        addToggle ("Metronome", showTransportClick, &StudioView::showTransportClick, "studio.transportClick");
        menu.addSeparator();
        menu.addSectionHeader ("LCD readouts");
        addToggle ("Elapsed time", showTransportElapsed, &StudioView::showTransportElapsed, "studio.transportElapsed");
        addToggle ("Tempo", showTransportTempo, &StudioView::showTransportTempo, "studio.transportTempo");
        addToggle ("Time signature", showTransportMeter, &StudioView::showTransportMeter, "studio.transportMeter");
        const auto safe = Component::SafePointer<StudioView> (this);
        menu.addSeparator();
        menu.addItem ("Restore default layout", [safe]
        {
            if (safe == nullptr) return;
            safe->showTransportCycle = safe->showTransportClick = true;
            safe->showTransportElapsed = safe->showTransportTempo = safe->showTransportMeter = true;
            auto& prefs = safe->engine.getPropertyStorage().getPropertiesFile();
            for (const auto* key : { "studio.transportCycle", "studio.transportClick",
                                     "studio.transportElapsed", "studio.transportTempo", "studio.transportMeter" })
                prefs.setValue (key, true);
            prefs.saveIfNeeded();
            safe->resized();
        });
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&transportOptions));
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
        menu.addItem ("Show Selected Track Inspector", true, inspectorOpen, [safe]
        {
            if (safe != nullptr) safe->setInspectorOpen (! safe->inspectorOpen);
        });
        menu.addItem ("Show Stage Reverb Return in Arrangement", true, showRoomReturnTrack, [safe]
        {
            if (safe != nullptr) safe->setRoomReturnTrackVisible (! safe->showRoomReturnTrack);
        });
        menu.addItem ("Classic Channel Workspace (temporary)", [safe]
        {
            if (safe == nullptr) return;
            safe->setInspectorOpen (false);
            safe->showWorkspace (Workspace::channel);
        });
        menu.addSeparator();
        PopupMenu diagnostics;
        diagnostics.addItem ("MIDI Monitor", [safe]
        {
            if (safe != nullptr) safe->monitorButton.onClick();
        });
        diagnostics.addItem ("Resource Inspector", [safe]
        {
            if (safe != nullptr) safe->resources.onOpen();
        });
        menu.addSubMenu ("Diagnostics", diagnostics);
        menu.addItem ("MIDI Keyboard Follows Selected Track", true, followSelectedMidi, [safe]
        {
            if (safe == nullptr) return;
            safe->followSelectedMidi = ! safe->followSelectedMidi;
            auto& properties = safe->engine.getPropertyStorage().getPropertiesFile();
            properties.setValue ("studio.midiFollowSelection", safe->followSelectedMidi);
            properties.saveIfNeeded();
            // The normal focus timer releases routing only after held notes
            // and sustain pedal have ended; don't cut off a note here.
        });
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

    // Native macOS menus use the existing action handlers; no duplicate DAW logic.
    enum MenuAction
    {
        mPicture = 2000, mNew = 2001, mOpen, mSave, mSaveAs, mMidiImport, mAudioImport,
        mMidiExport, mAudioExport, mAudioSettings, mPluginManager, mUndo, mRedo,
        mAddInstrument, mAddSynth, mAddSampler, mAddAudio, mAddMidiClip,
        mRegionActions, mRewind, mRecord, mFlashback, mCycle, mClick,
        mBrowser, mPiano, mStage, mMixer, mInspector, mEnvironments,
        mMidiMonitor, mResources, mRoomReturnArrangement, mScaleUp, mScaleDown, mScaleReset,
        mTransportCycle, mTransportClick, mTransportElapsed, mTransportTempo,
        mTransportMeter, mTransportReset, mCompact, mSplit, mExpanded,
        mPerformanceFull, mPerformanceArticulation, mPerformanceNone,
        mAboutStudio, mKeyboardHelp, mSettings, mAtelier
    };

    PopupMenu applicationMenu (int index)
    {
        PopupMenu menu;
        const bool project = edit != nullptr;
        const bool editable = canEditRegions();
        auto add = [&] (int id, const char* label, bool enabled = true, bool ticked = false)
        {
            menu.addItem (id, label, enabled, ticked);
        };
        switch (index)
        {
            case 0: // File
                add (mNew, "New Project..."); add (mOpen, "Open Project...");
                menu.addSeparator();
                add (mSave, "Save", saveProject.isEnabled());
                add (mSaveAs, "Save As...", saveAsProject.isEnabled());
                menu.addSeparator();
                add (mMidiImport, "Import MIDI...", importMidi.isEnabled());
                add (mAudioImport, "Import Audio...", importAudio.isEnabled());
                add (mMidiExport, "Export MIDI...", exportMidi.isEnabled());
                add (mAudioExport, "Export Audio...", exportWav.isEnabled());
                menu.addSeparator();
                add (mAudioSettings, "Audio Settings...");
                add (mSettings, "Studio Settings...");
                break;
            case 1: // Edit
                add (mUndo, "Undo", editable && edit->getUndoManager().canUndo());
                add (mRedo, "Redo", editable && edit->getUndoManager().canRedo());
                menu.addSeparator(); add (mRegionActions, "Region Actions...", regionActions.isEnabled());
                break;
            case 2: // Track
                add (mAddInstrument, "New Instrument Track", editable);
                add (mAddSynth, "New Studio Synth Track", canInsertPlugin());
                add (mAddSampler, "New Studio Sampler Track", canInsertPlugin());
                add (mAddAudio, "New Audio Track", editable);
                add (mAddMidiClip, "New MIDI Clip", addMidiClip.isEnabled());
                break;
            case 3: // Navigate
                add (mRewind, "Return to Start", project);
                menu.addSeparator();
                add (mBrowser, "Instrument Browser");
                add (mPiano, "Piano Roll");
                add (mStage, "Stage");
                add (mPicture, "Picture");
                add (mMixer, "Mixer");
                break;
            case 4: // Record
                add (mRecord, "Record", record.isEnabled());
                add (mFlashback, "Capture Recent MIDI", project);
                menu.addSeparator();
                add (mCycle, "Toggle Cycle", project);
                add (mClick, "Toggle Metronome", project);
                break;
            case 5: // Mix
                add (mMixer, "Show Mixer");
                add (mInspector, "Selected Track Inspector", project, inspectorOpen);
                add (mRoomReturnArrangement, "Show Stage Reverb Return in Arrangement", project, showRoomReturnTrack);
                menu.addSeparator();
                add (mPluginManager, "Plug-in Manager...");
                add (mAudioSettings, "Audio Settings...");
                break;
            case 6: // View
                add (mInspector, "Selected Track Inspector", project, inspectorOpen);
                add (mRoomReturnArrangement, "Show Stage Reverb Return in Arrangement", project, showRoomReturnTrack);
                add (mEnvironments, "Studio Environments...");
                add (mSettings, "Studio Settings...");
                add (mAtelier, "Cinematic Atelier Instrument...", project && workflowTrack() != nullptr);
                menu.addSeparator();
                add (mTransportCycle, "Transport: Cycle", true, showTransportCycle);
                add (mTransportClick, "Transport: Metronome", true, showTransportClick);
                add (mTransportElapsed, "Transport: Elapsed Time", true, showTransportElapsed);
                add (mTransportTempo, "Transport: Tempo", true, showTransportTempo);
                add (mTransportMeter, "Transport: Time Signature", true, showTransportMeter);
                add (mTransportReset, "Reset Transport Layout");
                menu.addSeparator();
                add (mScaleDown, "Decrease Interface Scale", studio::appearance.interfacePercent() > 100);
                add (mScaleUp, "Increase Interface Scale", studio::appearance.interfacePercent() < 175);
                add (mScaleReset, "Reset Appearance");
                break;
            case 7: // Window
                add (mCompact, "Compact Workspace", workspace != Workspace::filmScoring, dockMode == DockMode::compact);
                add (mSplit, "Split Workspace", true, dockMode == DockMode::split);
                add (mExpanded, "Expanded Workspace", workspace != Workspace::filmScoring, dockMode == DockMode::expanded);
                menu.addSeparator();
                add (mPerformanceFull, "Performance Panel", true, performanceMode.getSelectedId() == 1);
                add (mPerformanceArticulation, "Articulations Only", true, performanceMode.getSelectedId() == 2);
                add (mPerformanceNone, "Hide Performance Panel", true, performanceMode.getSelectedId() == 3);
                menu.addSeparator();
                add (mMidiMonitor, "MIDI Monitor");
                add (mResources, "Resource Inspector");
                break;
            case 8: // Help
                add (mAboutStudio, "About Studio...");
                add (mKeyboardHelp, "Keyboard Shortcuts...");
                break;
        }
        return menu;
    }

    void applicationMenuSelected (int action)
    {
        const auto invoke = [] (TextButton& button)
        {
            if (button.isEnabled() && button.onClick) button.onClick();
        };
        switch (action)
        {
            case mNew: invoke (newProject); break;
            case mOpen: invoke (openProject); break;
            case mSave: invoke (saveProject); break;
            case mSaveAs: invoke (saveAsProject); break;
            case mMidiImport: invoke (importMidi); break;
            case mAudioImport: invoke (importAudio); break;
            case mMidiExport: invoke (exportMidi); break;
            case mAudioExport: invoke (exportWav); break;
            case mAudioSettings: invoke (audioSettings); break;
            case mPluginManager: invoke (plugins); break;
            case mSettings: showStudioSettings(); break;
            case mAtelier:
                if (auto* target = workflowTrack())
                    if (auto* inst = studio::findInstrument (*target))
                        if (! showAtelierInstrument (*inst) && ! showNativeInstrument (*inst))
                            message.setText ("Choose a hosted Audio Unit instrument for Cinematic Atelier.",
                                             dontSendNotification);
                break;
            case mUndo: if (canEditRegions()) edit->undo(); break;
            case mRedo: if (canEditRegions()) edit->redo(); break;
            case mAddInstrument: if (canEditRegions()) addTrack ("Instrument"); break;
            case mAddSynth: invoke (addSynthTrack); break;
            case mAddSampler: invoke (addSamplerTrack); break;
            case mAddAudio: invoke (addAudio); break;
            case mAddMidiClip: invoke (addMidiClip); break;
            case mRegionActions: invoke (regionActions); break;
            case mRewind: invoke (rewind); break;
            case mRecord: invoke (record); break;
            case mFlashback: invoke (flashbackButton); break;
            case mCycle: invoke (cycleButton); break;
            case mClick: invoke (clickButton); break;
            case mBrowser: showWorkspace (Workspace::instruments); break;
            case mPiano: showWorkspace (Workspace::piano); break;
            case mStage: showWorkspace (Workspace::stage); break;
            case mPicture: showWorkspace (Workspace::filmScoring); break;
            case mMixer: showWorkspace (Workspace::mix); break;
            case mInspector: setInspectorOpen (! inspectorOpen); break;
            case mRoomReturnArrangement: setRoomReturnTrackVisible (! showRoomReturnTrack); break;
            case mEnvironments: environmentWelcome.open (false); break;
            case mMidiMonitor: invoke (monitorButton); break;
            case mResources: if (resources.onOpen) resources.onOpen(); break;
            case mAboutStudio:
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon,
                    "About Studio", "Studio 0.2 · Native macOS music production.\n"
                    "Orchestral Stage, native instruments, MIDI sequencing and mixing.", "OK");
                break;
            case mKeyboardHelp:
            {
                DialogWindow::LaunchOptions options;
                options.dialogTitle = "Studio Commands and Shortcuts";
                options.dialogBackgroundColour = studio::colours::canvas;
                options.escapeKeyTriggersCloseButton = true;
                options.useNativeTitleBar = true;
                options.resizable = true;
                options.content.setOwned (new StudioCommandReference());
                options.content->setSize (jmin (getWidth() - 30, 600), jmin (getHeight() - 60, 460));
                options.launchAsync();
                break;
            }
            case mScaleUp: applyAppearance (studio::appearance.adjacentInterfacePercent (1), studio::appearance.textPercent(), true); break;
            case mScaleDown: applyAppearance (studio::appearance.adjacentInterfacePercent (-1), studio::appearance.textPercent(), true); break;
            case mScaleReset: applyAppearance (100, 100, true); break;
            case mCompact: setDockMode (DockMode::compact); break;
            case mSplit: setDockMode (DockMode::split); break;
            case mExpanded: setDockMode (DockMode::expanded); break;
            case mPerformanceFull: performanceMode.setSelectedId (1, sendNotification); break;
            case mPerformanceArticulation: performanceMode.setSelectedId (2, sendNotification); break;
            case mPerformanceNone: performanceMode.setSelectedId (3, sendNotification); break;
            case mTransportCycle: showTransportCycle = ! showTransportCycle; break;
            case mTransportClick: showTransportClick = ! showTransportClick; break;
            case mTransportElapsed: showTransportElapsed = ! showTransportElapsed; break;
            case mTransportTempo: showTransportTempo = ! showTransportTempo; break;
            case mTransportMeter: showTransportMeter = ! showTransportMeter; break;
            case mTransportReset:
                showTransportCycle = showTransportClick = showTransportElapsed = true;
                showTransportTempo = showTransportMeter = true;
                break;
            default: break;
        }
        if (action >= mTransportCycle && action <= mTransportReset)
        {
            auto& prefs = engine.getPropertyStorage().getPropertiesFile();
            prefs.setValue ("studio.transportCycle", showTransportCycle);
            prefs.setValue ("studio.transportClick", showTransportClick);
            prefs.setValue ("studio.transportElapsed", showTransportElapsed);
            prefs.setValue ("studio.transportTempo", showTransportTempo);
            prefs.setValue ("studio.transportMeter", showTransportMeter);
            prefs.saveIfNeeded();
            resized();
        }
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
    bool inspectorOpen = false;
    bool showRoomReturnTrack = false; // Visual-only arrangement preference.
    int inspectorWidth = 270;
    int stereoScanTicks = 0;
    bool showTransportCycle = true, showTransportClick = true, showTransportElapsed = true;
    bool showTransportTempo = true, showTransportMeter = true;
    Rectangle<int> topBar, musicalDisplay, railBounds, ariaBanner, arrangementTools, workspacePanel, rulerRow, rulerTrackLabel;
    Component railContent, workspaceBody;
    TooltipWindow tooltips { this, 650 };
    class WorkspaceViewport final : public Viewport
    {
    public:
        std::function<void()> onScroll;
        void visibleAreaChanged (const Rectangle<int>&) override
        {
            if (onScroll) onScroll();
        }
    };

    Viewport railViewport;
    WorkspaceViewport workspaceViewport;
    bool collapsedDockLinks = false;
    TextButton viewButton { "View..." }, inspectorButton { "Inspector" }, projectMenu { "Project..." },
               trackMenu { "Tracks..." }, dockMenu { "Layout..." };
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
    ComboBox zoom;
    StringArray trackNames;
    TextButton arrangementZoomOut { "-" }, arrangementZoomIn { "+" };
    studio::RailButton instrumentsButton { "Instruments", studio::RailIcon::instruments },
                       pictureButton { "Picture", studio::RailIcon::picture },
                       pianoButton { "Piano Roll", studio::RailIcon::piano },
                       articulationsButton { "Articulations", studio::RailIcon::articulations },
                       stageButton { "Stage", studio::RailIcon::stage },
                       mixButton { "Mixer", studio::RailIcon::mixer };
    TextButton compactButton { "Compact" }, splitButton { "Split" },
               expandedButton { "Expanded" }, closeWorkspace { "Hide" };
    std::array<TextButton, 2> workspaceLinks;
    studio::TrackActions trackActions;
    studio::MidiPerformanceFocus midiFocus;
    bool followSelectedMidi = true;
    juce::String midiFocusStatus;
    studio::InstrumentBrowser browser { engine.getPluginManager().knownPluginList,
                                        &engine.getPropertyStorage().getPropertiesFile() };
    studio::ChannelInspector channelInspector { trackActions };
    studio::InspectorDrawer inspectorDrawer { trackActions };
    studio::StudioMixer mixer { trackActions };
    studio::StudioResources resources { engine.getDeviceManager() };
    studio::spatial::SpatialWorkspace spatialWorkspace;
    studio::film::Workspace filmWorkspace;
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
    TextButton transportOptions { "Customize..." }, flashbackButton { "Capture MIDI" };
    TextButton retryAriaButton { "Retry ARIA" };
    TextButton confirmAriaButton { "Confirm ARIA" };
    TextButton newProject { "New Project" }, openProject { "Open Project" },
               saveProject { "Save" }, saveAsProject { "Save As" },
               audioSettings { "Audio Settings" }, plugins { "Plugin Manager" },
               regionActions { "Edit Regions" }, trackOptions { "Track..." },
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
        return {{ { Workspace::instruments, &instrumentsButton }, { Workspace::piano, &pianoButton },
                  { Workspace::articulations, &articulationsButton }, { Workspace::stage, &stageButton },
                  { Workspace::filmScoring, &pictureButton }, { Workspace::mix, &mixButton } }};
    }

    std::array<DockItem, 3> dockItems()
    {
        return {{ { DockMode::compact, &compactButton }, { DockMode::split, &splitButton },
                  { DockMode::expanded, &expandedButton } }};
    }

    std::array<WorkspaceEntry, 2> contextLinks() const
    {
        switch (workspace)
        {
            case Workspace::stage:       return {{ { Workspace::rooms, "Rooms" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::rooms:       return {{ { Workspace::stage, "Stage" }, { Workspace::microphones, "Microphones" } }};
            case Workspace::microphones: return {{ { Workspace::stage, "Stage" }, { Workspace::rooms, "Rooms" } }};
            case Workspace::instrumentEditor: return {{ { Workspace::instruments, "Browser" }, { Workspace::channel, "Inspector" } }};
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
        // Mixer, Picture and docked Cinematic Atelier need the dock height
        // for their own console. The right Inspector remains available.
        const bool dockedAtelier = workspace == Workspace::instrumentEditor
            && instrumentWorkspace.isAtelier() && ! instrumentWorkspace.isFloating();
        if (workspace == Workspace::mix || workspace == Workspace::filmScoring || dockedAtelier)
        {
            performance.setVisible (false);
            articulationStrip.setVisible (false);
            return;
        }
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

    bool isWorkspaceDividerLocked() const
    {
        return workspace == Workspace::filmScoring
            || (workspace == Workspace::instrumentEditor && ! instrumentWorkspace.isFloating());
    }

    void layoutDock (Rectangle<int>& arrangement)
    {
        const auto& m = studio::appearance;
        const int available = arrangement.getHeight();
        const bool unifiedInstrumentHeader = workspace == Workspace::instrumentEditor
            && dockMode != DockMode::compact && ! instrumentWorkspace.isFloating()
            && instrumentWorkspace.getEditor() != nullptr;
        const int headerHeight = m.control (unifiedInstrumentHeader ? 42 : 34);
        const int dividerHeight = m.px (12);
        const bool fixedInstrument = workspace == Workspace::instrumentEditor && ! instrumentWorkspace.isFloating();
        workspaceDivider.setResizeLocked (isWorkspaceDividerLocked());
        // Revised MacBook screenshot: the fixed Instrument View is 425 points
        // including its header. On shorter MacBooks, dockRoom clamps this to
        // retain the arrangement; only the Instrument View is fixed.
        int requestedHeight = workspaceHeight;
        if (workspace == Workspace::filmScoring)
            requestedHeight = m.px (438);
        else if (fixedInstrument && dockMode == DockMode::split)
            requestedHeight = jmax (m.px (425), instrumentWorkspace.minimumContentHeight() + headerHeight);
        const auto room = m.dockRoom (available, requestedHeight, dockMode);
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
        if (unifiedInstrumentHeader)
        {
            // One compact bar: the real instrument selector and Float action
            // sit beside the existing Browser/Inspector/layout controls.
            // Remove the duplicate Instrument Editor label and second row.
            workspaceTitle.setVisible (false);
            workspaceTitle.setBounds ({});
            instrumentWorkspace.layoutDockedHeader (this, header.withWidth (jmin (header.getWidth(), m.span (450))));
        }
        else
        {
            instrumentWorkspace.layoutDockedHeader (nullptr, {});
            workspaceTitle.setVisible (workspace != Workspace::closed);
            workspaceTitle.setBounds (header);
        }
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
            if (filmWorkspace.isVisible()) return filmWorkspace.minimumContentHeight (width);
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
        for (Component* item : std::initializer_list<Component*> { &browser, &channelInspector, &mixer, &piano, &resources, &spatialWorkspace, &instrumentWorkspace, &filmWorkspace })
            if (item->isVisible()) item->setBounds (workspaceBody.getLocalBounds());
        if (dockedMonitor) monitor.setBounds (workspaceBody.getLocalBounds());
        if (articulationWindow == nullptr && articulation.isVisible()) articulation.setBounds (workspaceBody.getLocalBounds());
        fitDockedPicture();
    }

    void fitDockedPicture()
    {
        if (filmWorkspace.isVisible())
            filmWorkspace.setDockedVideoViewport (workspaceViewport.getScreenBounds());
        style.setNativeVideoTooltipAvoidance (filmWorkspace.dockedVideoScreenBounds());
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

    void setDockMode (DockMode mode)
    {
        if (workspace == Workspace::filmScoring && mode != DockMode::split) return;
        if (dockMode == mode) return;
        rememberWorkspaceScroll();
        dockMode = mode;
        // Switching from Expanded to Split must not restore a short dock that
        // clips the Mixer faders. Entering Mixer already enforces this minimum;
        // apply it to subsequent dock-mode changes as well.
        if (mode == DockMode::split && workspace == Workspace::mix)
            workspaceHeight = jmax (workspaceHeight,
                jmax (mixer.minimumContentHeight() + studio::appearance.control (34),
                       getHeight() * 57 / 100));
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
        if (next == Workspace::mix && changing)
        {
            // A click on Mixer is a change of task, not an attempt to stack a
            // third panel underneath the current instrument. Show an actual
            // console with usable faders and some arrangement above it.
            if (dockMode == DockMode::compact)
                dockMode = DockMode::split;
            if (dockMode == DockMode::split)
                workspaceHeight = jmax (workspaceHeight,
                    jmax (mixer.minimumContentHeight() + studio::appearance.control (34),
                           getHeight() * 57 / 100));
        }
        workspace = next;
        if (workspace == Workspace::filmScoring)
            dockMode = DockMode::split;
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
        const bool scoringPicture = workspace == Workspace::filmScoring;
        const bool showingArticulation = workspace == Workspace::articulations;
        const bool showingSpatial = workspace == Workspace::stage || workspace == Workspace::rooms
                                 || workspace == Workspace::microphones;
        String name;
        for (const auto& item : railItems())
        {
            const bool stageSubview = item.destination == Workspace::stage
                && (workspace == Workspace::rooms || workspace == Workspace::microphones
                    || workspace == Workspace::orchestraSeating);
            const bool instrumentSubview = item.destination == Workspace::instruments
                && workspace == Workspace::instrumentEditor;
            const bool selected = workspace == item.destination || stageSubview || instrumentSubview
                || (item.destination == Workspace::articulations && articulationWindow != nullptr);
            item.button->setToggleState (selected, dontSendNotification);
            if (workspace == item.destination)
                name = item.button->getButtonText();
        }
        // Contextual tools stay under their owning destination, not in a
        // second global navigation list.
        switch (workspace)
        {
            case Workspace::instruments:       name = "Instrument Browser"; break;
            case Workspace::instrumentEditor: name = "Instrument Editor"; break;
            case Workspace::stage:             name = "Orchestral Stage"; break;
            case Workspace::rooms:             name = "Rooms"; break;
            case Workspace::microphones:       name = "Virtual Microphones"; break;
            case Workspace::orchestraSeating:  name = "Arrangement Reference"; break;
            case Workspace::midi:              name = "MIDI Monitor"; break;
            case Workspace::resources:         name = "Resource Inspector"; break;
            default: break;
        }

        instrumentWorkspace.setVisible (workspace == Workspace::instrumentEditor && ! compact);
        browser.setVisible (browsing && ! compact);
        channelInspector.setVisible (inspecting && ! compact);
        resources.setVisible (checkingResources && ! compact);
        filmWorkspace.setVisible (scoringPicture && ! compact);
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
            item.button->setEnabled (! scoringPicture || item.mode == DockMode::split);
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

    void populateTrackEditMenu (PopupMenu& parent, te::AudioTrack& track, bool nested = true)
    {
        const auto id = track.itemID;
        PopupMenu subMenu;
        PopupMenu& tracks = nested ? subMenu : parent;
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
        if (! nested)
        {
            PopupMenu headerMenu;
            for (const auto& option : std::initializer_list<std::pair<const char*, const char*>> {
                     { "studioHeaderVolume", "Volume mini-fader" },
                     { "studioHeaderPan", "Pan mini-control" },
                     { "studioHeaderMeter", "Audio activity meter" } })
            {
                const Identifier key (option.first);
                const bool enabled = (bool) track.state.getProperty (key, false);
                headerMenu.addItem (option.second, true, enabled,
                    trackMenuAction (id, [key, enabled] (StudioView&, te::AudioTrack& target)
                    {
                        target.state.setProperty (key, ! enabled, &target.edit.getUndoManager());
                    }));
            }
            headerMenu.addSeparator();
            headerMenu.addItem ("Show All Mini Controls", trackMenuAction (id,
                [] (StudioView&, te::AudioTrack& target)
                {
                    auto& undo = target.edit.getUndoManager();
                    undo.beginNewTransaction ("Show track header mini controls");
                    for (const auto* property : { "studioHeaderVolume", "studioHeaderPan", "studioHeaderMeter" })
                        target.state.setProperty (Identifier (property), true, &undo);
                }));
            headerMenu.addItem ("Hide All Mini Controls", trackMenuAction (id,
                [] (StudioView&, te::AudioTrack& target)
                {
                    auto& undo = target.edit.getUndoManager();
                    undo.beginNewTransaction ("Hide track header mini controls");
                    for (const auto* property : { "studioHeaderVolume", "studioHeaderPan", "studioHeaderMeter" })
                        target.state.setProperty (Identifier (property), false, &undo);
                }));
            tracks.addSeparator();
            tracks.addSubMenu ("Track Header Controls", headerMenu);
        }
        if (nested)
            parent.addSubMenu ("Track · " + track.getName(), subMenu);
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
        // Picking another sound device can recreate Tracktion output groups.
        // Correct a newly selected mono group only while stopped, never in an audio callback.
        if (++stereoScanTicks >= 24)
        {
            stereoScanTicks = 0;
            if (edit == nullptr || (! edit->getTransport().isPlaying() && ! edit->getTransport().isRecording()))
                ensureStereoOutputGroup();
        }
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
        filmWorkspace.tick();
        // AVKit is embedded as a native view. Track its screen bounds for tooltips.
        fitDockedPicture();
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
        exportWav.setEnabled (canStartAudioExport() && ! filmWorkspace.isRendering());
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
                if (workspace != Workspace::closed && workspace != Workspace::articulations
                    && workspace != Workspace::mix && workspace != Workspace::filmScoring
                    && dockMode != DockMode::compact)
                    showWorkspace (Workspace::piano);
            }
            piano.setComparisonClips (selectedMidiClips);
        }
        else
            piano.setComparisonClips ({});

        auto* track = selectedTrack();
        StringArray names;
        const auto tracks = edit != nullptr ? te::getAudioTracks (*edit) : juce::Array<te::AudioTrack*>();
        if (edit != nullptr)
        {
            midiFocusStatus = midiFocus.follow (*edit, workflowTrack(), followSelectedMidi,
                ready && ! recording && ! recentlyStoppedRecording(), engine.getDeviceManager());
            for (auto* item : tracks)
                names.add (item->getName());
        }
        else
            midiFocusStatus.clear();
        if (names != trackNames)
            trackNames = std::move (names);
        trackOptions.setEnabled (ready && workflowTrack() != nullptr);
        trackDetails.setColour (Label::textColourId, recording ? studio::colours::record : studio::colours::secondary);
        const bool focusNeedsAttention = midiFocusStatus.contains ("record-armed")
            || midiFocusStatus.contains ("multi-track") || midiFocusStatus.contains ("Release ");
        trackDetails.setText (recording ? String ("Recording")
            : focusNeedsAttention ? midiFocusStatus
            : workflowTrack() != nullptr && midiFocus.isFollowing (workflowTrack()->itemID) ? midiFocusStatus
            : track == nullptr ? String (trackNames.size()) + " tracks"
            : EngineHelpers::isTrackArmed (*track) ? "Record armed"
            : EngineHelpers::trackHasInput (*track) ? "Input assigned" : "No input assigned", dontSendNotification);
        trackDetails.setTooltip (midiFocusStatus);

        if ((++workspaceTicks & 1u) == 0)
        {
            instrumentWorkspace.updateSelection (workflowTrack(), tracks,
                workspace == Workspace::instrumentEditor || instrumentWorkspace.isFloating());
            refreshInstrumentWorkflow (tracks, selectedMidiClips.size() == 1 ? selectedMidiClips.getFirst() : nullptr);
            auto* context = workflowTrack();
            if (performance.isVisible()) performance.refresh (context, transportReady);
            if (inspectorOpen)
            {
                const auto recovery = ariaReviewPending
                    ? (autoAriaRetryPending ? "ARIA recovery | Restore pending"
                                            : "ARIA recovery | Verify sound before saving")
                    : "ARIA recovery | No verification pending";
                const auto* clip = selectedMidiClips.size() == 1 ? selectedMidiClips.getFirst() : nullptr;
                const bool priorAtelier = inspectorDrawer.isShowingAtelier();
                inspectorDrawer.refresh (context, ready, canEditRegions(), recovery,
                    clip != nullptr && context != nullptr && clip->getAudioTrack() == context ? clip : nullptr);
                if (priorAtelier != inspectorDrawer.isShowingAtelier())
                    resized(); // Reclaim or reserve the appropriate sidebar width.
            }
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
        {
            edit->getTransport().stop (false, false);
            midiFocus.release (edit.get());
        }
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
        inspectorDrawer.setEdit (nullptr);
        mixer.setEdit (nullptr);
        resources.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
        filmWorkspace.setEdit (nullptr);
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
        inspectorDrawer.setEdit (edit.get());
        resources.setEdit (edit.get());
        spatialWorkspace.setEdit (edit.get());
        filmWorkspace.setEdit (edit.get());
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
        // The FX menu now lives in the left-hand track header. Retain the
        // actual plug-in chain but reclaim the old Tracktion footer column.
        view.showFooters = false;
        view.showMidiDevices = true;
        view.showStudioTrackFX = [this] (te::EditItemID id, Component& anchor)
        {
            if (edit == nullptr || busy) return;
            if (auto* track = trackById (id))
            {
                selection.selectOnly (track);
                showTrackMenu (id, studio::TrackMenu::inserts, anchor);
            }
        };
        view.showStudioTrackMenu = [this] (te::EditItemID id, Component& anchor)
        {
            if (edit == nullptr || busy) return;
            if (auto* track = trackById (id))
            {
                selection.selectOnly (track);
                PopupMenu menu;
                populateTrackEditMenu (menu, *track, false);
                menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
            }
        };
        view.showWaveDevices = true;
        // New and existing sessions keep their managed aux return in the
        // actual Edit and Mixer; only the arrangement defaults to hiding it.
        editor->setStudioRoomReturnVisible (showRoomReturnTrack);
        timeline.setViewedComponent (editor.get(), false);
        zoom.onChange();
        sizeEditor();
        title.setText ("Studio 0.2  /  " + file.getFileNameWithoutExtension(), dontSendNotification);
        message.setText ("Select an instrument track to play your MIDI keyboard. A only arms recording.", dontSendNotification);
        const auto lastInstrument = instrumentWorkspace.rememberedInstrument();
        if (! create && lastInstrument.isValid())
            if (auto savedPlugin = te::findPluginForID (*edit, lastInstrument); savedPlugin != nullptr)
            {
                const bool savedAtelier = instrumentWorkspace.rememberedAtelier() && ! ariaReviewPending;
                const bool restoreFloating = instrumentWorkspace.rememberedFloating();
                if (savedAtelier)
                {
                    // Restore a hosted instrument to its Inspector, preserving
                    // an explicitly floating full editor only when requested.
                    if (restoreFloating) showFullAtelierInstrument (*savedPlugin);
                    else showAtelierInstrument (*savedPlugin);
                }
                else if (showNativeInstrument (*savedPlugin))
                {
                    setDockMode (instrumentWorkspace.rememberedLayout());
                    if (restoreFloating) instrumentWorkspace.setFloating (true);
                }
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

    bool canStartAudioExport() const
    {
        return canTransferMidi() && ! ariaReviewPending;
    }

    void chooseWavExport()
    {
        if (! canStartAudioExport() || filmWorkspace.isRendering()) return;
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
                || safe->ariaReviewPending || safe->filmWorkspace.isRendering())
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
                || ! safe->canStartAudioExport() || safe->filmWorkspace.isRendering())
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

    bool showAtelierInstrument (te::Plugin& plug)
    {
        // For hosted Audio Units, the always-available performance panel is
        // the right Inspector. The lower workspace belongs to production
        // tools, except for native Studio Synth and Studio Sampler editors.
        if (edit == nullptr || &plug.edit != edit.get()
            || dynamic_cast<te::ExternalPlugin*> (&plug) == nullptr)
            return false;
        auto* target = dynamic_cast<te::AudioTrack*> (plug.getOwnerTrack());
        if (target == nullptr) return false;
        if (workflowTrack() != target) selection.selectOnly (target);
        if (workspace == Workspace::instrumentEditor && instrumentWorkspace.isAtelier()
            && ! instrumentWorkspace.isFloating())
            showWorkspace (Workspace::closed);
        setInspectorOpen (true);
        return true;
    }

    bool showFullAtelierInstrument (te::Plugin& plug)
    {
        if (edit == nullptr || &plug.edit != edit.get()
            || dynamic_cast<te::ExternalPlugin*> (&plug) == nullptr)
            return false;
        auto* target = dynamic_cast<te::AudioTrack*> (plug.getOwnerTrack());
        if (target == nullptr || ! instrumentWorkspace.openAtelier (plug, *target)) return false;
        if (workflowTrack() != target) selection.selectOnly (target);
        // Floating opens the same original instrument. Do not reveal a
        // second docked Atelier underneath the Inspector.
        instrumentWorkspace.setFloating (true);
        if (workspace == Workspace::instrumentEditor)
            showWorkspace (Workspace::closed);
        setInspectorOpen (true);
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
        trackActions.editTrackColour = [this] (te::EditItemID id, Component& anchor)
        {
            if (edit == nullptr || busy || edit->getTransport().isRecording()) return;
            auto* target = trackById (id);
            if (target == nullptr || target->isFrozen (te::Track::anyFreeze)) return;
            PopupMenu menu;
            constexpr std::array<std::pair<const char*, uint32_t>, 10> palette {{
                { "Strings blue", 0xff7ba9db }, { "Woodwinds green", 0xff75b8a1 },
                { "Brass gold", 0xffd2ad70 }, { "Percussion red", 0xffcf8194 },
                { "Keyboards violet", 0xffa08fd7 }, { "Guitars amber", 0xffc6956a },
                { "Voice pink", 0xffcf92bd }, { "Audio teal", 0xff55aeb9 },
                { "Neutral silver", 0xff9caabd }, { "Deep blue", 0xff5978af }
            }};
            for (const auto& [name, colour] : palette)
                menu.addItem (name, true, target->getColour() == Colour (colour),
                    [safe = Component::SafePointer<StudioView> (this), id, colour]
                    {
                        if (safe == nullptr || safe->edit == nullptr || safe->busy
                            || safe->edit->getTransport().isRecording()) return;
                        if (auto* track = safe->trackById (id))
                        {
                            safe->edit->getUndoManager().beginNewTransaction ("Set track colour");
                            track->setColour (Colour (colour));
                            safe->repaint();
                        }
                    });
            menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
        };
        trackActions.editTrackIcon = [this] (te::EditItemID id, Component& anchor)
        {
            if (edit == nullptr || busy || edit->getTransport().isRecording()) return;
            auto* target = trackById (id);
            if (target == nullptr || target->isFrozen (te::Track::anyFreeze)) return;
            PopupMenu menu;
            for (const auto& name : { "Strings", "Woodwind", "Brass", "Percussion",
                                      "Keyboard", "Guitar", "Bass", "Voice", "Synth", "Audio", "Other" })
            {
                const auto value = String (name);
                menu.addItem (value, true,
                    target->state.getProperty (Identifier ("studioTrackIcon")).toString() == value,
                    [safe = Component::SafePointer<StudioView> (this), id, value]
                    {
                        if (safe == nullptr || safe->edit == nullptr || safe->busy
                            || safe->edit->getTransport().isRecording()) return;
                        if (auto* track = safe->trackById (id))
                        {
                            safe->edit->getUndoManager().beginNewTransaction ("Set instrument icon");
                            track->state.setProperty (Identifier ("studioTrackIcon"),
                                value, &safe->edit->getUndoManager());
                        }
                    });
            }
            menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
        };
        trackActions.openInstrument = [this] (te::EditItemID id)
        {
            if (! busy)
                if (auto* track = trackById (id))
                    if (auto* instrument = studio::findInstrument (*track); instrument != nullptr
                        && ! showNativeInstrument (*instrument) && ! showAtelierInstrument (*instrument))
                        instrument->showWindowExplicitly();
        };
        trackActions.openFullInstrument = [this] (te::EditItemID id)
        {
            if (! busy)
                if (auto* selected = trackById (id))
                    if (auto* instrument = studio::findInstrument (*selected))
                        if (! showFullAtelierInstrument (*instrument))
                            showNativeInstrument (*instrument);
        };
        trackActions.openArticulations = [this] (te::EditItemID id)
        {
            if (auto* selected = trackById (id))
            {
                selection.selectOnly (selected);
                if (dockMode == DockMode::compact) setDockMode (DockMode::split);
                showWorkspace (Workspace::articulations);
            }
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
            PopupMenu pluginMenu;
            pluginMenu.addItem ("Open editor", trackMenuAction (id,
                [pluginId] (StudioView& studio, te::AudioTrack& target)
                {
                    for (auto* current : target.pluginList.getPlugins())
                        if (current->itemID == pluginId)
                        {
                            if (! studio.showNativeInstrument (*current)) current->showWindowExplicitly();
                            break;
                        }
                }));
            // Keep internal stage/room plumbing out of destructive plug-in
            // management: those processors have dedicated Stage controls.
            const auto type = plugin->getPluginType();
            const bool managed = type == "studioStagePanV1" || type == "studioRoomSendV1"
                || type == "studioWetRoomV1" || dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr
                || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr;
            if (! managed)
                pluginMenu.addItem ("Remove...", ! edit->getTransport().isPlaying(), false,
                    trackMenuAction (id, [pluginId] (StudioView& studio, te::AudioTrack& target)
                    {
                        if (studio.ariaReviewPending || studio.edit->getTransport().isPlaying()
                            || studio.edit->getTransport().isRecording()) return;
                        auto* candidate = [&]() -> te::Plugin*
                        {
                            for (auto* current : target.pluginList.getPlugins())
                                if (current->itemID == pluginId) return current;
                            return nullptr;
                        }();
                        if (candidate == nullptr || candidate == target.getVolumePlugin()
                            || candidate == target.getLevelMeterPlugin()) return;
                        const auto name = candidate->getName();
                        const auto generation = studio.projectGeneration;
                        const auto trackId = target.itemID;
                        const auto safe = Component::SafePointer<StudioView> (&studio);
                        AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon,
                            "Remove plug-in?",
                            "Remove " + name + " from " + target.getName()
                                + "? The track and MIDI/audio regions will remain. You can Undo.",
                            "Remove", "Cancel", &studio,
                            ModalCallbackFunction::create ([safe, generation, trackId, pluginId] (int result)
                            {
                                if (result != 1 || safe == nullptr
                                    || safe->projectGeneration != generation || ! safe->canEditRegions()
                                    || safe->ariaReviewPending || safe->edit->getTransport().isPlaying()) return;
                                auto* currentTrack = safe->trackById (trackId);
                                if (currentTrack == nullptr || currentTrack->isFrozen (te::Track::anyFreeze)) return;
                                for (auto* current : currentTrack->pluginList.getPlugins())
                                    if (current->itemID == pluginId
                                        && current != currentTrack->getVolumePlugin()
                                        && current != currentTrack->getLevelMeterPlugin())
                                    {
                                        auto& undo = safe->edit->getUndoManager();
                                        undo.beginNewTransaction ("Remove " + current->getName());
                                        current->deleteFromParent();
                                        safe->instrumentWorkspace.updateSelection (safe->selectedTrack(),
                                            te::getAudioTracks (*safe->edit), true);
                                        safe->message.setText ("Plug-in removed. Use Undo to restore it.",
                                                              dontSendNotification);
                                        break;
                                    }
                            }));
                    }));
            menu.addSubMenu (plugin->getName(), pluginMenu);
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
        PopupMenu appleEffects;
        for (const auto& description : engine.getPluginManager().knownPluginList.getTypes())
            if (description.pluginFormatName == "AudioUnit" && ! description.isInstrument
                && ! description.name.containsIgnoreCase ("ARIA Player"))
            {
                auto& destination = description.manufacturerName.containsIgnoreCase ("Apple")
                    ? appleEffects : effects;
                destination.addItem (description.name, trackMenuAction (id,
                    [description] (StudioView& studio, te::AudioTrack& target)
                    {
                        studio.startExternalPlugin (description, target.itemID, false, false);
                    }));
            }
        if (appleEffects.getNumItems() > 0)
            effects.addSubMenu ("Apple Audio Unit Effects", appleEffects);
        menu.addSubMenu ("Add Effect", effects, canInsertPlugin());
    }

    // JUCE can have both physical output channels active while Tracktion's
    // virtual default wave output still groups only one channel. That routes
    // instrument playback to the left output, but the JUCE hardware test still
    // plays in both ears. Expand only a mono *virtual group* when the active
    // physical device and Tracktion's description support a stereo pair.
    void ensureStereoOutputGroup()
    {
        auto& devices = engine.getDeviceManager();
        auto* hardware = devices.deviceManager.getCurrentAudioDevice();
        if (hardware == nullptr || hardware->getActiveOutputChannels().countNumberOfSetBits() < 2)
            return;
        auto* output = devices.getDefaultWaveOutDevice();
        if (output == nullptr || ! output->isEnabled() || output->getChannels().getNumChannels() != 1)
            return;
        if (devices.getPossibleChannelGroupsForDevice (*output, 2).size() < 2)
            return;
        Logger::writeToLog ("Studio: repairing mono Tracktion default output group to stereo for "
                            + output->getName());
        devices.setDeviceNumChannels (*output, 2);
    }

    void showStudioSettings()
    {
        DialogWindow::LaunchOptions options;
        options.dialogTitle = "Studio Settings";
        options.dialogBackgroundColour = studio::colours::canvas;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto panel = std::make_unique<StudioSettingsHub>();
        const Component::SafePointer<StudioView> safe (this);
        panel->openAudio = [safe] { if (safe != nullptr) safe->showAudioSettings(); };
        panel->openPlugins = [safe] { if (safe != nullptr) safe->showPlugins(); };
        panel->openMidi = [safe]
        {
            if (safe != nullptr && safe->monitorButton.onClick)
                safe->monitorButton.onClick();
        };
        panel->restoreAppearance = [safe]
        {
            if (safe != nullptr) safe->applyAppearance (100, 100, true);
        };
        options.content.setOwned (panel.release());
        options.content->setSize (jmin (getWidth() - 30, 510), 310);
        options.launchAsync();
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
    // Regression: the shared return remains a real Tracktion audio/aux
    // track and a dedicated Mixer strip, but is absent from the arrangement.
    auto* wetReturn = roomReturnTrack (*edit);
    if (wetReturn == nullptr || ! isRoomReturn (*wetReturn))
        return fail ("load the existing managed room return");
    const auto returnID = wetReturn->itemID;
    const auto pluginCount = wetReturn->pluginList.size();
    const int fullTrackCount = te::getAudioTracks (*edit).size();
    const auto waitForArrangementRows = [this] (int target)
    {
        for (int attempt = 0; attempt < 24 && editor->getVisibleTrackCount() != target; ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (35);
        return editor->getVisibleTrackCount() == target;
    };
    setRoomReturnTrackVisible (false, false);
    if (! waitForArrangementRows (fullTrackCount - 1))
        return fail ("managed return is hidden from arrangement by default");
    mixer.refresh (te::getAudioTracks (*edit), legacy, true, true);
    if (! mixer.hasChannelForTrack (returnID))
        return fail ("hidden arrangement return still has a dedicated Mixer channel");
    setRoomReturnTrackVisible (true, false);
    if (! waitForArrangementRows (fullTrackCount))
        return fail ("View menu can reveal the managed return for inspection");
    setRoomReturnTrackVisible (false, false);
    if (! waitForArrangementRows (fullTrackCount - 1)
        || roomReturnTrack (*edit) != wetReturn
        || wetReturn->pluginList.size() != pluginCount
        || ! hasRoomSend (*legacy))
        return fail ("hiding and revealing return cannot change the aux track or sends");
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
        || hasRoomSend (*unplaced) || studio::seating::read (edit->state) != archived || roomReturnTrack (*edit) == nullptr
        || ! waitForArrangementRows (te::getAudioTracks (*edit).size() - 1))
        return fail ("restore positions, acoustic send, hidden room return and unmodified reference");
    mixer.refresh (te::getAudioTracks (*edit), legacy, true, true);
    if (! mixer.hasChannelForTrack (roomReturnTrack (*edit)->itemID))
        return fail ("reopened project keeps its Stage Reverb Mixer return");
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

int StudioView::runFilmScoringReview (const File& output)
{
    stopTimer();
    environmentWelcome.dismiss();
    const ScopedValueSetter<studio::AppearanceMetrics> appearanceScope (studio::appearance, studio::appearance);
    applyAppearance (100, 100, false);
    setSize (1280, 800);
   #if JUCE_MAC
    DocumentWindow pictureReview ("Studio Picture review", studio::colours::canvas, 0);
    pictureReview.setUsingNativeTitleBar (true);
    pictureReview.setContentNonOwned (this, true);
    pictureReview.centreWithSize (1280, 800);
    pictureReview.setVisible (true);
   #endif
    const auto fail = [] (const char* message)
    {
        std::fprintf (stderr, "Film Scoring workspace review failed: %s\n", message);
        return 1;
    };
    TemporaryFile fixture (".tracktionedit");
    loadProject (fixture.getFile(), true);
    if (! edit) return fail ("create film scoring project");
    const auto tracks = te::getAudioTracks (*edit);
    const auto trackCount = tracks.size();
    auto* score = tracks.getFirst();
    if (score == nullptr) return fail ("create a score track");
    score->setName ("Opening score");
    if (score->insertMIDIClip ("Opening theme",
            { te::TimePosition(), te::TimePosition::fromSeconds (10) }, nullptr) == nullptr)
        return fail ("create an arrangement region");
    setDockMode (DockMode::expanded);
    setInspectorOpen (true, false);
    pictureButton.onClick();
    if (! filmWorkspace.isVisible() || ! workspaceViewport.isVisible()
        || workspacePanel.getRight() != inspectorDrawer.getX() || timeline.getBounds().isEmpty()
        || workspacePanel.intersects (timeline.getBounds()) || ! isWorkspaceDividerLocked()
        || performance.isVisible() || articulationStrip.isVisible())
        return fail ("Picture shares the lower dock with a separate timeline and Inspector column");

    studio::film::Project picture;
    picture.rate = studio::film::Rate::fps2997DF;
    picture.pictureStartFrame = 6;
    if (! picture.addSpot ({ "test-1", "1M1", "Door closes", "Sync exactly", 12 })
        || ! picture.addSpot ({ "test-2", "1M1", "Music ends", "Hold end", 45 }))
        return fail ("create picture-anchored hits");
    if (! picture.addCue ({ "1M1", "Opening score", "Transition", 12, 45 })
        || ! picture.addCue ({ "1M2", "Next sequence", "", 54, std::nullopt }))
        return fail ("create frame-anchored film cues");
    studio::film::store (edit->state, picture, &edit->getUndoManager());
    filmWorkspace.setEdit (nullptr);
    filmWorkspace.setEdit (edit.get());
    if (filmWorkspace.spotCount() != 2 || filmWorkspace.snapshot().cues.size() != 2
        || filmWorkspace.snapshot().pictureStartFrame != picture.pictureStartFrame
        || filmWorkspace.snapshot().rate != studio::film::Rate::fps2997DF)
        return fail ("dock reads versioned picture metadata");
    if (te::getAudioTracks (*edit).size() != trackCount)
        return fail ("spotting modified audio tracks");
    if (! edit->getUndoManager().undo()) return fail ("undo spotting metadata");
    filmWorkspace.tick();
    if (! filmWorkspace.snapshot().cues.empty() || ! filmWorkspace.snapshot().spots.empty())
        return fail ("workspace did not reconcile project Undo");
    if (! edit->getUndoManager().redo()) return fail ("redo spotting metadata");
    filmWorkspace.tick();
    if (filmWorkspace.snapshot().cues.size() != 2 || filmWorkspace.snapshot().spots.size() != 2)
        return fail ("workspace did not reconcile project Redo");

   #if JUCE_MAC
    const auto videoFixture = SystemStats::getEnvironmentVariable ("STUDIO_FILM_TEST_VIDEO", {});
    if (videoFixture.isNotEmpty() && ! filmWorkspace.testImportAndResync (File (videoFixture)))
        return fail ("import synthetic picture, float, redock and seek the native AVKit player");
   #endif

    const auto expected = edit->state.getChildWithName ("STUDIO_FILM_SCORING").createXml();
    const auto trackID = te::getAudioTracks (*edit).getFirst()->itemID.toString().toStdString();
    studio::film::saveStemAssignments (edit->state, {{ trackID, studio::film::StemFamily::strings }},
                                      &edit->getUndoManager());
    {
        te::EditFileOperations project (*edit);
        if (! project.saveTempVersion (true))
            return fail ("write recovery snapshot with Film Scoring changes");
        const auto recoveryXml = XmlDocument::parse (project.getTempVersionFile());
        project.deleteTempVersion();
        const auto* recoveryPicture = recoveryXml != nullptr
            ? recoveryXml->getChildByName ("STUDIO_FILM_SCORING") : nullptr;
        if (recoveryPicture == nullptr || ! recoveryPicture->isEquivalentTo (expected.get(), true))
            return fail ("recovery snapshot preserves Film Scoring state");
    }
    const auto saved = std::make_shared<std::optional<bool>>();
    save ([saved] (bool ok) { *saved = ok; });
    for (int i = 0; i < 100 && ! saved->has_value(); ++i)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    if (! saved->value_or (false))
        return fail ("save project with film scoring metadata");
    const auto generation = projectGeneration;
    loadProject (fixture.getFile(), false);
    if (projectGeneration != generation + 1 || edit == nullptr)
        return fail ("reopen film scoring project");
    const auto restored = edit->state.getChildWithName ("STUDIO_FILM_SCORING").createXml();
    const auto assignments = studio::film::readStemAssignments (edit->state);
    if (restored == nullptr || ! restored->isEquivalentTo (expected.get(), true)
        || assignments.size() != 1 || assignments.front().trackId != trackID
        || assignments.front().family != studio::film::StemFamily::strings)
        return fail ("restore picture scoring metadata and track stem family");
    if (filmWorkspace.spotCount() != 2 || filmWorkspace.snapshot().find ("test-1") == nullptr
        || filmWorkspace.snapshot().find ("test-1")->pictureFrame != 12
        || filmWorkspace.snapshot().findCue ("1M1") == nullptr
        || filmWorkspace.snapshot().findCue ("1M1")->endPictureFrame != 45
        || te::getAudioTracks (*edit).size() != trackCount)
        return fail ("restore exact source frame anchors without altering tracks");
    showWorkspace (Workspace::filmScoring);
    auto* restoredTrack = te::getAudioTracks (*edit).getFirst();
    if (restoredTrack == nullptr || restoredTrack->getClips().isEmpty())
        return fail ("restore the live timeline alongside picture");
    auto* restoredClip = restoredTrack->getClips().getFirst();
    selection.selectOnly (restoredClip);
    edit->dispatchPendingUpdatesSynchronously();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    timerCallback();
    if (workspace != Workspace::filmScoring || ! filmWorkspace.isVisible())
        return fail ("selecting a MIDI region does not close Picture");
    selection.selectOnly (restoredTrack);
    timerCallback();
    if (output != File())
    {
        if (output.createDirectory().failed()) return fail ("create screenshot folder");
        for (const auto width : { 1280, 1536 })
        {
            setSize (width, width == 1280 ? 800 : 960);
            FileOutputStream stream (output.getChildFile ("film-scoring-" + String (width) + ".png"));
            if (! stream.openedOk() || ! PNGImageFormat().writeImageToStream (
                    createComponentSnapshot (getLocalBounds()), stream))
                return fail ("render workspace screenshot");
        }
    }
    std::puts ("Film Scoring workspace, frame anchors, project isolation and reload passed.");
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
        inspectorDrawer.setEdit (nullptr);
        spatialWorkspace.setEdit (nullptr);
        filmWorkspace.setEdit (nullptr);
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
    // Each macOS menu category must expose at least one real command; keep
    // menu generation platform-neutral so Linux CI can verify its structure.
    for (int category = 0; category < 9; ++category)
        if (applicationMenu (category).getNumItems() == 0)
            return fail ("native application menu has no commands");
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
    // A compact composer rail must expose only distinct destinations.
    // Stage sub-pages, instrument detail, and diagnostics remain reachable
    // through their owning workspace or View, never a duplicate More menu.
    if (railContent.getNumChildComponents() != (int) railItems().size())
        return fail ("composer navigation contains redundant rail destinations");
    setDockMode (DockMode::split);
    showWorkspace (Workspace::stage);
    if (! stageButton.getToggleState() || ! spatialWorkspace.isVisible()
        || contextLinks()[0].destination != Workspace::rooms
        || contextLinks()[1].destination != Workspace::microphones)
        return fail ("Stage exposes Rooms and Microphones");
    for (auto target : { Workspace::rooms, Workspace::microphones })
    {
        showWorkspace (target);
        if (! stageButton.getToggleState() || ! spatialWorkspace.isVisible()
            || workspaceTitle.getText().isEmpty())
            return fail ("Stage subview remains in Stage navigation");
    }
   #if JUCE_MAC
    showWorkspace (Workspace::stage);
    if (! spatialWorkspace.onOpenSeating)
        return fail ("Arrangement Reference remains available from Stage");
    spatialWorkspace.onOpenSeating();
    if (! stageButton.getToggleState() || ! seatingWorkspace.isVisible())
        return fail ("Arrangement Reference stays within Stage");
   #endif
    showWorkspace (Workspace::instrumentEditor);
    if (! instrumentsButton.getToggleState() || workspaceTitle.getText() != "Instrument Editor")
        return fail ("Instrument Editor stays under Instruments");
    if (resources.onOpen)
        resources.onOpen();
    if (! resources.isVisible() || workspaceTitle.getText() != "Resource Inspector"
        || stageButton.getToggleState())
        return fail ("Resource Inspector remains available from View");
    monitorButton.onClick();
    if (! monitor.isVisible() || workspaceTitle.getText() != "MIDI Monitor")
        return fail ("MIDI Monitor remains available from View");
    showWorkspace (Workspace::closed);
    for (auto scale : studio::AppearanceMetrics::interfaceOptions)
        for (auto text : studio::AppearanceMetrics::textOptions)
        {
            applyAppearance (scale, text, false);
            for (const auto size : { Point<int> (1024, 640), Point<int> (1280, 800), Point<int> (1536, 960) })
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
                setInspectorOpen (true, false);
                showWorkspace (Workspace::mix);
                setDockMode (DockMode::compact);
                if (! workspaceSummary.isVisible() || mixer.isVisible()) return fail ("compact Mixer");
                setDockMode (DockMode::split);
                const int contentRight = inspectorOpen ? inspectorDrawer.getX() : getWidth();
                if (! workspaceViewport.isVisible() || ! mixer.isVisible() || ruler.isEmpty()
                    || timeline.getRight() != contentRight || workspacePanel.getRight() != contentRight
                    || ! rulerRow.contains (ruler) || timeline.getBounds().intersects (workspacePanel))
                    return fail ("split arrangement and Mixer");
                if (performance.isVisible() || articulationStrip.isVisible())
                    return fail ("Mixer cannot share lower space with instrument or articulation panels");
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
                if (workspaceViewport.getHeight() == 0 || mixer.getWidth() != workspaceBody.getWidth()
                    || mixer.getWidth() < workspaceViewport.getWidth() - workspaceViewport.getScrollBarThickness())
                    return fail ("expanded mixer uses the available workspace width");
                setInspectorOpen (false, false);
                if (timeline.getRight() != getWidth() || workspacePanel.getRight() != getWidth()
                    || mixer.getWidth() <= 2 * studio::appearance.mixerStripWidth (mixer.getWidth()))
                    return fail ("closing Inspector restores the full arrangement and Mixer widths");
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
    if (performance.isVisible() || articulationStrip.isVisible())
        return fail ("expanded Mixer must be the only visible lower workspace");
    if (! capture ("mixer-1280x800.png")) return fail ("write Mixer screenshot");
    // New M14 dialog snapshots come from real JUCE components at native
    // logical sizes; these are not concept mockups or older M13 captures.
    if (writeImages)
    {
        StudioCommandReference reference;
        reference.setSize (580, 440);
        if (! capture ("m14-command-reference-580x440.png", &reference))
            return fail ("capture M14 searchable command reference");
        StudioSettingsHub settings;
        settings.setSize (510, 310);
        if (! capture ("m14-settings-510x310.png", &settings))
            return fail ("capture M14 Studio Settings panel");
    }
    // Visual-regression capture for the small projects that previously left a
    // distracting empty gulf between the last channel and pinned Master.
    Array<te::AudioTrack*> smallMixer { tracks[0], tracks[1], tracks[2] };
    mixer.refresh (smallMixer, tracks[1], true, true);
    if (! capture ("mixer-three-tracks-1280x800.png")) return fail ("write three-track Mixer screenshot");
    mixer.refresh (tracks, selectedTrack(), true, true);
    setDockMode (DockMode::split);
    mixer.refresh (tracks, selectedTrack(), true, true);
    if (! capture ("mixer-split-1280x800.png")) return fail ("write compact Mixer screenshot");
    if (workspacePanel.getHeight() < mixer.minimumContentHeight()
        || performance.isVisible() || articulationStrip.isVisible())
        return fail ("Mixer-focused Split must leave the console fully usable");
    if (! capture ("mixer-focused-1280x800.png"))
        return fail ("write Mixer-first arrangement screenshot");
    setDockMode (DockMode::expanded);
    // The Inspector owns a right-hand column instead of covering either
    // arrangement or lower workspace. Closing returns the full available width.
    setInspectorOpen (false, false);
    const auto unclippedRuler = ruler;
    const int timelineRightBefore = timeline.getRight();
    setInspectorOpen (true, false);
    timerCallback();
    timerCallback();
    if (! inspectorDrawer.isVisible() || inspectorDrawer.getWidth() < studio::appearance.span (225)
        || inspectorDrawer.inspectedTrack() != tracks[1]->itemID
        || timeline.getRight() > inspectorDrawer.getX()
        || workspacePanel.getRight() > inspectorDrawer.getX()
        || timeline.getRight() >= timelineRightBefore || ruler == unclippedRuler)
        return fail ("right Inspector owns a column without covering timeline or lower workspace");
    if (! capture ("right-inspector-1280x800.png")) return fail ("write right Inspector screenshot");
    inspectorDrawer.setDetailsExpanded (true);
    if (! inspectorDrawer.areDetailsExpanded())
        return fail ("Inspector advanced track settings expand");
    if (! capture ("right-inspector-track-settings-1280x800.png"))
        return fail ("write expanded Inspector settings screenshot");
    inspectorDrawer.setDetailsExpanded (false);
    selection.selectOnly (tracks[2]);
    timerCallback();
    timerCallback();
    if (inspectorDrawer.inspectedTrack() != tracks[2]->itemID)
        return fail ("right Inspector follows arrangement selection");
    setSize (1024, 640);
    if (! inspectorDrawer.isVisible() || ! getLocalBounds().contains (inspectorDrawer.getBounds()))
        return fail ("right Inspector fits compact MacBook");
    if (! capture ("right-inspector-1024x640.png")) return fail ("write compact Inspector screenshot");
    setSize (1280, 800);
    setInspectorOpen (false, false);
    if (inspectorDrawer.isVisible() || ! inspectorDrawer.getBounds().isEmpty()
        || ruler != unclippedRuler || timeline.getRight() != timelineRightBefore)
        return fail ("hiding Inspector restores full-width arrangement");
    setSize (1280, 800);
    trackActions.selectTrack (tracks[1]->itemID);
    showWorkspace (Workspace::mix);

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
    showWorkspace (Workspace::instruments);
    timerCallback();
    if (! browser.isVisible() || ! capture ("m14-instrument-browser-1280x800.png"))
        return fail ("capture M14 instrument browser");
    showWorkspace (Workspace::closed);
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

// Native macOS system menu bar, with a safe pointer to the current window.
class StudioApplicationMenu final : public MenuBarModel
{
public:
    Component::SafePointer<StudioView> target;
    StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Track", "Navigate", "Record", "Mix", "View", "Window", "Help" };
    }
    PopupMenu getMenuForIndex (int index, const String&) override
    {
        return target != nullptr ? target->applicationMenu (index) : PopupMenu();
    }
    void menuItemSelected (int id, int) override
    {
        if (target != nullptr) target->applicationMenuSelected (id);
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
        if (commandLine == "--studio-test-film-scoring-workspace")
        {
            StudioView view;
            const auto path = SystemStats::getEnvironmentVariable ("STUDIO_FILM_SCORING_SCREENSHOTS", {});
            setApplicationReturnValue (view.runFilmScoringReview (path.isEmpty() ? File() : File (path)));
            quit();
            return;
        }
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
       #if JUCE_MAC
        appMenu.target = static_cast<StudioView*> (window->getContentComponent());
        MenuBarModel::setMacMainMenu (&appMenu);
       #endif
    }
    void shutdown() override
    {
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
        appMenu.target = nullptr;
       #endif
        window.reset();
    }
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            static_cast<StudioView*> (window->getContentComponent())->requestQuit();
    }

private:
    StudioApplicationMenu appMenu;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (StudioApplication)
