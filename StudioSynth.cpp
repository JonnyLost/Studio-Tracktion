// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioSynth.h"
#include <StudioSynthData.h>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace studio::synth
{
using namespace juce;
namespace te = tracktion;
namespace
{
constexpr std::array familyKeys { "analog", "fm", "wavetable", "additive", "granular", "physical", "hybrid" };
constexpr std::array familyNames { "Analog", "FM", "Wavetable", "Additive", "Granular", "Physical Modeling", "Hybrid" };
constexpr auto presetFormat = "garritan.studio.synth.preset";
const Identifier stateKey ("studioSynthState");
constexpr int maximumDocumentBytes = 1024 * 1024;

var object() { return var (new DynamicObject()); }
void set (const var& target, const Identifier& key, var value) { target.getDynamicObject()->setProperty (key, std::move (value)); }
var newPreset (String name, var patch, String factory = {})
{
    auto preset = object();
    set (preset, "id", Uuid().toDashedString());
    set (preset, "name", name);
    set (preset, "createdAt", static_cast<double> (Time::currentTimeMillis()) / 1000 - 978307200.0);
    set (preset, "patch", std::move (patch));
    auto root = object();
    set (root, "format", presetFormat);
    set (root, "version", 1);
    set (root, "preset", preset);
    set (root, "factoryID", factory);
    return root;
}
bool numeric (const var& v, double low, double high)
{
    return (v.isInt() || v.isInt64() || v.isDouble()) && static_cast<double> (v) >= low && static_cast<double> (v) <= high;
}
float number (const var& o, const char* key, float fallback, float low, float high)
{
    const auto v = o[key];
    return numeric (v, low, high) ? static_cast<float> (v) : fallback;
}
bool integer (const var& v, int low, int high)
{
    return numeric (v, low, high) && static_cast<double> (v) == static_cast<int> (v);
}
void fillLegacyLayerDefaults (const var& document)
{
    const auto layers = document["preset"]["patch"]["layers"];
    if (const auto* list = layers.getArray())
        for (const auto& layer : *list)
            if (layer.isObject())
            {
                const auto family = parseFamily (layer["engine"].toString());
                const auto key = patchKey (family);
                if (family != Family::hybrid && layer[key].isVoid())
                {
                    auto patch = object();
                    set (patch, "version", 1);
                    set (patch, "values", object());
                    if (family == Family::granular) set (patch, "source", 0);
                    set (layer, key, patch);
                }
            }
}
bool validEnvelope (const var& root)
{
    const auto name = root["preset"]["name"].toString();
    return root["format"].toString() == presetFormat && integer (root["version"], 1, 1)
           && root["preset"]["patch"].isObject() && name.isNotEmpty() && name.length() <= 80;
}
std::shared_ptr<SSSharedPCM> readPCM (const File& file)
{
    if (! file.existsAsFile()) return {};
    struct Cached
    {
        File file;
        Time timestamp;
        int64 size;
        std::weak_ptr<SSSharedPCM> data;
    };
    static std::mutex lock;
    static std::vector<Cached> cache;
    const std::lock_guard guard (lock);
    std::erase_if (cache, [] (const auto& item) { return item.data.expired(); });
    const auto timestamp = file.getLastModificationTime();
    const auto size = file.getSize();
    for (auto& item : cache)
        if (item.file == file && item.timestamp == timestamp && item.size == size)
            if (auto data = item.data.lock()) return data;

    AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 128
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 1000 || reader->sampleRate > 384000)
        return {};
    constexpr int maxFrames = 524288;
    const int length = static_cast<int> (std::min<int64> (reader->lengthInSamples, maxFrames));
    AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), length);
    if (! reader->read (&buffer, 0, length, 0, true, reader->numChannels > 1)) return {};
    auto result = std::shared_ptr<SSSharedPCM> (sspcm_create_copy (buffer.getReadPointer (0),
        reader->numChannels > 1 ? buffer.getReadPointer (1) : nullptr, static_cast<uint32_t> (length),
        reader->sampleRate), sspcm_release);
    if (result) cache.push_back ({ file, timestamp, size, result });
    return result;
}
}

