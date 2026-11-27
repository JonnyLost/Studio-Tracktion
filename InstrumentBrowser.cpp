// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentBrowser.h"
#include "InstrumentImportWindow.h"
#include "StudioSynth.h"
#include "StudioGeneralMidi.h"
#include "StudioUI.h"
#include "StudioInstrumentIcons.h"
#include "StudioLibraryManager.h"
#include <algorithm>
#include <optional>

using namespace juce;

namespace studio
{
namespace
{
bool isAria (const PluginDescription& plugin)
{
    return plugin.name.containsIgnoreCase ("ARIA Player")
        || (plugin.manufacturerName.containsIgnoreCase ("Garritan") && plugin.name.containsIgnoreCase ("ARIA"));
}

void drawInstrument (Graphics& g, Rectangle<float> bounds, const String& name, const String& family, bool effect, Colour colour)
{
    drawInstrumentIcon(g,bounds,effect?InstrumentIcon::effect:instrumentIcon(name,family),colour);
}
}

InstrumentBrowser::InstrumentBrowser (KnownPluginList& source, PropertiesFile* prefs)
    : known (source), preferences (prefs)
{
    if (preferences != nullptr)
    {
        importedDirectory = preferences->getFile().getSiblingFile ("Imported Instruments");
        preferredDetailsWidth = jlimit (180, 420, preferences->getIntValue ("studio.browserDetailsWidth", 258));
        const auto parsed = JSON::parse (preferences->getValue ("studio.browserFavourites", "[]"));
        if (auto* array = parsed.getArray())
            for (const auto& value : *array)
                if (value.isString())
                    favouriteKeys.addIfNotAlreadyThere (value.toString());
    }
    for (auto* list : { &familyList, &typeList, &resultList })
    {
        list->setColour (ListBox::backgroundColourId, colours::canvas);
        addAndMakeVisible (*list);
    }
    familyList.setName ("Instrument families");
    typeList.setName ("Instrument types");
    resultList.setName ("Instrument results");
    familyList.setModel (&familyRows);
    typeList.setModel (&typeRows);
    resultList.setModel (&resultRows);
    for (Component* item : std::initializer_list<Component*> {
             &search, &compactFamily, &compactType, &libraryView, &manage, &detailsTitle, &instrumentName, &vendor,
             &explanation, &status, &count, &load, &add, &programs, &favourite, &favouritesFilter, &ariaTools, &importLibrary, &close })
        addAndMakeVisible (*item);
    search.setTitle ("Search instruments");
    search.setTextToShowWhenEmpty ("Search instruments, formats or makers", colours::secondary);
    search.setColour (TextEditor::backgroundColourId, colours::control);
    search.setColour (TextEditor::outlineColourId, colours::divider);
    search.onTextChange = [this] { rebuildResults(); };
    libraryView.setTitle ("Apple sounds and effects");
    libraryView.addItem ("All Sounds", 1);
    libraryView.addItem ("Apple Sounds", 2);
    libraryView.addItem ("Apple Effects", 3);
    libraryView.addItem ("Sample Library Manager...", 4);
    libraryView.setSelectedId (1, dontSendNotification);
    libraryView.setTooltip ("Browse installed instruments, Apple's Audio Unit sound devices, or Apple Audio Unit effects");
    libraryView.onChange = [this]
    {
        if (libraryView.getSelectedId() == 4)
        {
            libraryView.setSelectedId(static_cast<int>(collection)+1,dontSendNotification);
            if(libraryWindow){libraryWindow->toFront(true);return;}
            DialogWindow::LaunchOptions options;
            options.dialogTitle="Studio Universal Instrument Library";
            options.dialogBackgroundColour=colours::panel;
            options.useNativeTitleBar=true;options.resizable=true;options.componentToCentreAround=this;
            const auto safe=Component::SafePointer<InstrumentBrowser>(this);
            options.content.setOwned(new library::Manager([safe](sampler::Instrument instrument,library::Presentation presentation)
            {
                if(safe&&safe->loadLibraryInstrument)safe->loadLibraryInstrument(std::move(instrument),std::move(presentation));
            }));
            libraryWindow=options.launchAsync();return;
        }
        collection = static_cast<Collection> (jlimit (1, 3, libraryView.getSelectedId()) - 1);
        // Preserve family/type filters inside each collection where possible.
        family = "All Instruments";
        type = "All Types";
        rebuildCatalog();
    };
    compactFamily.setTitle ("Instrument family");
    compactFamily.onChange = [this]
    {
        const auto index = compactFamily.getSelectedItemIndex();
        if (isPositiveAndBelow (index, families.size()))
            familyList.selectRow (index);
    };
    compactType.setTitle ("Instrument type");
    compactType.onChange = [this]
    {
        const auto index = compactType.getSelectedItemIndex();
        if (isPositiveAndBelow (index, types.size()))
            typeList.selectRow (index);
    };
    detailsTitle.setText ("INSTRUMENT DETAILS", dontSendNotification);
    detailsTitle.setFont (FontOptions (10.5f, Font::bold));
    instrumentName.setFont (FontOptions (14.0f, Font::bold));
    for (auto* label : { &detailsTitle, &vendor, &explanation, &status, &count })
        label->setColour (Label::textColourId, colours::secondary);
    explanation.setMinimumHorizontalScale (1.0f);
    status.setMinimumHorizontalScale (1.0f);
    vendor.setMinimumHorizontalScale (1.0f);
    instrumentName.setMinimumHorizontalScale (1.0f);
    count.setJustificationType (Justification::centredRight);
    load.setColour (TextButton::buttonColourId, colours::action);
    load.setColour (TextButton::textColourOffId, colours::rail);
    load.setTooltip ("Load the selected instrument on the selected track");
    add.setTooltip ("Create a track with the selected instrument");
    load.onClick = [this] { loadSelected (false); };
    add.onClick = [this] { loadSelected (true); };
    programs.setTooltip ("Select a sound program exposed by the Apple Audio Unit installed on this track");
    programs.onClick = [this]
    {
        if (const auto* entry = selected(); entry != nullptr && entry->apple && ! entry->effect
            && entry->plugin.createIdentifierString() == hostedAppleIdentifier && browseApplePrograms)
            browseApplePrograms (entry->plugin, programs);
    };

    importLibrary.setEnabled (importedDirectory != File());
    importLibrary.onClick = [this]
    {
        if (! importer)
        {
            importer = std::make_unique<instrumentimport::ImportWindow> (importedDirectory);
            importer->onCatalogChanged = [this] { rebuildCatalog(); };
        }
        importer->setVisible (true);
        importer->toFront (true);
    };
    manage.onClick = [this] { if (openPluginManager) openPluginManager(); };
    close.onClick = [this] { if (onClose) onClose(); };
    ariaTools.setTooltip ("Discover Garritan/ARIA instruments and register presets");
    ariaTools.onClick = [this] { if (openAriaCatalog) openAriaCatalog (ariaTools); };
    favourite.setTooltip ("Mark this instrument as a favorite or remove its favorite");
    favourite.onClick = [this]
    {
        if (const auto* entry = selected())
        {
            if (favouriteKeys.contains (entry->key))
                favouriteKeys.removeString (entry->key);
            else
                favouriteKeys.addIfNotAlreadyThere (entry->key);
            saveFavourites();
            rebuildResults();
        }
    };
    favouritesFilter.setClickingTogglesState (true);
    favouritesFilter.setTooltip ("Show only favorite instruments");
    favouritesFilter.onClick = [this]
    {
        favouritesOnly = favouritesFilter.getToggleState();
        rebuildResults();
    };
    appearanceChanged();
    known.addChangeListener (this);
    rebuildCatalog();
}

InstrumentBrowser::~InstrumentBrowser()
{
    if(libraryWindow)libraryWindow->exitModalState(0);
    known.removeChangeListener (this);
    for (auto* list : { &familyList, &typeList, &resultList })
        list->setModel (nullptr);
}

void InstrumentBrowser::saveFavourites()
{
    if (preferences == nullptr) return;
    Array<var> keys;
    for (const auto& key : favouriteKeys)
        keys.add (var (key));
    preferences->setValue ("studio.browserFavourites", JSON::toString (var (keys), true));
    preferences->saveIfNeeded();
}

void InstrumentBrowser::changeListenerCallback (ChangeBroadcaster*) { rebuildCatalog(); }

void InstrumentBrowser::setAriaCatalog (const aria::Catalog& catalog)
{
    ariaCatalog = &catalog;
    rebuildCatalog();
}

std::optional<aria::Patch> InstrumentBrowser::selectedAriaPatch() const
{
    if (const auto* item = selected(); item != nullptr && item->patch != nullptr
        && item->patch->canCapture())
        return *item->patch;
    return {};
}

void InstrumentBrowser::rebuildCatalog()
{
    entries.clear();
    bool hasAria = false;
    std::optional<PluginDescription> dls;
    Array<PluginDescription> ariaUnits;
    for (const auto& plugin : known.getTypes())
    {
        if (plugin.pluginFormatName != "AudioUnit") continue;
        const bool aria = isAria (plugin);
        hasAria |= aria;
        if (aria) ariaUnits.add (plugin);
        const bool apple = ! aria && plugin.manufacturerName.containsIgnoreCase ("Apple");
        const bool effect = apple && ! plugin.isInstrument;
        if (apple && plugin.isInstrument && plugin.name.containsIgnoreCase ("DLSMusicDevice"))
            dls = plugin;
        if (! plugin.isInstrument && ! aria && ! effect) continue;
        const String type = plugin.category.isNotEmpty()
            ? plugin.category : effect ? "Audio Effect" : "Instrument";
        entries.push_back ({ plugin.createIdentifierString(), plugin.name,
            effect ? "Apple Effects" : aria ? "Orchestral" : apple ? "Apple Sounds" : "Audio Units",
            aria ? "Sample Player" : type, plugin.manufacturerName + " | Audio Unit",
            effect ? "Apple effect. Add it to an instrument or audio track, then edit its real parameters."
              : aria ? "Choose library patches in the hosted ARIA editor."
              : apple ? "Apple sound device. Load it and browse the programs the AU actually exposes."
                      : "Scanned Audio Unit instrument.",
            plugin, true, effect, apple });
    }
    // DLSMusicDevice is a General MIDI sound bank, not just a generic
    // AU editor. Offer its real 128 GM1 program numbers as library entries.
    // These are named GM slots in the standard Apple bank; they are not
    // claims about patches in custom SF2/DLS files.
    if (dls.has_value())
        for (int index = 0; index < 128; ++index)
            entries.push_back ({ "apple-dls-gm-" + String (index),
                String (gm::displayNumber (index)).paddedLeft ('0', 3) + "  " + gm::name (index),
                gm::family (index), gm::family (index), "Apple DLSMusicDevice | General MIDI",
                "GM program " + String (gm::displayNumber (index))
                    + " on MIDI channel 1. Load through the installed Apple DLSMusicDevice.",
                *dls, true, false, true, index });

    if (! hasAria)
        entries.push_back ({ "missing-aria", "ARIA Player", "Orchestral", "Sample Player", "Garritan | Audio Unit",
                             "Install and scan the ARIA Audio Unit in Plug-ins.", {}, false });
    if (ariaCatalog != nullptr)
        for (const auto& [id, patch] : ariaCatalog->patches())
        {
            if (patch.kind == "original") continue;
            const auto capture = ariaCatalog->preset (patch);
            PluginDescription unit;
            for (const auto& candidate : ariaUnits)
            {
                if (capture.state.isNotEmpty()
                    && (candidate.createIdentifierString() != capture.pluginId
                        || candidate.version != capture.pluginVersion))
                    continue;
                unit = candidate;
                if (candidate.name.equalsIgnoreCase ("ARIA Player")) break;
            }
            const bool available = unit.name.isNotEmpty() && patch.present
                && (patch.kind == "sfz" || capture.state.isNotEmpty());
            const auto description = capture.state.isNotEmpty() && ! available
                ? String ("Requires the ARIA Audio Unit version used to capture this preset.")
                : ariaCatalog->status (patch);
            const auto patchStatus = ! available && patch.present && patch.kind == "snapshot" && capture.state.isEmpty()
                ? "Possible ensemble" : ! available ? "Unavailable" : capture.state.isNotEmpty() ? "Recall preset"
                : patch.missingSamples > 0 ? "Check samples in ARIA" : "Choose patch in ARIA";
            const auto separator = String::fromUTF8 (" — ");
            entries.push_back ({ "aria:" + id, patch.name + separator + patch.library + separator + "ARIA",
                patch.family, patch.library, "ARIA Player required",
                description + " | " + patch.health + " | " + patch.source.getFullPathName(),
                unit, available, false, false, -1, {}, &patch, patchStatus });
        }
    for (int i = 0; i < 7; ++i)
    {
        const auto engine = static_cast<synth::Family> (i);
        const auto title = synth::familyName (engine);
        entries.push_back ({ "studio-synth-" + synth::familyKey (engine), "Studio Synth | " + title, "Synth", title,
                            "Studio | Native instrument", "Open " + title + " factory sounds and edit the instrument.", {}, true });
    }
    for (const auto& sound : synth::catalog().sounds)
        entries.push_back ({ "studio-preset-" + sound.id, sound.name, "Synth Sounds",
                             synth::familyName (sound.family), "Studio Synth | " + sound.category,
                             sound.summary, {}, true });
    entries.push_back ({ "studio-sampler", "Studio Sampler", "Studio", "Sampler", "Studio | Native instrument",
                         "Play WAV and AIFF samples, edit the sound, and build multisample instruments.", {}, true });
    if (importedDirectory.isDirectory())
        for (const auto& item : RangedDirectoryIterator (importedDirectory, false, "*.studiosampler",
                 File::findFiles, File::FollowSymlinks::no))
        {
            const auto file = item.getFile();
            const auto stem = file.getFileNameWithoutExtension();
            const auto suffix = stem.fromLastOccurrenceOf ("-", false, false);
            const auto display = suffix.length() == 16 && suffix.containsOnly ("0123456789abcdef")
                ? stem.upToLastOccurrenceOf ("-", false, false) : stem;
            entries.push_back ({ "imported:" + file.getFileName(), display,
                "Imported Libraries", "Studio Sampler", "Studio | Imported instrument",
                "Native preset using samples in the original library folder.", {}, true, false, false, -1, file });
        }
    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.available != b.available) return a.available;
        return a.name.compareNatural (b.name) < 0;
    });
    families.clear();
    families.add ("All Instruments");
    for (const auto& entry : entries)
        if ((collection == Collection::appleEffects) == entry.effect
            && (collection != Collection::appleSounds || entry.apple)
            )
            families.addIfNotAlreadyThere (entry.family);
    families.sortNatural();
    if (! families.contains (family) || family.isEmpty()) family = "All Instruments";
    {
        ScopedValueSetter<bool> updating (refreshing, true);
        familyList.updateContent();
        familyList.selectRow (families.indexOf (family));
        compactFamily.clear (dontSendNotification);
        compactFamily.addItemList (families, 1);
        compactFamily.setSelectedItemIndex (families.indexOf (family), dontSendNotification);
    }
    rebuildTypes();
}

