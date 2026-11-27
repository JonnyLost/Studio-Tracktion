// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioMixing.h"
#include "StudioSampler.h"
#include "StudioSynth.h"
#include "AtelierInspectorPanel.h"
#include "StudioInstrumentIcons.h"
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
        control->setSliderStyle (control == &volume && vertical ? Slider::LinearVertical
                                : control == &pan && vertical ? Slider::RotaryHorizontalVerticalDrag
                                : Slider::LinearHorizontal);
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
    volume.setTextBoxStyle (vertical ? Slider::TextBoxBelow : Slider::TextBoxRight,
                            false, vertical ? m.span (64) : valueWidth, valueHeight);
    pan.setTextBoxStyle (vertical ? Slider::TextBoxBelow : Slider::TextBoxRight,
                         false, vertical ? m.span (56) : valueWidth, valueHeight);
    if (vertical)
    {
        panLabel.setBounds (area.removeFromTop (m.line (11, 1)));
        // Rotary pan provides precise centring without consuming strip width.
        pan.setBounds (area.removeFromTop (m.control (47, 11) + m.px (5)));
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
            if (auto* t = track()) return te::VolumeAndPanPlugin::Ptr (lastTrackPlugin<te::VolumeAndPanPlugin> (*t));
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
    int height = m.line (14, 5) + (showMixControls ? 2 * m.control (25) : 0)
               + 3 * m.line (12, 4) + m.px (4);
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
    if (showMixControls)
    {
        auto bottom = area.removeFromTop (2 * m.control (25));
        mix.setBounds (bottom.removeFromLeft (jmin (m.span (465), bottom.getWidth())));
    }
    else mix.setBounds ({});
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

// The Inspector's selected-track channel strip uses the same volume plug-in
// and level meter as the main Mixer. It is a view, never another signal path.
class InspectorChannelStrip final : public Component
{
public:
    explicit InspectorChannelStrip (TrackActions& callbacks) : actions (callbacks)
    {
        for (Component* item : std::initializer_list<Component*> {
                 &trackName, &colourButton, &iconButton, &midiIn, &midiOut, &mix, &meter })
            addAndMakeVisible (*item);
        for (auto* label : { &trackName, &midiIn, &midiOut })
        {
            label->setColour (Label::textColourId, colours::text);
            label->setMinimumHorizontalScale (0.85f);
        }
        trackName.setFont (FontOptions (12.0f, Font::bold));
        midiIn.setFont (FontOptions (11.0f));
        midiOut.setFont (FontOptions (11.0f));
        colourButton.setButtonText ("Colour");
        colourButton.setTooltip ("Choose the selected track's colour");
        iconButton.setButtonText ("Icon");
        iconButton.setTooltip ("Choose the selected track's icon");
        colourButton.onClick = [this] { if (actions.editTrackColour) actions.editTrackColour (id, colourButton); };
        iconButton.onClick = [this] { if (actions.editTrackIcon) actions.editTrackIcon (id, iconButton); };
        mix.resolveVolume = [this]() -> te::VolumeAndPanPlugin::Ptr
        {
            if (! actions.canChangeMix || ! actions.canChangeMix()) return {};
            if (auto* t = trackFor (edit, id)) return te::VolumeAndPanPlugin::Ptr (lastTrackPlugin<te::VolumeAndPanPlugin> (*t));
            return {};
        };
    }

    ~InspectorChannelStrip() override { meter.attach (nullptr); }

    void setEdit (te::Edit* next)
    {
        meter.attach (nullptr);
        edit = next;
        id = {};
    }

    void refresh (te::AudioTrack* track, bool ready, const te::MidiClip* selectedClip)
    {
        id = track != nullptr ? track->itemID : te::EditItemID {};
        const bool enabled = track != nullptr;
        trackName.setText (enabled ? track->getName() : "Select an instrument track", dontSendNotification);
        trackName.setTooltip (trackName.getText());

        colourButton.setEnabled (enabled && ready);
        iconButton.setEnabled (enabled && ready);
        if (! enabled)
        {
            midiIn.setText ("MIDI In: —", dontSendNotification);
            midiOut.setText ("MIDI Out: —", dontSendNotification);
            iconButton.setButtonText ("Icon");
            meter.attach (nullptr);
            mix.refresh (nullptr, false);
            return;
        }

        colourButton.setColour (TextButton::buttonColourId, track->getColour());
        const auto icon = track->state.getProperty (Identifier ("studioTrackIcon")).toString();
        iconButton.setButtonText (icon.isEmpty() ? "Icon" : icon);
        iconButton.setTooltip (icon.isEmpty() ? "Choose an instrument icon" : "Instrument icon: " + icon);

        StringArray inputs;
        for (auto* input : track->edit.getAllInputDevices())
            if (input->getTargets().contains (id))
                if (dynamic_cast<const te::MidiInputDevice*> (&input->getInputDevice()) != nullptr)
                    inputs.addIfNotAlreadyThere (input->getInputDevice().getName());

        const auto inputName = inputs.isEmpty() ? "Not assigned" : inputs.joinIntoString (", ");
        midiIn.setText ("MIDI In: " + inputName, dontSendNotification);
        midiIn.setTooltip ("MIDI input device: " + inputName);

        const auto out = selectedClip != nullptr
            ? (selectedClip->getMPEMode() ? String ("MPE") :
                "Ch " + String (selectedClip->getMidiChannel().getChannelNumber()))
            : outputLabel (*track);
        midiOut.setText ("Output: " + out, dontSendNotification);
        midiOut.setTooltip ("Output routing / selected MIDI clip channel: " + out);

        mix.refresh (lastTrackPlugin<te::VolumeAndPanPlugin> (*track), ready);
        auto* plugin = lastTrackPlugin<te::LevelMeterPlugin> (*track);
        meter.attach (plugin != nullptr ? &plugin->measurer : nullptr);
        meter.sample();
    }

    void resized() override
    {
        const auto& m = appearance;
        auto area = getLocalBounds().reduced (m.px (6), m.px (5));
        const auto heading = area.removeFromTop (m.control (23, 11));
        trackName.setBounds (heading);
        auto settings = area.removeFromTop (m.control (25, 11));
        colourButton.setBounds (settings.removeFromLeft (settings.getWidth() / 2).reduced (m.px (2), 0));
        iconButton.setBounds (settings.reduced (m.px (2), 0));
        auto info = area.removeFromTop (2 * m.line (11, 2));
        midiIn.setBounds (info.removeFromTop (m.line (11, 2)));
        midiOut.setBounds (info);
        const int meterWidth = m.px (22);
        meter.setBounds (area.removeFromRight (meterWidth).reduced (m.px (1), m.px (2)));
        mix.setBounds (area);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (colours::panel); // Opaque backing: never show the timeline through the drawer.
        g.setColour (colours::control);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), (float) appearance.px (5));
        g.setColour (colours::divider);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f),
                                (float) appearance.px (5), 1.0f);
    }

