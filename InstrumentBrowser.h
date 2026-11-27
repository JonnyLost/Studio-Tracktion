// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <functional>
#include "StudioSampler.h"
#include "StudioLibraryPresentation.h"
#include <vector>

namespace studio
{
class InstrumentBrowser final : public juce::Component, private juce::ChangeListener
{
public:
    explicit InstrumentBrowser (juce::KnownPluginList&, juce::PropertiesFile* preferences = nullptr);
    ~InstrumentBrowser() override;

    std::function<void()> openPluginManager;
    std::function<void(sampler::Instrument,library::Presentation)> loadLibraryInstrument;
    std::function<void(bool newTrack)> loadSampler;
    std::function<void(juce::String family, bool newTrack)> loadSynth;
    std::function<void()> openSampler, openSynth;
    std::function<void(const juce::PluginDescription&, bool newTrack)> loadInstrument;
    std::function<void(const juce::PluginDescription&)> insertAppleEffect;
    std::function<void(const juce::PluginDescription&, int gmProgram, bool newTrack)> loadGeneralMidi;
    // Browse only the actual programs exposed by the *loaded* Audio Unit.
    // Never substitute a fictitious GM list for a bank the AU cannot open.
    std::function<void(const juce::PluginDescription&, juce::Component&)> browseApplePrograms;
    std::function<void(const juce::PluginDescription&)> openLoadedAppleEditor;
    void setLoadContext (bool canCreate, juce::String targetName, juce::String targetRestriction,
                         bool samplerOnTrack = false, bool synthOnTrack = false,
                         bool canInsertEffect = false, juce::String hostedAppleIdentifier = {},
                         int selectedGmProgram = -1);
    void setStatus (const juce::String&);
    void syncLoadedSound (juce::String key);
    void browseAllSounds();
    std::function<void(int)> onWidthChanged;
    std::function<void()> onClose;
    void browseAppleEffects();
    void browseGeneralMidi();
    void appearanceChanged();
    int minimumContentHeight (int width) const;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

private:
    struct Entry
    {
        juce::String key, name, family, type, vendor, description;
        juce::PluginDescription plugin;
        bool available = false, effect = false, apple = false;
        int gmProgram = -1; // MIDI 0-127, -1 means native AU entry rather than a GM sound
    };

    struct Rows final : juce::ListBoxModel
    {
        enum Kind { families, types, results };
        Rows (InstrumentBrowser& b, Kind k) : browser (b), kind (k) {}
        int getNumRows() override;
        juce::String getNameForRow (int) override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        InstrumentBrowser& browser;
        Kind kind;
    };

    juce::Component::SafePointer<juce::DialogWindow> libraryWindow;
    juce::KnownPluginList& known;
    juce::PropertiesFile* preferences = nullptr;
    juce::StringArray favouriteKeys;
    bool favouritesOnly = false;
    int preferredDetailsWidth = 258;
    int resizeStartX = 0, resizeStartWidth = 0;
    bool resizingDetails = false;
    void saveFavourites();
    std::vector<Entry> entries;
    std::vector<size_t> filtered;
    juce::StringArray families, types;
    juce::String family, type, selectedKey, targetName, targetRestriction, statusMessage, hostedAppleIdentifier;
    enum class Collection { instruments, appleSounds, appleEffects };
    Collection collection = Collection::instruments;
    bool canCreate = false, samplerOnTarget = false, synthOnTarget = false;
    bool canInsertEffect = false, refreshing = false, compact = false;
    int currentGmProgram = -1;
    Rows familyRows { *this, Rows::families }, typeRows { *this, Rows::types }, resultRows { *this, Rows::results };
    juce::ListBox familyList, typeList, resultList;
    juce::ComboBox compactFamily, compactType, libraryView;
    juce::TextEditor search;
    juce::Label detailsTitle, instrumentName, vendor, explanation, status, count;
    juce::TextButton close { "Hide" }, manage { "Plug-ins..." }, load { "Load on Selected Track" }, add { "Add as New Track" },
                     programs { "Apple Sound Bank..." },
                     favourite { "Favorite" }, favouritesFilter { "Favorites" };
    juce::Rectangle<int> art, detailPanel;
    const Entry* selected() const;
    void rebuildCatalog();
    void rebuildTypes();
    void rebuildResults();
    void updateDetails();
    void loadSelected (bool newTrack);
    void activateRow (int row);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
}
