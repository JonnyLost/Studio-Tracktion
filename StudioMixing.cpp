// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioMixing.h"
#include "StudioUI.h"

using namespace juce;
namespace te = tracktion;

namespace studio
{
namespace
{
bool instrumentPlugin (te::Plugin& plugin)
{
    if (plugin.isSynth()) return true;
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        return external->isAU() && external->getName().containsIgnoreCase ("ARIA Player");
    return false;
}

te::AudioTrack* trackFor (te::Edit* edit, te::EditItemID id)
{
    return edit != nullptr && id.isValid() ? te::findAudioTrackForID (*edit, id) : nullptr;
}

template <typename PluginType>
PluginType* lastTrackPlugin (te::AudioTrack& track)
{
    for (int index = track.pluginList.size(); --index >= 0;)
        if (auto* plugin = dynamic_cast<PluginType*> (track.pluginList[index]))
            return plugin;
    return nullptr;
}

class StereoMeter final : public Component, public SettableTooltipClient
{
public:
    StereoMeter() { setTitle ("Stereo peak meter"); }
    ~StereoMeter() override { attach (nullptr); }

    void attach (te::LevelMeasurer* next)
    {
        if (meter.get() == next && registered == (next != nullptr)) return;
        if (auto* old = meter.get()) old->removeClient (client);
        meter = next;
        registered = next != nullptr;
        client.reset();
        levels.fill (-60.0f);
        if (next != nullptr) next->addClient (client);
        repaint();
    }

    void sample()
    {
        if (meter == nullptr) return;
        const auto channels = client.getNumChannelsUsed();
        const auto before = levels;
        for (int i = 0; i < (int) levels.size(); ++i)
        {
            const float db = i < channels ? client.getAndClearAudioLevel (i).dB : -60.0f;
            levels[(size_t) i] = jlimit (-60.0f, 3.0f, jmax (db, levels[(size_t) i] - 4.0f));
        }
        if (before != levels) repaint();
    }

