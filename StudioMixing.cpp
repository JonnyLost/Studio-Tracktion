// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioMixing.h"
#include "SpatialRouting.h"
#include "StudioUI.h"

using namespace juce;
namespace te = tracktion;

namespace studio
{
namespace
{
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

int channelColumns (int width)
{
    return width >= appearance.span (785) ? 5 : width >= appearance.span (340) ? 2 : 1;
}

int cardHeight (int card)
{
    const auto& m = appearance;
    const int heading = m.line (12, 4);
    const int detail = 2 * m.line (12, 1);
    const int control = m.control (23);
    if (card == 0) return heading + detail + 3 * control + m.px (12);
    if (card == 2) return heading + control + m.px (6);
    return heading + detail + control + m.px (6);
}

int cardRowHeight (int start, int count)
{
    int height = 0;
    for (int i = start; i < jmin (start + count, 5); ++i)
        height = jmax (height, cardHeight (i));
    return height;
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
        auto area = getLocalBounds().reduced (appearance.px (2));
        if (meter == nullptr)
        {
            g.setColour (colours::secondary);
            g.setFont (uiFont (11.0f));
            g.drawText ("-", area, Justification::centred);
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

bool isInstrumentPlugin (te::Plugin& plugin)
{
    if (plugin.isSynth()) return true;
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        return external->desc.fileOrIdentifier.startsWithIgnoreCase ("AudioUnit:MusicDevice/")
            || (external->isAU() && external->getName().containsIgnoreCase ("ARIA Player"));
    return false;
}

te::Plugin* findInstrument (te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (isInstrumentPlugin (*plugin)) return plugin;
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
    if (vertical) volume.getProperties().set ("studioFader", true);
    volume.setTooltip ("Channel level. Double-click to return to 0 dB.");
    pan.setName ("Pan");
    pan.setTooltip ("Stereo balance. Double-click to centre.");
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
    const auto& m = appearance;
    const auto valueWidth = m.span (74);
    const auto valueHeight = m.line (13, 2);
    auto area = getLocalBounds();
    for (auto* slider : { &volume, &pan })
        slider->setTextBoxStyle (vertical ? Slider::TextBoxBelow : Slider::TextBoxRight,
                                 false, valueWidth, valueHeight);
    if (vertical)
    {
        panLabel.setBounds (area.removeFromTop (m.line (11, 1)));
        pan.setBounds (area.removeFromTop (m.line (13, 2) + m.px (15)));
        volumeLabel.setBounds (area.removeFromTop (m.line (11, 1)));
        volume.setBounds (area);
    }
    else
    {
        auto volumeRow = area.removeFromTop (m.control (25));
        volumeLabel.setBounds (volumeRow.removeFromLeft (m.span (61)));
        volume.setBounds (volumeRow);
        panLabel.setBounds (area.removeFromLeft (m.span (61)));
        pan.setBounds (area);
    }
}

ChannelInspector::ChannelInspector (TrackActions& callbacks) : actions (callbacks)
{
    for (Component* component : std::initializer_list<Component*> {
             &identity, &inputSummary, &instrument, &sends, &output, &ariaStatus, &audioInput, &midiStatus,
             &midiInput, &arm, &monitor, &editor, &inserts, &routing, &mix })
        addAndMakeVisible (*component);
    identity.setFont (FontOptions (14.0f, Font::bold));
    for (auto* label : { &inputSummary, &instrument, &sends, &output, &ariaStatus, &midiStatus })
    {
        label->setColour (Label::textColourId, colours::secondary);
        label->setFont (FontOptions (11.5f));
        label->setMinimumHorizontalScale (1.0f);
    }
    audioInput.onClick = [this] { actions.showMenu (id, TrackMenu::audioInput, audioInput); };
    midiInput.onClick = [this] { actions.showMenu (id, TrackMenu::midiInput, midiInput); };
    inserts.onClick = [this] { actions.showMenu (id, TrackMenu::inserts, inserts); };
    routing.onClick = [this] { actions.showMenu (id, TrackMenu::output, routing); };
    editor.onClick = [this] { actions.openInstrument (id); };
    arm.onClick = [this] { actions.toggleArm (id); };
    monitor.onClick = [this] { actions.toggleMonitoring (id); };
    monitor.setTooltip ("Listen to the assigned input. Changes also affect tracks using the same input.");
    addAndMakeVisible (stage);
    stage.onClick = [this] { actions.showStage (id); };
    stage.setTooltip ("Open this instrument on Stage and adjust its room send");
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

void ChannelInspector::refresh (te::AudioTrack* selected, bool ready, bool canRoute, const String& recovery, const te::MidiClip* selectedClip)
{
    id = selected != nullptr ? selected->itemID : te::EditItemID {};
    identity.setText (selected ? selected->getName() : "Select a track in the arrangement", dontSendNotification);
    auto* plugin = selected != nullptr ? findInstrument (*selected) : nullptr;
    instrument.setText (plugin != nullptr ? plugin->getName() : selected != nullptr ? "No instrument" : "-", dontSendNotification);
    output.setText (selected != nullptr ? outputLabel (*selected) : "-", dontSendNotification);
    instrument.setTooltip (instrument.getText());
    output.setTooltip (output.getText());
    identity.setTooltip (identity.getText());
    StringArray assigned, sendNames;
    bool isArmed = false, isMonitoring = false, fixedMonitor = false;
    if (selected != nullptr)
    {
        for (auto* instance : selected->edit.getAllInputDevices())
            if (instance->getTargets().contains (selected->itemID))
            {
                const auto& device = instance->getInputDevice();
                String name = device.getName();
                if (const auto* input = dynamic_cast<const te::MidiInputDevice*> (&device))
                {
                    StringArray channels;
                    for (int ch = 1; ch <= 16; ++ch)
                        if (input->isChannelAllowed (ch)) channels.add (String (ch));
                    name << (channels.size() == 16 ? " (all incoming channels)"
                          : channels.isEmpty() ? " (no incoming channels)"
                          : " (incoming " + channels.joinIntoString (", ") + ")");
                }
                assigned.add (name);
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
    inputSummary.setText (assigned.isEmpty() ? "No input assigned" : assigned.joinIntoString (" | "), dontSendNotification);
    inputSummary.setTooltip (assigned.joinIntoString ("\n"));
    sends.setText (sendNames.isEmpty() ? "No sends" : sendNames.joinIntoString (" | "), dontSendNotification);
    sends.setTooltip (sendNames.joinIntoString ("\n"));
    String transmit = "Select a MIDI clip to see its output channel";
    if (selectedClip != nullptr)
        transmit = selectedClip->getMPEMode() ? "MIDI clip  |  MPE"
            : "MIDI clip  |  Channel " + String (selectedClip->getMidiChannel().getChannelNumber());
    if (plugin != nullptr)
        transmit << "  |  Instrument receive: set in plug-in";
    midiStatus.setText (selected != nullptr ? transmit : String(), dontSendNotification);
    midiStatus.setTooltip (midiStatus.getText());
    ariaStatus.setText (recovery, dontSendNotification);
    ariaStatus.setTooltip (recovery);
    const bool configurable = canRoute && selected != nullptr && ! selected->isFrozen (te::Track::anyFreeze);
    for (auto* button : { &audioInput, &midiInput, &routing, &inserts })
        button->setEnabled (configurable);
    editor.setEnabled (ready && plugin != nullptr);
    editor.setButtonText (plugin != nullptr && plugin->getName().containsIgnoreCase ("ARIA") ? "Open ARIA" : "Open Instrument");
    stage.setEnabled (ready && selected != nullptr && ! spatial::isRoomReturn (*selected));
    arm.setEnabled (configurable && ! assigned.isEmpty());
    monitor.setEnabled (configurable && fixedMonitor);
    arm.setToggleState (isArmed, dontSendNotification);
    monitor.setToggleState (isMonitoring, dontSendNotification);
    mix.refresh (selected != nullptr ? lastTrackPlugin<te::VolumeAndPanPlugin> (*selected) : nullptr, ready);
}

int ChannelInspector::minimumContentHeight (int width) const
{
    const auto& m = appearance;
    int height = m.line (14, 5) + 2 * m.control (25) + 3 * m.line (12, 4) + m.px (4);
    const int count = channelColumns (width);
    for (int i = 0; i < 5; i += count)
        height += cardRowHeight (i, count) + m.px (3);
    return height;
}

void ChannelInspector::appearanceChanged()
{
    resized();
    repaint();
}

void ChannelInspector::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (8), m.px (2));
    identity.setBounds (area.removeFromTop (m.line (14, 5)));
    const int count = channelColumns (getWidth());
    for (int start = 0; start < 5; start += count)
    {
        auto row = area.removeFromTop (cardRowHeight (start, count));
        area.removeFromTop (m.px (3));
        for (int i = 0; i < count && start + i < 5; ++i)
            columns[(size_t) (start + i)] = row.removeFromLeft (row.getWidth() / (count - i)).reduced (m.px (2), 0);
    }
    auto bottom = area.removeFromTop (2 * m.control (25));
    mix.setBounds (bottom.removeFromLeft (jmin (m.span (465), bottom.getWidth())));
    midiStatus.setBounds (area.removeFromTop (2 * m.line (12, 4)));
    ariaStatus.setBounds (area.removeFromTop (m.line (12, 4)));
    const auto heading = m.line (12, 4);
    const auto detail = 2 * m.line (12, 1);
    const auto control = m.control (23);
    auto input = columns[0].reduced (m.px (4), m.px (2)).withTrimmedTop (heading);
    inputSummary.setBounds (input.removeFromTop (detail));
    audioInput.setBounds (input.removeFromTop (control));
    input.removeFromTop (m.px (3));
    midiInput.setBounds (input.removeFromTop (control));
    input.removeFromTop (m.px (3));
    auto toggles = input.removeFromTop (control);
    arm.setBounds (toggles.removeFromLeft (toggles.getWidth() / 3));
    monitor.setBounds (toggles.reduced (m.px (2), 0));
    auto instr = columns[1].reduced (m.px (4), m.px (2)).withTrimmedTop (heading);
    instrument.setBounds (instr.removeFromTop (detail));
    editor.setBounds (instr.removeFromTop (control));
    inserts.setBounds (columns[2].reduced (m.px (4), m.px (2)).withTrimmedTop (heading).withHeight (control));
    auto send = columns[3].reduced (m.px (4), m.px (2)).withTrimmedTop (heading);
    sends.setBounds (send.removeFromTop (detail));
    stage.setBounds (send.removeFromTop (control));
    auto out = columns[4].reduced (m.px (4), m.px (2)).withTrimmedTop (heading);
    output.setBounds (out.removeFromTop (detail));
    routing.setBounds (out.removeFromTop (control));
}

void ChannelInspector::paint (Graphics& g)
{
    constexpr const char* stages[] { "Input", "Instrument", "Inserts", "Sends", "Output" };
    const auto& m = appearance;
    const int count = channelColumns (getWidth());
    g.setFont (uiFont (12.0f, Font::bold));
    for (size_t i = 0; i < columns.size(); ++i)
    {
        g.setColour (i == 1 ? colours::selected : colours::control);
        g.fillRoundedRectangle (columns[i].toFloat(), (float) m.px (4));
        g.setColour (colours::divider);
        g.drawRoundedRectangle (columns[i].toFloat().reduced (0.5f), (float) m.px (4), 0.75f);
        g.setColour (colours::text);
        auto heading = columns[i].reduced (m.px (7), 0).withHeight (m.line (12, 4));
        if (i + 1 < columns.size() && ((int) i + 1) % count != 0)
            g.drawText (String::charToString (0x2192), heading.removeFromRight (m.span (17)), Justification::centred);
        g.drawFittedText (stages[i], heading, Justification::centredLeft, 1, 1.0f);
    }
}

class StudioMixer::Strip final : public Component
{
public:
    Strip (StudioMixer& owner, te::EditItemID trackId, bool isMaster)
        : mixer (owner), id (trackId), masterStrip (isMaster)
    {
        for (Component* component : std::initializer_list<Component*> {
                 &name, &identity, &source, &inserts, &room, &mute, &solo, &arm, &out, &mix, &meter })
            addAndMakeVisible (*component);
        identity.setColour (Label::textColourId, colours::secondary);
        identity.setFont (FontOptions (10.5f));
        identity.setJustificationType (Justification::centred);
        name.setButtonText (masterStrip ? "Master" : "Track");
        name.onClick = [this] { mixer.actions.selectTrack (id); };
        inserts.onClick = [this] { mixer.actions.showMenu (id, TrackMenu::inserts, inserts); };
        out.onClick = [this] { mixer.actions.showMenu (id, TrackMenu::output, out); };
        source.onClick = [this] { mixer.actions.showMenu (id, inputMenu, source); };
        room.onClick = [this] { mixer.actions.showStage (id); };
        arm.onClick = [this] { mixer.actions.toggleArm (id); };
        mute.onClick = [this] { mixer.actions.toggleTrackState (id, false); };
        solo.onClick = [this] { mixer.actions.toggleTrackState (id, true); };
        solo.setColour (TextButton::buttonOnColourId, colours::selected.brighter (0.3f));
        mute.setColour (TextButton::buttonOnColourId, colours::record.darker (0.65f));
        mute.setTooltip ("Mute track");
        solo.setTooltip ("Solo track");
        arm.setTitle ("Record arm");
        arm.setTooltip ("Arm the assigned input for recording");
        arm.setColour (TextButton::buttonOnColourId, colours::record.darker (0.5f));
        room.setTitle ("Stage room send");
        inserts.setTitle ("Insert effects");
        inserts.setTooltip ("Open the track's insert chain");
        mix.resolveVolume = [this]() -> te::VolumeAndPanPlugin::Ptr
        {
            if (! mixer.actions.canChangeMix() || mixer.edit == nullptr) return {};
            if (masterStrip) return mixer.edit->getMasterVolumePlugin();
            if (auto* t = trackFor (mixer.edit, id)) return t->getVolumePlugin();
            return {};
        };
        for (auto* c : { &source, &inserts, &room, &mute, &solo, &arm })
            c->setVisible (! masterStrip);
        name.setEnabled (! masterStrip);
        out.setEnabled (! masterStrip);
        if (masterStrip) identity.setText ("Main output", dontSendNotification);
    }

    void refresh (te::AudioTrack* track, bool isSelected, bool ready, bool canRoute,
                  const Array<te::InputDeviceInstance*>& inputs)
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
            meter.setTooltip ("Master stereo peak levels");
            auto* device = edit != nullptr ? edit->engine.getDeviceManager().getDefaultWaveOutDevice() : nullptr;
            out.setButtonText (device != nullptr ? device->getName() : "Default audio");
            mix.refresh (edit != nullptr ? edit->getMasterVolumePlugin().get() : nullptr, ready);
        }
        else if (track != nullptr)
        {
            name.setButtonText (track->getName());
            name.setTooltip (track->getName());
            auto* instrument = findInstrument (*track);
            const bool wetReturn = spatial::isRoomReturn (*track);
            const auto nextAccent = wetReturn ? colours::roomReturn : track->getColour().interpolatedWith (colours::action, 0.38f);
            if (accent != nextAccent) { accent = nextAccent; repaint(); }
            identity.setText (wetReturn ? "Auxiliary wet return" : instrument != nullptr ? instrument->getName() : "Audio / MIDI",
                              dontSendNotification);
            identity.setTooltip (identity.getText());
            String firstInput;
            int inputCount = 0;
            bool armed = false;
            inputMenu = instrument != nullptr ? TrackMenu::midiInput : TrackMenu::audioInput;
            for (auto* instance : inputs)
                if (instance->getTargets().contains (id))
                {
                    if (inputCount++ == 0)
                    {
                        const auto& device = instance->getInputDevice();
                        firstInput = device.getName();
                        if (dynamic_cast<const te::MidiInputDevice*> (&device) != nullptr) inputMenu = TrackMenu::midiInput;
                    }
                    armed |= instance->isRecordingEnabled (id);
                }
            source.setButtonText (wetReturn ? "Stage sends" : firstInput.isEmpty() ? "Input..." : firstInput);
            source.setTooltip (wetReturn ? "Receives the shared Stage sends"
                : inputCount == 0 ? "Choose an input. Both input menus are available in Channel."
                : firstInput + (inputCount > 1 ? " (+" + String (inputCount - 1) + " more)" : String()));
            source.setEnabled (editable && ! wetReturn);
            arm.setEnabled (editable && inputCount > 0 && ! wetReturn);
            arm.setToggleState (armed, dontSendNotification);
            const bool sending = ! wetReturn && spatial::hasRoomSend (*track);
            room.setEnabled (! wetReturn && ready);
            room.setToggleState (sending, dontSendNotification);
            room.setTooltip (sending ? "Room send enabled. Open this seat on Stage." : "Open Stage to set the room send");
            out.setButtonText (outputLabel (*track));
            out.setTooltip (out.getButtonText());
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
                                     : levelPlugin == nullptr ? "This track has no Level Meter insert"
                                     : wetReturn ? "Wet room return after the fader" : "Track peak levels");
        }
        meter.sample();
    }

    void detachMeter() { meter.attach (nullptr); }
    te::EditItemID getId() const { return id; }

    void paint (Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const auto radius = (float) appearance.px (5);
        g.setColour (selected ? colours::selected : masterStrip ? colours::raised : colours::panel);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (selected ? colours::focus : colours::divider);
        g.drawRoundedRectangle (bounds, radius, selected ? 1.25f : 0.75f);
        g.setColour (accent);
        g.fillRoundedRectangle (bounds.reduced (radius, 0.0f).withHeight (2.0f), 1.0f);
    }

    void resized() override
    {
        const auto& m = appearance;
        auto area = getLocalBounds().reduced (m.px (6), m.px (4));
        name.setBounds (area.removeFromTop (m.control (22)));
        identity.setBounds (area.removeFromTop (m.line (11, 3)));
        source.setBounds (area.removeFromTop (m.control (22)));
        auto chain = area.removeFromTop (m.control (22));
        inserts.setBounds (chain.removeFromLeft (chain.getWidth() / 2).reduced (m.px (1)));
        room.setBounds (chain.reduced (m.px (1)));
        out.setBounds (area.removeFromBottom (m.control (22)));
        auto toggles = area.removeFromBottom (m.control (22));
        if (! masterStrip)
        {
            mute.setBounds (toggles.removeFromLeft (toggles.getWidth() / 3).reduced (m.px (1)));
            solo.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (m.px (1)));
            arm.setBounds (toggles.reduced (m.px (1)));
        }
        meter.setBounds (area.removeFromRight (m.px (20)).withTrimmedTop (2 * m.line (11, 1) + m.line (13, 2) + m.px (15)));
        mix.setBounds (area);
    }

private:
    StudioMixer& mixer;
    te::EditItemID id;
    bool masterStrip, selected = false;
    TrackMenu inputMenu = TrackMenu::audioInput;
    Colour accent = colours::action;
    TextButton name, source, inserts { "FX" }, room { "Room" }, mute { "M" }, solo { "S" }, arm { "R" }, out;
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
    repaint();
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
        repaint(); // Clear stale "open a project" empty-state text when strips arrive.
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
    const auto inputs = edit != nullptr ? edit->getAllInputDevices() : Array<te::InputDeviceInstance*>();
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto& strip = *strips[(size_t) i];
        if (visible.intersects (strip.getBounds()))
            strip.refresh (tracks[i], tracks[i] == selected, ready, canRoute, inputs);
        else
            strip.detachMeter();
    }
    master->refresh (nullptr, false, ready, false, inputs);
}

void StudioMixer::appearanceChanged()
{
    viewport.setScrollBarThickness (appearance.scrollbar());
    resized();
    for (auto& strip : strips) strip->resized();
    master->resized();
    repaint();
}

int StudioMixer::minimumContentHeight() const
{
    const auto& m = appearance;
    return m.px (12) + 5 * m.control (22) + m.line (11, 3) + 2 * m.line (11, 1)
        + 2 * m.line (13, 2) + m.px (58) + m.scrollbar();
}

void StudioMixer::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (7), m.px (2));
    const int stripWidth = m.mixerStripWidth (getWidth());
    master->setBounds (area.removeFromRight (stripWidth + m.px (4)).withTrimmedBottom (viewport.getScrollBarThickness()));
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
