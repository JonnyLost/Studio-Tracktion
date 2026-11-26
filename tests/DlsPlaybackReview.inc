// SPDX-License-Identifier: GPL-3.0-or-later
// Real Apple AU acceptance test, using the same hosted audio graph as the
// native instrument workspace tests. It never uses the UI name as evidence
// that a sound changed.
#if JUCE_MAC
int StudioView::runDlsPlaybackReview()
{
    stopTimer();
    environmentWelcome.dismiss();
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "DLS playback review failed: %s\n", reason);
        return 1;
    };

    auto& manager = engine.getPluginManager();
    std::optional<PluginDescription> description;
    for (const auto& candidate : manager.knownPluginList.getTypes())
        if (candidate.pluginFormatName == "AudioUnit" && candidate.isInstrument
            && candidate.manufacturerName.containsIgnoreCase ("Apple")
            && candidate.name.containsIgnoreCase ("DLSMusicDevice"))
            description = candidate;

    // A fresh Intel runner has no saved Studio plug-in catalog.
    if (! description)
        for (auto* format : manager.pluginFormatManager.getFormats())
        {
            if (format->getName() != "AudioUnit") continue;
            for (const auto& identifier : format->searchPathsForPlugins (FileSearchPath(), false, false))
            {
                if (! identifier.containsIgnoreCase ("dls")) continue;
                OwnedArray<PluginDescription> found;
                format->findAllTypesForFile (found, identifier);
                for (auto* candidate : found)
                    if (candidate->isInstrument && candidate->manufacturerName.containsIgnoreCase ("Apple")
                        && candidate->name.containsIgnoreCase ("DLSMusicDevice"))
                        description = *candidate;
            }
        }
    if (! description)
    {
        std::puts ("DLS playback review skipped: Apple's DLSMusicDevice AU was not available.");
        return 77;
    }
    manager.knownPluginList.addType (*description);

    const auto folder = File::getSpecialLocation (File::tempDirectory)
        .getNonexistentChildFile ("Studio DLS Playback Review", {}, false);
    if (folder.createDirectory().failed()) return fail ("create temporary project");
    const ScopeGuard removeFixtures { [folder] { folder.deleteRecursively(); } };

    auto& devices = engine.getDeviceManager();
    devices.setCpuLimitBeforeMuting (1000);
    auto& hosted = devices.getHostedAudioDeviceInterface();
    hosted.initialise ({ .sampleRate = 48000, .blockSize = 256, .inputChannels = 0, .outputChannels = 2 });
    hosted.prepareToPlay (48000, 256);
    devices.dispatchPendingUpdates();
    const ScopeGuard resetDevice { [&]
    {
        pendingPlugin.reset();
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
        seatingWorkspace.setEdit (nullptr);
        edit.reset();
        devices.deviceManager.closeAudioDevice();
        devices.removeHostedAudioDeviceInterface();
    } };

    loadProject (folder.getChildFile ("General MIDI.tracktionedit"), true);
    if (edit == nullptr || ! browser.loadGeneralMidi) return fail ("prepare GM session");
    edit->clickTrackEnabled = false;
    juce::AudioBuffer<float> audio (2, 256);
    MidiBuffer midi;
    auto render = [&]
    {
        audio.clear();
        midi.clear();
        hosted.processBlock (audio, midi);
    };
    auto silence = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b) render();
    };

    auto load = [&] (int program, bool addNew)
    {
        browser.loadGeneralMidi (*description, program, addNew);
        for (int attempt = 0; pendingPlugin && attempt < 300; ++attempt)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            completePendingPlugin();
        }
        if (pendingPlugin) return false;
        edit->dispatchPendingUpdatesSynchronously();
        edit->getTransport().ensureContextAllocated (addNew);
        for (int pass = 0; pass < 8; ++pass)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (20);
            restoreDlsPrograms();
            silence (8);
        }
        auto* selected = workflowTrack();
        auto* plugin = selected != nullptr
            ? dynamic_cast<te::ExternalPlugin*> (studio::findInstrument (*selected)) : nullptr;
        return plugin != nullptr && isAppleDls (*plugin)
            && plugin->getAudioPluginInstance() != nullptr
            && static_cast<int> (plugin->state.getProperty (Identifier ("studioDlsGmProgram"), -1)) == program;
    };

    // Render about 0.68 s of a sustained A4. Capture the waveform, not the
    // display name. Release and clear the note before the next measurement.
    auto capture = [&] (te::AudioTrack& track, int channel = 1)
    {
        silence (32);
        track.injectLiveMidiMessage (MidiMessage::noteOn (channel, 69, static_cast<juce::uint8> (100)), {});
        std::vector<float> samples;
        samples.reserve (128 * 256);
        for (int b = 0; b < 128; ++b)
        {
            render();
            for (int n = 0; n < audio.getNumSamples(); ++n)
                samples.push_back (0.5f * (audio.getSample (0, n) + audio.getSample (1, n)));
        }
        track.injectLiveMidiMessage (MidiMessage::noteOff (channel, 69), {});
        silence (320);
        return samples;
    };
    auto peak = [] (const std::vector<float>& samples)
    {
        float maximum = 0.0f;
        for (auto x : samples) maximum = std::max (maximum, std::abs (x));
        return maximum;
    };

    // Compare normalized amplitude-envelope and harmonic-energy signatures.
    // This is insensitive to absolute track gain, phase, and reverb tails.
    auto fingerprint = [] (const std::vector<float>& samples)
    {
        std::array<double, 16> result {};
        const int window = 4096;
        double total = 0.0;
        for (int segment = 0; segment < 8; ++segment)
        {
            double energy = 0.0;
            for (int n = 0; n < window; ++n)
            {
                const double value = samples[static_cast<size_t> (segment * window + n)];
                energy += value * value;
            }
            result[static_cast<size_t> (segment)] = energy;
            total += energy;
        }
        for (int i = 0; i < 8; ++i)
            result[static_cast<size_t> (i)] /= std::max (total, 1.0e-20);

        // Scan broad bands around the first eight harmonics; the bandwidth
        // also accommodates the mild natural vibrato of a GM violin sample.
        constexpr int fftWindow = 8192;
        total = 0.0;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            double energy = 0.0;
            for (const double shift : { -0.008, 0.0, 0.008 })
            {
                const double omega = 2.0 * juce::MathConstants<double>::pi * (440.0 * harmonic * (1.0 + shift)) / 48000.0;
                double re = 0.0, im = 0.0;
                for (int n = 0; n < fftWindow; ++n)
                {
                    const double taper = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / (fftWindow - 1));
                    const double value = samples[static_cast<size_t> (8192 + n)] * taper;
                    re += value * std::cos (omega * n);
                    im += value * std::sin (omega * n);
                }
                energy += re * re + im * im;
            }
            result[static_cast<size_t> (7 + harmonic)] = energy;
            total += energy;
        }
        for (int i = 8; i < 16; ++i)
            result[static_cast<size_t> (i)] /= std::max (total, 1.0e-20);
        return result;
    };
    auto distance = [&] (const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto fa = fingerprint (a), fb = fingerprint (b);
        double d = 0.0;
        for (size_t i = 0; i < fa.size(); ++i) d += std::abs (fa[i] - fb[i]);
        return d;
    };
    // Estimate the piano's stable pitch around A4 using normalized
    // autocorrelation; interpolation gives sub-sample period resolution.
    auto pitch = [] (const std::vector<float>& samples)
    {
        std::array<double, 41> scores {};
        for (int lag = 90; lag <= 130; ++lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int n = 4096; n < 20480; ++n)
            {
                const double x = samples[static_cast<size_t> (n)];
                const double y = samples[static_cast<size_t> (n + lag)];
                xy += x * y; xx += x * x; yy += y * y;
            }
            scores[static_cast<size_t> (lag - 90)] = xy / std::sqrt (std::max (xx * yy, 1.0e-30));
        }
        const auto best = std::max_element (scores.begin(), scores.end());
        const auto i = static_cast<int> (best - scores.begin());
        if (i == 0 || i == 40 || *best < 0.5) return 0.0;
        const double a = scores[static_cast<size_t> (i - 1)], b = *best,
                     c = scores[static_cast<size_t> (i + 1)];
        const double denom = a - 2.0 * b + c;
        const double offset = std::abs (denom) > 1.0e-10 ? 0.5 * (a - c) / denom : 0.0;
        return 48000.0 / (90 + i + offset);
    };

    if (! load (0, true)) return fail ("create real DLS acoustic piano");
    auto* first = workflowTrack();
    auto* firstPlugin = dynamic_cast<te::ExternalPlugin*> (studio::findInstrument (*first));
    const auto pianoA = capture (*first), pianoB = capture (*first);
    if (peak (pianoA) < 0.0005f || peak (pianoB) < 0.0005f) return fail ("piano produces audio");

    // Changing the patch on this existing instance must change actual audio.
    if (! load (40, false) || workflowTrack() != first
        || studio::findInstrument (*first) != firstPlugin)
        return fail ("same-instance violin selection");
    const auto violinA = capture (*first);
    const auto baseline = distance (pianoA, pianoB);
    const auto changed = distance (pianoA, violinA);
    if (peak (violinA) < 0.0005f || changed < std::max (0.10, baseline * 2.0 + 0.04))
        return fail ("piano to violin did not change the rendered audio");

    if (! load (56, true)) return fail ("create second real DLS trumpet");
    auto* second = workflowTrack();
    auto* secondPlugin = dynamic_cast<te::ExternalPlugin*> (studio::findInstrument (*second));
    if (second == first || secondPlugin == firstPlugin
        || secondPlugin->getAudioPluginInstance() == firstPlugin->getAudioPluginInstance())
        return fail ("distinct tracks and AU instances");
    const auto trumpet = capture (*second);
    const auto violinB = capture (*first);
    if (peak (trumpet) < 0.0005f || peak (violinB) < 0.0005f
        || distance (trumpet, violinB) < 0.08
        || distance (violinA, violinB) >= distance (pianoA, violinB))
        return fail ("second track changed or duplicated first track's patch");

    selection.selectOnly (first);
    if (! load (0, false)) return fail ("return first track to piano with second AU still loaded");
    const auto pianoC = capture (*first);
    const auto beforeHz = pitch (pianoA), afterHz = pitch (pianoC);
    if (beforeHz < 430.0 || beforeHz > 450.0 || afterHz < 430.0 || afterHz > 450.0
        || std::abs (1200.0 * std::log2 (afterHz / beforeHz)) > 12.0)
        return fail ("piano pitch changed with second AU present");

    std::printf ("DLS playback review passed: piano-to-violin distance %.3f, repeat distance %.3f, "
                 "piano tuning %.2f Hz -> %.2f Hz, two independent AU instances.\n",
                 changed, baseline, beforeHz, afterHz);
    return 0;
}
#endif