String familyKey (Family f) { return familyKeys[static_cast<int> (f)]; }
String familyName (Family f) { return familyNames[static_cast<int> (f)]; }
Identifier patchKey (Family f) { return Identifier (familyKey (f) + "Patch"); }
Family parseFamily (const String& name)
{
    for (std::size_t i = 0; i < familyKeys.size(); ++i)
        if (name == familyKeys[i]) return static_cast<Family> (i);
    return Family::hybrid; // Hybrid is also the invalid sentinel for a single layer.
}
const Catalog& catalog()
{
    static const auto result = []
    {
        Catalog c;
        const auto json = JSON::parse (String::fromUTF8 (StudioSynthData::StudioSynthFactory_json, StudioSynthData::StudioSynthFactory_jsonSize));
        for (int i = 0; i < 6; ++i)
            if (const auto entries = json["parameters"][Identifier (familyKey (static_cast<Family> (i)))]; entries.isArray())
                for (const auto& parameter : *entries.getArray())
                    c.parameters[i].push_back ({ parameter["name"].toString(), static_cast<float> (parameter["minimum"]),
                        static_cast<float> (parameter["maximum"]), static_cast<float> (parameter["initial"]),
                        static_cast<bool> (parameter["logarithmic"]) });
        const auto sounds = json["sounds"];
        if (sounds.isArray())
            for (const auto& entry : *sounds.getArray())
                c.sounds.push_back ({ entry["id"].toString(), entry["name"].toString(), entry["category"].toString(),
                    entry["summary"].toString(), parseFamily (entry["family"].toString()), entry["patch"] });
        return c;
    }();
    return result;
}

