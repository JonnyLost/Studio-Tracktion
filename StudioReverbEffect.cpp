// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioReverbEffect.h"
#include "SpatialState.h"
#include "SpatialWorkspace.h"
#include "StudioUI.h"

namespace studio::spatial
{
using namespace juce;
namespace te = tracktion;

namespace
{
const Identifier roomKey { "studioReverbSettings" };

const std::array creativeRooms {
    FactoryRoom { "Smooth Hall", "Creative", venue (20, 28, 11, .30f, Surface::wood, Surface::wood, Surface::plaster, .23f, .78f, 20,
        { 1.23f, 1.07f, .77f, .91f, .93f, .42f, .31f, 1.17f }), "Soft buildup and a broad tail." },
    FactoryRoom { "Warm Hall", "Creative", venue (22, 27, 13, .40f, Surface::wood, Surface::wood, Surface::plaster, .28f, .75f, 15,
        { 1.3f, 1.10f, .48f, .78f, .8f, .34f, .31f, .98f, .4f, -3.8f }), "Muted highs and a long midrange decay." },
    FactoryRoom { "Bright Hall", "Creative", venue (21, 33, 13, .22f, Surface::plaster, Surface::wood, Surface::plaster, .44f, .73f, 20,
        { 1.06f, 1.09f, .44f, .73f, .83f, .42f, .4f, 1.16f, -3, 2.5f }), "Clear, bright reflections and a wide tail." },
    FactoryRoom { "Dark Hall", "Creative", venue (23, 35, 15, .49f, Surface::wood, Surface::carpet, Surface::plaster, .31f, .67f, 18,
        { 1.4f, 1.22f, .6f, .82f, .88f, .6f, .26f, .91f, -1, -7 }), "Dark, slow-building ambience." },
    FactoryRoom { "Dense Room", "Creative", venue (8, 13, 4, .41f, Surface::plaster, Surface::wood, Surface::plaster, .44f, .64f, 3,
        { .76f, .61f, .68f, .96f, 1, .27f, .48f, .97f }), "A compact tail with tightly spaced echoes." },
    FactoryRoom { "Atmospheric Space", "Creative", venue (31, 53, 24, .23f, Surface::stone, Surface::wood, Surface::plaster, .13f, .87f, 39,
        { 1.9f, 1.36f, .91f, .92f, 1, 1.05f, .34f, 1.34f, -4, -2 }), "An expansive, slowly modulated tail." },
    FactoryRoom { "Algorithmic Plate", "Creative", venue (9, 11, 4, .28f, Surface::plaster, Surface::wood, Surface::plaster, .06f, .9f, 7,
        { 1.8f, .58f, .57f, 1, .96f, .35f, .55f, 1.19f, -5, 2.1f, .34f, Quality::high }), "Bright, compact diffusion with a sustained tail." },
    FactoryRoom { "Spring-inspired", "Creative", venue (5, 8, 3, .24f, Surface::plaster, Surface::wood, Surface::plaster, .37f, .68f, 11,
        { 1.25f, .65f, .12f, .28f, .21f, .92f, 1.08f, .47f, -7, -2, .31f, Quality::eco }), "Fluttery, sparse echoes for a spring-like texture." }
};

PropertiesFile::Options presetOptions()
{
    PropertiesFile::Options options;
    options.applicationName = "Studio Reverb Presets";
    options.filenameSuffix = "xml";
    options.folderName = "Studio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = PropertiesFile::storeAsXML;
    return options;
}
}

ReverbPlugin::ReverbPlugin (te::PluginCreationInfo info) : Plugin (info) { publish(); }
ReverbPlugin::~ReverbPlugin() { notifyListenersOfDeletion(); }

Room ReverbPlugin::room() const
{
    if (auto xml = parseXML (state.getProperty (roomKey).toString()))
        return readRoom (ValueTree::fromXml (*xml));
    return factoryRooms.front().room;
}

void ReverbPlugin::setRoom (Room settings)
{
    state.setProperty (roomKey, writeRoom (settings, {}).createXml()->toString (XmlElement::TextFormat().singleLine()),
                       getUndoManager());
}

void ReverbPlugin::publish() { publication.write (makeRoomPlan (room(), RoomMode::effect)); }

void ReverbPlugin::valueTreePropertyChanged (ValueTree& node, const Identifier& property)
{
    if (node == state && property == roomKey) publish();
    Plugin::valueTreePropertyChanged (node, property);
}

void ReverbPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    accepted = 0;
    auto plan = makeRoomPlan (factoryRooms.front().room, RoomMode::effect);
    publication.read (plan, accepted);
    dsp.preparePlan (info.sampleRate, plan);
    meterFall = (float) std::max (2400., .3 * info.sampleRate);
    input.store (0);
    output.store (0);
}