void InstrumentBrowser::rebuildTypes()
{
    types.clear();
    types.add ("All Types");
    for (const auto& entry : entries)
        if ((collection == Collection::appleEffects) == entry.effect
            && (collection != Collection::appleSounds || entry.apple)
            && (family == "All Instruments" || entry.family == family))
            types.addIfNotAlreadyThere (entry.type);
    types.sortNatural();
    if (! types.contains (type) || type.isEmpty()) type = "All Types";
    {
        ScopedValueSetter<bool> updating (refreshing, true);
        typeList.updateContent();
        typeList.selectRow (types.indexOf (type));
        compactType.clear (dontSendNotification);
        compactType.addItemList (types, 1);
        compactType.setSelectedItemIndex (types.indexOf (type), dontSendNotification);
    }
    rebuildResults();
}

void InstrumentBrowser::rebuildResults()
{
    ScopedValueSetter<bool> updating (refreshing, true);
    filtered.clear();
    const auto query = search.getText().trim();
    int row = -1;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if ((collection == Collection::appleEffects) != e.effect
            || (collection == Collection::appleSounds && ! e.apple)
            || (family != "All Instruments" && family != e.family)
            || (type != "All Types" && type != e.type)
            || (favouritesOnly && ! favouriteKeys.contains (e.key)))
            continue;
        if (query.isNotEmpty() && ! (e.name + " " + e.family + " " + e.type + " " + e.vendor).containsIgnoreCase (query))
            continue;
        if (e.key == selectedKey) row = (int) filtered.size();
        filtered.push_back (i);
    }
    if (row < 0 && ! filtered.empty()) row = 0;
    selectedKey = row >= 0 ? entries[filtered[(size_t) row]].key : String();
    resultList.updateContent();
    resultList.selectRow (row);
    if (row < 0) resultList.deselectAllRows();
    familyList.repaint();
    typeList.repaint();
    resultList.repaint();
    count.setText (filtered.empty() ? "No matching instruments" : String (filtered.size()) + " results", dontSendNotification);
    updateDetails();
}

