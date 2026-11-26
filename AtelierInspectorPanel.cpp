// SPDX-License-Identifier: GPL-3.0-or-later
#include "AtelierInspectorPanel.h"
#include "SpatialState.h"
#include "SpatialRouting.h"
#include "StudioUI.h"
#include "StudioGeneralMidi.h"
#include "StudioSampler.h"
#include "StudioLibraryPresentation.h"
#include <cmath>

namespace studio
{
using namespace juce;
namespace te = tracktion;
namespace
{
// Inspector and floating editor share Studio's neutral slate-blue palette.
const Colour background = colours::canvas;
const Colour panel = colours::panel;
const Colour accent = colours::focus;
const Colour primary = colours::text;
const Colour secondary = colours::secondary;
struct Controller { const char* name; int cc, value; };
constexpr std::array<Controller, 8> controllers {{
    { "Mod", 1, 0 }, { "Breath", 2, 0 }, { "MIDI Vol", 7, 100 },
    { "Expr", 11, 127 }, { "Sustain", 64, 0 }, { "Resonance", 71, 64 },
    { "Brightness", 74, 64 }, { "Reverb CC", 91, 40 }
}};
const Identifier portraitProperty ("studioAtelierPortrait");
const Identifier ccProperty ("studioAtelierCCState");
const Identifier channelProperty ("studioAtelierPreviewChannel");

File artDirectory()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Studio").getChildFile ("AtelierArtwork");
}
te::VolumeAndPanPlugin* volumeFor (te::AudioTrack& source)
{
    for (int index = source.pluginList.size(); --index >= 0;)
        if (auto* plugin = dynamic_cast<te::VolumeAndPanPlugin*> (source.pluginList[index]))
            return plugin;
    return nullptr;
}
void setLabel (Label& label, const String& caption, float size, Colour colour)
{
    label.setText (caption, dontSendNotification);
    label.setFont (FontOptions (size, Font::plain));
    label.setColour (Label::textColourId, colour);
    label.setJustificationType (Justification::centredLeft);
}
void makeFader (Slider& slider, double low, double high, double step, const String& suffix)
{
    slider.setSliderStyle (Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 76, 20);
    slider.setRange (low, high, step);
    slider.setTextValueSuffix (suffix);
    slider.setColour (Slider::trackColourId, accent);
    slider.setColour (Slider::thumbColourId, primary);
    slider.setColour (Slider::backgroundColourId, colours::control);
    slider.setColour (Slider::textBoxTextColourId, primary);
    slider.setColour (Slider::textBoxBackgroundColourId, background);
    slider.setColour (Slider::textBoxOutlineColourId, accent.withAlpha (.3f));
}
}