void ReverbPlugin::deinitialise() { input.store (0); output.store (0); }
void ReverbPlugin::reset() { dsp.reset(); input.store (0); output.store (0); }
std::array<float, 2> ReverbPlugin::levels() const noexcept { return { input.load(), output.load() }; }

void ReverbPlugin::applyToBuffer (const te::PluginRenderContext& context)
{
    auto* b = context.destBuffer;
    if (b == nullptr || b->getNumChannels() == 0) return;
    RoomPlan plan;
    if (publication.read (plan, accepted)) dsp.setPlan (plan);
    auto* l = b->getWritePointer (0, context.bufferStartSample);
    auto* r = b->getNumChannels() > 1 ? b->getWritePointer (1, context.bufferStartSample) : nullptr;
    const int samples = context.bufferNumSamples;
    const auto peak = dsp.process (l, r, samples);
    float sum = 0;
    for (int i = 0; i < samples; ++i)
        sum = std::max (sum, std::max (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.f));
    const auto fall = 1.f / (1.f + float (samples) / meterFall);
    input.store (std::max (input.load (std::memory_order_relaxed) * fall, peak.input), std::memory_order_relaxed);
    output.store (std::max (output.load (std::memory_order_relaxed) * fall, sum), std::memory_order_relaxed);
}

class ReverbControls::Field final : public SpatialField
{
public:
    Field (const char* label, double low, double high, double step, const char* suffix,
           std::function<float(Room)> getValue, std::function<void(Room&, float)> setValue)
        : SpatialField (label, low, high, step, suffix), read (std::move (getValue)), write (std::move (setValue)) {}
    std::function<float(Room)> read;
    std::function<void(Room&, float)> write;
};