const InstrumentBrowser::Entry* InstrumentBrowser::selected() const
{
    const auto i = std::find_if (entries.begin(), entries.end(), [this] (const Entry& e) { return e.key == selectedKey; });
    return i == entries.end() ? nullptr : &*i;
}

void InstrumentBrowser::setLoadContext (bool ready, String name, String restriction,
                                         bool samplerOnTrack, bool synthOnTrack,
                                         bool effectReady, String loadedAppleId, int selectedGmProgram)
{
    if (canCreate == ready && targetName == name && targetRestriction == restriction
        && samplerOnTarget == samplerOnTrack && synthOnTarget == synthOnTrack
        && canInsertEffect == effectReady && hostedAppleIdentifier == loadedAppleId
        && currentGmProgram == selectedGmProgram) return;
    canCreate = ready;
    samplerOnTarget = samplerOnTrack;
    synthOnTarget = synthOnTrack;
    canInsertEffect = effectReady;
    hostedAppleIdentifier = std::move (loadedAppleId);
    currentGmProgram = selectedGmProgram;
    targetName = std::move (name);
    targetRestriction = std::move (restriction);
    updateDetails();
}

void InstrumentBrowser::browseAppleEffects()
{
    collection = Collection::appleEffects;
    libraryView.setSelectedId (3, dontSendNotification);
    family = "All Instruments";
    type = "All Types";
    rebuildCatalog();
}

