// SPDX-License-Identifier: GPL-3.0-or-later
#include "AtelierInspectorPanel.h"
#include "SpatialState.h"
#include "SpatialRouting.h"
#include "StudioUI.h"
#include "StudioInstrumentIcons.h"
#include "StudioGeneralMidi.h"
#include "StudioSampler.h"
#include "StudioSynth.h"
#include "StudioLibraryPresentation.h"
#include "SharedInstrumentControls.h"
#include <cmath>

namespace studio
{
using namespace juce;
namespace te = tracktion;
namespace
{
// Inspector uses the shared console palette with a restrained Atelier brass accent.
const Colour& background = colours::canvas;
const Colour& panel = colours::panel;
const Colour& accent = colours::atelierAccent;
const Colour& primary = colours::text;
const Colour& secondary = colours::secondary;
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
    label.setFont (FontOptions (size, colour == accent ? Font::bold : Font::plain));
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
    setLabel (heading, "CINEMATIC ATELIER", design::section, accent);
    setLabel (instrumentName, "Selected instrument", design::body, primary);
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
    artworkPicker.setTextWhenNothingSelected ("Artwork unavailable");
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

    for (auto* button : { &mappingsButton, &sendValues, &fullInstrument, &originalInstrument, &stageButton, &moreControls })
        addAndMakeVisible (*button);
    mappingsButton.onClick = [this]
    {
        if(dynamic_cast<sampler::Plugin*>(hosted()))
        {if(actions.openFullInstrument&&trackId.isValid())actions.openFullInstrument(trackId);}
        else if (actions.openArticulations && trackId.isValid()) actions.openArticulations (trackId);
    };
    fullInstrument.onClick = [this]
    {
        if (actions.openFullInstrument && trackId.isValid()) actions.openFullInstrument (trackId);
    };
    originalInstrument.onClick = [this]
    {
        if (profile() != AtelierProfile::hosted)
        {if (actions.openFullInstrument && trackId.isValid())actions.openFullInstrument(trackId);}
        else if (actions.openOriginalInstrument && trackId.isValid())
            actions.openOriginalInstrument (trackId);
    };
    originalInstrument.setTooltip ("Open the hosted instrument's original Audio Unit editor directly.");
    stageButton.onClick = [this]
    {
        if (actions.showStage && trackId.isValid()) actions.showStage (trackId);
    };
    moreControls.setTooltip ("Show additional MIDI controls without sending MIDI.");
    moreControls.onClick = [this]
    {
        advancedMidi = ! advancedMidi;
        if (auto* t = track()) refresh (t, editing);
        resized();
    };
    sendValues.setButtonText ("Send shown");
    sendValues.setTooltip ("Send only displayed MIDI controller values.");
    sendValues.onClick = [this]
    {
        if (! canSendMidi()) return;
        for (int i = 0; i < (int) ccKnobs.size(); ++i)
            if (atelierControllerVisible (profile(), advancedMidi, i)) sendCC (i);
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
        setLabel (label, String (controllers[i].name) + " - CC" + String (controllers[i].cc), 10.0f, primary);
        label.setJustificationType (Justification::centred);
        knob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (Slider::TextBoxBelow, false, 52, 17);
        knob.setColour (Slider::rotarySliderFillColourId, accent);
        knob.setColour (Slider::thumbColourId, primary);
        knob.setColour (Slider::textBoxTextColourId, primary);
        knob.setColour (Slider::textBoxBackgroundColourId, background);
        knob.setColour (Slider::textBoxOutlineColourId, accent.withAlpha (.25f));
        knob.setRange (0, 127, i == 4 ? 127 : 1);
        knob.setDoubleClickReturnValue (true, controllers[i].value);
        knob.setValue (controllers[i].value, dontSendNotification);
        knob.setTooltip ("Live MIDI CC" + String (controllers[i].cc) + " on the selected channel; patch support varies.");
        knob.onValueChange = [this, i]
        {
            if (updating) return;
            if (auto* t = track()) writeSharedCC (t->state, controllers[i].cc, roundToInt (ccKnobs[i].getValue()));
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
            if (dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr || dynamic_cast<sampler::Plugin*> (plugin) != nullptr
                || dynamic_cast<synth::Plugin*> (plugin) != nullptr)
                return plugin;
    return nullptr;
}
AtelierProfile AtelierInspectorPanel::profile() const
{
    auto* plugin = hosted();
    if (dynamic_cast<sampler::Plugin*> (plugin)) return AtelierProfile::sampler;
    if (dynamic_cast<synth::Plugin*> (plugin)) return AtelierProfile::synth;
    return AtelierProfile::hosted;
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
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin);
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
    ccStateLoaded = false;
    mappingCacheValid = false;
    nativeMappingInstance.clear();
    mappings.clear();
    for (auto& pad : articulationPads) pad.setVisible (false);
    portraitName.clear();
    nativePortraitToken.clear();
    portraitImage = {};
    editing = false;
}

void AtelierInspectorPanel::loadArtwork()
{
    portraitImage = {};
    if (portraitName.isEmpty() || portraitName.containsChar ('/') || portraitName.containsChar ('\\')
        || portraitName.containsChar (':')) { repaint(); return; }
    for (const auto* suffix : { ".jpg", ".jpeg", ".png" })
    {
        const auto candidate = artDirectory().getChildFile (portraitName + suffix);
        // Never follow arbitrary source-project paths outside AtelierArtwork.
        if (! candidate.isAChildOf (artDirectory()) || candidate.isSymbolicLink()) continue;
        portraitImage = library::previewImage (candidate);
        if (portraitImage.isValid()) break;
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
    if (profile() == AtelierProfile::synth)
    {
        if (! mappings.empty() || mappingCacheValid)
        {
            mappings.clear();
            mappingCacheValid = false;
            nativeMappingInstance.clear();
            for (auto& pad : articulationPads) pad.setVisible (false);
            resized();
        }
        return;
    }
    if (auto* native = dynamic_cast<sampler::Plugin*> (p))
    {
        const auto instance=native->itemID.toString();
        const auto revision=native->changeCount();
        const bool changed=cachedMappingXml!="native"||nativeMappingInstance!=instance||nativeMappingRevision!=revision;
        if(changed)
        {
            auto next=native->articulations();
            const auto& model=native->instrument();
            std::erase_if(next,[&model](const auto& a)
            {
                for(const auto& g:model.groups)
                    if(a.id=="sampler-group-"+String(g.id))
                        return g.playback.mute||std::none_of(model.zones.begin(),model.zones.end(),[&g](const auto& z)
                            {return z.folder==g.id&&z.mapping.sample&&z.problem.isEmpty();});
                return true;
            });
            cachedMappingXml="native";nativeMappingInstance=instance;nativeMappingRevision=revision;mappings=std::move(next);
        }
        for (size_t i=0;i<articulationPads.size();++i)
        {
            auto& b=articulationPads[i];
            if(changed)b.setVisible(i<mappings.size());
            if(i>=mappings.size())continue;
            if(changed){b.setButtonText(mappings[i].name);b.setTooltip("Send this native articulation. All mapped articulations are available in the sampler Play view.");}
            const auto& a=mappings[i];
            b.setToggleState(a.message==ArticulationMessage::keyswitch && native->activeKeyswitch(a.channel)==a.number,dontSendNotification);
            b.setEnabled(canSendMidi());
        }
        if(changed)resized();
        return;
    }
    const auto xmlText = t->state.getProperty ("studioArticulationMappings").toString();
    const auto instance = p->itemID.toString();
    if (mappingCacheValid && xmlText == cachedMappingXml && instance == cachedMappingInstance) return;
    mappingCacheValid = true;
    cachedMappingInstance = instance;
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
        button.setEnabled (true);
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
        ccStateLoaded = false;
        mappingCacheValid = false;
        nativeMappingInstance.clear();
        mappings.clear();
        for (auto& pad : articulationPads) pad.setVisible (false);
        const ScopedValueSetter<bool> hold (updating, true);
        nativePortraitToken.clear();
        portraitName = chosen->state.getProperty (portraitProperty).toString();
        int selection = portraitName.isEmpty() ? 1 : 0;
        for (int i = 0; i < artworkPicker.getNumItems(); ++i)
            if (artworkPicker.getItemText (i) == portraitName) selection = artworkPicker.getItemId (i);
        artworkPicker.setSelectedId (selection, dontSendNotification);
        midiChannel.setSelectedId (jlimit (1, 16,
            (int) chosen->state.getProperty (channelProperty, 1)), dontSendNotification);
        loadArtwork();
    }
    auto* native = dynamic_cast<sampler::Plugin*> (hosted());
    const bool nativeMode = native != nullptr;
    auto* synthesizer = dynamic_cast<synth::Plugin*> (hosted());
    const auto mode = profile();
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
    artworkPicker.setVisible(false);
    artCaption.setVisible(false);
    originalInstrument.setVisible(false);
    artCaption.setText(nativeMode?(presentation.family.isNotEmpty()?presentation.family:String("Native instrument")):portraitName,dontSendNotification);
    originalInstrument.setButtonText(nativeMode?"Open Sampler":synthesizer?"Open Synth":"Open Plug-in");
    mappingsButton.setButtonText(nativeMode?"Sampler articulations":"Edit articulations");
    mappingsButton.setTooltip(nativeMode?"Open the sampler to see its mapped playing techniques and edit them under Build > Groups.":"Edit the verified MIDI articulation mappings for this hosted instrument.");
    originalInstrument.setTooltip(nativeMode?"Open the native sampler editor.":synthesizer?"Open the selected Studio Synth editor.":"Open the hosted instrument's original Audio Unit editor directly.");
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
    if(synthesizer) displayedInstrument=synthesizer->presetName();
    if (instrumentName.getText() != displayedInstrument)
        instrumentName.setText (displayedInstrument, dontSendNotification);

    // A floating full editor can change the same art, CC values, and
    // audition channel. Poll only the tiny saved-state strings: images are
    // decoded only when the selected portrait actually changes.
    const auto sharedPortrait = chosen->state.getProperty (portraitProperty).toString();
    if (!nativeMode && sharedPortrait != portraitName)
    {
        int selection = sharedPortrait.isEmpty() ? 1 : 0;
        for (int i = 0; i < artworkPicker.getNumItems(); ++i)
            if (artworkPicker.getItemText (i) == sharedPortrait)
                selection = artworkPicker.getItemId (i);
        const ScopedValueSetter<bool> hold (updating, true);
        portraitName = sharedPortrait;
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
    if (! ccStateLoaded || saved != cachedCCState)
    {
        ccStateLoaded = true;
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
    const bool showMidi = mode != AtelierProfile::synth;
    const bool showAdvanced = mode == AtelierProfile::hosted && advancedMidi;
    bool layoutChanged = ccHeading.isVisible() != showMidi || moreControls.isVisible() != (mode == AtelierProfile::hosted);
    for (auto* component : std::initializer_list<Component*> { &mappingsHeading, &mappingsButton, &ccHeading, &midiChannel, &sendValues, &midiStatus })
        component->setVisible (showMidi);
    moreControls.setVisible (mode == AtelierProfile::hosted);
    midiChannel.setVisible (showAdvanced);
    sendValues.setVisible (showAdvanced);
    midiStatus.setVisible (showAdvanced);
    micStatus.setVisible (false);
    moreControls.setButtonText (advancedMidi ? "Essential controls" : "More MIDI controls");
    moreControls.setToggleState (advancedMidi, dontSendNotification);
    for(size_t i=0;i<ccKnobs.size();++i)
    {
        const bool supported=atelierControllerVisible (mode, advancedMidi, (int) i);
        layoutChanged = layoutChanged || ccKnobs[i].isVisible() != supported;
        ccKnobs[i].setVisible(supported);ccLabels[i].setVisible(supported);ccKnobs[i].setEnabled(live&&supported);
        ccLabels[i].setText(nativeMode?(i==0?"Vibrato":i==2?"MIDI Vol":i==3?"Expression":"Sustain"):String(i==0?"Modulation":i==3?"Expression":controllers[i].name),dontSendNotification);
        ccKnobs[i].setTooltip(nativeMode?(i==0?"Vibrato depth (CC1).":i==2?"MIDI note volume (CC7), separate from the track mixer.":i==3?"Musical expression level (CC11).":"Sustain pedal (CC64)."):("Live MIDI CC"+String(controllers[i].cc)+" on the selected channel; patch support varies."));
    }
    ccHeading.setText(nativeMode?"EXPRESSION":"MIDI CONTROLLERS",dontSendNotification);
    sendValues.setEnabled (live);
    mappingsButton.setEnabled (editing && showMidi);
    if (showMidi)
    {
        const auto count = (int) mappings.size();
        mappingsHeading.setText (count > 0 ? "ARTICULATIONS (" + String (count) + ")" : String ("ARTICULATIONS"), dontSendNotification);
        for (size_t i = 0; i < articulationPads.size(); ++i)
            if (articulationPads[i].isVisible())
                articulationPads[i].setEnabled (nativeMode ? live : editing && ! chosen->edit.getTransport().isRecording()
                                                    && ! chosen->isFrozen (te::Track::anyFreeze));
    }
    if (layoutChanged) resized();
    originalInstrument.setEnabled (ready && hosted() != nullptr);
    const String summary = live ? (nativeMode?"Live native expression":"Live CC - instrument response depends on patch")
        : "Live controls unavailable - active playback context required";
    if (midiStatus.getText() != summary)
        midiStatus.setText (summary, dontSendNotification);
}
void AtelierInspectorPanel::reflectPerformance (te::EditItemID id, const KeyboardPerformanceValues& values)
{
    if (trackId != id) return;
    using V = KeyboardPerformanceValues;
    const ScopedValueSetter<bool> hold (updating, true);
    const auto set = [&] (int index, V::Parameter parameter, int value)
    {
        if ((values.changed & V::bit(parameter)) && !ccKnobs[(size_t)index].isMouseButtonDown())
            ccKnobs[(size_t)index].setValue(value,dontSendNotification);
    };
    set(0,V::modulation,values.mod); set(2,V::volume,values.vol);
    set(3,V::expression,values.expr); set(6,V::brightness,values.tone); set(7,V::room,values.reverb);
}
void AtelierInspectorPanel::appearanceChanged() { resized(); repaint(); }

int AtelierInspectorPanel::desiredHeight (int width) const
{
    return atelierLayout (width, (int) mappings.size(), profile(), advancedMidi, appearance).total;
}

void AtelierInspectorPanel::resized()
{
    const auto& m = appearance;
    const auto mode = profile();
    const auto layout = atelierLayout (getWidth(), (int) mappings.size(), mode, advancedMidi, m);
    auto area = getLocalBounds().reduced (m.px (6), m.px (5));
    heading.setBounds (area.removeFromTop (m.control (20)));
    instrumentName.setBounds (area.removeFromTop (m.control (20, 11)));
    area.removeFromTop (m.px (3));
    imageBounds = area.removeFromTop (layout.portrait);
    originalInstrument.setBounds ({}); artworkPicker.setBounds ({}); artCaption.setBounds ({});
    articulationBounds = {};
    for (auto& pad : articulationPads) pad.setBounds ({});
    if (layout.articulationRows > 0)
    {
        area.removeFromTop (m.px (3));
        auto section = area.removeFromTop (m.control (18) + layout.articulationRows * m.control (25, 10) + m.control (24, 11));
        articulationBounds = section;
        mappingsHeading.setBounds (section.removeFromTop (m.control (18)));
        for (int row = 0; row < layout.articulationRows; ++row)
        {
            auto items = section.removeFromTop (m.control (25, 10));
            const int w = items.getWidth() / layout.columns;
            for (int col = 0; col < layout.columns; ++col)
            {
                const int index = row * layout.columns + col;
                auto bounds = items.removeFromLeft (w).reduced (m.px (2), m.px (2));
                if (index < (int) articulationPads.size() && index < (int) mappings.size())
                    articulationPads[(size_t) index].setBounds (bounds);
            }
        }
        mappingsButton.setBounds (section.removeFromTop (m.control (24, 11)).reduced (m.px (2), 0));
    }
    else { mappingsHeading.setBounds ({}); mappingsButton.setBounds ({}); }
    ccBounds = {};
    for (size_t i = 0; i < ccKnobs.size(); ++i) { ccKnobs[i].setBounds ({}); ccLabels[i].setBounds ({}); }
    if (layout.controllerRows > 0)
    {
        area.removeFromTop (m.px (3));
        const bool expanded = mode == AtelierProfile::hosted && advancedMidi;
        auto section = area.removeFromTop (m.control (24, 11) + layout.controllerRows * layout.controllerCell
                                          + (expanded ? m.control (26) + m.control (26,10) : 0));
        ccBounds = section;
        auto header = section.removeFromTop (m.control (24,11));
        moreControls.setBounds (mode == AtelierProfile::hosted ? header.removeFromRight (header.getWidth()/2).reduced (1) : Rectangle<int>{});
        ccHeading.setBounds (header);
        auto midiRow = expanded ? section.removeFromTop (m.control (26)) : Rectangle<int>{};
        midiChannel.setBounds (midiRow.removeFromLeft (jmin (m.span (106), midiRow.getWidth() / 2)));
        sendValues.setBounds (midiRow.reduced (m.px (2), 0));
        int slot = 0;
        for (size_t i = 0; i < ccKnobs.size(); ++i)
        {
            if (! atelierControllerVisible (mode, advancedMidi, (int) i)) continue;
            const int w = section.getWidth() / layout.columns;
            auto cell = Rectangle<int> (section.getX() + (slot % layout.columns) * w,
                section.getY() + (slot / layout.columns) * layout.controllerCell, w, layout.controllerCell).reduced (m.px (2));
            ccLabels[i].setBounds (cell.removeFromTop (m.control (18, 10)));
            ccKnobs[i].setBounds (cell);
            ++slot;
        }
        section.removeFromTop (layout.controllerRows * layout.controllerCell);
        midiStatus.setBounds (section);
    }
    else
        for (auto* component : std::initializer_list<Component*> { &ccHeading, &moreControls, &midiChannel, &sendValues, &midiStatus })
            component->setBounds ({});
    area.removeFromTop (m.px (3));
    mixerBounds = area.removeFromTop (m.control (20) + m.px (80));
    auto mixArea = mixerBounds;
    mixHeading.setBounds (mixArea.removeFromTop (m.control (20)));
    const int stripW = mixArea.getWidth() / 3;
    for (auto [label, control] : std::initializer_list<std::pair<Label*, Slider*>> {
             { &volumeLabel, &volume }, { &panLabel, &pan }, { &roomLabel, &roomSend } })
    {
        auto strip = mixArea.removeFromLeft (stripW).reduced (m.px (3), 0);
        label->setBounds (strip.removeFromTop (m.control (20, 10)));
        control->setBounds (strip);
    }
    area.removeFromTop (m.px (3));
    micStatus.setBounds ({});
    auto actionsRow = area.removeFromTop (m.control (26,11));
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
    if (auto* target=track())
        drawInstrumentIcon(g,imageBounds.toFloat().reduced((float)appearance.px(4)),
            instrumentIcon(target->getName(),target->state.getProperty("studioTrackIcon").toString()),colours::action);
    // Artwork stays untouched; its frame follows Studio's subdued dividers.
    g.setColour (colours::divider);
    g.drawRoundedRectangle (card, 8.0f, 1.0f);
    for (auto section : { articulationBounds, ccBounds, mixerBounds })
    {
        if (section.isEmpty()) continue;
        g.setColour (colours::divider.withAlpha (.65f));
        g.drawHorizontalLine (section.getY() - 3, (float) section.getX(), (float) section.getRight());
    }
    if (mappings.empty() && ! articulationBounds.isEmpty())
    {
        auto text = articulationBounds.withTrimmedTop (appearance.control (20))
                                       .withTrimmedBottom (appearance.control (28)).reduced (appearance.px (2));
        g.setColour (secondary);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText (dynamic_cast<sampler::Plugin*>(hosted())?"No mapped articulations. Use the sampler Build view to define them.":"No verified articulation mappings. Assign keyswitches or CCs using Edit Articulations.",
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
        || (profile() != AtelierProfile::synth && ! ccKnobs[0].isVisible()) || ! volume.isVisible() || ! pan.isVisible())
        return false;
    if (profile() == AtelierProfile::synth) return ! ccKnobs[0].isVisible() && ! mappingsHeading.isVisible();
    const auto before = t->state.getProperty (ccProperty).toString();
    {
        const ScopedValueSetter<bool> hold (updating, true);
        ccKnobs[0].setValue (76, dontSendNotification);
    }
    writeCCState();
    const bool saved = t->state.getProperty (ccProperty).toString().startsWith ("76,");
    writeSharedCC (t->state, 11, 93);
    const auto shared = sharedKeyboardValues (t->state);
    const bool linked = shared.mod == 76 && shared.expr == 93;
    auto observation = shared; observation.mod = 47;
    reflectPerformance (t->itemID, observation);
    const bool silent = roundToInt (ccKnobs[0].getValue()) == 47 && readSharedCC (t->state)[0] == 76;
    t->state.setProperty (ccProperty, before, nullptr);
    ccStateLoaded = false;
    cachedCCState.clear();
    refresh (t, editing);
    return saved && linked && silent;
}
#endif
}