ReverbControls::ReverbControls (bool effect, bool advanced)
{
    const auto control = [&] (const char* label, double low, double high, double step, const char* suffix,
                              auto read, auto write)
    {
        auto field = std::make_unique<Field> (label, low, high, step, suffix, read, write);
        if (effect && ! advanced) field->setControlStyle (Slider::RotaryHorizontalVerticalDrag);
        field->onChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible (*field);
        fields.push_back (std::move (field));
    };
    const auto shape = [&] (const char* label, float ReverbShape::*member, double low, double high, double step,
                            const char* suffix, float scale = 1.f)
    {
        control (label, low, high, step, suffix, [member, scale] (Room r) { return r.shape.*member * scale; },
                 [member, scale] (Room& r, float v) { r.shape.*member = v / scale; });
    };
    const auto roomField = [&] (const char* label, float Room::*member, double low, double high, double step,
                                const char* suffix, float scale = 1.f)
    {
        control (label, low, high, step, suffix, [member, scale] (Room r) { return r.*member * scale; },
                 [member, scale] (Room& r, float v) { r.*member = v / scale; });
    };
    shape ("Decay", &ReverbShape::decay, .25, 3, .05, " x");
    if (effect)
    {
        shape ("Wet mix", &ReverbShape::mix, 0, 100, 1, "%", 100);
        roomField ("Predelay", &Room::preDelayMs, 0, 100, 1, " ms");
        roomField ("Wet trim", &Room::gain, -60, 0, .5, " dB");
        if (! advanced)
        {
            shape ("Room size", &ReverbShape::size, .5, 1.5, .01, " x");
            shape ("Diffusion", &ReverbShape::diffusion, 0, 100, 1, "%", 100);
        }
    }
    if (advanced)
    {
        shape ("Room size", &ReverbShape::size, .5, 1.5, .01, " x");
        shape ("Buildup", &ReverbShape::attack, 0, 100, 1, "%", 100);
        shape ("Diffusion", &ReverbShape::diffusion, 0, 100, 1, "%", 100);
        shape ("Density", &ReverbShape::density, 0, 100, 1, "%", 100);
        shape ("Mod depth", &ReverbShape::motionDepthMs, 0, 1.3, .01, " ms");
        shape ("Mod rate", &ReverbShape::motionRate, .07, 1.25, .01, " Hz");
        shape ("Stereo width", &ReverbShape::width, 0, 140, 1, "%", 100);
        shape ("Low EQ", &ReverbShape::lowEq, -12, 6, .5, " dB");
        shape ("High EQ", &ReverbShape::highEq, -12, 6, .5, " dB");
        if (effect)
        {
            roomField ("Early", &Room::early, 0, 100, 1, "%", 100);
            roomField ("Late", &Room::late, 0, 100, 1, "%", 100);
            for (size_t band = 0; band < bandNames.size(); ++band)
                control (bandNames[band], .25, 2, .05, " x", [band] (Room r) { return r.decayTrim[band]; },
                         [band] (Room& r, float v) { r.decayTrim[band] = v; });
        }
    }
    if (advanced || effect)
    {
        for (int i = 0; i < (int) qualityNames.size(); ++i) quality.addItem (qualityNames[(size_t) i], i + 1);
        quality.setTitle ("Reverb quality");
        quality.setTextWhenNothingSelected ("Quality");
        quality.setTooltip ("Eco uses four delay lines, Standard eight, High sixteen.");
        quality.onChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible (quality);
    }
}

ReverbControls::~ReverbControls() = default;

void ReverbControls::setRoom (Room value)
{
    reference = value;
    for (auto& field : fields)
        if (! field->hasKeyboardFocus (true)) field->set (field->read (value));
    if (quality.isVisible()) quality.setSelectedId ((int) value.shape.quality + 1, dontSendNotification);
}

Room ReverbControls::readRoom() const
{
    auto result = reference;
    for (auto& field : fields) field->write (result, field->get());
    if (quality.isVisible() && quality.getSelectedId() > 0) result.shape.quality = (Quality) (quality.getSelectedId() - 1);
    return validate (result);
}

int ReverbControls::preferredHeight (int width) const
{
    const bool knobs = fields.size() == 6;
    const auto columns = jmax (1, jmin ((int) fields.size(), width / appearance.span (knobs ? 95 : 140)));
    const auto rows = ((int) fields.size() + columns - 1) / columns;
    return rows * (knobs ? appearance.px (112) : appearance.line (12, 2) + appearance.control (29))
        + (quality.isVisible() ? appearance.control (28) : 0);
}

void ReverbControls::resized()
{
    auto area = getLocalBounds();
    const bool knobs = fields.size() == 6;
    const auto columns = jmax (1, jmin ((int) fields.size(), getWidth() / appearance.span (knobs ? 95 : 140)));
    Rectangle<int> row;
    for (int i = 0; i < (int) fields.size(); ++i)
    {
        if (i % columns == 0) row = area.removeFromTop (knobs ? appearance.px (112) : appearance.line (12, 2) + appearance.control (29));
        fields[(size_t) i]->setBounds (row.removeFromLeft (row.getWidth() / (columns - i % columns)));
    }
    if (quality.isVisible()) quality.setBounds (area.removeFromTop (appearance.control (28)).withWidth (appearance.span (150)));
}