AtelierInspectorPanel::AtelierInspectorPanel (TrackActions& callbacks) : actions (callbacks)
{
    setOpaque (true);
    for (auto* label : { &heading, &instrumentName, &artCaption, &mappingsHeading, &ccHeading,
                         &mixHeading, &midiStatus, &micStatus, &volumeLabel, &panLabel, &roomLabel })
        addAndMakeVisible (*label);
    setLabel (heading, "CINEMATIC ATELIER", 15.0f, accent);
    setLabel (instrumentName, "Selected instrument", 13.0f, primary);
    setLabel (artCaption, "Choose your instrument artwork", 11.0f, secondary);
    setLabel (mappingsHeading, "ARTICULATIONS", 12.0f, accent);
    setLabel (ccHeading, "MIDI CONTROLLERS", 12.0f, accent);
    setLabel (mixHeading, "CHANNEL MIXER", 12.0f, accent);
    setLabel (midiStatus, "Controller response depends on the instrument", 10.0f, secondary);
    setLabel (micStatus, "Mic positions are configured in Orchestral Stage", 10.0f, secondary);
    setLabel (volumeLabel, "Volume", 11.0f, primary);
    setLabel (panLabel, "Pan", 11.0f, primary);
    setLabel (roomLabel, "Room", 11.0f, primary);
    for (auto* label : { &artCaption, &volumeLabel, &panLabel, &roomLabel })
        label->setJustificationType (Justification::centred);

    addAndMakeVisible (artworkPicker);
    artworkPicker.setTooltip ("The artwork is cosmetic; it does not change the Audio Unit patch.");
    artworkPicker.addItem ("Composer Study", 1);
    StringArray pictures { "Violin", "Cello", "Flute", "Clarinet", "Horn", "Oboe" };
    Array<File> files;
    for (const auto* ext : { "*.jpg", "*.jpeg", "*.png" })
        artDirectory().findChildFiles (files, File::findFiles, false, ext);
    for (const auto& file : files)
        if (! file.getFileNameWithoutExtension().containsChar ('|'))
            pictures.addIfNotAlreadyThere (file.getFileNameWithoutExtension());
    int id = 2;
    for (const auto& picture : pictures) artworkPicker.addItem (picture, id++);
    artworkPicker.onChange = [this] { if (! updating) changeArtwork(); };

    for (auto* button : { &mappingsButton, &sendValues, &fullInstrument, &originalInstrument, &stageButton })
        addAndMakeVisible (*button);
    mappingsButton.onClick = [this]
    {
        if (actions.openArticulations && trackId.isValid()) actions.openArticulations (trackId);
    };
    fullInstrument.onClick = [this]
    {
        if (actions.openFullInstrument && trackId.isValid()) actions.openFullInstrument (trackId);
    };
    originalInstrument.onClick = [this]
    {
        if (dynamic_cast<sampler::Plugin*>(hosted()))
        {if (actions.openFullInstrument && trackId.isValid())actions.openFullInstrument(trackId);}
        else if (actions.openOriginalInstrument && trackId.isValid())
            actions.openOriginalInstrument (trackId);
    };
    originalInstrument.setTooltip ("Open the hosted instrument's original Audio Unit editor directly.");
    stageButton.onClick = [this]
    {
        if (actions.showStage && trackId.isValid()) actions.showStage (trackId);
    };
    sendValues.onClick = [this]
    {
        if (! canSendMidi()) return;
        for (int i = 0; i < (int) ccKnobs.size(); ++i) sendCC (i);
    };

    for (auto& button : articulationPads)
    {
        addAndMakeVisible (button);
        button.setVisible (false);
        button.setColour (TextButton::buttonColourId, panel);
        button.setColour (TextButton::buttonOnColourId, colours::selected);
        button.setColour (TextButton::textColourOffId, primary);
        button.setColour (TextButton::textColourOnId, primary);
    }
    for (size_t i = 0; i < articulationPads.size(); ++i)
        articulationPads[i].onClick = [this, i] { selectMapping ((int) i); };

    addAndMakeVisible (midiChannel);
    midiChannel.setTooltip ("Audition MIDI channel. Does not change recorded clips or the track's MIDI routing.");
    for (int ch = 1; ch <= 16; ++ch) midiChannel.addItem ("Ch " + String (ch), ch);
    midiChannel.setSelectedId (1, dontSendNotification);
    midiChannel.onChange = [this]
    {
        if (updating) return;
        releaseSustain();
        if (auto* t = track())
            t->state.setProperty (channelProperty, midiChannel.getSelectedId(), nullptr);
    };

    for (size_t i = 0; i < ccKnobs.size(); ++i)
    {
        auto& knob = ccKnobs[i];
        auto& label = ccLabels[i];
        addAndMakeVisible (knob);
        addAndMakeVisible (label);
        setLabel (label, String (controllers[i].name) + " · CC" + String (controllers[i].cc), 10.0f, primary);
        label.setJustificationType (Justification::centred);
        knob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (Slider::TextBoxBelow, false, 52, 17);
        knob.setColour (Slider::rotarySliderFillColourId, accent);
        knob.setColour (Slider::thumbColourId, primary);
        knob.setColour (Slider::textBoxTextColourId, primary);
        knob.setColour (Slider::textBoxBackgroundColourId, background);
        knob.setColour (Slider::textBoxOutlineColourId, accent.withAlpha (.25f));
        knob.setRange (0, 127, i == 4 ? 127 : 1);
        knob.setValue (controllers[i].value, dontSendNotification);
        knob.setTooltip ("Live MIDI CC" + String (controllers[i].cc) + " on the selected channel; patch support varies.");
        knob.onValueChange = [this, i]
        {
            if (updating) return;
            writeCCState();
            sendCC ((int) i);
        };
    }

    for (auto* slider : { &volume, &pan, &roomSend })
        addAndMakeVisible (*slider);
    makeFader (volume, -100, 6, .1, " dB");
    volume.setSkewFactorFromMidPoint (-24.0);
    makeFader (pan, -1, 1, .01, "");
    makeFader (roomSend, -60, 0, .1, " dB");
    volume.setDoubleClickReturnValue (true, 0);
    pan.setDoubleClickReturnValue (true, 0);
    volume.setTooltip ("Studio Mixer channel volume; same Tracktion fader, not MIDI CC7.");
    pan.setTooltip ("Studio Mixer stereo pan, same track parameter.");
    roomSend.setTooltip ("Native Stage room send trim; requires an existing Stage seat and enabled send.");
    volume.onDragStart = [this]
    {
        if (auto* t = track()) t->edit.getUndoManager().beginNewTransaction ("Inspector volume");
    };
    pan.onDragStart = [this]
    {
        if (auto* t = track()) t->edit.getUndoManager().beginNewTransaction ("Inspector pan");
    };
    roomSend.onDragStart = [this]
    {
        if (auto* t = track()) t->edit.getUndoManager().beginNewTransaction ("Inspector room send");
    };
    volume.onValueChange = [this]
    {
        if (updating || ! editing) return;
        if (auto* control = fader()) control->setVolumeDb ((float) volume.getValue());
    };
    pan.onValueChange = [this]
    {
        if (updating || ! editing) return;
        if (auto* control = fader()) control->setPan ((float) pan.getValue());
    };
    roomSend.onValueChange = [this]
    {
        if (updating || ! editing) return;
        if (auto* t = track())
        {
            auto seat = t->state.getChildWithName (spatial::seatType);
            if (! seat.isValid() || ! spatial::hasRoomSend (*t)) return;
            if (! roomSend.isMouseButtonDown())
                t->edit.getUndoManager().beginNewTransaction ("Inspector room send");
            seat.setProperty ("trim", (float) roomSend.getValue(), &t->edit.getUndoManager());
            const auto placed = spatial::readSeat (seat);
            spatial::updateRoomSend (*t, placed.position, placed.sendTrim);
        }
    };
}

