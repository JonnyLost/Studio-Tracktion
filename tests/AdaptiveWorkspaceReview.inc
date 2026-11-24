// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise StudioView's production selection, header and workspace entry points.
int StudioView::runAdaptiveWorkspaceReview (const File& output)
{
    using studio::PerformanceMode;
    using namespace studio::synth;
    stopTimer();
    environmentWelcome.dismiss();
    applyAppearance (100, 100, false);
    setSize (1440, 900);
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Adaptive workspace: %s\n", reason);
        return 1;
    };
    auto& preferences = engine.getPropertyStorage().getPropertiesFile();
    const auto oldMode = preferences.getValue ("studio.performanceMode");
    const ScopeGuard restoreMode { [&]
    {
        if (oldMode.isEmpty()) preferences.removeValue ("studio.performanceMode");
        else preferences.setValue ("studio.performanceMode", oldMode);
    } };

    const auto dir = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Studio M9 Chamber Sketch", {}, false);
    if (dir.createDirectory().failed()) return fail ("create review directory");
    const ScopeGuard cleanup { [&] { dir.deleteRecursively(); } };
    auto& devices = engine.getDeviceManager();
    devices.setCpuLimitBeforeMuting (1000);
    auto& hostAudio = devices.getHostedAudioDeviceInterface();
    constexpr int sampleRate = 48000, blockSize = 256;
    hostAudio.initialise ({ .sampleRate = sampleRate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
    hostAudio.prepareToPlay (sampleRate, blockSize);
    devices.dispatchPendingUpdates();
    MessageManager::getInstance()->runDispatchLoopUntil (85);

    const auto project = dir.getChildFile ("Chamber Sketch.tracktionedit");
    loadProject (project, true);
    if (edit == nullptr || editor == nullptr) return fail ("create review project");
    const auto mode = [&] (PerformanceMode requested)
    {
        performanceMode.setSelectedId ((int) requested, dontSendNotification);
        applyPerformanceMode();
    };
    const auto refresh = [&]
    {
        edit->dispatchPendingUpdatesSynchronously();
        workspaceTicks = 1;
        timerCallback();
    };
    const auto snapshot = [&] (Component& component, const char* name)
    {
        if (output == File()) return true;
        MessageManager::getInstance()->runDispatchLoopUntil (40);
        if (output.createDirectory().failed()) return false;
        const auto path = output.getChildFile (String (name) + ".png");
        FileOutputStream stream (path);
        return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
            && PNGImageFormat().writeImageToStream (component.createComponentSnapshot (component.getLocalBounds()), stream);
    };

    auto* keys = te::getAudioTracks (*edit).getFirst();
    auto* mallets = addTrack ("Bells");
    auto* sample = addTrack ("Sampler");
    auto* violin = addTrack ("Solo Violin");
    auto* horn = addTrack ("French Horn");
    auto* external = addTrack ("Audio Unit Fixture");
    if (keys == nullptr || mallets == nullptr || sample == nullptr || violin == nullptr || horn == nullptr || external == nullptr)
        return fail ("create instrument tracks");
    keys->setName ("Analog Keys");
    mallets->setName ("FM Bells");
    sample->setName ("Sample Keys");
    violin->setName ("Solo Violin");
    horn->setName ("French Horn");
    external->setName ("Unavailable Audio Unit");

    const auto& sounds = catalog().sounds;
    const auto presetFor = [&] (Family family) -> const FactorySound&
    {
        return *std::find_if (sounds.begin(), sounds.end(), [family] (const auto& sound) { return sound.family == family; });
    };
    const auto install = [&] (te::AudioTrack& track, const String& type, const PluginDescription& desc = {})
    {
        auto result = edit->getPluginCache().createNewPlugin (type, desc);
        if (result != nullptr) track.pluginList.insertPlugin (result, 0, nullptr);
        return result;
    };
    auto synthOwner = install (*keys, Plugin::xmlTypeName);
    auto bellsOwner = install (*mallets, Plugin::xmlTypeName);
    auto samplerOwner = install (*sample, studio::sampler::Plugin::xmlTypeName);
    auto* synth = dynamic_cast<Plugin*> (synthOwner.get());
    auto* bells = dynamic_cast<Plugin*> (bellsOwner.get());
    auto* sampler = dynamic_cast<studio::sampler::Plugin*> (samplerOwner.get());
    if (! synth || ! bells || ! sampler || synth->selectFactory (presetFor (Family::analog)).failed()
        || bells->selectFactory (presetFor (Family::fm)).failed())
        return fail ("load native presets");

    const auto media = dir.getChildFile ("Sine Keys.wav");
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> stream = media.createOutputStream();
        auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
                                             .withNumChannels (1).withBitsPerSample (16));
        AudioBuffer<float> signal (1, 24000);
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample (0, i, .32f * std::sin (i * .03425f) * std::exp (-i / 11000.f));
        if (! writer || ! writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()))
            return fail ("write synthetic sampler source");
    }
    if (sampler->loadSample (media).failed()) return fail ("load synthetic sampler source");

    // The orchestral screens exercise verified-for-this-test MIDI mappings.
    // The unavailable descriptors declare fixtures, without any installed library.
    PluginDescription missing;
    missing.name = "Orchestral MIDI Fixture";
    missing.pluginFormatName = "AudioUnit";
    missing.fileOrIdentifier = "AudioUnit:MusicDevice/test,zzzz,m9st";
    missing.isInstrument = true;
    if (! install (*violin, te::ExternalPlugin::xmlTypeName, missing)
        || ! install (*horn, te::ExternalPlugin::xmlTypeName, missing))
        return fail ("create orchestral AU descriptors");
    missing.name = "Hosted Instrument Fixture";
    if (! install (*external, te::ExternalPlugin::xmlTypeName, missing))
        return fail ("create unavailable AU descriptor");

    const auto insert = [] (te::AudioTrack& track, const char* name, double start, double end, int channel)
    {
        auto clip = track.insertMIDIClip (name, { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (end) }, nullptr);
        if (clip != nullptr) clip->setMidiChannel (te::MidiChannel ((uint8) channel));
        return clip;
    };
    const auto theme = [] (te::MidiClip& clip, int root)
    {
        constexpr int melody[] { 0, 3, 7, 10, 7, 3, 2, 5, 7, 12, 10, 5 };
        for (int i = 0; i < 12; ++i)
            clip.getSequence().addNote (root + melody[i], te::BeatPosition::fromBeats (i * .75 + .25),
                                        te::BeatDuration::fromBeats (.47), 88, 0, nullptr);
    };

    auto violinClip = insert (*violin, "String phrase", 2, 10, 3);
    auto hornClip = insert (*horn, "Horn response", 5, 13, 5);
    if (! violinClip || ! hornClip) return fail ("create articulation review regions");
    theme (*violinClip, 59);
    theme (*hornClip, 57);
    for (const auto& [track, name, offset, root] : std::initializer_list<std::tuple<te::AudioTrack*, const char*, double, int>> {
             { keys, "Opening chords", 0.0, 51 }, { mallets, "Counterline", 1.0, 72 }, { sample, "Pulse", 4.0, 62 } })
        for (int i = 0; i < 2; ++i)
        {
            auto clip = insert (*track, name, offset + i * 9.0, offset + i * 9.0 + 7.0, 1);
            if (! clip) return fail ("create native instrument region");
            theme (*clip, root);
        }

    using studio::ArticulationMessage;
    if (! articulation.addReviewMappings (*violin, {
          { "v-leg", "Legato", ArticulationMessage::controller, 3, 20, 0 },
          { "v-stacc", "Staccato", ArticulationMessage::keyswitch, 3, 24, 100 },
          { "v-ten", "Tenuto", ArticulationMessage::keyswitch, 3, 25, 100 },
          { "v-pizz", "Pizzicato", ArticulationMessage::keyswitch, 3, 26, 100 },
          { "v-trem", "Tremolo", ArticulationMessage::keyswitch, 3, 27, 100 } })
        || ! articulation.addReviewMappings (*horn, {
          { "h-sustain", "Sustain", ArticulationMessage::controller, 5, 21, 0 },
          { "h-marcato", "Marcato", ArticulationMessage::keyswitch, 5, 32, 100 },
          { "h-fall", "Fall", ArticulationMessage::keyswitch, 5, 33, 100 } }))
        return fail ("create fixture mappings through the palette");

    if (! studio::setCycle (*edit, { 2_tp, 10_tp })
        || studio::addCueMarker (*edit, te::TimePosition::fromSeconds (0), "Opening") == nullptr
        || studio::addCueMarker (*edit, te::TimePosition::fromSeconds (9), "Response") == nullptr)
        return fail ("configure transport review markers");
    edit->getTransport().setPosition (2s);
    edit->clickTrackEnabled = true;
    MessageManager::getInstance()->runDispatchLoopUntil (130);
    mode (PerformanceMode::hidden);
    selection.selectOnly (keys);
    refresh();
    // Tracktion populates the arrangement asynchronously after the edit changes.
    const auto arrangementReady = [&]
    {
        return editor->getVisibleTrackCount() >= te::getAudioTracks (*edit).size() && hasArrangementRegion (*editor);
    };
    for (int attempt = 0; attempt < 25 && ! arrangementReady(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    if (! arrangementReady()) return fail ("arrangement tracks and regions must be visible in the header capture");
    if (! snapshot (*this, "01-header")) return fail ("capture the header");
    if (! cycleButton.getToggleState() || ! clickButton.getToggleState()) return fail ("transport shows actual cycle and click state");
    cycleButton.onClick();
    clickButton.onClick();
    if (edit->getTransport().looping || edit->clickTrackEnabled) return fail ("header buttons operate Tracktion transport");
    cycleButton.onClick();
    clickButton.onClick();

    mode (PerformanceMode::full);
    refresh();
    const auto fixed = performance.getHeight();
    if (fixed <= 0 || ! performance.visibleControls().contains ("Cutoff")
        || ! performance.exerciseControl ("Cutoff", 1200)
        || static_cast<float> (synth->getPatch()["layers"][0]["analogPatch"]["values"]["6"]) != 1200)
        return fail ("analog Cutoff binds to the selected Synth preset");
    if (! snapshot (*this, "02-synth-analog")) return fail ("capture analog performance");
    if (synth->selectFactory (presetFor (Family::fm)).failed()) return fail ("switch Synth family on the same track");
    refresh();
    if (performance.getHeight() != fixed || ! performance.visibleControls().contains ("Feedback")
        || performance.visibleControls().contains ("Cutoff"))
        return fail ("preset change updates controls at the same height");
    selection.selectOnly (mallets);
    refresh();
    if (! snapshot (*this, "03-synth-fm")) return fail ("capture FM performance");

    selection.selectOnly (sample);
    refresh();
    if (! performance.exerciseControl ("Attack", 40) || std::abs (sampler->instrument().settings.attack - .04f) > .0001f
        || performance.getHeight() != fixed)
        return fail ("sampler attack changes the real attack setting");
    if (! snapshot (*this, "04-sampler")) return fail ("capture sampler performance");

    selection.selectOnly (violin);
    refresh();
    if (! articulation.selectReviewMapping ("Legato")) return fail ("select the violin articulation");
    refresh();
    if (! snapshot (*this, "05-orchestral-mappings")) return fail ("capture orchestral performance");

    mode (PerformanceMode::articulations);
    selection.selectOnly (violinClip.get());
    showWorkspace (Workspace::piano);
    refresh();
    if (! piano.isVisible() || ! articulationStrip.isVisible() || performance.isVisible())
        return fail ("articulation-only strip and Piano Roll coexist");
    if (! piano.drawForReview (.5) || violinClip->getSequence().getNumControllerEvents() != 1
        || ! piano.drawForReview (1.0) || violinClip->getSequence().getNumControllerEvents() != 1)
        return fail ("piano draw writes verified CC and suppresses duplicate switches");
    edit->undo();
    edit->undo();
    if (violinClip->getSequence().getNumControllerEvents() != 0) return fail ("drawing and articulation undo together");
    edit->redo();
    edit->redo();
    if (violinClip->getSequence().getNumControllerEvents() != 1) return fail ("drawing and articulation redo together");
    if (! articulation.selectReviewMapping ("Staccato") || ! piano.drawForReview (2.0))
        return fail ("draw a staccato phrase");
    const auto keyswitches = violinClip->getSequence().getNotes();
    if (std::none_of (keyswitches.begin(), keyswitches.end(), [] (const auto* note)
        { return note->getNoteNumber() == 24 && std::abs (note->getStartBeat().inBeats() - 1.9375) < .001; }))
        return fail ("keyswitch leads the drawn note");
    if (! snapshot (*this, "06-piano-strings")) return fail ("capture Piano Roll with strings");

    selection.selectOnly (hornClip.get());
    refresh();
    if (! articulation.reviewActiveName().isEmpty() || ! articulation.selectReviewMapping ("Marcato"))
        return fail ("active articulation is local to each instrument");
    if (! articulation.selectReviewMapping ("Marcato") || ! articulation.reviewActiveName().isEmpty())
        return fail ("the active pad can be cleared without deleting its mapping");
    edit->undo();
    refresh();
    if (articulation.reviewActiveName() != "Marcato") return fail ("clearing the active pad is undoable");
    if (! snapshot (*this, "07-piano-horn")) return fail ("capture Piano Roll after switching to horn");
    mode (PerformanceMode::hidden);
    showWorkspace (Workspace::piano);
    refresh();
    const auto hornNotes = hornClip->getSequence().getNumNotes();
    if (! piano.drawForReview (2.0) || hornClip->getSequence().getNumNotes() != hornNotes + 1)
        return fail ("drawing with the articulation surfaces hidden writes only the requested note");
    mode (PerformanceMode::articulations);

    selection.selectOnly (sample);
    refresh();
    message.setText ({}, dontSendNotification);
    if (! showNativeInstrument (*sampler)) return fail ("open the production detailed editor");
    auto* original = instrumentWorkspace.getEditor();
    edit->getTransport().looping = false;
    edit->getTransport().ensureContextAllocated (true);
    sampler->preview (1, 1, 60, 100);
    AudioBuffer<float> buffer (2, blockSize);
    MidiBuffer midi;
    const auto audible = [&]
    {
        for (int block = 0; block < 5; ++block)
        {
            buffer.clear();
            midi.clear();
            hostAudio.processBlock (buffer, midi);
            if (buffer.getMagnitude (0, buffer.getNumSamples()) > .001f) return true;
        }
        return false;
    };
    if (! audible()) return fail ("native sampler previews through the edit");
    instrumentWorkspace.setFloating (true);
    if (! instrumentWorkspace.isFloating() || instrumentWorkspace.getEditor() != original
        || instrumentWorkspace.getPlugin() != sampler || ! audible() || ! articulationStrip.isVisible())
        return fail ("Float preserves editor, playback and the lower strip");
    if (! snapshot (*original->getTopLevelComponent(), "08-editor-floating")) return fail ("capture floating editor");
    instrumentWorkspace.setFloating (false);
    setDockMode (DockMode::expanded);
    if (instrumentWorkspace.getEditor() != original || ! audible())
        return fail ("Dock preserves the same editor and playback");
    if (! snapshot (*this, "09-editor-docked")) return fail ("capture docked editor");
    mode (PerformanceMode::hidden);
    if (performance.isVisible() || articulationStrip.isVisible() || workspace != Workspace::closed || ! audible())
        return fail ("Hide releases the arrangement without interrupting playback");
    if (! snapshot (*this, "10-workspace-hidden")) return fail ("capture hidden workspace");
    sampler->preview (1, 1, 60, 0);

    mode (PerformanceMode::full);
    if (synth->selectFactory (presetFor (Family::analog)).failed()) return fail ("restore the Analog Keys preset");
    selection.selectOnly (keys);
    refresh();
    setSize (1280, 800);
    if (! snapshot (*this, "11-compact-macbook")) return fail ("capture 1280 x 800");
    selection.selectOnly (external);
    refresh();
    if (! performance.visibleControls().isEmpty() || ! snapshot (*this, "12-au-fallback"))
        return fail ("capture unavailable AU without invented controls");

    // Regress the hardware report: the old 270-point Split opened the
    // Cinematic Atelier with its bottom row hidden behind a vertical scrollbar.
    // An unavailable hosted AU fixture is enough to exercise presentation;
    // no real Audio Unit or MIDI hardware is required. Explicitly reproduce
    // the too-short 270-point default before opening the new editor.
    showWorkspace (Workspace::closed);
    setDockMode (DockMode::split);
    workspaceHeight = studio::appearance.px (270);
    resized();
    // This fixture contains only verified message mappings, never invented
    // patch/program labels or audible articulation behavior.
    if (auto* hosted = studio::findInstrument (*external); hosted != nullptr)
    {
        XmlElement root ("StudioArticulations");
        auto* profile = root.createNewChildElement ("Instrument");
        profile->setAttribute ("instance", hosted->itemID.toString());
        studio::writeArticulations (*profile, {
            { "test-legato", "Legato", studio::ArticulationMessage::controller, 1, 20, 127 },
            { "test-staccato", "Staccato", studio::ArticulationMessage::keyswitch, 1, 24, 100 }
        });
        external->state.setProperty ("studioArticulationMappings",
            root.toString (XmlElement::TextFormat().singleLine()), &edit->getUndoManager());
        external->state.addChild (studio::spatial::writeSeat (external->itemID.toString(),
            { { -.25f, .45f }, studio::spatial::Section::strings, -18.0f }),
            -1, &edit->getUndoManager());
        if (! showAtelierInstrument (*hosted))
            return fail ("hosted Audio Unit opens in the right Inspector");
        timerCallback();
        timerCallback();
        if (! inspectorDrawer.isVisible() || ! inspectorDrawer.isShowingAtelier()
            || inspectorDrawer.inspectedTrack() != external->itemID
            || ! inspectorDrawer.verifyAtelierInspector()
            || timeline.getRight() > inspectorDrawer.getX()
            || workspacePanel.getRight() > inspectorDrawer.getX())
            return fail ("Atelier Inspector is bound and reserves its own column");
        if (! snapshot (*this, "13-atelier-performance-inspector"))
            return fail ("capture instrument performance sidebar");
        // Retain full-editor regression coverage, but access it explicitly:
        // normal hosted-AU selection must NOT use the lower workspace.
        setInspectorOpen (false, false);
        if (! instrumentWorkspace.openAtelier (*hosted, *external))
            return fail ("explicit Full Instrument creates an Atelier editor");
        showWorkspace (Workspace::instrumentEditor);
    }
    else return fail ("locate hosted AU fixture");
    auto* atelierEditor = dynamic_cast<studio::AtelierInstrumentEditor*> (instrumentWorkspace.getEditor());
    if (atelierEditor == nullptr || ! atelierEditor->reviewPage (0, false)
        || ! atelierEditor->reviewSelectMappedArticulation())
        return fail ("Atelier Main presents verified mappings and stores undoable selection");
    if (! atelierEditor->reviewPage (1, false) || ! atelierEditor->reviewPage (2, false)
        || ! atelierEditor->reviewPage (3, false) || ! atelierEditor->reviewPage (4, false)
        || ! atelierEditor->reviewMIDIControllers()
        || ! snapshot (*this, "13a-atelier-compact-midi-cc")
        || ! atelierEditor->reviewPage (0, false))
        return fail ("Atelier compact pages and MIDI CC knobs fit in the fixed 425-point dock");
    if (! instrumentWorkspace.isVisible() || instrumentWorkspace.getEditor() == nullptr
        || ! instrumentWorkspace.isAtelier() || ! workspaceDivider.isResizeLocked()
        || ! instrumentWorkspace.hasDockedHeader() || workspaceTitle.isVisible()
        || performance.isVisible() || articulationStrip.isVisible()
        || workspaceViewport.getHeight() < instrumentWorkspace.minimumContentHeight())
        return fail ("Atelier uses one fixed, readable dock without a duplicate Performance panel at 1280 x 800");
    if (! snapshot (*this, "13-atelier-no-scroll-1280x800"))
        return fail ("capture Atelier default dock height");

    // The screenshot-approved Instrument View is fixed in Split, unlike the
    // resizable Piano Roll and other workspaces. Test the callback as well as
    // the displayed divider lock: no stale drag may alter a future workspace.
    if (! workspaceDivider.isResizeLocked()) return fail ("Instrument divider is locked");
    const int genericDockHeight = workspaceHeight;
    const int fixedDockHeight = workspacePanel.getHeight();
    workspaceDivider.resizePane (studio::appearance.px (240));
    if (workspaceHeight != genericDockHeight || workspacePanel.getHeight() != fixedDockHeight)
        return fail ("Instrument divider cannot resize or change other workspaces");
    setSize (1536, 960);
    if (workspacePanel.getHeight() != jmin (studio::appearance.px (425), splitLimit))
        return fail ("Atelier Split uses its fixed screenshot height when room permits");
    if (! snapshot (*this, "14-atelier-fixed-1536x960"))
        return fail ("capture fixed Atelier dock");

    showWorkspace (Workspace::piano);
    if (instrumentWorkspace.hasDockedHeader() || ! workspaceTitle.isVisible())
        return fail ("Piano Roll restores its own header without floating instrument controls");
    if (workspaceDivider.isResizeLocked()) return fail ("Piano Roll divider stays resizable");
    workspaceDivider.resizePane (studio::appearance.px (320));
    if (workspaceHeight == genericDockHeight)
        return fail ("Other workspace dividers still change their height");
    showWorkspace (Workspace::instrumentEditor);
    if (! workspaceDivider.isResizeLocked() || ! instrumentWorkspace.hasDockedHeader()
        || workspacePanel.getHeight() != jmin (studio::appearance.px (425), splitLimit))
        return fail ("Returning to Atelier restores fixed size without changing the Piano Roll preference");
    workspaceHeight = genericDockHeight;
    setSize (1280, 800);

    // Isolate Atelier geometry from Synth/Sampler preferences, verify the
    // screenshot-sized first float, and preserve the user's resized window.
    {
        const String key = "studio.atelierInstrumentWindow";
        const String saved = preferences.getValue (key);
        const String nativeWindowState = preferences.getValue ("studio.instrumentEditorWindow");
        const ScopeGuard restoreAtelierState { [&]
        {
            if (saved.isEmpty()) preferences.removeValue (key);
            else preferences.setValue (key, saved);
        } };
        preferences.removeValue (key);
        instrumentWorkspace.setFloating (true);
        if (instrumentWorkspace.hasDockedHeader())
            return fail ("Floating Atelier returns selector and Dock button to its own toolbar");
        auto* floating = instrumentWorkspace.getEditor()->getTopLevelComponent();
        if (! instrumentWorkspace.isFloating() || floating == nullptr
            || floating->getWidth() != studio::appearance.px (960)
            || floating->getHeight() != studio::appearance.px (510))
            return fail ("Atelier opens at the approved 960x510 floating size");
        if (! snapshot (*floating, "15-atelier-floating-960x510"))
            return fail ("capture screenshot-sized floating Atelier");
        if (! atelierEditor->reviewPage (0, false))
            return fail ("Atelier starts in the compact floating presentation");
        // Full View enlarges the SAME editor, not a duplicate AU instance.
        if (atelierEditor->onFullView == nullptr)
            return fail ("Atelier Full View is connected to floating window");
        atelierEditor->onFullView();
        if (floating != instrumentWorkspace.getEditor()->getTopLevelComponent()
            || ! atelierEditor->reviewPage (0, true)
            || ! atelierEditor->reviewKeyboardHitMap()
            || ! atelierEditor->reviewStageEditing()
            || ! snapshot (*floating, "16-atelier-full-main"))
            return fail ("Atelier Full View opens Main, keyboard and undoable Stage editing");
        if (! atelierEditor->reviewPage (1, true)
            || ! snapshot (*floating, "17-atelier-full-articulations")
            || ! atelierEditor->reviewPage (2, true)
            || ! snapshot (*floating, "18-atelier-full-mixer")
            || ! atelierEditor->reviewPage (3, true)
            || ! snapshot (*floating, "19-atelier-full-advanced")
            || ! atelierEditor->reviewPage (4, true)
            || ! atelierEditor->reviewMIDIControllers()
            || ! snapshot (*floating, "20-atelier-full-midi-cc"))
            return fail ("Atelier full pages and MIDI CC knobs remain in the same floating editor");
        atelierEditor->reviewPage (0, true);
        floating->setSize (1050, 570);
        instrumentWorkspace.setFloating (false);
        instrumentWorkspace.setFloating (true);
        floating = instrumentWorkspace.getEditor()->getTopLevelComponent();
        if (floating == nullptr || floating->getWidth() != 1050 || floating->getHeight() != 570
            || preferences.getValue ("studio.instrumentEditorWindow") != nativeWindowState)
            return fail ("Floating Atelier saves its size independently of native windows");
        instrumentWorkspace.setFloating (false);
        // Closing a full Atelier window intentionally returns to Inspector;
        // only the legacy regression fixture explicitly reopens a lower dock.
        showWorkspace (Workspace::instrumentEditor);
    }
    if (instrumentWorkspace.isFloating() || ! workspaceDivider.isResizeLocked()
        || ! instrumentWorkspace.hasDockedHeader() || workspaceTitle.isVisible()
        || workspaceViewport.getHeight() < instrumentWorkspace.minimumContentHeight())
        return fail ("Atelier redocks at its fixed, readable height without a scrollbar");

    for (const auto size : { Point<int> (1024, 640), Point<int> (1280, 800), Point<int> (1600, 960) })
        for (const auto scale : studio::AppearanceMetrics::interfaceOptions)
        {
            applyAppearance (scale, scale == 175 ? 130 : 100, false);
            selection.selectOnly (keys);
            setSize (size.x, size.y);
            refresh();
            // On compact screens the primary transport is mandatory while
            // optional Cycle/Click controls may move to native menus.
            for (auto* button : { &rewind, &stop, &play, &record })
                if (! button->isVisible() || button->getBounds().isEmpty()
                    || ! topBar.contains (button->getBounds()))
                    return fail ("primary transport controls fit the header at supported sizes");
            for (auto* button : { &cycleButton, &clickButton })
                if (button->isVisible()
                    && (button->getBounds().isEmpty() || ! topBar.contains (button->getBounds())))
                    return fail ("visible optional transport controls fit the header");
            #if JUCE_MAC
            // macOS uses the system menu in place of the legacy header picker.
            if (applicationMenu (4).getNumItems() == 0)
                return fail ("Cycle and Click remain available from native Record menu");
            #endif
            if (! getLocalBounds().contains (performance.getBounds()) || performance.getHeight() <= 0
                || ! getLocalBounds().contains (performanceMode.getBounds()))
                return fail ("scaled performance controls fit inside the window");
        }

    applyAppearance (100, 100, false);
    const auto violinId = violin->itemID;
    const auto violinClipId = violinClip->itemID;
    for (auto* owned : { &synthOwner, &bellsOwner, &samplerOwner }) owned->reset();
    violinClip.reset();
    hornClip.reset();
    mode (PerformanceMode::articulations);
    if (! te::EditFileOperations (*edit).writeToFile (project, false)) return fail ("save the review project");
    loadProject (project, false);
    refresh();
    auto* restoredViolin = trackById (violinId);
    auto* restoredClip = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, violinClipId));
    if (! restoredViolin || ! restoredClip || restoredClip->getSequence().getNumControllerEvents() != 1)
        return fail ("reopen written articulation events");
    selection.selectOnly (restoredViolin);
    refresh();
    if (articulation.reviewActiveName() != "Staccato" || ! articulationStrip.isVisible()
        || performanceMode.getSelectedId() != preferences.getIntValue ("studio.performanceMode"))
        return fail ("reopen restores active mapping and workspace preference");
    std::puts ("Adaptive workspace, MIDI entry, Float/Dock, appearance and project reopening passed.");
    return 0;
}
