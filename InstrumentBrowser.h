// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <functional>
#include "AriaCatalog.h"
#include <vector>
#include <optional>

namespace studio
{
class InstrumentBrowser final : public juce::Component, private juce::ChangeListener
{
public:
    explicit InstrumentBrowser (juce::KnownPluginList&, juce::PropertiesFile* preferences = nullptr);
    ~InstrumentBrowser() override;

    std::function<void()> openPluginManager;
    std::function<void(bool newTrack)> loadSampler;
    std::function<void(juce::String family, bool newTrack)> loadSynth;
    std::function<void()> openSampler, openSynth;
    std::function<void(const juce::PluginDescription&, bool newTrack)> loadInstrument;
    std::function<void(juce::Component&)> openAriaCatalog;
    std::function<void(const aria::Patch&, const juce::PluginDescription&, bool)> loadAriaPatch;
    void setAriaCatalog (const aria::Catalog&);
    std::optional<aria::Patch> selectedAriaPatch() const;
    void setLoadContext (bool canCreate, juce::String targetName, juce::String targetRestriction, bool samplerOnTrack = false, bool synthOnTrack = false);
    void setStatus (const juce::String&);
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
        bool available = false;
        const aria::Patch* patch = nullptr;
        juce::String patchStatus {};
    };

    struct Rows final : juce::ListBoxModel
    {
        enum Kind { families, types, results };
        Rows (InstrumentBrowser& b, Kind k) : browser (b), kind (k) {}
        int getNumRows() override;
        juce::String getNameForRow (int) override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override;
        InstrumentBrowser& browser;
        Kind kind;
    };

    juce::KnownPluginList& known;
    juce::PropertiesFile* preferences = nullptr;
    juce::StringArray favouriteKeys;
    bool favouritesOnly = false;
    int preferredDetailsWidth = 258;
    int resizeStartX = 0, resizeStartWidth = 0;
    bool resizingDetails = false;
    void saveFavourites();
    const aria::Catalog* ariaCatalog = nullptr;
    std::vector<Entry> entries;
    std::vector<size_t> filtered;
    juce::StringArray families, types;
    juce::String family, type, selectedKey, targetName, targetRestriction, statusMessage;
    bool canCreate = false, samplerOnTarget = false, synthOnTarget = false, refreshing = false, compact = false;
    Rows familyRows { *this, Rows::families }, typeRows { *this, Rows::types }, resultRows { *this, Rows::results };
    juce::ListBox familyList, typeList, resultList;
    juce::ComboBox compactFamily, compactType;
    juce::TextEditor search;
    juce::Label detailsTitle, instrumentName, vendor, explanation, status, count;
    juce::TextButton manage { "Plug-ins..." }, ariaTools { "ARIA…" }, load { "Load on Selected Track" }, add { "Add as New Track" },
                     favourite { "☆ Favorite" }, favouritesFilter { "Favorites" };
    juce::Rectangle<int> art, detailPanel;
    const Entry* selected() const;
    void rebuildCatalog();
    void rebuildTypes();
    void rebuildResults();
    void updateDetails();
    void loadSelected (bool newTrack);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
}