    void paint (Graphics& g) override
    {
        auto area = getLocalBounds().reduced (2);
        if (meter == nullptr)
        {
            g.setColour (colours::secondary);
            g.drawText ("—", area, Justification::centred);
            return;
        }
        for (const auto db : levels)
        {
            auto channel = area.removeFromLeft (jmax (1, (getWidth() - 4) / 2)).reduced (1, 0);
            g.setColour (colours::rail);
            g.fillRect (channel);
            const auto fraction = jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
            g.setColour (db >= 0.0f ? colours::record : db > -8.0f ? colours::warning : Colour (0xff70bc9f));
            g.fillRect (channel.removeFromBottom (roundToInt (fraction * (float) channel.getHeight())));
        }
    }

private:
    WeakReference<te::LevelMeasurer> meter;
    te::LevelMeasurer::Client client;
    std::array<float, 2> levels { -60.0f, -60.0f };
    bool registered = false;
};
}

te::Plugin* findInstrument (te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (instrumentPlugin (*plugin)) return plugin;
    return nullptr;
}

String outputLabel (te::AudioTrack& track)
{
    auto& output = track.getOutput();
    if (auto* destination = output.getDestinationTrack())
        return destination->getName();
    if (output.usesDefaultAudioOut()) return "Master";
    if (output.usesDefaultMIDIOut()) return "Default MIDI";
    if (output.outputsToNone()) return "No direct output";
    return output.getDescriptiveOutputName();
}

MixControls::MixControls (bool fader) : vertical (fader)
{
    for (auto* control : { &volume, &pan })
    {
        addAndMakeVisible (*control);
        control->setSliderStyle (control == &volume && vertical ? Slider::LinearVertical : Slider::LinearHorizontal);
        control->setTextBoxStyle (vertical ? Slider::TextBoxBelow : Slider::TextBoxRight, false, 66, 18);
    }
    for (auto* label : { &volumeLabel, &panLabel })
    {
        addAndMakeVisible (*label);
        label->setColour (Label::textColourId, colours::secondary);
        label->setFont (FontOptions (11.0f));
    }
    volumeLabel.setText ("Volume", dontSendNotification);
    panLabel.setText ("Pan", dontSendNotification);
    volume.setName ("Volume");
    pan.setName ("Pan");
    volume.setRange (-100.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-24.0);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    const auto startGesture = [this]
    {
        if (resolveVolume)
            if (auto p = resolveVolume())
                p->edit.getUndoManager().beginNewTransaction ("Adjust channel");
    };
    volume.onDragStart = startGesture;
    pan.onDragStart = startGesture;
    volume.onValueChange = [this]
    {
        if (resolveVolume)
            if (auto p = resolveVolume())
                p->setVolumeDb ((float) volume.getValue());
    };
    pan.onValueChange = [this]
    {
        if (resolveVolume)
            if (auto p = resolveVolume())
                p->setPan ((float) pan.getValue());
    };
}

void MixControls::refresh (te::VolumeAndPanPlugin* plugin, bool enabled)
{
    setEnabled (plugin != nullptr && enabled);
    if (plugin == nullptr) return;
    if (! volume.isMouseButtonDown() && ! volume.hasKeyboardFocus (true))
        volume.setValue (plugin->getVolumeDb(), dontSendNotification);
    if (! pan.isMouseButtonDown() && ! pan.hasKeyboardFocus (true))
        pan.setValue (plugin->getPan(), dontSendNotification);
}

void MixControls::resized()
{
    auto area = getLocalBounds();
    if (vertical)
    {
        panLabel.setBounds (area.removeFromTop (13));
        pan.setBounds (area.removeFromTop (33));
        volumeLabel.setBounds (area.removeFromTop (13));
        volume.setBounds (area);
    }
    else
    {
        auto volumeRow = area.removeFromTop (24);
        volumeLabel.setBounds (volumeRow.removeFromLeft (58));
        volume.setBounds (volumeRow);
        panLabel.setBounds (area.removeFromLeft (58));
        pan.setBounds (area);
    }
}

ChannelInspector::ChannelInspector (TrackActions& callbacks) : actions (callbacks)
{
    for (Component* component : std::initializer_list<Component*> {
             &identity, &inputSummary, &instrument, &sends, &output, &ariaStatus, &audioInput,
             &midiInput, &arm, &monitor, &editor, &inserts, &routing, &mix })
        addAndMakeVisible (*component);
    identity.setFont (FontOptions (14.0f, Font::bold));
    for (auto* label : { &inputSummary, &instrument, &sends, &output, &ariaStatus })
    {
        label->setColour (Label::textColourId, colours::secondary);
        label->setFont (FontOptions (11.5f));
        label->setMinimumHorizontalScale (0.9f);
    }
    audioInput.onClick = [this] { actions.showMenu (id, TrackMenu::audioInput, audioInput); };
    midiInput.onClick = [this] { actions.showMenu (id, TrackMenu::midiInput, midiInput); };
    inserts.onClick = [this] { actions.showMenu (id, TrackMenu::inserts, inserts); };
    routing.onClick = [this] { actions.showMenu (id, TrackMenu::output, routing); };
    editor.onClick = [this] { actions.openInstrument (id); };
    arm.onClick = [this] { actions.toggleArm (id); };
    monitor.onClick = [this] { actions.toggleMonitoring (id); };
    monitor.setTooltip ("Device-wide monitoring for assigned inputs. Automatic MIDI monitoring follows Tracktion.");
    mix.resolveVolume = [this]() -> te::VolumeAndPanPlugin::Ptr
    {
        if (actions.canChangeMix())
            if (auto* t = track()) return t->getVolumePlugin();
        return {};
    };
}

te::AudioTrack* ChannelInspector::track() const { return trackFor (edit, id); }

void ChannelInspector::setEdit (te::Edit* next)
{
    id = {};
    edit = next;
    refresh (nullptr, false, false, {});
}

void ChannelInspector::refresh (te::AudioTrack* selected, bool ready, bool canRoute, const String& recovery)
{
    id = selected != nullptr ? selected->itemID : te::EditItemID {};
    identity.setText (selected ? selected->getName() : "Select a track in the arrangement", dontSendNotification);
    auto* plugin = selected != nullptr ? findInstrument (*selected) : nullptr;
    instrument.setText (plugin != nullptr ? plugin->getName() : selected != nullptr ? "No instrument" : "—", dontSendNotification);
    output.setText (selected != nullptr ? outputLabel (*selected) : "—", dontSendNotification);
    StringArray assigned, sendNames;
    bool isArmed = false, isMonitoring = false, fixedMonitor = false;
    if (selected != nullptr)
    {
        for (auto* instance : selected->edit.getAllInputDevices())
            if (instance->getTargets().contains (selected->itemID))
            {
                const auto& device = instance->getInputDevice();
                assigned.add (device.getName());
                isArmed |= instance->isRecordingEnabled (selected->itemID);
                isMonitoring |= instance->isLivePlayEnabled (*selected);
                fixedMonitor |= device.getMonitorMode() != te::InputDevice::MonitorMode::automatic;
            }
        for (auto* candidate : selected->pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (candidate))
            {
                auto bus = selected->edit.getAuxBusName (send->getBusNumber());
                sendNames.add (bus.isNotEmpty() ? bus : "Bus " + String (send->getBusNumber() + 1));
            }
    }
    inputSummary.setText (assigned.isEmpty() ? "No input assigned" : assigned.joinIntoString (" · "), dontSendNotification);
    inputSummary.setTooltip (assigned.joinIntoString ("\n"));
    sends.setText (sendNames.isEmpty() ? "No sends" : sendNames.joinIntoString (" · "), dontSendNotification);
    sends.setTooltip (sendNames.joinIntoString ("\n"));
    ariaStatus.setText (recovery, dontSendNotification);
    const bool configurable = canRoute && selected != nullptr && ! selected->isFrozen (te::Track::anyFreeze);
    for (auto* button : { &audioInput, &midiInput, &routing, &inserts })
        button->setEnabled (configurable);
    editor.setEnabled (ready && plugin != nullptr);
    arm.setEnabled (configurable && ! assigned.isEmpty());
    monitor.setEnabled (configurable && fixedMonitor);
    arm.setToggleState (isArmed, dontSendNotification);
    monitor.setToggleState (isMonitoring, dontSendNotification);
    mix.refresh (selected != nullptr ? lastTrackPlugin<te::VolumeAndPanPlugin> (*selected) : nullptr, ready);
}

void ChannelInspector::resized()
{
    auto area = getLocalBounds().reduced (9, 2);
    identity.setBounds (area.removeFromTop (23));
    ariaStatus.setBounds (area.removeFromBottom (19));
    auto bottom = area.removeFromBottom (49);
    mix.setBounds (bottom.removeFromLeft (jmin (480, bottom.getWidth())));
    for (size_t i = 0; i < columns.size(); ++i)
        columns[i] = area.removeFromLeft (area.getWidth() / (int) (columns.size() - i)).reduced (2, 0);
    auto input = columns[0].reduced (4, 2).withTrimmedTop (21);
    inputSummary.setBounds (input.removeFromTop (18));
    audioInput.setBounds (input.removeFromTop (22));
    midiInput.setBounds (input.removeFromTop (22));
    arm.setBounds (input.removeFromLeft (45).removeFromTop (21));
    monitor.setBounds (input.removeFromTop (21).reduced (2, 0));
    auto instr = columns[1].reduced (4, 2).withTrimmedTop (21);
    instrument.setBounds (instr.removeFromTop (20));
    editor.setBounds (instr.removeFromTop (23));
    inserts.setBounds (columns[2].reduced (4, 2).withTrimmedTop (22).removeFromTop (23));
    sends.setBounds (columns[3].reduced (4, 2).withTrimmedTop (21).removeFromTop (50));
    auto out = columns[4].reduced (4, 2).withTrimmedTop (21);
    output.setBounds (out.removeFromTop (20));
    routing.setBounds (out.removeFromTop (23));
}

void ChannelInspector::paint (Graphics& g)
{
    constexpr const char* stages[] { "Input", "Instrument", "Inserts", "Sends", "Output" };
    g.setFont (FontOptions (12.0f, Font::bold));
    for (size_t i = 0; i < columns.size(); ++i)
    {
        g.setColour (i == 1 ? colours::selected : colours::control);
        g.fillRoundedRectangle (columns[i].toFloat(), 4.0f);
        g.setColour (colours::text);
        g.drawText (stages[i], columns[i].reduced (7, 0).removeFromTop (22), Justification::centredLeft);
        if (i + 1 < columns.size())
        {
            const auto right = columns[i].withLeft (columns[i].getRight() - 16).withHeight (22);
            g.setColour (colours::secondary);
            g.drawText ("→", right, Justification::centred);
        }
    }
}

class StudioMixer::Strip final : public Component
{
public:
    Strip (StudioMixer& owner, te::EditItemID trackId, bool isMaster)
        : mixer (owner), id (trackId), masterStrip (isMaster)
    {
        for (Component* component : std::initializer_list<Component*> {
                 &name, &identity, &inserts, &mute, &solo, &out, &mix, &meter })
            addAndMakeVisible (*component);
        identity.setColour (Label::textColourId, colours::secondary);
        identity.setFont (FontOptions (10.5f));
        identity.setJustificationType (Justification::centred);
        name.setButtonText (masterStrip ? "Master" : "Track");
        name.onClick = [this] { mixer.actions.selectTrack (id); };
        inserts.onClick = [this] { mixer.actions.showMenu (id, TrackMenu::inserts, inserts); };
        out.onClick = [this] { mixer.actions.showMenu (id, TrackMenu::output, out); };
        mute.onClick = [this] { mixer.actions.toggleTrackState (id, false); };
        solo.onClick = [this] { mixer.actions.toggleTrackState (id, true); };
        solo.setColour (TextButton::buttonOnColourId, colours::selected.brighter (0.3f));
        mute.setColour (TextButton::buttonOnColourId, colours::record.darker (0.65f));
        mute.setTooltip ("Mute track");
        solo.setTooltip ("Solo track");
        inserts.setTooltip ("Open the track’s insert chain");
        mix.resolveVolume = [this]() -> te::VolumeAndPanPlugin::Ptr
        {
            if (! mixer.actions.canChangeMix() || mixer.edit == nullptr) return {};
            if (masterStrip) return mixer.edit->getMasterVolumePlugin();
            if (auto* t = trackFor (mixer.edit, id)) return t->getVolumePlugin();
            return {};
        };
        for (auto* c : { &inserts, &mute, &solo })
            c->setVisible (! masterStrip);
        name.setEnabled (! masterStrip);
        out.setEnabled (! masterStrip);
    }