void InstrumentBrowser::browseGeneralMidi()
{
    collection = Collection::appleSounds;
    libraryView.setSelectedId (2, dontSendNotification);
    family = "All Instruments";
    type = "All Types";
    rebuildCatalog();
}

void InstrumentBrowser::browseAllSounds()
{
    if (collection == Collection::instruments) return;
    collection = Collection::instruments;
    libraryView.setSelectedId (1, dontSendNotification);
    family = "All Instruments"; type = "All Types"; rebuildCatalog();
}

void InstrumentBrowser::syncLoadedSound (String key)
{
    if (key.isEmpty() || key == selectedKey || collection == Collection::appleEffects) return;
    const auto found = std::find_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.key == key; });
    if (found == entries.end()) return;
    ScopedValueSetter<bool> guard (refreshing, true);
    family = found->family; type = "All Types"; selectedKey = key;
    search.setText ({}, false); favouritesOnly = false;
    favouritesFilter.setToggleState (false, dontSendNotification);
    if (collection != Collection::instruments) { collection = Collection::instruments; libraryView.setSelectedId (1, dontSendNotification); }
    rebuildCatalog();
}

void InstrumentBrowser::setStatus (const String& message)
{
    statusMessage = message;
    updateDetails();
}

void InstrumentBrowser::loadSelected (bool newTrack)
{
    const auto* entry = selected();
    if (entry == nullptr || ! entry->available) return;
    if (! newTrack && ! entry->effect && (! canCreate || targetRestriction.isNotEmpty())) return;
    if (entry->preset != File())
    {
        if (loadImportedSampler) loadImportedSampler (entry->preset, newTrack);
    }
    else if (entry->gmProgram >= 0)
    {
        if (loadGeneralMidi) loadGeneralMidi (entry->plugin, entry->gmProgram, newTrack);
    }
    else if (entry->effect)
    {
        if (! newTrack && canInsertEffect && insertAppleEffect)
            insertAppleEffect (entry->plugin);
    }
    else if (! newTrack && entry->apple && entry->plugin.createIdentifierString() == hostedAppleIdentifier)
    {
        if (openLoadedAppleEditor) openLoadedAppleEditor (entry->plugin);
    }
    else if (entry->key == "studio-sampler")
    {
        if (! newTrack && samplerOnTarget && openSampler) openSampler();
        else if (loadSampler) loadSampler (newTrack);
    }
    else if (entry->key.startsWith ("studio-preset-"))
    {
        if (loadSynth) loadSynth ("preset:" + entry->key.fromFirstOccurrenceOf ("studio-preset-", false, false), newTrack);
    }
    else if (entry->key.startsWith ("studio-synth-"))
    {
        if (loadSynth) loadSynth (entry->key.fromFirstOccurrenceOf ("studio-synth-", false, false), newTrack);
    }
    else if (entry->patch != nullptr && loadAriaPatch)
        loadAriaPatch (*entry->patch, entry->plugin, newTrack);
    else if (loadInstrument)
        loadInstrument (entry->plugin, newTrack);
}

