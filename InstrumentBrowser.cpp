// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentBrowser.h"
#include "StudioUI.h"
#include <algorithm>

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

void drawInstrument (Graphics& g, Rectangle<float> bounds, bool orchestra, Colour colour)
{
    const auto box = bounds.withSizeKeepingCentre (jmin (bounds.getWidth(), bounds.getHeight() * 1.6f),
                                                    bounds.getHeight());
    const auto transform = AffineTransform::scale (box.getWidth() / 100.0f, box.getHeight() / 60.0f)
                               .translated (box.getX(), box.getY());
    Graphics::ScopedSaveState save (g);
    g.addTransform (transform);
    g.setColour (colour);
    if (orchestra)
    {
        // Simple in-house flute silhouette identifies the orchestral player.
        g.addTransform (AffineTransform::rotation (-0.46f, 50.0f, 30.0f));
        g.drawRoundedRectangle (7.0f, 26.0f, 86.0f, 8.0f, 3.5f, 2.0f);
        g.drawLine (25.0f, 22.0f, 88.0f, 22.0f, 1.5f);
        for (int x : { 31, 42, 53, 64, 76, 85 })
            g.drawEllipse ((float) x, 17.0f, 5.0f, 5.0f, 1.6f);
        g.drawVerticalLine (16, 26.0f, 35.0f);
        g.drawVerticalLine (89, 25.0f, 35.0f);
    }
    else
    {
        g.drawRoundedRectangle (8.0f, 10.0f, 84.0f, 40.0f, 5.0f, 2.0f);
        for (int x : { 22, 36, 50, 64, 78 })
            g.drawVerticalLine (x, 12.0f, 49.0f);
        for (int x : { 18, 32, 60, 74 })
            g.fillRect (x, 11, 8, 24);
    }
}
}

InstrumentBrowser::InstrumentBrowser (KnownPluginList& source) : known (source)
{
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
             &search, &compactFamily, &compactType, &manage, &detailsTitle, &instrumentName, &vendor,
             &explanation, &status, &count, &load, &add })
        addAndMakeVisible (*item);
    search.setTitle ("Search instruments");
    search.setTextToShowWhenEmpty ("Search instruments, formats or makers", colours::secondary);
    search.setColour (TextEditor::backgroundColourId, colours::control);
    search.setColour (TextEditor::outlineColourId, colours::divider);
    search.onTextChange = [this] { rebuildResults(); };
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
    manage.onClick = [this] { if (openPluginManager) openPluginManager(); };
    appearanceChanged();
    known.addChangeListener (this);
    rebuildCatalog();
}

InstrumentBrowser::~InstrumentBrowser()
{
    known.removeChangeListener (this);
    for (auto* list : { &familyList, &typeList, &resultList })
        list->setModel (nullptr);
}

void InstrumentBrowser::changeListenerCallback (ChangeBroadcaster*) { rebuildCatalog(); }

void InstrumentBrowser::rebuildCatalog()
{
    entries.clear();
    bool hasAria = false;
    for (const auto& plugin : known.getTypes())
    {
        if (plugin.pluginFormatName != "AudioUnit" || (! plugin.isInstrument && ! isAria (plugin)))
            continue;
        const bool aria = isAria (plugin);
        hasAria |= aria;
        entries.push_back ({ plugin.createIdentifierString(), plugin.name,
            aria ? "Orchestral" : "Audio Units",
            aria ? "Sample Player" : (plugin.category.isNotEmpty() ? plugin.category : "Instrument"),
            plugin.manufacturerName + " | Audio Unit",
            aria ? "Choose library patches in the hosted ARIA editor." : "Scanned Audio Unit instrument.",
            plugin, true });
    }
    if (! hasAria)
        entries.push_back ({ "missing-aria", "ARIA Player", "Orchestral", "Sample Player", "Garritan | Audio Unit",
                             "Install and scan the ARIA Audio Unit in Plug-ins.", {}, false });
    entries.push_back ({ "planned-synth", "Studio Synth", "Studio", "Synthesizer", "Planned native instrument",
                         "Studio Synth will be available in a later milestone.", {}, false });
    entries.push_back ({ "studio-sampler", "Studio Sampler", "Studio", "Sampler", "Studio | Native instrument",
                         "Play WAV and AIFF samples, edit the sound, and build multisample instruments.", {}, true });
    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.available != b.available) return a.available;
        return a.name.compareNatural (b.name) < 0;
    });
    families.clear();
    families.add ("All Instruments");
    for (const auto& entry : entries)
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
        if (family == "All Instruments" || entry.family == family)
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
        if ((family != "All Instruments" && family != e.family) || (type != "All Types" && type != e.type))
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

void InstrumentBrowser::setLoadContext (bool ready, String name, String restriction)
{
    if (canCreate == ready && targetName == name && targetRestriction == restriction) return;
    canCreate = ready;
    targetName = std::move (name);
    targetRestriction = std::move (restriction);
    updateDetails();
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
    if (entry->key == "studio-sampler")
    {
        if (loadSampler) loadSampler (newTrack);
    }
    else if (loadInstrument)
        loadInstrument (entry->plugin, newTrack);
}