    void refresh (te::AudioTrack* track, bool isSelected, bool ready, bool canRoute)
    {
        if (selected != isSelected)
        {
            selected = isSelected;
            name.setToggleState (selected, dontSendNotification);
            repaint();
        }
        const auto editable = canRoute && track != nullptr && ! track->isFrozen (te::Track::anyFreeze);
        auto* edit = mixer.edit;
        if (masterStrip)
        {
            auto* context = edit != nullptr ? edit->getTransport().getCurrentPlaybackContext() : nullptr;
            meter.attach (context != nullptr ? &context->masterLevels : nullptr);
            meter.setTooltip ("Tracktion master peak levels on the default output");
            auto* device = edit != nullptr ? edit->engine.getDeviceManager().getDefaultWaveOutDevice() : nullptr;
            out.setButtonText (device != nullptr ? device->getName() : "Default audio");
            mix.refresh (edit != nullptr ? edit->getMasterVolumePlugin().get() : nullptr, ready);
        }
        else if (track != nullptr)
        {
            name.setButtonText (track->getName());
            auto* instrument = findInstrument (*track);
            identity.setText (instrument != nullptr ? instrument->getName() : "Audio / MIDI", dontSendNotification);
            if (instrument != nullptr) identity.setTooltip (instrument->getName());
            else identity.setTooltip ({});
            out.setButtonText (outputLabel (*track));
            out.setEnabled (editable);
            inserts.setEnabled (editable);
            mute.setToggleState (track->isMuted (false), dontSendNotification);
            solo.setToggleState (track->isSolo (false), dontSendNotification);
            mute.setEnabled (ready);
            solo.setEnabled (ready);
            mix.refresh (lastTrackPlugin<te::VolumeAndPanPlugin> (*track), ready);
            auto* levelPlugin = lastTrackPlugin<te::LevelMeterPlugin> (*track);
            const bool frozen = track->isFrozen (te::Track::anyFreeze);
            meter.attach (levelPlugin != nullptr && ! frozen ? &levelPlugin->measurer : nullptr);
            meter.setTooltip (frozen ? "Meter unavailable on a frozen track"
                                     : levelPlugin == nullptr ? "This track has no Level Meter insert" : "Track peak levels");
        }
        meter.sample();
    }