void InstrumentBrowser::updateDetails()
{
    const auto* e = selected();
    const bool available = e != nullptr && e->available;
    instrumentName.setText (e != nullptr ? e->name : "No results", dontSendNotification);
    favourite.setEnabled (e != nullptr);
    favourite.setButtonText (e != nullptr && favouriteKeys.contains (e->key)
                             ? "Favorited" : "Favorite");
    vendor.setText (e != nullptr ? e->vendor : String(), dontSendNotification);
    vendor.setTooltip (vendor.getText());
    instrumentName.setTooltip (instrumentName.getText());
    explanation.setText (e != nullptr ? e->description : "Change the search or filters.", dontSendNotification);
    explanation.setTooltip (explanation.getText());
    const bool appleHosted = available && e->apple && ! e->effect
        && e->plugin.createIdentifierString() == hostedAppleIdentifier;
    const bool gmEntry = available && e->gmProgram >= 0;
    const bool gmOnTrack = gmEntry && appleHosted;
    const bool gmSelected = gmOnTrack && currentGmProgram == e->gmProgram;
    const bool openNative = available && ((e->key == "studio-sampler" && samplerOnTarget)
        || (e->key.startsWith ("studio-synth-") && synthOnTarget));
    const bool effect = available && e->effect;
    load.setButtonText (effect ? "Insert Apple Effect"
                               : gmSelected ? "Reselect GM Sound"
                               : gmOnTrack ? "Select GM Sound"
                               : gmEntry ? "Load GM Sound" : appleHosted ? "Open Apple AU"
                               : openNative ? "Open Instrument" : "Load on Selected Track");
    load.setEnabled (effect ? canInsertEffect : gmOnTrack
                      ? canCreate : appleHosted || openNative
                      || (available && canCreate && targetRestriction.isEmpty()));
    add.setVisible (! effect);
    add.setEnabled (! effect && available && canCreate);
    programs.setVisible (e != nullptr && e->apple && ! e->effect && e->gmProgram < 0);
    programs.setEnabled (appleHosted);
    programs.setTooltip (appleHosted ? "Browse programs supported by this hosted Apple Audio Unit"
                        : "Load this Apple sound device on the selected track first");
    load.setTooltip (effect ? (canInsertEffect ? "Add Apple AU effect after existing inserts, before the track fader"
                                             : "Select an unfrozen track and stop recording")
                    : gmEntry ? (gmOnTrack ? "Change DLSMusicDevice to this GM program"
                          : "Load this sound in Apple DLSMusicDevice on a compatible track")
                    : appleHosted ? "Open the original Apple instrument editor" : openNative
                    ? "Open the native instrument on " + targetName
                    : targetRestriction.isEmpty() ? "Load on " + targetName : targetRestriction);
    load.setTitle (load.getTooltip());
    add.setTooltip (available ? "Create a track and load the selected instrument" : "The selected instrument is unavailable");
    status.setText (statusMessage.isNotEmpty() ? statusMessage
                   : gmSelected ? "GM " + String (gm::displayNumber (e->gmProgram)) 
                                    + " selected | " + targetName
                   : gmOnTrack ? "DLSMusicDevice ready | " + targetName
                   : effect ? (canInsertEffect ? "Apple effect target | " + targetName
                          : "Select an unfrozen track and stop recording to insert an effect")
                   : targetRestriction.isNotEmpty() ? targetRestriction : "Selected track | " + targetName,
                   dontSendNotification);
    status.setTooltip (status.getText());
    resized();
    repaint (detailPanel);
}