AtelierInspectorPanel::~AtelierInspectorPanel()
{
    releaseSustain();
}

te::AudioTrack* AtelierInspectorPanel::track() const
{
    return edit != nullptr && trackId.isValid() ? te::findAudioTrackForID (*edit, trackId) : nullptr;
}
te::Plugin* AtelierInspectorPanel::hosted() const
{
    if (auto* t = track())
        if (auto* plugin = findInstrument (*t))
            if (dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr || dynamic_cast<sampler::Plugin*> (plugin) != nullptr)
                return plugin;
    return nullptr;
}
te::VolumeAndPanPlugin* AtelierInspectorPanel::fader() const
{
    if (auto* t = track()) return volumeFor (*t);
    return nullptr;
}
bool AtelierInspectorPanel::canSendMidi() const
{
    auto* t = track();
    auto* plugin = hosted();
    const auto* external = dynamic_cast<te::ExternalPlugin*> (plugin);
    const bool native = dynamic_cast<sampler::Plugin*> (plugin) != nullptr;
    return editing && t != nullptr && plugin != nullptr
        && plugin->isEnabled() && (native || (external != nullptr && external->getAudioPluginInstance() != nullptr))
        && t->edit.getCurrentPlaybackContext() != nullptr
        && ! t->edit.getTransport().isRecording()
        && ! t->isFrozen (te::Track::anyFreeze);
}

void AtelierInspectorPanel::releaseSustain()
{
    if (pedalChannel > 0)
        if (auto* t = track())
            t->injectLiveMidiMessage (MidiMessage::controllerEvent (pedalChannel, 64, 0), {});
    pedalChannel = -1;
    ccKnobs[4].setValue (0, dontSendNotification);
}

void AtelierInspectorPanel::sendCC (int index)
{
    if (! isPositiveAndBelow (index, (int) controllers.size()) || ! canSendMidi()) return;
    if (dynamic_cast<sampler::Plugin*> (hosted()) && index != 0 && index != 2 && index != 3 && index != 4) return;
    auto* t = track();
    if (t == nullptr) return;
    const int channel = jlimit (1, 16, midiChannel.getSelectedId());
    auto value = jlimit (0, 127, roundToInt (ccKnobs[(size_t) index].getValue()));
    if (controllers[(size_t) index].cc == 64)
    {
        value = value >= 64 ? 127 : 0;
        if (pedalChannel > 0 && pedalChannel != channel) releaseSustain();
        pedalChannel = value == 127 ? channel : -1;
    }
    t->injectLiveMidiMessage (MidiMessage::controllerEvent (
        channel, controllers[(size_t) index].cc, value), {});
}