private:
    TrackActions& actions;
    te::Edit* edit = nullptr;
    te::EditItemID id;
    Label trackName, midiIn, midiOut;
    TextButton colourButton, iconButton;
    MixControls mix { true };
    StereoMeter meter;
};

InspectorDrawer::InspectorDrawer (TrackActions& actions)
    : actions (actions), inspector (actions),
      channelStrip (std::make_unique<InspectorChannelStrip> (actions)),
      atelier (std::make_unique<AtelierInspectorPanel> (actions))
{
    setOpaque (true);
    setRepaintsOnMouseActivity (true);
    addAndMakeVisible (title);
    title.setText ("Inspector", dontSendNotification);
    title.setFont (FontOptions (14.0f, Font::bold));
    title.setColour (Label::textColourId, colours::text);
    title.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (close);
    close.setTitle ("Close selected-track Inspector");
    close.setTooltip ("Hide Inspector and return to the full arrangement");
    close.onClick = [this] { if (onClose) onClose(); };
    scroll.setScrollBarsShown (true, false);
    scroll.setScrollBarThickness (appearance.scrollbar());
    inspector.setShowMixControls (false);
    scroll.setViewedComponent (&inspector, false);
    addAndMakeVisible (scroll);
    addAndMakeVisible (*channelStrip);
    atelierScroll.setScrollBarsShown (true, false);
    atelierScroll.setScrollBarThickness (appearance.scrollbar());
    atelierScroll.setViewedComponent (atelier.get(), false);
    addAndMakeVisible (atelierScroll);
    atelierScroll.setVisible (false);
    addAndMakeVisible (details);
    addAndMakeVisible (quickFx);
    addChildComponent (instrumentSurface);
    instrumentSurface.setTooltip ("Switch between instrument controls and full channel settings.");
    instrumentSurface.onClick = [this]
    {
        preferTrackSettings = ! preferTrackSettings;
        if (preferTrackSettings) detailsOpen = true;
    };
    quickFx.setTooltip ("Open, edit or safely remove the selected track's instrument and effects");
    quickFx.onClick = [this]
    {
        if (selectedTrackId.isValid() && this->actions.showMenu)
            this->actions.showMenu (selectedTrackId, TrackMenu::inserts, quickFx);
    };
    details.setTooltip ("Expand or collapse the selected track's input, instrument, effects, sends and output");
    details.onClick = [this]
    {
        setDetailsExpanded (! detailsOpen);
    };
}