int InstrumentBrowser::Rows::getNumRows()
{
    return kind == families ? browser.families.size() : kind == types ? browser.types.size() : (int) browser.filtered.size();
}

String InstrumentBrowser::Rows::getNameForRow (int row)
{
    if (! isPositiveAndBelow (row, getNumRows())) return {};
    if (kind != results) return (kind == families ? browser.families : browser.types)[row];
    const auto& entry = browser.entries[browser.filtered[(size_t) row]];
    if (entry.patch != nullptr)
        return entry.name + ", " + entry.vendor + ", " + entry.patchStatus;
    return (browser.favouriteKeys.contains (entry.key) ? "Favorite: " : String())
             + entry.name + ", " + entry.type + ", " + (entry.available ? "available"
             : entry.family == "Studio" ? "planned" : "unavailable");
}

void InstrumentBrowser::Rows::paintListBoxItem (int row, Graphics& g, int w, int h, bool selectedRow)
{
    if (! isPositiveAndBelow (row, getNumRows())) return;
    auto bounds = Rectangle<int> (0, 0, w, h).reduced (3, 1);
    g.setColour (selectedRow ? colours::selected : kind == results ? colours::control : colours::canvas);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    if (selectedRow)
    {
        g.setColour (colours::focus.withAlpha (0.7f));
        g.fillRoundedRectangle (bounds.reduced (1).withWidth (appearance.px (2)).toFloat(), 1.0f);
    }
    bounds.reduce (appearance.px (6), 0);
    g.setColour (colours::text);
    g.setFont (uiFont (12.0f, selectedRow ? Font::bold : Font::plain));
    if (kind != results)
    {
        g.drawFittedText ((kind == families ? browser.families : browser.types)[row], bounds, Justification::centredLeft, 1, 1.0f);
        return;
    }
    const auto& entry = browser.entries[browser.filtered[(size_t) row]];
    drawInstrument (g, bounds.removeFromLeft (appearance.px (25)).reduced (appearance.px (2)).toFloat(), entry.name, entry.family, entry.effect, colours::action);
    bounds.removeFromLeft (appearance.px (4));
    g.drawFittedText (entry.name, bounds.removeFromTop (appearance.line (12, 0)), Justification::centredLeft, 1, 1.0f);
    g.setFont (uiFont (10.5f));
    const bool activeGm = entry.gmProgram >= 0
        && entry.plugin.createIdentifierString() == browser.hostedAppleIdentifier
        && entry.gmProgram == browser.currentGmProgram;
    g.setColour (activeGm ? colours::focus : entry.available ? colours::signal
                           : entry.family == "Studio" ? colours::secondary : colours::warning);
    g.drawFittedText (entry.type + " | " + (activeGm ? "Selected on track"
                    : entry.patch != nullptr ? entry.patchStatus
                    : entry.effect ? "Insert Effect"
                    : entry.available ? "Available" : entry.family == "Studio" ? "Planned" : "Unavailable"),
                      bounds, Justification::centredLeft, 1, 1.0f);
}