void AtelierInspectorPanel::writeCCState()
{
    auto* t = track();
    if (t == nullptr) return;
    String values;
    for (size_t i = 0; i < ccKnobs.size(); ++i)
    {
        if (i) values << ",";
        values << String (i == 4 ? 0 : roundToInt (ccKnobs[i].getValue()));
    }
    t->state.setProperty (ccProperty, values, nullptr);
    cachedCCState = values;
}

void AtelierInspectorPanel::setEdit (te::Edit* next)
{
    releaseSustain();
    edit = next;
    trackId = {};
    cachedMappingXml.clear();
    cachedCCState.clear();
    portraitName.clear();
    nativePortraitToken.clear();
    portraitImage = {};
    editing = false;
}

void AtelierInspectorPanel::loadArtwork()
{
    portraitImage = {};
    if (portraitName.isEmpty()) { repaint(); return; }
    for (const auto* suffix : { ".jpg", ".jpeg", ".png" })
    {
        const auto candidate = artDirectory().getChildFile (portraitName + suffix);
        // Never follow arbitrary source-project paths outside AtelierArtwork.
        if (! candidate.isAChildOf (artDirectory()) || ! candidate.existsAsFile()) continue;
        portraitImage = ImageFileFormat::loadFrom (candidate);
        if (portraitImage.isValid())
        {
            const float factor = jmin (1.0f, jmin (700.0f / portraitImage.getWidth(),
                                                   500.0f / portraitImage.getHeight()));
            if (factor < 1.0f)
                portraitImage = portraitImage.rescaled (
                    jmax (1, roundToInt (factor * portraitImage.getWidth())),
                    jmax (1, roundToInt (factor * portraitImage.getHeight())));
            break;
        }
    }
    repaint (imageBounds);
}
void AtelierInspectorPanel::changeArtwork()
{
    auto* t = track();
    if (t == nullptr) return;
    portraitName = artworkPicker.getSelectedId() == 1 ? String() : artworkPicker.getText();
    t->state.setProperty (portraitProperty, portraitName, nullptr);
    loadArtwork();
}

void AtelierInspectorPanel::refreshMappings()
{
    auto* t = track();
    auto* p = hosted();
    if (t == nullptr || p == nullptr) return;
    if (auto* native = dynamic_cast<sampler::Plugin*> (p))
    {
        auto next = native->articulations();
        const auto& model = native->instrument();
        std::erase_if (next, [&model](const auto& a)
        {
            for (const auto& g : model.groups)
                if (a.id == "sampler-group-" + String (g.id))
                    return g.playback.mute || std::none_of (model.zones.begin(),model.zones.end(),[&g](const auto& z)
                        {return z.folder == g.id && z.mapping.sample && z.problem.isEmpty();});
            return true;
        });
        const bool changed = next != mappings || cachedMappingXml != "native";
        cachedMappingXml = "native"; mappings = std::move(next);
        for (size_t i=0;i<articulationPads.size();++i)
        {
            auto& b=articulationPads[i];
            b.setVisible(i<mappings.size());
            if(i>=mappings.size())continue;
            b.setButtonText(mappings[i].name);
            b.setTooltip("Send this native articulation. All mapped articulations are available in the sampler Play view.");
            const auto& a=mappings[i];
            b.setToggleState(a.message==ArticulationMessage::keyswitch && native->activeKeyswitch(a.channel-1)==a.number,dontSendNotification);
            b.setEnabled(canSendMidi());
        }
        if(changed)resized();
        return;
    }
    const auto xmlText = t->state.getProperty ("studioArticulationMappings").toString();
    if (xmlText == cachedMappingXml) return;
    cachedMappingXml = xmlText;
    mappings.clear();
    activeMapping.clear();
    if (auto xml = parseXML (xmlText); xml != nullptr && xml->hasTagName ("StudioArticulations"))
        for (auto* profile : xml->getChildWithTagNameIterator ("Instrument"))
            if (profile->getStringAttribute ("instance") == p->itemID.toString())
            {
                mappings = readArticulations (*profile);
                activeMapping = profile->getStringAttribute ("active");
                break;
            }
    for (size_t i = 0; i < articulationPads.size(); ++i)
    {
        auto& button = articulationPads[i];
        const bool show = i < mappings.size();
        button.setVisible (show);
        if (! show) continue;
        button.setButtonText (mappings[i].name);
        button.setTooltip ("Select a verified user mapping; edit in the Articulation Palette.");
        button.setToggleState (mappings[i].id == activeMapping, dontSendNotification);
    }
    artCaption.setText (portraitName.isEmpty() ? "Composer Study" : portraitName, dontSendNotification);
    resized();
}
void AtelierInspectorPanel::selectMapping (int index)
{
    auto* t = track();
    auto* p = hosted();
    if (t == nullptr || p == nullptr || ! editing || ! isPositiveAndBelow (index, (int) mappings.size())
        || t->edit.getTransport().isRecording() || t->isFrozen (te::Track::anyFreeze)) return;
    if (auto* native=dynamic_cast<sampler::Plugin*>(p))
    {
        if (canSendMidi()) native->sendArticulation(mappings[(size_t)index]);
        return;
    }
    const auto text = t->state.getProperty ("studioArticulationMappings").toString();
    auto root = parseXML (text);
    if (root == nullptr || ! root->hasTagName ("StudioArticulations")) return;
    for (auto* profile : root->getChildWithTagNameIterator ("Instrument"))
        if (profile->getStringAttribute ("instance") == p->itemID.toString())
        {
            const auto id = mappings[(size_t) index].id;
            if (profile->getStringAttribute ("active") == id) profile->removeAttribute ("active");
            else profile->setAttribute ("active", id);
            auto& history = t->edit.getUndoManager();
            history.beginNewTransaction ("Select Inspector articulation");
            t->state.setProperty ("studioArticulationMappings",
                root->toString (XmlElement::TextFormat().singleLine()), &history);
            history.beginNewTransaction();
            refreshMappings();
            break;
        }
}