void InstrumentBrowser::updateDetails()
{
    const auto* e = selected();
    const bool available = e != nullptr && e->available;
    instrumentName.setText (e != nullptr ? e->name : "No results", dontSendNotification);
    vendor.setText (e != nullptr ? e->vendor : String(), dontSendNotification);
    vendor.setTooltip (vendor.getText());
    instrumentName.setTooltip (instrumentName.getText());
    explanation.setText (e != nullptr ? e->description : "Change the search or filters.", dontSendNotification);
    explanation.setTooltip (explanation.getText());
    load.setEnabled (available && canCreate && targetRestriction.isEmpty());
    add.setEnabled (available && canCreate);
    load.setTooltip (targetRestriction.isEmpty() ? "Load on " + targetName : targetRestriction);
    load.setTitle (targetRestriction.isEmpty() ? "Load on " + targetName : "Load on Selected Track. " + targetRestriction);
    add.setTooltip (available ? "Create a track and load the selected instrument" : "The selected instrument is unavailable");
    status.setText (statusMessage.isNotEmpty() ? statusMessage
                   : targetRestriction.isNotEmpty() ? targetRestriction : "Selected track | " + targetName,
                   dontSendNotification);
    status.setTooltip (status.getText());
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
    return entry.name + ", " + entry.type + ", " + (entry.available ? "available"
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
    drawInstrument (g, bounds.removeFromLeft (appearance.px (25)).reduced (appearance.px (2)).toFloat(), entry.family == "Orchestral", colours::action);
    bounds.removeFromLeft (appearance.px (4));
    g.drawFittedText (entry.name, bounds.removeFromTop (appearance.line (12, 0)), Justification::centredLeft, 1, 1.0f);
    g.setFont (uiFont (10.5f));
    g.setColour (entry.available ? colours::signal : entry.family == "Studio" ? colours::secondary : colours::warning);
    g.drawFittedText (entry.type + " | " + (entry.available ? "Available" : entry.family == "Studio" ? "Planned" : "Unavailable"),
                      bounds, Justification::centredLeft, 1, 1.0f);
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
        + 2 * m.line (13, 1) + 2 * m.control (25) + m.px (10);
    const int list = 3 * (m.line (12, 2) + m.line (11, 2));
    const bool stacked = m.stackedBrowserDetails (width);
    const bool stackedFilters = m.browserFilterRow (width);
    return m.control (30) * (stackedFilters ? 2 : 1) + 2 * m.line (13, 2) + m.px (6)
        + (stacked ? details + list + m.px (5) : jmax (details, list));
}

void InstrumentBrowser::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (8), m.px (2));
    auto toolbar = area.removeFromTop (m.control (30)).reduced (0, m.px (1));
    manage.setBounds (toolbar.removeFromRight (m.span (95)));
    const bool compactFamilyFilter = m.browserFilterRow (getWidth());
    compact = m.compactBrowser (getWidth());
    const bool stackDetails = m.stackedBrowserDetails (getWidth());
    compactFamily.setVisible (compactFamilyFilter);
    familyList.setVisible (! compactFamilyFilter);
    compactType.setVisible (compact);
    typeList.setVisible (! compact);
    count.setVisible (toolbar.getWidth() >= m.span (370));
    if (count.isVisible()) count.setBounds (toolbar.removeFromRight (m.span (122)));
    if (compactFamilyFilter)
    {
        auto filters = area.removeFromTop (m.control (30));
        compactFamily.setBounds (filters.removeFromLeft (filters.getWidth() / 2).reduced (m.px (2), 0));
        compactType.setBounds (filters.reduced (m.px (2), 0));
    }
    else if (compact)
        compactType.setBounds (toolbar.removeFromRight (m.span (128)).reduced (m.px (2), 0));
    search.setBounds (toolbar.reduced (m.px (2), 0));
    status.setBounds (area.removeFromBottom (2 * m.line (13, 2)).reduced (m.px (4), 0));
    if (stackDetails)
    {
        const int rows = 3 * resultList.getRowHeight();
        const auto result = area.removeFromTop (rows);
        resultList.setBounds (result);
        detailPanel = area.withTrimmedTop (m.px (5));
    }
    else
    {
        detailPanel = area.removeFromRight (jmin (area.getWidth() / 2, m.span (258)));
        if (! compactFamilyFilter) familyList.setBounds (area.removeFromLeft (m.span (110)));
        if (! compact) typeList.setBounds (area.removeFromLeft (m.span (120)));
        resultList.setBounds (area);
    }
    const auto shortActions = detailPanel.getWidth() < m.span (222);
    load.setButtonText (shortActions ? "Load on Track" : "Load on Selected Track");
    auto detail = detailPanel.reduced (m.px (8), m.px (2));
    add.setBounds (detail.removeFromBottom (m.control (25)).reduced (0, 1));
    load.setBounds (detail.removeFromBottom (m.control (25)).reduced (0, 1));
    explanation.setBounds (detail.removeFromBottom (2 * m.line (13, 1)));
    vendor.setBounds (detail.removeFromBottom (2 * m.line (13, 1)));
    instrumentName.setBounds (detail.removeFromBottom (2 * m.line (14, 1)));
    detailsTitle.setBounds (detail.removeFromTop (m.line (11)));
    art = detail.reduced (m.px (2), m.px (1));
}

void InstrumentBrowser::paint (Graphics& g)
{
    const auto detail = detailPanel.toFloat().reduced (0.5f);
    g.setColour (colours::control);
    g.fillRoundedRectangle (detail, (float) appearance.px (5));
    g.setColour (colours::divider);
    g.drawRoundedRectangle (detail, (float) appearance.px (5), 0.75f);
    if (const auto* e = selected(); e && art.getHeight() > 8)
    {
        g.setColour (colours::well);
        g.fillRoundedRectangle (art.toFloat(), (float) appearance.px (4));
        drawInstrument (g, art.toFloat().reduced (2.0f), e->family == "Orchestral", colours::action);
    }
}
}