    void detachMeter() { meter.attach (nullptr); }
    te::EditItemID getId() const { return id; }

    void paint (Graphics& g) override
    {
        g.setColour (selected || masterStrip ? colours::selected : colours::control);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f);
        if (selected)
        {
            g.setColour (colours::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 4);
        name.setBounds (area.removeFromTop (22));
        identity.setBounds (area.removeFromTop (17));
        if (! masterStrip) inserts.setBounds (area.removeFromTop (22));
        else area.removeFromTop (22);
        out.setBounds (area.removeFromBottom (22));
        auto toggles = area.removeFromBottom (22);
        if (! masterStrip)
        {
            mute.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (1));
            solo.setBounds (toggles.reduced (1));
        }
        meter.setBounds (area.removeFromRight (20).withTrimmedTop (49));
        mix.setBounds (area);
    }

private:
    StudioMixer& mixer;
    te::EditItemID id;
    bool masterStrip, selected = false;
    TextButton name, inserts { "Inserts…" }, mute { "M" }, solo { "S" }, out;
    Label identity;
    MixControls mix { true };
    StereoMeter meter;
};

StudioMixer::StudioMixer (TrackActions& callbacks) : actions (callbacks)
{
    viewport.setScrollBarsShown (false, true);
    viewport.setViewedComponent (&stripBank, false);
    addAndMakeVisible (viewport);
    master = std::make_unique<Strip> (*this, te::EditItemID {}, true);
    addAndMakeVisible (*master);
}