void AtelierInspectorPanel::refreshMix()
{
    auto* t = track();
    auto* control = fader();
    const ScopedValueSetter<bool> hold (updating, true);
    const bool canMix = editing && control != nullptr;
    volume.setEnabled (canMix);
    pan.setEnabled (canMix);
    if (canMix && ! volume.isMouseButtonDown())
        volume.setValue (control->getVolumeDb(), dontSendNotification);
    if (canMix && ! pan.isMouseButtonDown())
        pan.setValue (control->getPan(), dontSendNotification);
    bool availableRoom = false;
    if (t != nullptr)
    {
        const auto seat = t->state.getChildWithName (spatial::seatType);
        availableRoom = editing && seat.isValid() && spatial::hasRoomSend (*t);
        if (seat.isValid() && ! roomSend.isMouseButtonDown())
            roomSend.setValue (spatial::readSeat (seat).sendTrim, dontSendNotification);
    }
    roomSend.setEnabled (availableRoom);
    roomSend.setTooltip (availableRoom
        ? "Adjust the active native Studio Stage room send (not MIDI CC91)."
        : "Place the instrument on Orchestral Stage and enable its room send first.");
}

void AtelierInspectorPanel::refresh (te::AudioTrack* chosen, bool ready)
{
    if (chosen == nullptr || edit == nullptr)
    {
        setEdit (edit);
        return;
    }
    editing = ready;
    if (trackId != chosen->itemID)
    {
        releaseSustain();
        trackId = chosen->itemID;
        cachedMappingXml.clear();
        cachedCCState.clear();
        const ScopedValueSetter<bool> hold (updating, true);
        nativePortraitToken.clear();
        portraitName = chosen->state.getProperty (portraitProperty).toString();
        int selection = 1;
        for (int i = 0; i < artworkPicker.getNumItems(); ++i)
            if (artworkPicker.getItemText (i) == portraitName) selection = artworkPicker.getItemId (i);
        if (selection == 1) portraitName.clear();
        artworkPicker.setSelectedId (selection, dontSendNotification);
        midiChannel.setSelectedId (jlimit (1, 16,
            (int) chosen->state.getProperty (channelProperty, 1)), dontSendNotification);
        loadArtwork();
    }
    auto* native = dynamic_cast<sampler::Plugin*> (hosted());
    const bool nativeMode = native != nullptr;
    const auto presentation = nativeMode ? library::readPresentation(*native) : library::Presentation{};
    const auto token = nativeMode ? "native:"+presentation.portrait : String();
    if (nativeMode && nativePortraitToken != token)
    {
        nativePortraitToken=token;
        portraitImage=library::previewImage(library::portraitFile(presentation.portrait));
        repaint(imageBounds);
    }
    else if (!nativeMode && nativePortraitToken.isNotEmpty())
    {nativePortraitToken.clear();loadArtwork();}
    artworkPicker.setVisible(!nativeMode);
    artCaption.setVisible(nativeMode);
    artCaption.setText(nativeMode?(presentation.family.isNotEmpty()?presentation.family:String("Native instrument")):portraitName,dontSendNotification);
    originalInstrument.setButtonText(nativeMode?"Open Sampler":"Open Plug-in");
    originalInstrument.setTooltip(nativeMode?"Open the native sampler editor.":"Open the hosted instrument's original Audio Unit editor directly.");
    String displayedInstrument = chosen->getName() + " | "
        + (hosted() != nullptr ? hosted()->getName() : String ("Instrument"));
    if (auto* au = dynamic_cast<te::ExternalPlugin*> (hosted());
        au != nullptr && au->isAU()
        && au->desc.name.containsIgnoreCase ("DLSMusicDevice")
        && au->state.hasProperty (Identifier ("studioDlsGmProgram")))
    {
        const int gmProgram = (int) au->state.getProperty (Identifier ("studioDlsGmProgram"));
        if (gm::valid (gmProgram))
            displayedInstrument = chosen->getName() + " | GM "
                + String (gm::displayNumber (gmProgram)) + " " + gm::name (gmProgram);
    }
    if(nativeMode) displayedInstrument=native->instrument().name;
    if (instrumentName.getText() != displayedInstrument)
        instrumentName.setText (displayedInstrument, dontSendNotification);

    // A floating full editor can change the same art, CC values, and
    // audition channel. Poll only the tiny saved-state strings: images are
    // decoded only when the selected portrait actually changes.
    const auto sharedPortrait = chosen->state.getProperty (portraitProperty).toString();
    if (!nativeMode && sharedPortrait != portraitName)
    {
        int selection = 1;
        for (int i = 0; i < artworkPicker.getNumItems(); ++i)
            if (artworkPicker.getItemText (i) == sharedPortrait)
                selection = artworkPicker.getItemId (i);
        const ScopedValueSetter<bool> hold (updating, true);
        portraitName = selection == 1 ? String() : sharedPortrait;
        artworkPicker.setSelectedId (selection, dontSendNotification);
        loadArtwork();
    }
    const int updatedChannel = jlimit (1, 16,
        (int) chosen->state.getProperty (channelProperty, midiChannel.getSelectedId()));
    if (updatedChannel != midiChannel.getSelectedId())
    {
        releaseSustain();
        midiChannel.setSelectedId (updatedChannel, dontSendNotification);
    }
    const auto saved = chosen->state.getProperty (ccProperty).toString();
    if (saved != cachedCCState)
    {
        cachedCCState = saved;
        StringArray values = StringArray::fromTokens (saved, ",", "");
        const ScopedValueSetter<bool> hold (updating, true);
        for (size_t i = 0; i < ccKnobs.size(); ++i)
            ccKnobs[i].setValue (i == 4 ? 0
                  : values.size() == (int) ccKnobs.size()
                    ? jlimit (0, 127, values[(int) i].getIntValue())
                    : controllers[i].value, dontSendNotification);
    }
    refreshMappings();
    refreshMix();
    bool live = canSendMidi();
    if (! live && pedalChannel > 0) releaseSustain();
    for(size_t i=0;i<ccKnobs.size();++i)
    {
        const bool supported=!nativeMode||i==0||i==2||i==3||i==4;
        ccKnobs[i].setVisible(supported);ccLabels[i].setVisible(supported);ccKnobs[i].setEnabled(live&&supported);
        ccLabels[i].setText(nativeMode?(i==0?"Vibrato":i==2?"MIDI Vol":i==3?"Expression":"Sustain"):String(controllers[i].name)+" · CC"+String(controllers[i].cc),dontSendNotification);
    }
    ccHeading.setText(nativeMode?"EXPRESSION":"MIDI CONTROLLERS",dontSendNotification);
    sendValues.setEnabled (live);
    mappingsButton.setEnabled (editing);
    originalInstrument.setEnabled (ready && hosted() != nullptr);
    const String summary = live ? (nativeMode?"Live native expression":"Live CC · instrument response depends on patch")
        : "Live controls unavailable · active playback context required";
    if (midiStatus.getText() != summary)
        midiStatus.setText (summary, dontSendNotification);
}
void AtelierInspectorPanel::appearanceChanged() { resized(); repaint(); }

