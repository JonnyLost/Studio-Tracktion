// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioSynth.h"
#include "StudioUI.h"
#include <functional>
#include <utility>

namespace studio::synth
{
namespace
{
using namespace juce;
void set (const var& o, const Identifier& key, var value)
{
    o.getDynamicObject()->setProperty (key, std::move (value));
}
var object() { return var (new DynamicObject()); }

class Editor final : public tracktion::Plugin::EditorComponent, private Timer, private MidiKeyboardState::Listener
{
public:
    explicit Editor (Plugin&);
    ~Editor() override;
    bool allowWindowResizing() override { return true; }
    bool isEditorResizable() override { return true; }
    ComponentBoundsConstrainer* getBoundsConstrainer() override { return &limits; }
    void resized() override;
    void paint (Graphics& g) override { g.fillAll (colours::panel); }
private:
    struct Control { Label label; Slider slider; };
    enum Page { sound, shape, modulation, advanced };
    Plugin& plugin;
    StudioLookAndFeel style;
    ComponentBoundsConstrainer limits;
    Label title, preset, status, layerHeading, midiHeading;
    ComboBox family, factory, layer, layerFamily, layerPatch, parameter, midiChannel, grainSource;
    ComboBox routeList, routeSource, routeDestination;
    TextEditor search;
    TextButton previous { "<" }, next { ">" }, loadPreset { "Load Preset" }, savePreset { "Save Preset" };
    TextButton showFile { "Show File" }, deleteFile { "Delete Preset" }, locate { "Locate Audio" }, clearAudio { "Use Factory Source" };
    TextButton soundTab { "Sound" }, shapeTab { "Shape" }, modTab { "Modulation" }, advancedTab { "Advanced" };
    TextButton addLayer { "Add Layer" }, removeLayer { "Remove Layer" };
    TextButton addRoute { "Add Route" }, removeRoute { "Remove Route" };
    ToggleButton enabled { "Layer enabled" };
    std::array<Control, 5> edits;
    Control gain, pan, volume, polyphony;
    Viewport viewport;
    Component body;
    MidiKeyboardState keys;
    MidiKeyboardComponent keyboard { keys, MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<FileChooser> chooser;
    File presetFile;
    std::vector<const FactorySound*> matches, layerMatches;
    unsigned seen = 0;
    int selectedLayer = 0, selectedRoute = 0;
    Page page = sound;
    bool updating = false;
    void setup (Control&);
    void configure (Control&, String label, float lo, float hi, double interval, float value,
                    std::function<void(float)>, bool logarithmic = false);
    void edit (std::function<void(const var&, const var&)> change, bool rebuild = false);
    void updateFactories();
    void selectFactory (int);
    void setPage (Page);
    void refresh();
    void feedback (Result);
    void changeFamily (Family, bool add);
    void createRoute();
    void openChooser (bool save, bool audio);
    void timerCallback() override;
    void handleNoteOn (MidiKeyboardState*, int, int note, float velocity) override { plugin.preview (note, jlimit (1, 127, roundToInt (velocity * 127))); }
    void handleNoteOff (MidiKeyboardState*, int, int note, float) override { plugin.preview (note, 0); }
};

Editor::Editor (Plugin& p) : plugin (p)
{
    setLookAndFeel (&style);
    setName ("Studio Synth");
    setTitle ("Studio Synth instrument");
    limits.setSizeLimits (500, 370, 1600, 1000);
    limits.setMinimumOnscreenAmounts (0x10000, 32, 32, 32);
    for (auto* f : { &family, &layerFamily })
        for (int i = 0; i < (f == &family ? 7 : 6); ++i)
            f->addItem (familyName (static_cast<Family> (i)), i + 1);
    family.setTitle ("Synthesis family");
    layerFamily.setTitle ("Layer synthesis family");
    layerPatch.setTitle ("Layer factory sound");
    layerPatch.setTextWhenNothingSelected ("Choose a sound for this layer");
    routeList.setTitle ("Saved modulation routes");
    routeList.setTextWhenNothingSelected ("Choose a route");
    routeSource.setTitle ("Modulation source");
    routeSource.addItemList ({ "LFO 1", "LFO 2", "Envelope", "Mod Wheel", "Macro" }, 1);
    routeSource.setSelectedId (1, dontSendNotification);
    routeDestination.setTitle ("Modulation destination on the selected layer");
    routeDestination.setTextWhenNothingSelected ("Choose a destination");
    factory.setTitle ("Factory presets");
    layer.setTitle ("Layer to edit");
    parameter.setTitle ("Engine parameter");
    midiChannel.setTitle ("Receive channel");
    midiChannel.addItem ("All channels", 1);
    for (int i = 1; i <= 16; ++i) midiChannel.addItem ("Channel " + String (i), i + 1);
    grainSource.setTitle ("Granular source");
    grainSource.addItemList ({ "Harmonic Cloud", "Evolving Strings", "Bell Fragments", "Air / Noise" }, 1);
    search.setTitle ("Search factory presets");
    search.setTextToShowWhenEmpty ("Search presets or categories", colours::secondary);
    factory.setTextWhenNothingSelected ("Choose a factory preset");
    title.setText ("Studio Synth", dontSendNotification);
    title.setFont (FontOptions (21.0f, Font::bold));
    for (auto* label : { &preset, &status, &layerHeading, &midiHeading })
        label->setFont (FontOptions (13.0f));
    previous.setTitle ("Previous preset");
    next.setTitle ("Next preset");
    keyboard.setTitle ("Synth audition keyboard");
    keyboard.setOctaveForMiddleC (4);
    keyboard.setLowestVisibleKey (36);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffe0e7f0));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, colours::rail);
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, colours::rail);
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, colours::action.withAlpha (.7f));
    for (Component* c : std::initializer_list<Component*> { &title, &preset, &family, &factory, &search, &previous, &next,
         &loadPreset, &savePreset, &soundTab, &shapeTab, &modTab, &advancedTab, &viewport, &keyboard })
        addAndMakeVisible (*c);
    for (Component* c : std::initializer_list<Component*> { &status, &layerHeading, &layer, &enabled, &layerFamily, &addLayer, &removeLayer,
         &parameter, &midiHeading, &midiChannel, &showFile, &deleteFile, &locate, &clearAudio, &grainSource,
         &layerPatch, &routeList, &routeSource, &routeDestination, &addRoute, &removeRoute })
        body.addAndMakeVisible (*c);
    viewport.setViewedComponent (&body, false);
    viewport.setScrollBarsShown (true, false);
    for (auto& control : edits) setup (control);
    setup (gain);
    setup (pan);
    setup (volume);
    setup (polyphony);
    body.removeChildComponent (&volume.label);
    body.removeChildComponent (&volume.slider);
    addAndMakeVisible (volume.label);
    addAndMakeVisible (volume.slider);
    layerHeading.setText ("Layers", dontSendNotification);
    midiHeading.setText ("MIDI input", dontSendNotification);

    family.onChange = [this] { if (! updating) updateFactories(); };
    search.onTextChange = [this] { updateFactories(); };
    factory.onChange = [this] { if (! updating) selectFactory (factory.getSelectedItemIndex()); };
    previous.onClick = [this] { selectFactory (jmax (0, factory.getSelectedItemIndex() - 1)); };
    next.onClick = [this] { selectFactory (jmin (static_cast<int> (matches.size()) - 1, factory.getSelectedItemIndex() + 1)); };
    loadPreset.onClick = [this] { openChooser (false, false); };
    savePreset.onClick = [this] { openChooser (true, false); };
    locate.onClick = [this] { openChooser (false, true); };
    clearAudio.onClick = [this] { edit ([] (const var&, const var& selected)
    {
        selected["granularPatch"].getDynamicObject()->removeProperty ("importedAudioPath");
    }, true); };
    showFile.onClick = [this] { if (presetFile.existsAsFile()) presetFile.revealToUser(); };
    deleteFile.onClick = [this]
    {
        if (! presetFile.existsAsFile()) return;
        const auto file = presetFile;
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
            .withTitle ("Delete Synth Preset").withMessage ("Move " + file.getFileName() + " to the Trash?")
            .withButton ("Move to Trash", 1).withButton ("Cancel", 0),
            [safe = SafePointer<Editor> (this), file] (int button)
            {
                if (safe == nullptr || button != 1) return;
                if (file.moveToTrash()) { safe->presetFile = File(); safe->refresh(); }
                else safe->feedback (Result::fail ("Could not move the preset to the Trash."));
            });
    };
    soundTab.onClick = [this] { setPage (sound); };
    shapeTab.onClick = [this] { setPage (shape); };
    modTab.onClick = [this] { setPage (modulation); };
    advancedTab.onClick = [this] { setPage (advanced); };
    layer.onChange = [this] { if (! updating) { selectedLayer = layer.getSelectedItemIndex(); refresh(); } };
    layerFamily.onChange = [this] { if (! updating) changeFamily (static_cast<Family> (layerFamily.getSelectedItemIndex()), false); };
    enabled.onClick = [this] { edit ([this] (const var&, const var& l) { set (l, "enabled", enabled.getToggleState()); }, true); };
    addLayer.onClick = [this] { changeFamily (Family::analog, true); };
    removeLayer.onClick = [this]
    {
        edit ([this] (const var& root, const var& selected)
        {
            const auto id = selected["id"];
            root["layers"].getArray()->remove (selectedLayer);
            if (const auto routes = root["modulation"]["routes"]; routes.isArray())
                for (int i = routes.size(); --i >= 0;) if (routes[i]["layerID"] == id) routes.getArray()->remove (i);
            if (const auto mappings = root["midiControls"]["mappings"]; mappings.isArray())
                for (int i = mappings.size(); --i >= 0;) if (mappings[i]["target"]["parameter"]["layerID"] == id) mappings.getArray()->remove (i);
        }, true);
    };
    layerPatch.onChange = [this]
    {
        const auto index = layerPatch.getSelectedItemIndex();
        if (updating || ! isPositiveAndBelow (index, static_cast<int> (layerMatches.size()))) return;
        const auto* sound = layerMatches[static_cast<std::size_t> (index)];
        edit ([sound] (const var&, const var& selected)
        {
            set (selected, patchKey (sound->family), sound->patch["layers"][0][patchKey (sound->family)].clone());
            set (selected, "name", sound->name);
        }, true);
    };
    routeList.onChange = [this]
    {
        if (! updating) { selectedRoute = routeList.getSelectedItemIndex(); refresh(); }
    };
    routeDestination.onChange = [this] { addRoute.setEnabled (routeDestination.getSelectedId() != 0); };
    addRoute.onClick = [this] { createRoute(); };
    removeRoute.onClick = [this]
    {
        edit ([this] (const var& root, const var&)
        {
            auto routes = root["modulation"]["routes"];
            if (routes.isArray() && isPositiveAndBelow (selectedRoute, routes.size()))
                routes.getArray()->remove (selectedRoute);
        });
    };
    parameter.onChange = [this] { if (! updating) refresh(); };
    midiChannel.onChange = [this] { if (! updating) edit ([this] (const var& root, const var&) { set (root, "receive", midiChannel.getSelectedId() - 1); }, true); };
    grainSource.onChange = [this] { if (! updating) edit ([this] (const var&, const var& l) { set (l["granularPatch"], "source", grainSource.getSelectedId() - 1); }, true); };
    keys.addListener (this);
    refresh();
    setSize (800, 540);
    startTimerHz (16);
}
Editor::~Editor()
{
    stopTimer();
    keys.allNotesOff (0);
    keys.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
    setLookAndFeel (nullptr);
}
void Editor::setup (Control& c)
{
    body.addAndMakeVisible (c.label);
    body.addAndMakeVisible (c.slider);
    c.slider.setSliderStyle (Slider::LinearHorizontal);
    c.slider.setTextBoxStyle (Slider::TextBoxRight, false, appearance.span (64), appearance.control (23, 11));
    c.slider.setWantsKeyboardFocus (true);
}
void Editor::configure (Control& c, String label, float lo, float hi, double interval, float value,
                        std::function<void(float)> change, bool log)
{
    c.label.setText (label, dontSendNotification);
    c.slider.setTitle (label);
    c.slider.textFromValueFunction = {};
    c.slider.valueFromTextFunction = {};
    c.slider.setRange (lo, hi, interval);
    if (log && lo > 0) c.slider.setSkewFactorFromMidPoint (std::sqrt (lo * hi));
    else c.slider.setSkewFactor (1);
    if (! c.slider.isMouseButtonDown()) c.slider.setValue (value, dontSendNotification);
    c.slider.onValueChange = [this, &c, change = std::move (change)] { if (! updating) change (static_cast<float> (c.slider.getValue())); };
    c.label.setVisible (true);
    c.slider.setVisible (true);
}
void Editor::feedback (Result result)
{
    if (result.failed()) status.setText (result.getErrorMessage(), dontSendNotification);
    else refresh();
}
void Editor::edit (std::function<void(const var&, const var&)> change, bool rebuild)
{
    auto root = plugin.getPatch().clone();
    auto layers = root["layers"];
    if (! juce::isPositiveAndBelow (selectedLayer, layers.size())) return;
    change (root, layers[selectedLayer]);
    feedback (plugin.editPatch (root, rebuild));
}
void Editor::updateFactories()
{
    const ScopedValueSetter<bool> scope (updating, true);
    matches.clear();
    factory.clear (dontSendNotification);
    const auto filter = search.getText().trim();
    int active = 0;
    for (const auto& f : catalog().sounds)
        if (family.getSelectedId() == static_cast<int> (f.family) + 1
            && (filter.isEmpty() || (f.name + " " + f.category).containsIgnoreCase (filter)))
        {
            matches.push_back (&f);
            const auto id = static_cast<int> (matches.size());
            factory.addItem (f.category + "  |  " + f.name, id);
            if (f.id == plugin.factoryID()) active = id;
        }
    factory.setSelectedId (active, dontSendNotification);
    previous.setEnabled (! matches.empty());
    next.setEnabled (! matches.empty());
}
void Editor::selectFactory (int index)
{
    if (! isPositiveAndBelow (index, static_cast<int> (matches.size()))) return;
    const auto* choice = matches[static_cast<std::size_t> (index)];
    selectedLayer = 0;
    presetFile = File();
    feedback (plugin.selectFactory (*choice));
}
void Editor::setPage (Page value)
{
    page = value;
    viewport.setViewPosition (0, 0);
    refresh();
}
void Editor::changeFamily (Family newFamily, bool add)
{
    if (newFamily == Family::hybrid) return;
    const auto& sounds = catalog().sounds;
    const auto factoryLayer = std::find_if (sounds.begin(), sounds.end(), [newFamily] (const auto& f) { return f.family == newFamily; });
    if (factoryLayer == sounds.end()) return;
    auto replacement = factoryLayer->patch["layers"][0].clone();
    set (replacement, "id", Uuid().toDashedString());
    set (replacement, "name", factoryLayer->name);
    edit ([this, replacement, add] (const var& root, const var& selected)
    {
        auto layers = root["layers"];
        if (add)
        {
            if (layers.size() >= maxLayers) return;
            selectedLayer = layers.size();
            layers.getArray()->add (replacement);
            return;
        }
        set (replacement, "gain", selected["gain"]);
        set (replacement, "pan", selected["pan"]);
        set (replacement, "enabled", selected["enabled"]);
        const auto old = selected["id"];
        if (const auto routes = root["modulation"]["routes"]; routes.isArray())
            for (int i = routes.size(); --i >= 0;) if (routes[i]["layerID"] == old) routes.getArray()->remove (i);
        if (const auto mappings = root["midiControls"]["mappings"]; mappings.isArray())
            for (int i = mappings.size(); --i >= 0;) if (mappings[i]["target"]["parameter"]["layerID"] == old) mappings.getArray()->remove (i);
        layers.getArray()->set (selectedLayer, replacement);
    }, true);
}
void Editor::createRoute()
{
    constexpr std::array keys { "lfo1", "lfo2", "envelope", "modWheel", "macro1" };
    const auto source = routeSource.getSelectedItemIndex();
    const auto destination = routeDestination.getSelectedId() - 1;
    if (! isPositiveAndBelow (source, static_cast<int> (keys.size())) || destination < 0) return;
    edit ([this, destination, name = String (keys[static_cast<std::size_t> (source)])] (const var& root, const var& l)
    {
        auto mod = root["modulation"];
        if (! mod.isObject())
        {
            mod = object();
            set (mod, "version", 1);
            set (mod, "routes", Array<var>());
            set (root, "modulation", mod);
        }
        auto routes = mod["routes"];
        if (routes.size() >= maxRoutes) return;
        auto value = object();
        set (value, "_0", destination);
        auto parameter = object();
        set (parameter, l["engine"].toString(), value);
        auto route = object();
        set (route, "destination", parameter);
        set (route, "source", name);
        set (route, "layerID", l["id"]);
        set (route, "id", Uuid().toDashedString());
        set (route, "enabled", true);
        set (route, "depth", .15);
        selectedRoute = routes.size();
        routes.getArray()->add (route);
    });
}
void Editor::refresh()
{
    const ScopedValueSetter<bool> scope (updating, true);
    seen = plugin.changeCount();
    auto patch = plugin.getPatch();
    auto layers = patch["layers"];
    selectedLayer = jlimit (0, jmax (0, layers.size() - 1), selectedLayer);
    auto current = layers[selectedLayer];
    const auto engine = parseFamily (current["engine"].toString());
    const bool valid = engine != Family::hybrid;
    if (family.getSelectedId() == 0)
        family.setSelectedId (layers.size() > 1 ? 7 : valid ? static_cast<int> (engine) + 1 : 1, dontSendNotification);
    updateFactories();
    preset.setText (plugin.presetName() + (plugin.dirty() ? "  |  Edited" : ""), dontSendNotification);
    status.setText (plugin.warning(), dontSendNotification);
    const bool layerControls = layers.size() > 1 || page == advanced;
    for (Component* c : std::initializer_list<Component*> { &layerHeading, &layer, &layerFamily, &enabled, &addLayer, &removeLayer })
        c->setVisible (layerControls);
    layer.clear (dontSendNotification);
    for (int i = 0; i < layers.size(); ++i)
        layer.addItem (String (i + 1) + "  |  " + familyName (parseFamily (layers[i]["engine"].toString()))
            + "  |  " + layers[i]["name"].toString() + (static_cast<bool> (layers[i]["enabled"]) ? "" : " (Off)"), i + 1);
    layer.setSelectedId (selectedLayer + 1, dontSendNotification);
    layerFamily.setSelectedId (valid ? static_cast<int> (engine) + 1 : 0, dontSendNotification);
    layerPatch.setVisible (layerControls);
    layerPatch.clear (dontSendNotification);
    layerMatches.clear();
    for (const auto& sound : catalog().sounds)
        if (sound.family == engine && engine != Family::hybrid)
        {
            layerMatches.push_back (&sound);
            layerPatch.addItem (sound.name, static_cast<int> (layerMatches.size()));
            if (sound.name == current["name"].toString()) layerPatch.setSelectedId (static_cast<int> (layerMatches.size()), dontSendNotification);
        }
    enabled.setToggleState (static_cast<bool> (current["enabled"]), dontSendNotification);
    enabled.setEnabled (valid);
    layerFamily.setEnabled (valid);
    addLayer.setEnabled (valid && layers.size() < maxLayers);
    removeLayer.setEnabled (layers.size() > 1);
    const auto layerControl = [this] (const char* key)
    {
        return [this, key] (float v) { edit ([=] (const var&, const var& l) { set (l, key, v); }); };
    };
    configure (gain, "Layer Gain", 0, 1, .01, current["gain"], layerControl ("gain"));
    configure (pan, "Layer Pan", -1, 1, .01, current["pan"], layerControl ("pan"));
    configure (volume, "Volume", 0, 1, .01, patch.hasProperty ("output") ? static_cast<float> (patch["output"]) : 1,
        [this] (float v) { edit ([v] (const var& root, const var&) { set (root, "output", v); }); });
    gain.label.setVisible (layerControls);
    gain.slider.setVisible (layerControls);
    pan.label.setVisible (layerControls);
    pan.slider.setVisible (layerControls);
    midiChannel.setSelectedId (1 + static_cast<int> (patch["receive"]), dontSendNotification);
    configure (polyphony, "Layer Voices", 1, voicesPerLayer, 1,
        current["voices"].isVoid() ? voicesPerLayer : static_cast<int> (current["voices"]), [this] (float v)
    {
        edit ([v] (const var&, const var& l) { set (l, "voices", roundToInt (v)); }, true);
    });
    polyphony.label.setVisible (page == advanced && valid);
    polyphony.slider.setVisible (page == advanced && valid);
    midiChannel.setVisible (page == advanced);
    midiHeading.setVisible (page == advanced);
    showFile.setVisible (page == advanced && presetFile.existsAsFile());
    deleteFile.setVisible (showFile.isVisible());
    soundTab.setToggleState (page == sound, dontSendNotification);
    shapeTab.setToggleState (page == shape, dontSendNotification);
    modTab.setToggleState (page == modulation, dontSendNotification);
    advancedTab.setToggleState (page == advanced, dontSendNotification);
    for (auto& c : edits) { c.label.setVisible (false); c.slider.setVisible (false); }
    parameter.setVisible (page == advanced && valid);
    const auto native = current[patchKey (engine)];
    const bool granularTools = engine == Family::granular && page == sound;
    locate.setButtonText (native["importedAudioPath"].toString().isEmpty() ? "Load Audio" : "Locate Audio");
    locate.setVisible (granularTools);
    grainSource.setVisible (granularTools);
    grainSource.setSelectedId (1 + static_cast<int> (native["source"]), dontSendNotification);
    clearAudio.setVisible (granularTools && native["importedAudioPath"].toString().isNotEmpty());

    if (valid && page != modulation)
    {
        const auto& metadata = catalog().parameters[static_cast<int> (engine)];
        const auto currentParameter = parameter.getSelectedItemIndex();
        parameter.clear (dontSendNotification);
        for (std::size_t i = 0; i < metadata.size(); ++i)
            parameter.addItem (metadata[i].name, static_cast<int> (i) + 1);
        parameter.setSelectedItemIndex (jlimit (0, static_cast<int> (metadata.size()) - 1, currentParameter), dontSendNotification);
        constexpr int selections[6][2][5] {
            { { 0, 1, 5, 6, 7 }, { 13, 14, 15, 16, 8 } },
            { { 0, 1, 10, 16, 22 }, { 6, 7, 8, 9, 28 } },
            { { 0, 1, 4, 13, 14 }, { 5, 6, 7, 8, 12 } },
            { { 5, 6, 7, 9, 10 }, { 0, 1, 2, 3, 29 } },
            { { 0, 1, 2, 3, 4 }, { 8, 9, 5, 6, 7 } },
            { { 0, 1, 2, 3, 4 }, { 5, 10, 11, 13, 14 } }
        };
        for (int k = 0; k < (page == advanced ? 1 : 5); ++k)
        {
            const auto id = page == advanced ? parameter.getSelectedItemIndex() : selections[static_cast<int> (engine)][page == shape][k];
            const auto& p = metadata[static_cast<std::size_t> (id)];
            const auto value = native["values"][Identifier (String (id))];
            const bool discrete = (engine == Family::analog && (id == 0 || id == 1 || id == 23))
                || (engine == Family::fm && (id == 0 || id == 29)) || (engine == Family::physical && id == 0)
                || (engine == Family::additive && id == 28);
            auto label = p.name;
            StringArray options;
            if (engine == Family::analog && (id == 0 || id == 1))
            {
                label = "Oscillator " + String (id + 1) + " Wave";
                options = { "Sine", "Triangle", "Saw", "Pulse" };
            }
            if (engine == Family::fm && id == 0)
                options = { "Chain", "Two Stacks", "Three Modulators", "Parallel" };
            if (engine == Family::physical && id == 0)
                options = { "Plucked", "Struck", "Bowed", "Reed", "Brass", "Mallet", "Bell", "Membrane" };
            configure (edits[k], label, p.minimum, p.maximum, discrete ? 1 : .001, value.isVoid() ? p.initial : static_cast<float> (value),
                [this, engine, id] (float v) { edit ([=] (const var&, const var& l) { set (l[patchKey (engine)]["values"], String (id), v); }); }, p.logarithmic);
            if (! options.isEmpty())
            {
                edits[k].slider.textFromValueFunction = [options] (double v) { return options[roundToInt (v)]; };
                edits[k].slider.valueFromTextFunction = [options] (const String& text)
                {
                    const auto item = options.indexOf (text.trim(), true);
                    return item >= 0 ? static_cast<double> (item) : text.getDoubleValue();
                };
                edits[k].slider.updateText();
            }
        }
    }
    for (Component* c : std::initializer_list<Component*> { &routeList, &routeSource, &routeDestination, &addRoute, &removeRoute })
        c->setVisible (page == modulation);
    if (page == modulation)
    {
        const auto mod = patch["modulation"];
        const auto setting = [this] (const char* key)
        {
            return [this, key] (float v)
            {
                edit ([=] (const var& root, const var&)
                {
                    auto m = root["modulation"];
                    if (! m.isObject())
                    {
                        m = object();
                        set (m, "version", 1);
                        set (m, "routes", Array<var>());
                        set (root, "modulation", m);
                    }
                    set (m, key, v);
                    if (String (key) == "lfo1Rate") m.getDynamicObject()->removeProperty ("lfo1Sync");
                    if (String (key) == "lfo2Rate") m.getDynamicObject()->removeProperty ("lfo2Sync");
                });
            };
        };
        const auto currentValue = [&] (const char* key, float fallback)
        {
            return mod[key].isVoid() ? fallback : static_cast<float> (mod[key]);
        };
        configure (edits[0], "Macro", 0, 1, .01, currentValue ("macro1", 0), setting ("macro1"));
        configure (edits[1], "LFO 1 Rate (Hz)", .03f, 15, .01, currentValue ("lfo1Rate", .45f), setting ("lfo1Rate"));
        configure (edits[2], "LFO 2 Rate (Hz)", .03f, 15, .01, currentValue ("lfo2Rate", .18f), setting ("lfo2Rate"));
        configure (edits[3], "Mod Envelope Attack", .01f, 20, .01, currentValue ("envelopeAttack", .08f), setting ("envelopeAttack"));
        const auto routes = mod["routes"];
        selectedRoute = jlimit (0, jmax (0, routes.size() - 1), selectedRoute);
        routeList.clear (dontSendNotification);
        for (int i = 0; i < routes.size(); ++i)
        {
            const auto route = routes[i];
            const auto description = route["source"].toString() + "  >  ";
            for (int j = 0; j < layers.size(); ++j)
                if (layers[j]["id"] == route["layerID"])
                {
                    const auto f = parseFamily (layers[j]["engine"].toString());
                    const auto id = static_cast<int> (route["destination"][Identifier (familyKey (f))]["_0"]);
                    const auto& params = catalog().parameters[static_cast<int> (f)];
                    if (isPositiveAndBelow (id, static_cast<int> (params.size())))
                        routeList.addItem (description + "Layer " + String (j + 1) + " " + params[id].name, i + 1);
                    break;
                }
        }
        if (routes.size() > 0)
        {
            routeList.setSelectedId (selectedRoute + 1, dontSendNotification);
            configure (edits[4], "Route Depth", -1, 1, .01, routes[selectedRoute]["depth"], [this] (float v)
            {
                edit ([this, v] (const var& root, const var&) { set (root["modulation"]["routes"][selectedRoute], "depth", v); });
            });
        }
        removeRoute.setEnabled (routes.size() > 0);
        const auto target = routeDestination.getSelectedId();
        routeDestination.clear (dontSendNotification);
        if (valid)
        {
            const auto& params = catalog().parameters[static_cast<int> (engine)];
            for (std::size_t i = 0; i < params.size(); ++i)
            {
                const auto id = static_cast<int> (i);
                if ((engine == Family::analog && (id == 0 || id == 1 || id == 23))
                    || (engine == Family::fm && (id == 0 || id == 29)) || (engine == Family::physical && id == 0)
                    || (engine == Family::additive && id == 28)) continue;
                routeDestination.addItem (params[i].name, id + 1);
            }
        }
        routeDestination.setSelectedId (target, dontSendNotification);
        addRoute.setEnabled (routeDestination.getSelectedId() != 0 && routes.size() < maxRoutes);
    }
    resized();
}
void Editor::resized()
{
    const int gap = appearance.px (8);
    const int line = appearance.control (30, 13);
    const auto row = [=] (Rectangle<int>& area) { return area.removeFromTop (line + gap).withTrimmedBottom (gap); };
    const auto columns = [=] (Rectangle<int> r, std::initializer_list<Component*> items)
    {
        const auto w = (r.getWidth() - gap * (static_cast<int> (items.size()) - 1)) / static_cast<int> (items.size());
        for (auto* c : items) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
    };
    auto area = getLocalBounds().reduced (appearance.px (14), gap);
    auto header = row (area);
    title.setBounds (header.removeFromLeft (header.getWidth() / 3));
    preset.setBounds (header);
    auto pick = row (area);
    family.setBounds (pick.removeFromLeft (pick.getWidth() / 4).withTrimmedRight (gap));
    previous.setBounds (pick.removeFromRight (line));
    next.setBounds (previous.getBounds());
    previous.setBounds (pick.removeFromRight (line).withTrimmedRight (3));
    factory.setBounds (pick.withTrimmedRight (3));
    columns (row (area), { &search, &loadPreset, &savePreset });
    columns (row (area), { &soundTab, &shapeTab, &modTab, &advancedTab });
    keyboard.setBounds (area.removeFromBottom (appearance.px (53)));
    auto bottom = area.removeFromBottom (line + gap);
    volume.label.setBounds (bottom.removeFromLeft (appearance.span (90)));
    volume.slider.setBounds (bottom);
    viewport.setBounds (area);
    auto content = Rectangle<int> (0, 0, jmax (1, viewport.getWidth() - appearance.scrollbar()), 1500);
    const auto left = [&] (Control& c, Rectangle<int> r)
    {
        c.label.setBounds (r.removeFromLeft (r.getWidth() * 2 / 5));
        c.slider.setBounds (r);
    };
    if (layer.isVisible())
    {
        layerHeading.setBounds (row (content));
        columns (row (content), { &layer, &addLayer, &removeLayer });
        columns (row (content), { &layerFamily, &layerPatch, &enabled });
        auto mix = row (content);
        left (gain, mix.removeFromLeft (mix.getWidth() / 2).withTrimmedRight (gap));
        left (pan, mix);
    }
    if (grainSource.isVisible())
    {
        auto sources = row (content);
        if (clearAudio.isVisible()) columns (sources, { &grainSource, &locate, &clearAudio });
        else columns (sources, { &grainSource, &locate });
    }
    if (page == advanced)
    {
        columns (row (content), { &midiHeading, &midiChannel });
        if (polyphony.slider.isVisible()) left (polyphony, row (content));
        parameter.setBounds (row (content));
        if (showFile.isVisible()) columns (row (content), { &showFile, &deleteFile });
    }
    if (page == modulation)
    {
        columns (row (content), { &routeSource, &routeDestination, &addRoute });
        columns (row (content), { &routeList, &removeRoute });
    }
    for (auto& control : edits)
        if (control.slider.isVisible()) left (control, row (content));
    status.setBounds (row (content));
    body.setSize (jmax (1, viewport.getWidth() - appearance.scrollbar()), jmax (viewport.getHeight(), content.getY()));
}
void Editor::openChooser (bool save, bool audio)
{
    if (chooser) return;
    const auto stem = File::createLegalFileName (plugin.presetName());
    const auto path = save ? File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (stem + ".studiosynth") : presetFile;
    chooser = std::make_unique<FileChooser> (audio ? "Choose Granular audio" : save ? "Save Synth preset as" : "Load or import Synth preset",
        path, audio ? "*.wav;*.aif;*.aiff;*.flac" : "*.studiosynth", true, false, this);
    chooser->launchAsync (FileBrowserComponent::canSelectFiles | (save ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
                                                                     : FileBrowserComponent::openMode),
        [safe = SafePointer<Editor> (this), save, audio, editing = selectedLayer, revision = plugin.changeCount()] (const FileChooser& dialog)
        {
            const auto file = dialog.getResult();
            MessageManager::callAsync ([safe, file, save, audio, editing, revision]
            {
                if (safe == nullptr) return;
                safe->chooser.reset();
                if (file == File()) return;
                if (audio)
                {
                    if (safe->selectedLayer == editing && safe->plugin.changeCount() == revision)
                        safe->edit ([file] (const var&, const var& l) { set (l["granularPatch"], "importedAudioPath", file.getFullPathName()); }, true);
                    return;
                }
                const auto destination = save ? file.withFileExtension (".studiosynth") : file;
                if (save && destination != file && destination.existsAsFile())
                {
                    safe->feedback (Result::fail ("Select the existing .studiosynth file to replace it."));
                    return;
                }
                const auto result = save ? safe->plugin.savePreset (destination) : safe->plugin.loadPreset (destination);
                if (result.wasOk()) safe->presetFile = destination;
                safe->feedback (result);
            });
        });
}
void Editor::timerCallback()
{
    if (seen != plugin.changeCount()) refresh();
}
}
std::unique_ptr<tracktion::Plugin::EditorComponent> Plugin::createEditor() { return std::make_unique<Editor> (*this); }
}