namespace
{
class ReverbMeters final : public Component
{
public:
    void display (std::array<float, 2> values, bool ready)
    {
        peaks = values; available = ready; repaint();
    }
    void paint (Graphics& g) override
    {
        auto area = getLocalBounds().reduced (appearance.px (3), 1);
        g.setFont (uiFont (11.f));
        if (! available)
        {
            g.setColour (colours::secondary);
            g.drawFittedText ("Reverb unavailable while the project is busy.", area, Justification::centredLeft, 1);
            return;
        }
        for (int i = 0; i < 2; ++i)
        {
            auto row = area.removeFromTop (area.getHeight() / (2 - i)).reduced (0, 2);
            g.setColour (colours::secondary);
            g.drawFittedText (i == 0 ? "INPUT" : "OUTPUT", row.removeFromLeft (appearance.span (58)), Justification::centredLeft, 1);
            const float db = Decibels::gainToDecibels (peaks[(size_t) i], -90.f);
            g.drawFittedText (String (db, 1) + " dBFS", row.removeFromRight (appearance.span (88)), Justification::centredRight, 1);
            auto track = row.toFloat().reduced (3.f, 1.f);
            g.setColour (colours::well);
            g.fillRoundedRectangle (track, 2.f);
            const float fill = jlimit (0.f, 1.f, (db + 60.f) / 60.f);
            g.setColour (db > -3.f ? colours::warning : Colour (0xff72b58e));
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * fill), 2.f);
        }
    }
private:
    std::array<float, 2> peaks {};
    bool available = false;
};
class ReverbEditor final : public Component, private Timer
{
public:
    explicit ReverbEditor (ReverbWindow::Resolve resolver) : resolve (std::move (resolver))
    {
        for (Component* item : std::initializer_list<Component*> { &search, &presets, &saveName, &save, &advanced,
                 &bypass, &undo, &redo, &meters, &chart, &viewport, &details })
            addAndMakeVisible (item);
        details.setColour (Label::textColourId, colours::secondary);
        search.setTextToShowWhenEmpty ("Search presets", colours::secondary);
        saveName.setTextToShowWhenEmpty ("User preset name", colours::secondary);
        saveName.setInputRestrictions (64);
        search.onTextChange = [this] { populate(); };
        presets.setTitle ("Reverb presets");
        presets.onChange = [this]
        {
            const int index = presets.getSelectedId() - 1;
            if (! isPositiveAndBelow (index, (int) entries.size())) return;
            const auto& item = entries[(size_t) index];
            change (item.room, "Recall reverb preset");
            details.setText (item.info, dontSendNotification);
        };
        save.onClick = [this]
        {
            auto* plugin = resolve();
            const auto key = saveName.getText().trim();
            if (plugin == nullptr || key.isEmpty()) return;
            user.setValue (key, writeRoom (plugin->room(), key).createXml().get());
            user.saveIfNeeded();
            populate();
            details.setText ("Saved " + key, dontSendNotification);
        };
        advanced.onClick = [this] { showControls (! expanded); };
        bypass.onClick = [this]
        {
            if (auto* plugin = resolve())
            {
                auto room = plugin->room();
                room.bypass = ! room.bypass;
                change (room, "Bypass Studio Reverb");
            }
        };
        undo.onClick = [this] { if (auto* plugin = resolve()) plugin->edit.getUndoManager().undo(); };
        redo.onClick = [this] { if (auto* plugin = resolve()) plugin->edit.getUndoManager().redo(); };
        viewport.setScrollBarsShown (true, false);
        populate();
        showControls (false);
        startTimerHz (12);
    }

    void resized() override
    {
        auto box = getLocalBounds().reduced (appearance.px (10));
        const auto h = appearance.control (29);
        auto row = box.removeFromTop (h);
        search.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (6));
        presets.setBounds (row);
        details.setBounds (box.removeFromTop (appearance.line (12, 6)));
        row = box.removeFromTop (h);
        save.setBounds (row.removeFromRight (appearance.span (75)));
        saveName.setBounds (row.withTrimmedRight (6));
        row = box.removeFromTop (h);
        const auto width = row.getWidth() / 4;
        advanced.setBounds (row.removeFromLeft (width).withTrimmedRight (3));
        bypass.setBounds (row.removeFromLeft (width).withTrimmedRight (3));
        undo.setBounds (row.removeFromLeft (width).withTrimmedRight (3));
        redo.setBounds (row);
        meters.setBounds (box.removeFromTop (2 * appearance.line (11, 2)));
        chart.setBounds (box.removeFromTop (jlimit (appearance.px (68), appearance.px (138), box.getHeight() / 2)));
        viewport.setBounds (box);
        if (controls != nullptr)
        {
            const auto w = std::max (1, box.getWidth() - appearance.scrollbar());
            controls->setSize (w, controls->preferredHeight (w));
        }
    }