InspectorDrawer::~InspectorDrawer()
{
    scroll.setViewedComponent (nullptr);
    atelierScroll.setViewedComponent (nullptr);
}

void InspectorDrawer::setEdit (te::Edit* next)
{
    inspector.setEdit (next);
    channelStrip->setEdit (next);
    atelier->setEdit (next);
    selectedTrackId = {};
    preferTrackSettings = false;
    scroll.setViewPosition (0, 0);
    atelierScroll.setViewPosition (0, 0);
}

void InspectorDrawer::refresh (te::AudioTrack* track, bool ready, bool canRoute,
                               const String& recovery, const te::MidiClip* selectedClip)
{
    const auto nextTrack = track != nullptr ? track->itemID : te::EditItemID {};
    if (nextTrack != selectedTrackId) { preferTrackSettings = false; atelierScroll.setViewPosition (0, 0); }
    selectedTrackId = nextTrack;
    auto* instrument = track != nullptr ? findInstrument (*track) : nullptr;
    const bool eligible = dynamic_cast<te::ExternalPlugin*> (instrument) != nullptr
        || dynamic_cast<sampler::Plugin*> (instrument) != nullptr || dynamic_cast<synth::Plugin*> (instrument) != nullptr;
    const bool eligibilityChanged = atelierEligible != eligible;
    atelierEligible = eligible;
    const bool needsAtelier = atelierEligible && ! preferTrackSettings;
    if (needsAtelier)
    {
        // Avoid an unused meter client and non-visible channel Inspector
        // work while Cinematic Atelier owns the right sidebar.
        channelStrip->refresh (nullptr, false, nullptr);
    }
    else
    {
        inspector.refresh (track, ready, canRoute, recovery, selectedClip);
        channelStrip->refresh (track, ready, selectedClip);
    }
    if (atelierMode != needsAtelier || eligibilityChanged)
    {
        atelierMode = needsAtelier;
        atelierScroll.setViewPosition (0, 0);
        resized();
    }
    if (atelierMode)
    {
        atelier->refresh (track, ready);
        const auto width=atelier->getWidth();
        const auto height=jmax(atelierScroll.getHeight(),atelier->desiredHeight(width));
        if(atelier->getHeight()!=height)atelier->setSize(width,height);
    }
    else atelier->suspend();
    quickFx.setEnabled (track != nullptr && canRoute);
    // Avoid encoded separators that can render as stray characters with
    // some Intel macOS font/fallback combinations.
    title.setText (track != nullptr ? track->getName() + " Inspector" : "Inspector", dontSendNotification);
    title.setTooltip (title.getText());
}

te::EditItemID InspectorDrawer::inspectedTrack() const
{
    return selectedTrackId;
}

void InspectorDrawer::appearanceChanged()
{
    scroll.setScrollBarThickness (appearance.scrollbar());
    atelierScroll.setScrollBarThickness (appearance.scrollbar());
    inspector.appearanceChanged();
    atelier->appearanceChanged();
    resized();
    repaint();
}

void InspectorDrawer::reflectPerformance (te::EditItemID id, const KeyboardPerformanceValues& values)
{
    if (atelierMode && selectedTrackId == id) atelier->reflectPerformance (id, values);
}