Result makePlan (const var& raw, const File& base, Patch& out, String& warning)
{
    const auto sourceLayers = raw["layers"];
    if (! integer (raw["version"], 1, 1) || ! sourceLayers.isArray()
        || sourceLayers.size() < 1 || sourceLayers.size() > maxLayers)
        return Result::fail ("Unsupported Synth patch version or layer count.");
    out = {};
    warning.clear();
    out.count = sourceLayers.size();
    out.receive = static_cast<int> (number (raw, "receive", 0, 0, 16));
    out.gain = number (raw, "output", 1, 0, 1);
    StringArray ids;
    for (int i = 0; i < out.count; ++i)
    {
        const auto layer = sourceLayers[i];
        const auto id = layer["id"].toString();
        auto& target = out.layers[i];
        target.family = parseFamily (layer["engine"].toString());
        if (id.isEmpty() || ids.contains (id) || target.family == Family::hybrid || ! layer["enabled"].isBool()
            || ! numeric (layer["gain"], 0, 1) || ! numeric (layer["pan"], -1, 1))
            return Result::fail ("A Synth layer has invalid settings.");
        ids.add (id);
        const auto voiceCount = layer["voices"];
        if (! voiceCount.isVoid() && ! integer (voiceCount, 1, voicesPerLayer))
            return Result::fail ("Synth polyphony is outside the supported range.");
        if (! voiceCount.isVoid()) target.voices = static_cast<int> (voiceCount);
        target.enabled = static_cast<bool> (layer["enabled"]);
        target.gain = layer["gain"];
        target.pan = layer["pan"];
        auto rawPatch = layer[patchKey (target.family)];
        if (rawPatch.isVoid())
        {
            rawPatch = object();
            set (rawPatch, "version", 1);
            set (rawPatch, "values", object());
            if (target.family == Family::granular) set (rawPatch, "source", 0);
        }
        const auto version = rawPatch["version"];
        const auto values = rawPatch["values"];
        if (! integer (version, 1, target.family == Family::analog ? 2 : 1) || ! values.isObject())
            return Result::fail ("Unsupported engine parameter format.");
        const auto& meta = catalog().parameters[static_cast<int> (target.family)];
        if (static_cast<int> (meta.size()) != parameterCounts[static_cast<int> (target.family)])
            return Result::fail ("The factory parameter catalog could not be read.");
        for (std::size_t j = 0; j < meta.size(); ++j) target.values[j] = meta[j].initial;
        for (const auto& property : values.getDynamicObject()->getProperties())
        {
            const auto key = property.name.toString();
            const int j = key.getIntValue();
            if (key != String (j) || ! isPositiveAndBelow (j, static_cast<int> (meta.size()))
                || ! numeric (property.value, meta[static_cast<std::size_t> (j)].minimum - 0.00001,
                                              meta[static_cast<std::size_t> (j)].maximum + 0.00001))
                return Result::fail ("A Synth parameter is out of range or unknown.");
            target.values[j] = jlimit (meta[j].minimum, meta[j].maximum, static_cast<float> (property.value));
        }

        const auto wires = rawPatch["connections"];
        if (target.family == Family::analog && ! wires.isVoid())
        {
            if (! wires.isArray() || wires.size() > static_cast<int> (target.wires.size()))
                return Result::fail ("Invalid Analog routing.");
            target.wireCount = wires.size();
            for (int j = 0; j < wires.size(); ++j)
            {
                const auto w = wires[j];
                if (! integer (w["source"], 0, 9) || ! integer (w["destination"], 0, 9)
                    || ! integer (w["input"], 0, 2) || ! numeric (w["gain"], -4, 4))
                    return Result::fail ("Invalid Analog connection.");
                target.wires[j] = { static_cast<int> (w["source"]), static_cast<int> (w["destination"]),
                    static_cast<int> (w["input"]), static_cast<float> (w["gain"]) };
            }
        }
        if (target.family == Family::granular)
        {
            if (! integer (rawPatch["source"], 0, 3)) return Result::fail ("Invalid Granular source.");
            target.source = rawPatch["source"];
            const auto path = rawPatch["importedAudioPath"].toString();
            if (target.enabled && path.isNotEmpty())
            {
                const auto file = File::isAbsolutePath (path) ? File (path) : base != File() ? base.getChildFile (path) : File();
                target.pcm = readPCM (file);
                if (! target.pcm)
                {
                    warning << (warning.isEmpty() ? "" : "\n") << "Unavailable Granular audio: " << path;
                    target.enabled = false;
                }
            }
        }
    }

    const auto destination = [&] (int layer, const var& rawDestination)
    {
        Destination result;
        if (! isPositiveAndBelow (layer, out.count)) return result;
        const auto engine = out.layers[layer].family;
        const auto value = rawDestination[Identifier (familyKey (engine))]["_0"];
        const auto& parameters = catalog().parameters[static_cast<int> (engine)];
        if (! integer (value, 0, static_cast<int> (parameters.size()) - 1)) return result;
        const auto id = static_cast<int> (value);
        const auto& p = parameters[id];
        return Destination { layer, id, p.minimum, p.maximum, p.logarithmic };
    };
    if (const auto modulation = raw["modulation"]; ! modulation.isVoid())
    {
        const auto routes = modulation["routes"];
        if (! integer (modulation["version"], 1, 1) || ! routes.isArray() || routes.size() > maxRoutes)
            return Result::fail ("Unsupported modulation settings.");
        constexpr auto names = std::array { "lfo1", "lfo2", "envelope", "modWheel", "macro1" };
        out.attack = number (modulation, "envelopeAttack", .08f, .01f, 20);
        out.decay = number (modulation, "envelopeDecay", .3f, .01f, 20);
        out.sustain = number (modulation, "envelopeSustain", .7f, 0, 1);
        out.release = number (modulation, "envelopeRelease", .5f, .01f, 20);
        out.macro = number (modulation, "macro1", 0, 0, 1);
        out.lfoRate = { number (modulation, "lfo1Rate", .45f, .03f, 15), number (modulation, "lfo2Rate", .18f, .03f, 15) };
        out.lfoPhase = { number (modulation, "lfo1Phase", 0, 0, 1), number (modulation, "lfo2Phase", 0, 0, 1) };
        const StringArray notes { "whole", "half", "quarter", "eighth", "sixteenth", "thirtySecond", "sixtyFourth" };
        for (int i = 0; i < 2; ++i)
            if (const auto sync = modulation[Identifier ("lfo" + String (i + 1) + "Sync")]; sync.isObject())
            {
                const auto note = notes.indexOf (sync["note"].toString());
                const auto timing = sync["timing"].toString();
                if (note < 0 || (timing != "straight" && timing != "triplet" && timing != "dotted"))
                    return Result::fail ("Invalid synchronized LFO division.");
                out.lfoBeats[i] = std::ldexp (4.0f, -note) * (timing == "triplet" ? 2.0f / 3 : timing == "dotted" ? 1.5f : 1);
            }
        for (const auto& route : *routes.getArray())
        {
            const auto kind = route["source"].toString();
            const auto found = std::find_if (names.begin(), names.end(), [&] (auto name) { return kind == name; });
            const auto target = destination (ids.indexOf (route["layerID"].toString()), route["destination"]);
            if (found == names.end() || target.layer < 0 || ! numeric (route["depth"], -1, 1))
                return Result::fail ("Invalid modulation route.");
            if (static_cast<bool> (route["enabled"]))
                out.routes[out.routeCount++] = { static_cast<Source> (found - names.begin()), target, route["depth"] };
        }
    }
    if (const auto controls = raw["midiControls"]; ! controls.isVoid())
    {
        const auto mappings = controls["mappings"];
        if (! integer (controls["version"], 1, 1) || ! mappings.isArray() || mappings.size() > maxMappings)
            return Result::fail ("Unsupported MIDI Learn settings.");
        for (const auto& mapping : *mappings.getArray())
        {
            const auto rawTarget = mapping["target"];
            const auto parameter = rawTarget["parameter"];
            const auto target = destination (ids.indexOf (parameter["layerID"].toString()), parameter["destination"]);
            const auto channel = mapping["channel"];
            if ((target.layer < 0 && ! rawTarget["macro1"].isObject())
                || ! integer (mapping["controller"], 0, 119) || (! channel.isVoid() && ! integer (channel, 0, 15))
                || ! numeric (mapping["minimum"], 0, 1) || ! numeric (mapping["maximum"], 0, 1))
                return Result::fail ("Invalid MIDI Learn mapping.");
            if (static_cast<bool> (mapping["enabled"]))
                out.mappings[out.mappingCount++] = { mapping["controller"], channel.isVoid() ? -1 : static_cast<int> (channel),
                    target, mapping["minimum"], mapping["maximum"] };
        }
    }
    return Result::ok();
}