StudioMixer::~StudioMixer()
{
    setEdit (nullptr);
    viewport.setViewedComponent (nullptr);
}

void StudioMixer::pauseMeters()
{
    for (auto& strip : strips) strip->detachMeter();
    master->detachMeter();
}

void StudioMixer::setEdit (te::Edit* next)
{
    strips.clear();
    master->detachMeter();
    edit = next;
    lastSelection = {};
    viewport.setViewPosition (0, 0);
    resized();
}

void StudioMixer::refresh (const Array<te::AudioTrack*>& tracks, te::AudioTrack* selected, bool ready, bool canRoute)
{
    bool rebuild = tracks.size() != (int) strips.size();
    for (int i = 0; ! rebuild && i < tracks.size(); ++i)
        rebuild = tracks[i]->itemID != strips[(size_t) i]->getId();
    if (rebuild)
    {
        strips.clear();
        for (auto* t : tracks)
        {
            auto strip = std::make_unique<Strip> (*this, t->itemID, false);
            stripBank.addAndMakeVisible (*strip);
            strips.push_back (std::move (strip));
        }
        resized();
    }

    const auto selectedId = selected != nullptr ? selected->itemID : te::EditItemID {};
    if ((selectedId != lastSelection || rebuild) && selectedId.isValid())
        for (auto& strip : strips)
            if (strip->getId() == selectedId && ! viewport.getViewArea().contains (strip->getBounds()))
            {
                viewport.setViewPosition (strip->getX(), 0);
                break;
            }
    lastSelection = selectedId;
    const auto visible = viewport.getViewArea().expanded (2, 0);
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto& strip = *strips[(size_t) i];
        if (visible.intersects (strip.getBounds()))
            strip.refresh (tracks[i], tracks[i] == selected, ready, canRoute);
        else
            strip.detachMeter();
    }
    master->refresh (nullptr, false, ready, false);
}

void StudioMixer::resized()
{
    auto area = getLocalBounds().reduced (7, 2);
    const int stripWidth = getWidth() < 950 ? 118 : 134;
    master->setBounds (area.removeFromRight (stripWidth + 4).withTrimmedBottom (viewport.getScrollBarThickness()));
    viewport.setBounds (area);
    const auto height = jmax (0, area.getHeight() - viewport.getScrollBarThickness());
    stripBank.setSize (jmax (area.getWidth(), (int) strips.size() * stripWidth), height);
    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds ((int) i * stripWidth, 0, stripWidth, height);
}

void StudioMixer::paint (Graphics& g)
{
    if (strips.empty())
    {
        g.setColour (colours::secondary);
        g.drawText ("Open a project or add a track to start mixing.", viewport.getBounds(), Justification::centred);
    }
}
}