void InstrumentBrowser::activateRow (int row)
{
    if (refreshing || ! isPositiveAndBelow (row, (int) filtered.size())) return;
    selectedKey = entries[filtered[(size_t) row]].key;
    updateDetails();
    loadSelected (false);
}

void InstrumentBrowser::Rows::listBoxItemClicked (int row, const MouseEvent& event)
{
    if (kind == results && ! event.mods.isPopupMenu() && event.getNumberOfClicks() == 1)
        browser.activateRow (row);
}
void InstrumentBrowser::Rows::listBoxItemDoubleClicked (int, const MouseEvent&) {}
void InstrumentBrowser::Rows::returnKeyPressed (int row)
{
    if (kind == results) browser.activateRow (row);
}

void InstrumentBrowser::Rows::selectedRowsChanged (int row)
{
    if (browser.refreshing || ! isPositiveAndBelow (row, getNumRows())) return;
    if (kind == families)
    {
        browser.family = browser.families[row];
        browser.compactFamily.setSelectedItemIndex (row, dontSendNotification);
        browser.type = "All Types";
        browser.rebuildTypes();
    }
    else if (kind == types)
    {
        browser.type = browser.types[row];
        browser.compactType.setSelectedItemIndex (row, dontSendNotification);
        browser.rebuildResults();
    }
    else
    {
        browser.selectedKey = browser.entries[browser.filtered[(size_t) row]].key;
        browser.setStatus ({});
        browser.updateDetails();
    }
}

void InstrumentBrowser::appearanceChanged()
{
    const auto& m = appearance;
    familyList.setRowHeight (m.control (24, 12));
    typeList.setRowHeight (m.control (24, 12));
    resultList.setRowHeight (m.line (12, 2) + m.line (11, 2));
    for (auto* list : { &familyList, &typeList, &resultList })
        list->getViewport()->setScrollBarThickness (m.scrollbar());
    search.setFont (uiFont (13.0f));
    search.applyFontToAllText (uiFont (13.0f));
    resized();
    repaint();
}