Plugin::Plugin (const te::PluginCreationInfo& info) : te::Plugin (info)
{
    outputTrim = addParam ("studioSynthTrim", "Synth Trim", { -24.0f, 12.0f, .1f });
    trimValue.referTo (state, Identifier ("synthTrimBase"), getUndoManager(), 0.0f);
    outputTrim->attachToCurrentValue (trimValue);
    macroOffset = addParam ("studioSynthMacroOffset", "Synth Macro Offset", { -1.0f, 1.0f, .01f });
    macroValue.referTo (state, Identifier ("synthMacroBase"), getUndoManager(), 0.0f);
    macroOffset->attachToCurrentValue (macroValue);
    restore();
    startTimerHz (20);
}
Plugin::~Plugin()
{
    stopTimer();
    windowState->hideWindowForShutdown();
    notifyListenersOfDeletion();
    outputTrim->detachFromCurrentValue();
    macroOffset->detachFromCurrentValue();
}
void registerPlugin (te::Engine& engine) { engine.getPluginManager().createBuiltInType<Plugin>(); }
const var& Plugin::getPatch() const { return document.getDynamicObject()->getProperty ("preset").getDynamicObject()->getProperty ("patch"); }
String Plugin::presetName() const { return document["preset"]["name"].toString(); }
String Plugin::factoryID() const { return document["factoryID"].toString(); }
bool Plugin::dirty() const { return static_cast<bool> (document["dirty"]); }
bool Plugin::isUserPreset() const { return static_cast<bool> (document["userPreset"]); }
Plugin::BusLayout Plugin::getBusses() const
{
    BusLayout result;
    result.outputs.push_back (te::ChannelConfiguration::stereo());
    return result;
}
File Plugin::projectDirectory() const
{
    if (edit.editFileRetriever)
        if (const auto file = edit.editFileRetriever(); file != File()) return file.getParentDirectory();
    return {};
}
Result Plugin::build (const var& root, bool rebuild, const File& base, std::unique_ptr<Bank>& next, String& warning)
{
    if (! validEnvelope (root)) return Result::fail ("Unsupported Studio Synth preset.");
    next = std::make_unique<Bank>();
    if (auto parsed = makePlan (root["preset"]["patch"], base, next->patch, warning); parsed.failed()) return parsed;
    next->rate = rate.load();
    if (std::ranges::any_of (next->patch.lfoBeats, [] (auto beats) { return beats > 0; }))
        next->tempo = std::make_shared<te::tempo::Sequence> (edit.tempoSequence.getInternalSequence());
    const auto compatible = [&]
    {
        if (rebuild || ! current || current->rate != next->rate || current->patch.count != next->patch.count
            || current->patch.receive != next->patch.receive) return false;
        for (int i = 0; i < next->patch.count; ++i)
        {
            const auto& a = next->patch.layers[i];
            const auto& b = current->patch.layers[i];
            if (a.family != b.family || a.enabled != b.enabled || a.voices != b.voices || a.pcm != b.pcm || a.source != b.source) return false;
        }
        return true;
    };
    next->engine = compatible() ? current->engine : std::make_shared<Engine> (next->patch, next->rate);
    if (! next->engine->valid()) return Result::fail ("A legacy Synth patch could not be applied to its original DSP.");
    return Result::ok();
}
void Plugin::publish (std::unique_ptr<Bank> bank)
{
    published.store (bank.get());
    auto previous = std::move (current);
    current = std::move (bank);
    const auto* reading = borrowed.load();
    if (retired.get() != reading) retired.reset();
    if (previous && previous.get() == reading) retired = std::move (previous);
}
void Plugin::timerCallback()
{
    const std::lock_guard lock (publicationMutex);
    if (retired && borrowed.load() != retired.get()) retired.reset();
    if (current && current->tempo && current->tempo->hash() != edit.tempoSequence.getInternalSequence().hash())
    {
        auto next = std::make_unique<Bank> (*current);
        next->tempo = std::make_shared<te::tempo::Sequence> (edit.tempoSequence.getInternalSequence());
        publish (std::move (next));
    }
}
void Plugin::restore()
{
    const std::lock_guard lock (publicationMutex);
    const auto content = state.getProperty (stateKey).toString();
    var root;
    if (content.isEmpty())
    {
        const auto& sounds = catalog().sounds;
        const auto found = std::find_if (sounds.begin(), sounds.end(), [] (const auto& s) { return s.family == Family::analog; });
        if (found != sounds.end()) root = newPreset (found->name, found->patch.clone(), found->id);
    }
    else if (content.getNumBytesAsUTF8() <= maximumDocumentBytes)
        root = JSON::parse (content);
    fillLegacyLayerDefaults (root);
    std::unique_ptr<Bank> next;
    const auto result = build (root, true, projectDirectory(), next, problem);
    document = result.wasOk() ? root : newPreset ("Unavailable Preset", object());
    if (result.wasOk()) publish (std::move (next));
    else
    {
        problem = result.getErrorMessage();
        publish (nullptr);
    }
    ++revision;
}
void Plugin::valueTreePropertyChanged (ValueTree& tree, const Identifier& key)
{
    te::Plugin::valueTreePropertyChanged (tree, key);
    if (tree == state && key == stateKey && ! writing) restore();
}
Result Plugin::commit (var nextDoc, bool rebuild, const File& base)
{
    const std::lock_guard lock (publicationMutex);
    const auto json = JSON::toString (nextDoc, true);
    if (json.getNumBytesAsUTF8() > maximumDocumentBytes) return Result::fail ("Synth state is too large.");
    std::unique_ptr<Bank> next;
    String warning;
    if (const auto result = build (nextDoc, rebuild, base, next, warning); result.failed()) return result;
    const ScopedValueSetter<bool> scope (writing, true);
    if (auto* undo = getUndoManager()) undo->beginNewTransaction ("Edit Studio Synth");
    state.setProperty (stateKey, json, getUndoManager());
    document = std::move (nextDoc);
    problem = warning;
    ++revision;
    publish (std::move (next));
    return Result::ok();
}
Result Plugin::selectFactory (const FactorySound& sound)
{
    return commit (newPreset (sound.name, sound.patch.clone(), sound.id), true, projectDirectory());
}
Result Plugin::editPatch (var changed, bool rebuild)
{
    auto copy = document.clone();
    set (copy["preset"], "patch", std::move (changed));
    set (copy, "dirty", true);
    return commit (std::move (copy), rebuild, projectDirectory());
}
Result Plugin::loadPreset (const File& file)
{
    if (! file.existsAsFile() || file.getSize() > maximumDocumentBytes) return Result::fail ("Cannot read this Synth preset.");
    auto root = JSON::parse (file.loadFileAsString());
    if (! validEnvelope (root)) return Result::fail ("Unsupported Studio Synth preset.");
    fillLegacyLayerDefaults (root);
    const auto layers = root["preset"]["patch"]["layers"];
    if (layers.isArray())
        for (const auto& layer : *layers.getArray())
        {
            const auto granular = layer["granularPatch"];
            const auto path = granular["importedAudioPath"].toString();
            if (granular.isObject() && path.isNotEmpty() && ! File::isAbsolutePath (path))
                set (granular, "importedAudioPath", file.getSiblingFile (path).getFullPathName());
        }
    set (root, "dirty", false);
    set (root, "userPreset", true);
    return commit (std::move (root), true, projectDirectory());
}
Result Plugin::savePreset (const File& file)
{
    auto next = document.clone();
    if (! validEnvelope (next)) return Result::fail ("No valid Synth patch to save.");
    const auto name = file.getFileNameWithoutExtension().trim().substring (0, 80);
    if (name.isEmpty()) return Result::fail ("Enter a name for the Synth preset.");
    const auto identity = next["preset"];
    if (! file.existsAsFile())
    {
        set (identity, "id", Uuid().toDashedString());
        set (identity, "createdAt", static_cast<double> (Time::currentTimeMillis()) / 1000 - 978307200.0);
    }
    set (identity, "name", name);
    set (next, "dirty", false);
    set (next, "userPreset", true);
    TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (JSON::toString (next)) || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not save the Synth preset.");
    return commit (std::move (next), false, projectDirectory());
}
void Plugin::initialise (const te::PluginInitialisationInfo& info)
{
    const std::lock_guard lock (publicationMutex);
    rate.store (info.sampleRate);
    if (current && current->rate != info.sampleRate)
    {
        auto next = std::make_unique<Bank> (*current);
        next->rate = info.sampleRate;
        next->engine = std::make_shared<Engine> (next->patch, next->rate);
        if (next->engine->valid()) publish (std::move (next));
    }
    reset();
}
void Plugin::deinitialise() { reset(); }
void Plugin::reset() { resetPending.store (true); }
void Plugin::preview (int note, int velocity)
{
    const auto index = sent.load (std::memory_order_relaxed);
    if (index - received.load (std::memory_order_acquire) >= previewQueue.size()) { overflow.store (true); return; }
    previewQueue[index % previewQueue.size()] = (note << 8) | velocity;
    sent.store (index + 1, std::memory_order_release);
}
bool Plugin::noteActive (int note) const noexcept
{
    return (noteBits[note / 64].load (std::memory_order_relaxed) & (std::uint64_t { 1 } << (note % 64))) != 0;
}

