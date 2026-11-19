// SPDX-License-Identifier: GPL-3.0-or-later
#include <JuceHeader.h>
using namespace juce;
using namespace tracktion::literals;

#include "Utilities.h"
#include "Components.h"
#include "PluginWindow.h"

class StudioView final : public Component, private Timer
{
public:
    StudioView()
    {
        LookAndFeel::getDefaultLookAndFeel().setColour (ResizableWindow::backgroundColourId, Colour (0xff172027));
        for (auto* control : { &newProject, &openProject, &saveProject, &audioSettings, &plugins,
                               &rewind, &play, &record, &addMidi, &addAudio, &importAudio })
            addAndMakeVisible (*control);

        addAndMakeVisible (timeline);
        timeline.setScrollBarsShown (true, false);
        for (auto* label : { &title, &position, &mixerLabel, &message })
            addAndMakeVisible (*label);
        title.setFont (FontOptions (23.0f, Font::bold));
        title.setText ("Studio 0.1", dontSendNotification);
        addAndMakeVisible (zoom);
        zoom.addItem ("10 seconds", 1);
        zoom.addItem ("30 seconds", 2);
        zoom.addItem ("60 seconds", 3);
        zoom.addItem ("180 seconds", 4);
        zoom.setSelectedId (2);
        zoom.onChange = [this]
        {
            if (editor != nullptr)
            {
                constexpr double spans[] { 10, 30, 60, 180 };
                auto& view = editor->getEditViewState();
                view.viewX1 = 0s;
                view.viewX2 = te::TimePosition::fromSeconds (spans[jlimit (1, 4, zoom.getSelectedId()) - 1]);
                repaint (ruler);
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
        audioSettings.onClick = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        plugins.onClick = [this] { showPlugins(); };
        rewind.onClick = [this] { edit->getTransport().setPosition (0s); };
        play.onClick = [this]
        {
            if (edit->getTransport().isRecording())
                EngineHelpers::toggleRecord (*edit);
            else
                EngineHelpers::togglePlay (*edit);
        };
        record.onClick = [this] { EngineHelpers::toggleRecord (*edit); };
        addMidi.onClick = [this] { addTrack ("Instrument"); };
        addAudio.onClick = [this] { addTrack ("Audio"); };
        importAudio.onClick = [this] { chooseAudio(); };

        message.setText ("Create or open a project to begin.", dontSendNotification);
        setSize (1100, 650);
        timerCallback();
        startTimerHz (12);
    }

    ~StudioView() override
    {
        timeline.setViewedComponent (nullptr);
    }

    void requestQuit()
    {
        if (busy)
            return;
        save ([] (bool ok) { if (ok) JUCEApplicationBase::quit(); });
    }

    void paint (Graphics& graphics) override
    {
        auto area = ruler;
        graphics.setColour (Colour (0xff25343d));
        graphics.fillRect (area);
        if (editor == nullptr || area.isEmpty())
            return;

        auto span = editor->getEditViewState().viewX2.get().inSeconds();
        auto interval = span <= 10.0 ? 1 : span <= 30.0 ? 5 : span <= 60.0 ? 10 : 30;
        for (int second = 0; second < span; second += interval)
        {
            int x = area.getX() + roundToInt (area.getWidth() * second / span);
            graphics.setColour (Colour (0xff869da8));
            graphics.drawVerticalLine (x, static_cast<float> (area.getBottom() - 6),
                                         static_cast<float> (area.getBottom()));
            graphics.drawText (String (second) + " s", x + 4, area.getY(), 42,
                               area.getHeight(), Justification::centredLeft);
        }
    }

    void mouseDown (const MouseEvent& event) override
    {
        if (editor == nullptr || busy || edit->getTransport().isRecording() || ! ruler.contains (event.getPosition()))
            return;

        auto& view = editor->getEditViewState();
        edit->getTransport().setPosition (view.xToTime (event.x - ruler.getX(), ruler.getWidth()));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        title.setBounds (area.removeFromTop (38));
        auto files = area.removeFromTop (34);
        for (auto* button : { &newProject, &openProject, &saveProject, &audioSettings, &plugins })
            button->setBounds (files.removeFromLeft (121).reduced (2));
        auto controls = area.removeFromTop (34);
        for (auto* button : { &rewind, &play, &record, &addMidi, &addAudio, &importAudio })
            button->setBounds (controls.removeFromLeft (112).reduced (2));
        zoom.setBounds (controls.removeFromLeft (115).reduced (2));
        position.setBounds (controls.reduced (3));
        message.setBounds (area.removeFromBottom (28));
        auto mixer = area.removeFromBottom (45);
        mixerLabel.setBounds (mixer.removeFromLeft (180));
        volume.setBounds (mixer.removeFromLeft (275).reduced (4));
        pan.setBounds (mixer.removeFromLeft (245).reduced (4));
        auto rulerRow = area.removeFromTop (24);
        timeline.setBounds (area);
        ruler = rulerRow.withTrimmedLeft (150).withTrimmedRight (150 + timeline.getScrollBarThickness());
        repaint();
        sizeEditor();
    }

private:
    te::Engine engine { "Studio 0.1", std::make_unique<ExtendedUIBehaviour>(), nullptr };
    te::SelectionManager selection { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editor;
    Viewport timeline;
    std::unique_ptr<FileChooser> chooser;
    File projectFile;
    Rectangle<int> ruler;
    bool busy = false;

    Label title, position, mixerLabel, message;
    ComboBox zoom;
    Slider volume, pan;
    TextButton newProject { "New Project" }, openProject { "Open Project" }, saveProject { "Save" },
               audioSettings { "Audio Settings" }, plugins { "Plugin Manager" },
               rewind { "Return to Start" }, play { "Play" }, record { "Record" },
               addMidi { "+ Instrument" }, addAudio { "+ Audio" }, importAudio { "Import Audio" };

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
            editor->setSize (jmax (1, timeline.getMaximumVisibleWidth() - timeline.getScrollBarThickness()),
                             jmax (timeline.getMaximumVisibleHeight(), 52 * te::getAudioTracks (*edit).size()));
    }

    void timerCallback() override
    {
        const bool ready = edit != nullptr && ! busy;
        const bool recording = edit != nullptr && edit->getTransport().isRecording();
        for (auto* button : { &newProject, &openProject, &audioSettings, &plugins })
            button->setEnabled (! busy && ! recording);
        for (auto* button : { &saveProject, &rewind, &addMidi, &addAudio, &importAudio })
            button->setEnabled (ready && ! recording);
        play.setEnabled (ready);
        record.setEnabled (ready);
        zoom.setEnabled (ready);
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            play.setButtonText (transport.isPlaying() ? "Stop" : "Play");
            record.setButtonText (recording ? "Stop Recording" : "Record");
            position.setText (String (transport.getPosition().inSeconds(), 2) + " s", dontSendNotification);
            sizeEditor();
        }

        auto* track = selectedTrack();
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

    void save (std::function<void (bool)> after = {})
    {
        if (edit == nullptr)
        {
            if (after) after (true);
            return;
        }

        if (edit->getTransport().isRecording())
            EngineHelpers::toggleRecord (*edit);
        busy = true;
        te::EditFileOperations (*edit).save (true, true, false,
            [this, after = std::move (after)] (bool ok)
            {
                busy = false;
                message.setText (ok ? "Saved " + projectFile.getFileName() : "Project save failed.", dontSendNotification);
                if (after)
                    MessageManager::callAsync ([after, ok] { after (ok); });
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
        if (! create)
        {
            auto xml = XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("EDIT"))
            {
                message.setText ("Could not read this Studio project.", dontSendNotification);
                return;
            }
        }

        auto next = create ? te::createEmptyEdit (engine, file) : te::loadEditFromFile (engine, file);
        if (next == nullptr)
            return;

        if (edit != nullptr)
            edit->getTransport().stop (false, false);
        selection.deselectAll();
        timeline.setViewedComponent (nullptr);
        editor.reset();
        edit = std::move (next);
        projectFile = file;
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
        title.setText ("Studio 0.1  /  " + file.getFileNameWithoutExtension(), dontSendNotification);
        message.setText ("I selects input. A arms recording. + at right adds an instrument or effect.", dontSendNotification);
        if (create)
            save();
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
            setResizeLimits (850, 430, 2600, 1800);
            setContentOwned (new StudioView(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

public:
    const String getApplicationName() override { return "Studio"; }
    const String getApplicationVersion() override { return "0.1.0"; }
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