int InstrumentBrowser::minimumContentHeight (int width) const
{
    const auto& m = appearance;
    const int details = m.line (11) + m.px (35) + 2 * m.line (14, 1) + 2 * m.line (13, 1)
        + 2 * m.line (13, 1) + 4 * m.control (25) + m.px (10);
    const int list = 3 * (m.line (12, 2) + m.line (11, 2));
    const bool stacked = m.stackedBrowserDetails (width);
    const bool stackedFilters = m.browserFilterRow (width);
    return m.control (30) * (stackedFilters ? 3 : 2) + 2 * m.line (13, 2) + m.px (6)
        + (stacked ? details + list + m.px (5) : jmax (details, list));
}

void InstrumentBrowser::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (6), m.px (4));
    area.removeFromRight (m.px (5)); // sidebar resize handle
    auto header = area.removeFromTop (m.control (27));
    close.setBounds (header.removeFromRight (jmin (header.getWidth(), m.span (55))));
    detailsTitle.setText ("Instrument Library", dontSendNotification);
    detailsTitle.setBounds (header);
    auto artwork = area.removeFromTop (jmin (m.px (100), area.getHeight() / 5));
    art = artwork.reduced (m.px (4));
    instrumentName.setBounds (area.removeFromTop (m.line (14, 3)));
    search.setBounds (area.removeFromTop (m.control (28)).reduced (0, 1));
    auto filters = area.removeFromTop (m.control (27));
    favouritesFilter.setBounds (filters.removeFromRight (jmin (filters.getWidth()/2, m.span (85))).reduced (1));
    libraryView.setBounds (filters.reduced (1));
    // Musical family is the primary filter; source/format is optional.
    compact = true;
    const bool twoColumns = area.getWidth() >= m.span (260);
    compactFamily.setVisible (! twoColumns);
    familyList.setVisible (twoColumns);
    compactType.setVisible (false); typeList.setVisible (false);
    count.setVisible (false); vendor.setVisible (false); explanation.setVisible (false);
    manage.setVisible (false); add.setVisible (false);
    load.setVisible (collection == Collection::appleEffects);
    if (load.isVisible()) load.setBounds (area.removeFromBottom (m.control (26)));
    auto libraryActions = area.removeFromBottom (m.control (26));
    ariaTools.setBounds (libraryActions.removeFromRight (m.span (68)).reduced (1));
    importLibrary.setBounds (libraryActions.reduced (1));
    status.setBounds (area.removeFromBottom (2 * m.line (12, 2)));
    auto actions = area.removeFromBottom (m.control (26));
    if (programs.isVisible()) programs.setBounds (actions.removeFromLeft (actions.getWidth()/2).reduced (1));
    favourite.setBounds (actions.reduced (1));
    if (twoColumns)
        familyList.setBounds (area.removeFromLeft (area.getWidth() * 42 / 100).reduced (1));
    else compactFamily.setBounds (area.removeFromTop (m.control (27)));
    resultList.setBounds (area.reduced (1));
    detailPanel = artwork;
}

MouseCursor InstrumentBrowser::getMouseCursor()
{
    return getMouseXYRelative().x >= getWidth() - appearance.px (10)
        ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor;
}
void InstrumentBrowser::mouseDown (const MouseEvent& e)
{
    resizingDetails = e.x >= getWidth() - appearance.px (10);
    if (resizingDetails) { resizeStartX = e.getScreenPosition().x; resizeStartWidth = getWidth(); }
}
void InstrumentBrowser::mouseDrag (const MouseEvent& e)
{
    if (resizingDetails && onWidthChanged) onWidthChanged (resizeStartWidth + e.getScreenPosition().x - resizeStartX);
}
void InstrumentBrowser::mouseUp (const MouseEvent&) { resizingDetails = false; }

void InstrumentBrowser::paint (Graphics& g)
{
    const auto detail = detailPanel.toFloat().reduced (0.5f);
    g.fillAll (colours::panel);
    g.setColour (colours::divider);
    g.fillRect (getWidth() - appearance.px (5), 0, appearance.px (2), getHeight());
    g.setColour (colours::control);
    g.fillRoundedRectangle (detail, (float) appearance.px (5));
    g.setColour (colours::divider);
    g.drawRoundedRectangle (detail, (float) appearance.px (5), 0.75f);
    if (const auto* e = selected(); e && art.getHeight() > 8)
    {
        g.setColour (colours::well);
        g.fillRoundedRectangle (art.toFloat(), (float) appearance.px (4));
        drawInstrument (g, art.toFloat().reduced (2.0f), e->name, e->family, e->effect, colours::action);
    }
}
}