private:
    struct Entry { String name, info; Room room; };
    ReverbWindow::Resolve resolve;
    PropertiesFile user { presetOptions() };
    TextEditor search, saveName;
    ComboBox presets;
    TextButton save { "Save" }, advanced { "Advanced" }, bypass { "Bypass" }, undo { "Undo" }, redo { "Redo" };
    ReverbMeters meters;
    Label details;
    RoomGraphic chart;
    std::unique_ptr<ReverbControls> controls;
    Viewport viewport;
    bool expanded = false;

    void populate()
    {
        entries.clear();
        presets.clear (dontSendNotification);
        const auto match = search.getText().trim();
        const auto append = [&] (String name, String info, String category, Room room)
        {
            if (name.containsIgnoreCase (match) || category.containsIgnoreCase (match))
            {
                entries.push_back ({ name, info, room });
                presets.addItem (name, (int) entries.size());
            }
        };
        for (const auto& p : factoryRooms) append (p.name, p.description, p.category, p.room);
        for (const auto& p : creativeRooms) append (p.name, p.description, p.category, p.room);
        for (const auto& key : user.getAllProperties().getAllKeys())
            if (const auto xml = user.getXmlValue (key))
                append (key, "User preset", "User", readRoom (ValueTree::fromXml (*xml)));
        presets.setTextWhenNothingSelected (entries.empty() ? "No matching presets" : "Choose a preset");
    }

    std::vector<Entry> entries;

    void showControls (bool value)
    {
        expanded = value;
        advanced.setButtonText (expanded ? "Basic" : "Advanced");
        viewport.setViewedComponent (nullptr);
        controls = std::make_unique<ReverbControls> (true, value);
        viewport.setViewedComponent (controls.get(), false);
        controls->onChange = [this] { change (controls->readRoom(), "Edit Studio Reverb"); };
        timerCallback();
        resized();
    }

    void change (Room room, const String& action)
    {
        if (auto* plugin = resolve())
        {
            auto* manager = plugin->getUndoManager();
            if (manager == nullptr) return;
            manager->beginNewTransaction (action);
            plugin->setRoom (room);
            manager->beginNewTransaction();
            timerCallback();
        }
    }

    void timerCallback() override
    {
        auto* plugin = resolve();
        const bool available = plugin != nullptr;
        controls->setEnabled (available);
        bypass.setEnabled (available);
        save.setEnabled (available);
        presets.setEnabled (available);
        undo.setEnabled (available && plugin->edit.getUndoManager().canUndo());
        redo.setEnabled (available && plugin->edit.getUndoManager().canRedo());
        if (! available)
        {
            meters.display ({}, false);
            return;
        }
        const auto room = plugin->room();
        controls->setRoom (room);
        chart.setRoom (room);
        bypass.setToggleState (room.bypass, dontSendNotification);
        bypass.setButtonText (room.bypass ? "Enable" : "Bypass");
        const auto peaks = plugin->levels();
        meters.display (peaks, true);
    }
};
}

ReverbWindow::ReverbWindow (Resolve resolve)
    : DocumentWindow ("Studio Reverb", colours::canvas, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ReverbEditor (std::move (resolve)), true);
    setResizable (true, false);
    setResizeLimits (470, 385, 1200, 900);
    centreWithSize (740, 455);
    setVisible (true);
}

void ReverbWindow::appearanceChanged()
{
    setBackgroundColour (colours::canvas);
    if (auto* content = getContentComponent()) { content->resized(); content->repaint(); }
}
}