void InspectorDrawer::suspendAtelier()
{
    if (atelier != nullptr) atelier->suspend();
}

void InspectorDrawer::setDetailsExpanded (bool expanded)
{
    detailsOpen = expanded;
    resized();
}

void InspectorDrawer::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (8), m.px (8));
    auto header = area.removeFromTop (m.control (32));
    close.setBounds (header.removeFromRight (m.span (60)).reduced (m.px (2), 0));
    title.setBounds (header);
    area.removeFromTop (m.px (5));
    instrumentSurface.setVisible (atelierEligible);
    if (atelierEligible)
    {
        auto surfaceRow = area.removeFromTop (m.control (28));
        instrumentSurface.setButtonText (atelierMode ? "Track Settings" : "Instrument");
        if (atelierMode)
            quickFx.setBounds (surfaceRow.removeFromRight (m.span (52)).reduced (m.px (2), 0));
        instrumentSurface.setBounds (surfaceRow.reduced (m.px (2), 0));
        area.removeFromTop (m.px (4));
    }
    if (atelierMode)
    {
        channelStrip->setVisible (false);
        scroll.setVisible (false);
        details.setVisible (false);
        quickFx.setVisible (true);
        atelierScroll.setVisible (true);
        atelierScroll.setBounds (area);
        const int width = jmax (1, area.getWidth() - m.scrollbar() - m.px (1));
        atelier->setSize (width, jmax (area.getHeight(), atelier->desiredHeight (width)));
        return;
    }
    channelStrip->setVisible (true);
    details.setVisible (true);
    quickFx.setVisible (true);
    atelierScroll.setVisible (false);
    atelierScroll.setBounds ({});
    const int channelHeight = jmin (m.span (278), jmax (m.span (223), area.getHeight() * 61 / 100));
    channelStrip->setBounds (area.removeFromTop (jmin (channelHeight, area.getHeight())));
    area.removeFromTop (m.px (4));
    auto settingsRow = area.removeFromTop (m.control (26, 11));
    quickFx.setBounds (settingsRow.removeFromRight (m.span (58)).reduced (m.px (2), 0));
    details.setBounds (settingsRow);
    details.setButtonText (detailsOpen ? "Track Settings  v" : "Track Settings  >");
    area.removeFromTop (m.px (3));
    scroll.setVisible (detailsOpen);
    scroll.setBounds (detailsOpen ? area : Rectangle<int>());
    if (detailsOpen)
    {
        const int width = jmax (1, area.getWidth() - m.scrollbar() - m.px (2));
        const int height = jmax (area.getHeight(),
                                 inspector.minimumContentHeight (width) + m.px (18));
        inspector.setSize (width, height);
    }
}

#if STUDIO_SPATIAL_TESTS
bool InspectorDrawer::verifyAtelierInspector()
{
    return atelierMode && atelierScroll.isVisible() && ! channelStrip->isVisible()
        && atelier->verifyInspectorBindings();
}
#endif

void InspectorDrawer::paint (Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour (colours::panel);
    g.fillRect (bounds);
    g.setColour (colours::divider);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.drawHorizontalLine (appearance.control (40), 0.0f, (float) getWidth());
    // Dedicated visible resize grip on the left edge.
    g.setColour (colours::secondary.withAlpha (0.75f));
    const auto x = (float) appearance.px (3);
    const auto y = (float) getHeight() * 0.5f;
    for (int i = -2; i <= 2; ++i)
        g.fillRoundedRectangle (x, y + i * (float) appearance.px (7),
                                (float) appearance.px (2), (float) appearance.px (4), 1.0f);
}