int AtelierInspectorPanel::desiredHeight (int width) const
{
    const auto& m = appearance;
    const bool four = width >= m.span (316);
    const bool native=dynamic_cast<sampler::Plugin*>(hosted())!=nullptr;
    const int ccRows = (native?4:8)/(four?4:2);
    return m.px (20) + m.control (52) + m.px (180) + m.control (27)
        + m.px (20) + m.control (20) + m.control (37) * 2 + m.control (29)
        + m.px (19) + m.control (20) + m.control (28) + ccRows * m.px (93)
        + m.control (26) + m.px (17) + m.control (22) + m.px (204)
        + m.control (40) + m.control (34) + m.px (20);
}

void AtelierInspectorPanel::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (9), m.px (9));
    const int gap = m.px (6);
    heading.setBounds (area.removeFromTop (m.control (26)));
    instrumentName.setBounds (area.removeFromTop (m.control (25, 11)));
    area.removeFromTop (gap);
    imageBounds = area.removeFromTop (m.px (180));
    auto artworkRow = area.removeFromTop (m.control (28, 11));
    originalInstrument.setBounds (artworkRow.removeFromRight (m.span (113)).reduced (m.px (2), 0));
    artworkPicker.setBounds (artworkRow);
    artCaption.setBounds (dynamic_cast<sampler::Plugin*>(hosted()) ? artworkRow : Rectangle<int>{});
    area.removeFromTop (m.px (9));
    auto artSection = area.removeFromTop (m.control (20) + m.control (37) * 2 + m.control (29) + m.px (6));
    articulationBounds = artSection;
    mappingsHeading.setBounds (artSection.removeFromTop (m.control (20)));
    if (mappings.empty())
    {
        for (auto& button : articulationPads) button.setBounds ({});
        auto message = artSection.removeFromTop (m.control (37) * 2);
        // The empty articulation state is explained by the Edit button.
        (void) message;
    }
    else
    {
        for (int row = 0; row < 2; ++row)
        {
            auto items = artSection.removeFromTop (m.control (37));
            const int w = jmax (1, items.getWidth() / 4);
            for (int col = 0; col < 4; ++col)
                articulationPads[(size_t) (row * 4 + col)].setBounds (
                    items.removeFromLeft (jmin (w, items.getWidth())).reduced (m.px (2), m.px (2)));
        }
    }
    mappingsButton.setBounds (artSection.removeFromBottom (m.control (28)).reduced (m.px (2), 0));
    area.removeFromTop (m.px (12));
    const int columns = area.getWidth() >= m.span (316) ? 4 : 2;
    const bool native=dynamic_cast<sampler::Plugin*>(hosted())!=nullptr;
    const int rows = (native?4:8) / columns;
    ccBounds = area.removeFromTop (m.control (20) + m.control (28) + rows * m.px (93) + m.control (26));
    auto ccArea = ccBounds;
    ccHeading.setBounds (ccArea.removeFromTop (m.control (20)));
    auto midiRow = ccArea.removeFromTop (m.control (28));
    midiChannel.setBounds (midiRow.removeFromLeft (m.span (106)));
    sendValues.setBounds (midiRow.removeFromRight (m.span (96)));
    int visibleIndex=0;
    for (size_t i = 0; i < ccKnobs.size(); ++i)
    {
        if(native && i!=0&&i!=2&&i!=3&&i!=4){ccKnobs[i].setBounds({});ccLabels[i].setBounds({});continue;}
        const int slot=visibleIndex++;
        const int w = jmax (1, ccArea.getWidth() / columns);
        const int h = m.px (93);
        const int x = ccArea.getX() + (slot % columns) * w;
        const int y = ccArea.getY() + (slot / columns) * h;
        auto cell = Rectangle<int> (x, y, w, h).reduced (m.px (2));
        ccLabels[i].setBounds (cell.removeFromTop (m.control (18, 8)));
        ccKnobs[i].setBounds (cell);
    }
    midiStatus.setBounds (ccBounds.removeFromBottom (m.control (24, 9)));
    area.removeFromTop (m.px (9));
    mixerBounds = area.removeFromTop (m.control (22) + m.px (204));
    auto mixArea = mixerBounds;
    mixHeading.setBounds (mixArea.removeFromTop (m.control (22)));
    const int stripW = jmax (1, mixArea.getWidth() / 3);
    for (auto [label, control] : std::initializer_list<std::pair<Label*, Slider*>> {
             { &volumeLabel, &volume }, { &panLabel, &pan }, { &roomLabel, &roomSend } })
    {
        auto strip = mixArea.removeFromLeft (jmin (stripW, mixArea.getWidth())).reduced (m.px (3), 0);
        label->setBounds (strip.removeFromTop (m.control (20, 9)));
        control->setBounds (strip);
    }
    area.removeFromTop (m.px (5));
    micStatus.setBounds (area.removeFromTop (m.control (30, 10)));
    auto actionsRow = area.removeFromTop (m.control (34));
    fullInstrument.setBounds (actionsRow.removeFromLeft (actionsRow.getWidth() / 2).reduced (m.px (2), 0));
    stageButton.setBounds (actionsRow.reduced (m.px (2), 0));
}