void Plugin::applyToBuffer (const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.destBuffer->getNumChannels() < 1 || context.bufferNumSamples <= 0) return;
    const auto* bank = published.load();
    borrowed.store (bank);
    if (! bank || bank != published.load() || bank->rate != rate.load())
    {
        borrowed.store (nullptr);
        return;
    }
    auto& engine = *bank->engine;
    engine.settings (bank->patch);
    const auto* midi = context.bufferForMidiMessages;
    const bool stopped = wasPlaying && ! context.isPlaying;
    const bool previewOverflowed = overflow.exchange (false);
    if (resetPending.exchange (false) || previewOverflowed || (midi && midi->isAllNotesOff) || stopped)
    {
        engine.clear();
        for (auto& bits : noteBits) bits.store (0, std::memory_order_relaxed);
    }
    wasPlaying = context.isPlaying;
    const int channel = bank->patch.receive > 0 ? bank->patch.receive - 1 : 0;
    const auto pending = sent.load (std::memory_order_acquire);
    if (! context.isRendering && ! stopped && ! previewOverflowed)
        for (auto index = received.load (std::memory_order_relaxed); index != pending; ++index)
        {
            const auto value = previewQueue[index % previewQueue.size()];
            const int note = value >> 8;
            const int velocity = value & 127;
            engine.midi ((velocity ? 0x90 : 0x80) | channel, note, velocity);
            auto& bits = noteBits[note / 64];
            const auto mask = std::uint64_t { 1 } << (note % 64);
            if (velocity) bits.fetch_or (mask, std::memory_order_relaxed);
            else bits.fetch_and (~mask, std::memory_order_relaxed);
        }
    received.store (pending, std::memory_order_release);
    const auto start = context.bufferStartSample;
    auto* l = context.destBuffer->getWritePointer (0, start);
    auto* r = context.destBuffer->getNumChannels() >= 2 ? context.destBuffer->getWritePointer (1, start) : nullptr;
    const float trim = Decibels::decibelsToGain (outputTrim->getCurrentValue());
    const float macro = macroOffset->getCurrentValue();
    const auto bpm = bank->tempo ? bank->tempo->getBpmAt (context.editTime.getStart()) : 120.0;
    int offset = 0;
    const auto render = [&] (int end)
    {
        if (end <= offset) return;
        engine.render (l + offset, r != nullptr ? r + offset : nullptr, end - offset, trim, macro, bpm);
        offset = end;
    };
    if (midi)
    {
        int events = 0;
        for (const auto& message : *midi)
        {
            if (++events > 4096) { engine.clear(); break; }
            const auto count = message.getRawDataSize();
            const auto time = message.getTimeStamp();
            if (count < 2 || count > 3 || ! std::isfinite (time)) continue;
            const auto* data = message.getRawData();
            if (data[0] < 0x80 || data[0] >= 0xf0) continue;
            const auto sample = static_cast<int> (jlimit (0.0, static_cast<double> (context.bufferNumSamples), time * bank->rate + .5));
            render (sample);
            engine.midi (data[0], data[1] & 127, count == 3 ? data[2] & 127 : 0);
        }
    }
    render (context.bufferNumSamples);
    borrowed.store (nullptr);
}
}