juce::MouseCursor InspectorDrawer::getMouseCursor()
{
    return juce::MouseCursor (getMouseXYRelative().x <= appearance.px (12)
        ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void InspectorDrawer::mouseDown (const juce::MouseEvent& event)
{
    resizingWidth = event.x <= appearance.px (12);
    if (resizingWidth)
    {
        resizeStartX = event.getScreenX();
        resizeStartWidth = getWidth();
    }
}

void InspectorDrawer::mouseDrag (const juce::MouseEvent& event)
{
    if (resizingWidth && onWidthChanged)
        onWidthChanged (resizeStartWidth + resizeStartX - event.getScreenX());
}

void InspectorDrawer::mouseUp (const juce::MouseEvent&)
{
    resizingWidth = false;
}

class StudioMixer::Strip final : public Component
{
public:
    Strip (StudioMixer& owner, te::EditItemID trackId, bool isMaster)
        : mixer (owner), id (trackId), masterStrip (isMaster)
    {
        for (Component* component : std::initializer_list<Component*> {
                 &name, &identity, &inserts, &room, &mute, &solo, &arm, &out, &mix, &meter })
            addAndMakeVisible (*component);
        identity.setColour (Label::textColourId, colours::secondary);
        identity.setFont (FontOptions (10.5f));
        identity.setJustificationType (Justification::centred);
        name.setButtonText (masterStrip ? "Master" : "Track");
        name.onClick = [this] { mixer.actions.selectTrack (id); };
        inserts.onClick = [this]
        {
            if (mixer.edit != nullptr
                && dynamic_cast<te::FolderTrack*> (te::findTrackForID (*mixer.edit, id)) != nullptr)
            {
                if (mixer.actions.showGroupEffects) mixer.actions.showGroupEffects (id, inserts);
            }
            else if (mixer.actions.showMenu) mixer.actions.showMenu (id, TrackMenu::inserts, inserts);
        };
        out.onClick = [this] { mixer.actions.showMenu (id, TrackMenu::output, out); };
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
            if (auto* t = trackFor (mixer.edit, id))
                return te::VolumeAndPanPlugin::Ptr (lastTrackPlugin<te::VolumeAndPanPlugin> (*t));
            if (auto* group = dynamic_cast<te::FolderTrack*> (te::findTrackForID (*mixer.edit, id)))
                return te::VolumeAndPanPlugin::Ptr (group->getVolumePlugin());
            return {};
        };
        for (auto* c : { &inserts, &room, &mute, &solo, &arm })
            c->setVisible (! masterStrip);
        name.setEnabled (! masterStrip);
        out.setEnabled (! masterStrip);
        if (masterStrip) identity.setText ("Main output", dontSendNotification);
    }

    void refresh (te::Track* source, bool isSelected, bool ready, bool canRoute,
                  const Array<te::InputDeviceInstance*>& inputs)
    {
        if (selected != isSelected)
        {
            selected = isSelected;
            name.setToggleState (selected, dontSendNotification);
            repaint();
        }
        auto* track = dynamic_cast<te::AudioTrack*> (source);
        auto* group = dynamic_cast<te::FolderTrack*> (source);
        const auto editable = canRoute && source != nullptr && ! source->isFrozen (te::Track::anyFreeze);
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
            const bool wetReturn = spatial::isRoomReturn (*track);
            name.setButtonText (wetReturn ? "Stage Reverb" : track->getName());
            const auto icon = track->state.getProperty (Identifier ("studioTrackIcon")).toString();
            name.setTooltip (wetReturn
                ? "Studio Room Return · managed effects return. Select to control its wet volume, pan and output."
                : (icon.isNotEmpty() ? icon + " · " : String()) + track->getName() + " (select track)");
            auto* instrument = findInstrument (*track);
            const auto nextAccent = wetReturn ? colours::roomReturn : track->getColour().interpolatedWith (colours::action, 0.38f);
            if (accent != nextAccent) { accent = nextAccent; repaint(); }
            identity.setText (wetReturn ? "WET FX RETURN" : instrument != nullptr ? instrument->getName() : "Audio / MIDI",
                              dontSendNotification);
            identity.setTooltip (identity.getText());
            // Count only user-facing instruments/effects; do not count Studio's
            // mixer gain, meters, room sends or stage spatial processors.
            int insertsCount = 0;
            for (auto* plugin : track->pluginList.getPlugins())
            {
                if (plugin == nullptr || dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr
                    || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr
                    || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr
                    || dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr
                    || plugin->getPluginType() == "studioStagePanV1"
                    || plugin->getPluginType() == "studioWetRoomV1")
                    continue;
                ++insertsCount;
            }
            inserts.setButtonText (wetReturn ? "Managed Room FX"
                : insertsCount > 0 ? "FX " + String (insertsCount) : "FX");
            inserts.setTooltip (wetReturn
                ? "Studio owns the native room return. Adjust its wet level here and room sends on Stage."
                : insertsCount > 0
                ? String (insertsCount) + " instrument/effect insert(s). Open to edit or remove."
                : "No instrument/effect inserts. Open to add an effect.");
            int inputCount = 0;
            bool armed = false;
            for (auto* instance : inputs)
                if (instance->getTargets().contains (id))
                {
                    ++inputCount;
                    armed |= instance->isRecordingEnabled (id);
                }
            // Input routing lives in the selected-track Inspector or the
            // arrangement's I button, not in the compact mixer channel.
            arm.setEnabled (editable && inputCount > 0 && ! wetReturn);
            const bool sending = ! wetReturn && spatial::hasRoomSend (*track);
            const bool returnLayoutChanged = (room.isVisible() == wetReturn)
                || (arm.isVisible() == wetReturn);
            arm.setVisible (! wetReturn);
            arm.setToggleState (armed, dontSendNotification);
            room.setEnabled (! wetReturn && ready);
            room.setVisible (! wetReturn);
            if (returnLayoutChanged) resized();
            room.setToggleState (sending, dontSendNotification);
            room.setTooltip (sending ? "Room send enabled. Open this seat on Stage." : "Open Stage to set the room send");
            out.setButtonText (outputLabel (*track));
            out.setTooltip (out.getButtonText());
            out.setEnabled (editable);
            // The shared room chain must remain managed by Studio: do not
            // offer destructive insert editing on this special return strip.
            inserts.setEnabled (editable && ! wetReturn);
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
        else if (group != nullptr)
        {
            name.setButtonText (group->getName());
            name.setTooltip ("Submix folder: select or use the FX button to edit group inserts.");
            if (accent != colours::action) { accent = colours::action; repaint(); }
            const auto count = group->getInputTracks().size();
            identity.setText ("GROUP BUS · " + String (count) + " input(s)", dontSendNotification);
            identity.setTooltip ("Native Tracktion summing submix with independent level, pan and insert chain");
            inserts.setButtonText ("Group FX");
            inserts.setTooltip ("Open the group's actual plug-in chain and add EQ, compression or delay");
            inserts.setEnabled (editable);
            const bool relayout = room.isVisible() || arm.isVisible();
            arm.setVisible (false);
            room.setVisible (false);
            if (relayout) resized();
            out.setButtonText ("Group Sum");
            out.setTooltip ("Native submix: nested tracks are summed through their configured output.");
            out.setEnabled (false);
            mute.setToggleState (group->isMuted (false), dontSendNotification);
            solo.setToggleState (group->isSolo (false), dontSendNotification);
            mute.setEnabled (ready);
            solo.setEnabled (ready);
            mix.refresh (group->getVolumePlugin(), ready);
            auto* level = group->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>();
            meter.attach (level != nullptr ? &level->measurer : nullptr);
            meter.setTooltip (level != nullptr ? "Group submix level" : "No group level meter available");
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
        if (! masterStrip)
            drawInstrumentIcon(g,Rectangle<float>((float)appearance.px(5),(float)appearance.px(6),(float)appearance.px(17),(float)appearance.px(17)),instrumentIcon(name.getButtonText()),colours::action);
    }

    void resized() override
    {
        const auto& m = appearance;
        auto area = getLocalBounds().reduced (m.px (6), m.px (4));
        auto nameRow = area.removeFromTop (m.control (24));
        if (! masterStrip) nameRow.removeFromLeft(m.px(18));
        name.setBounds (nameRow);
        identity.setBounds (area.removeFromTop (m.line (11, 3)));
        area.removeFromTop (m.px (3));
        if (! masterStrip)
        {
            // The shared Stage Reverb has no outgoing room send. Give its
            // managed-return identifier the space of both unused buttons.
            inserts.setBounds (area.removeFromTop (m.control (room.isVisible() ? 19 : 38, 10))
                                   .reduced (m.px (1), 0));
            room.setBounds (room.isVisible()
                ? area.removeFromTop (m.control (19, 10)).reduced (m.px (1), 0)
                : Rectangle<int>());
        }
        else
        {
            inserts.setBounds ({});
            room.setBounds ({});
        }
        out.setBounds (area.removeFromBottom (m.control (22)));
        if (! masterStrip)
        {
            auto toggles = area.removeFromBottom (m.control (23));
            const bool canArm = arm.isVisible();
            mute.setBounds (toggles.removeFromLeft (toggles.getWidth() / (canArm ? 3 : 2)).reduced (m.px (1)));
            solo.setBounds (toggles.removeFromLeft (toggles.getWidth() / (canArm ? 2 : 1)).reduced (m.px (1)));
            arm.setBounds (canArm ? toggles.reduced (m.px (1)) : Rectangle<int>());
        }
        else
            for (auto* toggle : { &mute, &solo, &arm })
                toggle->setBounds ({});
        area.removeFromBottom (m.px (4));
        meter.setBounds (area.removeFromRight (m.px (19)).withTrimmedTop (
            m.line (11, 1) + m.line (13, 2) + m.px (13)));
        mix.setBounds (area);
    }

private:
    StudioMixer& mixer;
    te::EditItemID id;
    bool masterStrip, selected = false;
    Colour accent = colours::action;
    TextButton name, inserts { "FX" }, room { "Stage" }, mute { "M" }, solo { "S" }, arm { "A" }, out;
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

void StudioMixer::refresh (const Array<te::AudioTrack*>& tracks, te::Track* selected, bool ready, bool canRoute)
{
    // Real native FolderTrack buses appear at their place in the session.
    // Preserve the caller's exact AudioTrack filter for small-layout review.
    Array<te::Track*> channels;
    if (edit != nullptr)
        for (auto* t : te::getAllTracks (*edit))
        {
            if (dynamic_cast<te::FolderTrack*> (t) != nullptr)
                channels.add (t);
            else if (auto* audio = dynamic_cast<te::AudioTrack*> (t))
                if (tracks.contains (audio)) channels.add (audio);
        }
    bool rebuild = channels.size() != (int) strips.size();
    for (int i = 0; ! rebuild && i < channels.size(); ++i)
        rebuild = channels[i]->itemID != strips[(size_t) i]->getId();
    if (rebuild)
    {
        strips.clear();
        for (auto* t : channels)
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
    for (int i = 0; i < channels.size(); ++i)
    {
        auto& strip = *strips[(size_t) i];
        if (visible.intersects (strip.getBounds()))
            strip.refresh (channels[i], channels[i] == selected, ready, canRoute, inputs);
        else
            strip.detachMeter();
    }
    master->refresh (nullptr, false, ready, false, inputs);
}

#if STUDIO_SPATIAL_TESTS
bool StudioMixer::hasChannelForTrack (te::EditItemID target) const
{
    for (const auto& strip : strips)
        if (strip->getId() == target) return true;
    return false;
}
#endif

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
    // Include a full-size rotary pan, stacked FX/Stage and usable fader.
    // The Mixer-focused split mode requests enough height to avoid clipping.
    return m.px (20) + 2 * m.control (19, 10) + 3 * m.control (23)
        + m.line (11, 3) + 2 * m.line (11, 1) + m.control (47, 11)
        + m.px (125) + m.scrollbar();
}

void StudioMixer::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (7), m.px (2));
    const int stripWidth = m.mixerStripWidth (getWidth());
    const int masterSpace = jmin (area.getWidth(), stripWidth + m.px (4));
    const int bankCapacity = jmax (0, area.getWidth() - masterSpace);
    // Small sessions should look like a single coherent mixer bank, not
    // a few tracks on the left and a disconnected Master at the far right.
    // Once the tracks exceed the bank's available width, the Master stays
    // pinned while the channels scroll beneath the viewport.
    const int bankWidth = strips.empty() ? bankCapacity
        : jmin (bankCapacity, (int) strips.size() * stripWidth + m.px (4));
    auto bankBounds = area.removeFromLeft (bankWidth);
    master->setBounds (area.removeFromLeft (masterSpace).withTrimmedBottom (viewport.getScrollBarThickness()));
    viewport.setBounds (bankBounds);
    const auto height = jmax (0, bankBounds.getHeight() - viewport.getScrollBarThickness());
    stripBank.setSize (jmax (bankBounds.getWidth(), (int) strips.size() * stripWidth), height);
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