void AtelierInspectorPanel::paint (Graphics& g)
{
    g.fillAll (background);
    auto card = imageBounds.toFloat().reduced (1);
    g.setGradientFill (ColourGradient (colours::panel, card.getTopLeft(),
                                      colours::well, card.getBottomRight(), false));
    g.fillRoundedRectangle (card, 8.0f);
    if (portraitImage.isValid())
    {
        auto destination = imageBounds.reduced (appearance.px (5));
        const auto scale = jmin ((float) destination.getWidth() / portraitImage.getWidth(),
                                (float) destination.getHeight() / portraitImage.getHeight());
        const int w = jmax (1, roundToInt (portraitImage.getWidth() * scale));
        const int h = jmax (1, roundToInt (portraitImage.getHeight() * scale));
        auto target = Rectangle<int> (w, h).withCentre (destination.getCentre());
        g.drawImage (portraitImage, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                     0, 0, portraitImage.getWidth(), portraitImage.getHeight());
    }
    else
    {
        g.setColour (secondary);
        g.setFont (FontOptions (14.0f));
        g.drawFittedText (dynamic_cast<sampler::Plugin*>(hosted())?"Assign artwork in the Instrument Library":"Select instrument artwork", imageBounds.reduced (12), Justification::centred, 2);
    }
    // Artwork stays untouched; its frame follows Studio's subdued dividers.
    g.setColour (colours::divider);
    g.drawRoundedRectangle (card, 8.0f, 1.0f);
    for (auto section : { articulationBounds, ccBounds, mixerBounds })
    {
        g.setColour (colours::divider.withAlpha (.65f));
        g.drawHorizontalLine (section.getY() - 3, (float) section.getX(), (float) section.getRight());
    }
    if (mappings.empty() && ! articulationBounds.isEmpty())
    {
        auto text = articulationBounds.reduced (appearance.px (7)).withTrimmedTop (appearance.control (25))
                                       .withTrimmedBottom (appearance.control (30));
        g.setColour (secondary);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText ("No verified articulation mappings. Assign keyswitches or CCs using Edit Articulations.",
                          text, Justification::centred, 3);
    }
}

#if STUDIO_SPATIAL_TESTS
bool AtelierInspectorPanel::verifyInspectorBindings()
{
    auto* t = track();
    if (t == nullptr || hosted() == nullptr || ! getLocalBounds().contains (imageBounds)
        || ! getLocalBounds().contains (mixerBounds)
        || ! getLocalBounds().contains (ccBounds)
        || ! ccKnobs[0].isVisible() || ! volume.isVisible() || ! pan.isVisible())
        return false;
    const auto before = t->state.getProperty (ccProperty).toString();
    {
        const ScopedValueSetter<bool> hold (updating, true);
        ccKnobs[0].setValue (76, dontSendNotification);
    }
    writeCCState();
    const bool saved = t->state.getProperty (ccProperty).toString().startsWith ("76,");
    t->state.setProperty (ccProperty, before, nullptr);
    cachedCCState.clear();
    refresh (t, editing);
    return saved;
}
#endif
}
