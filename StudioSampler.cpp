// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioSampler.h"
#include "SamplerMapping.h"
#include <algorithm>
#include <cmath>

namespace studio::sampler
{
using namespace juce;
namespace te = tracktion;
namespace
{
const Identifier presetState { "studioSamplerState" };
constexpr auto documentTag = "STUDIO_SAMPLER";
constexpr int version = 2;
constexpr auto maxPresetBytes = 2 * 1024 * 1024;

float bounded (const XmlElement& xml, const char* key, float fallback, float min, float max)
{
    const auto value = xml.getDoubleAttribute (key, fallback);
    return std::isfinite (value) ? static_cast<float> (jlimit<double> (min, max, value)) : fallback;
}

XmlElement encode (const Instrument& model, const File& base, bool project)
{
    XmlElement xml (documentTag);
    xml.setAttribute ("version", version);
    xml.setAttribute ("name", model.name);
    xml.setAttribute ("preset", model.preset);
    xml.setAttribute ("dirty", model.dirty);
    const auto& s = model.settings;
    xml.setAttribute ("attack", s.attack);
    xml.setAttribute ("decay", s.decay);
    xml.setAttribute ("sustain", s.sustain);
    xml.setAttribute ("release", s.release);
    xml.setAttribute ("gain", s.gainDb);
    xml.setAttribute ("tune", s.semitones);
    xml.setAttribute ("bend", s.bendRange);
    xml.setAttribute ("voices", s.polyphony);
    for (const auto& group : model.groups)
    {
        auto* child = xml.createNewChildElement ("GROUP");
        child->setAttribute ("id", group.id);
        child->setAttribute ("name", group.name);
    }
    for (const auto& entry : model.zones)
    {
        auto& node = *xml.createNewChildElement ("ZONE");
        if (base != File() && entry.file.isAChildOf (base))
        {
            node.setAttribute ("relative", entry.file.getRelativePathFrom (base));
            if (project) node.setAttribute ("file", entry.file.getFullPathName());
        }
        else
            node.setAttribute ("file", entry.file.getFullPathName());

        const auto& z = entry.mapping;
        node.setAttribute ("folder", entry.folder);
        node.setAttribute ("start", z.start);
        node.setAttribute ("end", z.end);
        node.setAttribute ("root", z.root);
        node.setAttribute ("low", z.low);
        node.setAttribute ("high", z.high);
        node.setAttribute ("soft", z.soft);
        node.setAttribute ("hard", z.hard);
        node.setAttribute ("gain", z.gainDb);
        node.setAttribute ("cents", z.cents);
        node.setAttribute ("oneShot", z.oneShot);
        node.setAttribute ("loop", z.loop);
        node.setAttribute ("loopStart", z.loopStart);
        node.setAttribute ("loopEnd", z.loopEnd);
        node.setAttribute ("group", z.group);
    }
    return xml;
}

Result decode (const XmlElement* xml, const File& base, Instrument& model)
{
    if (xml == nullptr || ! xml->hasTagName (documentTag) || (xml->getIntAttribute ("version") < 1 || xml->getIntAttribute ("version") > version)
        || xml->getNumChildElements() > maxZones + maxGroups)
        return Result::fail ("Unsupported or invalid Studio Sampler instrument.");

    model.name = xml->getStringAttribute ("name", "Studio Sampler").substring (0, 120);
    if (model.name.trim().isEmpty()) model.name = "Studio Sampler";
    model.preset = xml->getStringAttribute ("preset").substring (0, 120);
    model.dirty = xml->getBoolAttribute ("dirty");
    auto& s = model.settings;
    s.attack = bounded (*xml, "attack", s.attack, 0, 10);
    s.decay = bounded (*xml, "decay", s.decay, 0, 10);
    s.sustain = bounded (*xml, "sustain", s.sustain, 0, 1);
    s.release = bounded (*xml, "release", s.release, 0, 15);
    s.gainDb = bounded (*xml, "gain", s.gainDb, -60, 12);
    s.semitones = bounded (*xml, "tune", s.semitones, -24, 24);
    s.bendRange = bounded (*xml, "bend", s.bendRange, 0, 24);
    s.polyphony = jlimit (1, maxVoices, xml->getIntAttribute ("voices", s.polyphony));
    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName ("GROUP"))
        {
            const int id = child->getIntAttribute ("id", -1);
            if (id < 0 || id >= maxGroups) return Result::fail ("Invalid instrument group.");
            auto found = std::find_if (model.groups.begin(), model.groups.end(), [id] (auto& g) { return g.id == id; });
            if (found != model.groups.end()) found->name = child->getStringAttribute ("name").substring (0, 120);
            else model.groups.push_back ({ id, child->getStringAttribute ("name").substring (0, 120) });
            continue;
        }
        if (model.zones.size() >= maxZones) return Result::fail ("Too many sample zones.");
        if (! child->hasTagName ("ZONE")) return Result::fail ("Invalid sample zone.");
        SampleZone entry;
        const auto absolute = child->getStringAttribute ("file");
        const auto relative = child->getStringAttribute ("relative");
        if (File::isAbsolutePath (absolute)) entry.file = File (absolute);
        if (base != File() && relative.isNotEmpty() && ! File::isAbsolutePath (relative))
        {
            const auto candidate = base.getChildFile (relative);
            if (candidate.isAChildOf (base) && (entry.file == File() || (! entry.file.existsAsFile() && candidate.existsAsFile())))
                entry.file = candidate;
        }
        if (entry.file == File()) return Result::fail ("Invalid sample reference.");
        entry.folder = child->getIntAttribute ("folder");
        auto& z = entry.mapping;
        z.start = child->getIntAttribute ("start");
        z.end = child->getIntAttribute ("end");
        if (entry.folder < 0 || entry.folder >= maxGroups || z.start < 0 || z.end < 0 || (z.end && z.end <= z.start))
            return Result::fail ("Invalid sample boundaries or group.");
        z.root = child->getIntAttribute ("root", z.root);
        z.low = child->getIntAttribute ("low", z.low);
        z.high = child->getIntAttribute ("high", z.high);
        z.soft = child->getIntAttribute ("soft", z.soft);
        z.hard = child->getIntAttribute ("hard", z.hard);
        z.gainDb = bounded (*child, "gain", z.gainDb, -60, 12);
        z.cents = bounded (*child, "cents", z.cents, -100, 100);
        z.oneShot = child->getBoolAttribute ("oneShot");
        z.loop = child->getBoolAttribute ("loop");
        z.loopStart = child->getIntAttribute ("loopStart");
        z.loopEnd = child->getIntAttribute ("loopEnd");
        z.group = child->getIntAttribute ("group");
        if (! validMapping (z) || z.loopStart < 0 || z.loopEnd < z.loopStart
            || (z.loop && ! validLoop (z, static_cast<int> (maxSampleBytes / sizeof (float)))))
            return Result::fail ("A sample zone has an invalid key, velocity or loop range.");
        model.zones.push_back (std::move (entry));
    }
    return Result::ok();
}

std::size_t sampleBytes (const Instrument& model, unsigned* sampleCount = nullptr)
{
    std::size_t bytes = 0;
    std::array<const Sample*, maxZones> counted {};
    std::size_t count = 0;
    for (const auto& zone : model.zones)
        if (const auto* sample = zone.mapping.sample.get(); sample != nullptr
            && std::find (counted.begin(), counted.begin() + count, sample) == counted.begin() + count)
        {
            counted[count++] = sample;
            bytes += sample->bytes();
        }
    if (sampleCount) *sampleCount = static_cast<unsigned> (count);
    return bytes;
}

Result readSample (const File& file, std::size_t budget, std::shared_ptr<const Sample>& output)
{
    if (! file.existsAsFile()) return Result::fail ("Missing sample");
    AudioFormatManager formats;
    formats.registerFormat (new WavAudioFormat(), true);
    formats.registerFormat (new AiffAudioFormat(), false);
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 2
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 1000 || reader->sampleRate > 768000)
        return Result::fail ("Choose a readable mono or stereo WAV or AIFF sample.");

    const auto floatFrameBytes = reader->numChannels * sizeof (float);
    if (reader->lengthInSamples > static_cast<int64> (std::min (budget, maxSampleBytes) / floatFrameBytes))
        return Result::fail ("Sample exceeds Studio's memory limit.");

    auto sample = std::make_shared<Sample>();
    sample->rate = reader->sampleRate;
    const auto frames = static_cast<int> (reader->lengthInSamples);
    sample->left.resize (static_cast<std::size_t> (frames));
    if (reader->numChannels == 2) sample->right.resize (static_cast<std::size_t> (frames));
    float* channels[] { sample->left.data(), sample->right.data() };
    AudioBuffer<float> buffer (channels, static_cast<int> (reader->numChannels), frames);
    if (! reader->read (&buffer, 0, frames, 0, true, reader->numChannels == 2))
        return Result::fail ("Could not decode the sample.");

    for (auto* channel : { &sample->left, &sample->right })
        for (auto& frame : *channel)
            frame = std::isfinite (frame) ? jlimit (-1.0f, 1.0f, frame) : 0.0f;

    output = std::move (sample);
    return Result::ok();
}

void prepareSamples (Instrument& next, const Instrument& current)
{
    for (auto& entry : next.zones)
    {
        auto& sample = entry.mapping.sample;
        const auto reuse = [&] (const auto& zone) { return zone.file == entry.file && zone.mapping.sample != nullptr; };
        if (auto loaded = std::find_if (next.zones.begin(), next.zones.end(), reuse); loaded != next.zones.end())
            sample = loaded->mapping.sample;
        else if (auto previous = std::find_if (current.zones.begin(), current.zones.end(), reuse);
                 previous != current.zones.end() && previous->file.existsAsFile()
                    && previous->mapping.sample->bytes() <= maxBankBytes - sampleBytes (next))
            sample = previous->mapping.sample;
        else
            entry.problem = readSample (entry.file, maxBankBytes - sampleBytes (next), sample).getErrorMessage();

        if (! sample) continue;
        if (entry.mapping.start >= static_cast<int> (sample->left.size()) || entry.mapping.end > static_cast<int> (sample->left.size()))
        { entry.problem = "Sample boundaries exceed file length"; sample.reset(); continue; }
        const auto frames = static_cast<int> (sample->left.size());
        if (entry.mapping.loopEnd == 0) entry.mapping.loopEnd = frames;
        if (entry.mapping.loop && ! validLoop (entry.mapping, frames))
        {
            entry.problem = "Loop is outside the sample. Adjust the loop in Loops.";
        }
    }
}
}

Plugin::Plugin (const te::PluginCreationInfo& info) : te::Plugin (info)
{
    loadState();
    startTimerHz (10);
}

Plugin::~Plugin()
{
    stopTimer();
    windowState->hideWindowForShutdown();
    notifyListenersOfDeletion();
}

void registerPlugin (te::Engine& engine) { engine.getPluginManager().createBuiltInType<Plugin>(); }

Plugin::BusLayout Plugin::getBusses() const
{
    BusLayout layout;
    layout.outputs.push_back (te::ChannelConfiguration::stereo());
    return layout;
}

File Plugin::projectDirectory() const
{
    const auto file = edit.editFileRetriever ? edit.editFileRetriever() : File();
    return file == File() ? File() : file.getParentDirectory();
}

void Plugin::loadState()
{
    Instrument next;
    const auto raw = state.getProperty (presetState).toString();
    if (raw.isNotEmpty())
    {
        auto parsed = raw.getNumBytesAsUTF8() <= maxPresetBytes ? XmlDocument::parse (raw) : nullptr;
        const auto result = decode (parsed.get(), projectDirectory(), next);
        restoreError = result.getErrorMessage();
        if (result.failed()) next = {};
    }
    else
        restoreError.clear();

    prepareSamples (next, model);
    publish (std::move (next));
}

void Plugin::valueTreePropertyChanged (ValueTree& tree, const Identifier& property)
{
    te::Plugin::valueTreePropertyChanged (tree, property);
    if (tree == state && property == presetState && ! writing) loadState();
}

void Plugin::change (Instrument next, bool markDirty)
{
    if (markDirty) next.dirty = true;
    const auto xml = encode (next, projectDirectory(), true).toString (XmlElement::TextFormat().singleLine());
    if (xml.getNumBytesAsUTF8() > maxPresetBytes) return;

    const ScopedValueSetter<bool> transaction (writing, true);
    if (auto* undo = getUndoManager()) undo->beginNewTransaction ("Edit Studio Sampler");
    state.setProperty (presetState, xml, getUndoManager());
    restoreError.clear();
    publish (std::move (next));
}

void Plugin::timerCallback()
{
    if (retired && borrowed.load() != retired.get()) retired.reset();
}

void Plugin::publish (Instrument next)
{
    const auto sameSamples = std::ranges::equal (model.zones, next.zones, [] (const auto& a, const auto& b)
    {
        return a.mapping.sample == b.mapping.sample;
    });
    if (! sameSamples) ++sampleRevision;
    auto bank = std::make_unique<Bank>();
    bank->settings = next.settings;
    bank->sampleRevision = sampleRevision;
    for (const auto& entry : next.zones) bank->zones.push_back (entry.mapping);

    published.store (bank.get());
    auto previous = std::move (current);
    current = std::move (bank);
    const auto* reading = borrowed.load();
    if (retired.get() != reading) retired.reset();
    if (previous && previous.get() == reading) retired = std::move (previous);
    model = std::move (next);
    loadedBytes = sampleBytes (model, &loadedSamples);
    ++modelRevision;
}

Result Plugin::loadSample (const File& file, int replace)
{
    if (replace < -1 || replace >= static_cast<int> (model.zones.size()))
        return Result::fail ("Select a sample zone.");
    if (replace == -1 && model.zones.size() >= maxZones)
        return Result::fail ("This instrument has reached its zone limit.");

    auto next = model;
    SampleZone entry;
    entry.file = file;
    if (replace >= 0)
    {
        entry.folder = next.zones[static_cast<std::size_t> (replace)].folder;
        entry.mapping = next.zones[static_cast<std::size_t> (replace)].mapping;
        next.zones[static_cast<std::size_t> (replace)].mapping.sample.reset();
    }

    auto existing = std::find_if (next.zones.begin(), next.zones.end(), [&] (const auto& z)
    {
        return z.file == file && z.mapping.sample != nullptr;
    });
    if (existing != next.zones.end())
        entry.mapping.sample = existing->mapping.sample;
    else if (const auto result = readSample (file, maxBankBytes - sampleBytes (next), entry.mapping.sample); result.failed())
        return result;

    const auto frames = static_cast<int> (entry.mapping.sample->left.size());
    if (entry.mapping.loopEnd == 0) entry.mapping.loopEnd = frames;
    if (! validLoop (entry.mapping, frames))
    {
        if (entry.mapping.loop) return Result::fail ("The replacement is too short for the saved loop. Adjust or disable the loop first.");
        entry.mapping.loopStart = 0;
        entry.mapping.loopEnd = frames;
    }
    if (replace >= 0)
        next.zones[static_cast<std::size_t> (replace)] = std::move (entry);
    else
        next.zones.push_back (std::move (entry));

    change (std::move (next), true);
    return Result::ok();
}

void Plugin::undoMapping (bool redo)
{
    if (auto* undo = getUndoManager()) { if (redo) undo->redo(); else undo->undo(); }
}
void Plugin::duplicateZone (int index)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size())) || model.zones.size() >= maxZones) return;
    auto next = model; next.zones.push_back (next.zones[static_cast<std::size_t> (index)]); change (std::move (next), true);
}
void Plugin::editGroups (std::vector<InstrumentGroup> groups)
{
    if (groups.empty() || groups.size() > maxGroups) return;
    auto next = model; next.groups = std::move (groups);
    for (auto& z : next.zones)
        if (std::none_of (next.groups.begin(), next.groups.end(), [&] (auto& g) { return g.id == z.folder; })) z.folder = next.groups.front().id;
    change (std::move (next), true);
}
void Plugin::moveZone (int index, int folder)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size()))) return;
    auto next = model; next.zones[static_cast<std::size_t> (index)].folder = folder; change (std::move (next), true);
}
Result Plugin::proposeImport (const Array<File>& files, bool automatic, Instrument& next, String& summary)
{
    next = model;
    int accepted = 0, unsupported = 0, provisional = 0;
    StringArray errors;
    std::vector<Zone> mapping;
    for (const auto& file : files)
    {
        if (! file.hasFileExtension ("wav;wave;aif;aiff;aifc")) { ++unsupported; continue; }
        if (next.zones.size() >= maxZones) { errors.add (file.getFileName() + ": zone limit (128)"); continue; }
        SampleZone entry; entry.file = file;
        if (auto result = readSample (file, maxBankBytes - sampleBytes (next), entry.mapping.sample); result.failed())
        { errors.add (file.getFileName() + ": " + result.getErrorMessage()); continue; }
        entry.mapping.loopEnd = static_cast<int> (entry.mapping.sample->left.size());
        if (auto note = filenameNote (file.getFileName().toStdString())) entry.mapping.root = *note;
        else { entry.mapping.root = jlimit (0, 127, 48 + accepted); ++provisional; }
        if (! automatic) entry.mapping.low = entry.mapping.high = entry.mapping.root;
        mapping.push_back (entry.mapping); next.zones.push_back (std::move (entry)); ++accepted;
    }
    if (automatic) spreadMapping (mapping);
    summary = String (accepted) + " files accepted / zones created; " + String (unsupported) + " unsupported; "
        + String (errors.size()) + " errors.\nC4 = MIDI 60. " + String (provisional) + " provisional roots (no pitch detection).\n";
    const auto offset = next.zones.size() - mapping.size();
    for (std::size_t i = 0; i < mapping.size(); ++i)
    {
        next.zones[offset + i].mapping = mapping[i];
        summary += next.zones[offset + i].file.getFileName() + ": root " + String (mapping[i].root)
            + ", keys " + String (mapping[i].low) + "–" + String (mapping[i].high) + "\n";
    }
    summary += errors.joinIntoString ("\n");
    return accepted ? Result::ok() : Result::fail (summary);
}

void Plugin::removeZone (int index)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size()))) return;
    auto next = model;
    next.zones.erase (next.zones.begin() + index);
    change (std::move (next), true);
}

void Plugin::editZone (int index, const Zone& z)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size())) || ! validMapping (z)) return;
    auto next = model;
    auto& entry = next.zones[static_cast<std::size_t> (index)];
    if (z.start < 0 || (z.end && z.end <= z.start) || (z.sample && (z.start >= static_cast<int> (z.sample->left.size()) || z.end > static_cast<int> (z.sample->left.size())))) return;
    if (z.sample != entry.mapping.sample || z.loopStart < 0 || z.loopEnd < z.loopStart) return;
    if (z.loop && z.sample && ! validLoop (z, static_cast<int> (z.sample->left.size()))
        && (z.loopStart != entry.mapping.loopStart || z.loopEnd != entry.mapping.loopEnd || ! entry.mapping.loop))
        return;
    entry.mapping = z;
    if (z.sample && (! z.loop || validLoop (z, static_cast<int> (z.sample->left.size())))) entry.problem.clear();
    change (std::move (next), true);
}

void Plugin::editSettings (const Settings& settings)
{
    auto next = model;
    next.settings = settings;
    change (std::move (next), true);
}

void Plugin::rename (String name)
{
    name = name.trim().substring (0, 120);
    if (name.isEmpty() || name == model.name) return;
    auto next = model;
    next.name = std::move (name);
    change (std::move (next), true);
}

Result Plugin::savePreset (const File& file)
{
    if (! file.hasFileExtension ("studiosampler")) return Result::fail ("Use the .studiosampler instrument extension.");
    for (const auto& zone : model.zones) if (zone.file == file) return Result::fail ("Cannot overwrite a source sample.");
    auto next = model;
    next.preset = file.getFileNameWithoutExtension().substring (0, 120);
    next.dirty = false;
    TemporaryFile temp (file);
    if (! encode (next, file.getParentDirectory(), false).writeTo (temp.getFile())
        || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not save the Studio Sampler preset.");
    change (std::move (next), false);
    return Result::ok();
}

Result Plugin::loadPreset (const File& file)
{
    Instrument next;
    const auto xml = file.getSize() <= maxPresetBytes ? XmlDocument::parse (file) : nullptr;
    if (auto result = decode (xml.get(), file.getParentDirectory(), next); result.failed())
        return result;
    prepareSamples (next, model);
    if (next.preset.isEmpty()) next.preset = file.getFileNameWithoutExtension().substring (0, 120);
    next.dirty = false;
    change (std::move (next), false);
    return Result::ok();
}

InstrumentDiagnosticSnapshot Plugin::resourceSnapshot() const
{
    InstrumentDiagnosticSnapshot stats;
    stats.attribution = ResourceAttribution::measured;
    stats.sampleBytes = loadedBytes;
    stats.memoryBytes = stats.sampleBytes;
    stats.activeVoices = renderer.activeVoices();
    stats.detail = String (loadedSamples)
                 + " loaded sample(s) | " + String (model.settings.polyphony) + " max voices.";
    return stats;
}

void Plugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    renderer.prepare (rate);
    wasPlaying = false;
}

void Plugin::deinitialise()
{
    renderer.panic();
    renderer.publishMeters();
    readIndex.store (writeIndex.load());
}

void Plugin::reset() { needsReset.store (true, std::memory_order_relaxed); }

void Plugin::preview (int source, int channel, int note, int velocity, int zone)
{
    const auto write = writeIndex.load (std::memory_order_relaxed);
    if (write - readIndex.load (std::memory_order_acquire) >= queueSize)
    {
        previewOverflow.store (true, std::memory_order_release);
        return;
    }
    queue[write % queueSize] = { source, channel - 1, note, velocity, zone, sampleRevision };
    writeIndex.store (write + 1, std::memory_order_release);
}

void Plugin::applyToBuffer (const te::PluginRenderContext& context)
{
    auto* audio = context.destBuffer;
    if (audio == nullptr || audio->getNumChannels() == 0 || context.bufferNumSamples <= 0) return;

    // Publish the borrow before re-reading; if it changed, leave this block
    // untouched. The bounded retry keeps publication off the render thread.
    const auto* bank = published.load();
    borrowed.store (bank);
    if (bank != published.load())
    {
        borrowed.store (nullptr);
        return;
    }

    const auto* midi = context.bufferForMidiMessages;
    auto write = writeIndex.load (std::memory_order_acquire);
    auto read = readIndex.load (std::memory_order_relaxed);
    renderer.use (*bank);
    const bool stopped = wasPlaying && ! context.isPlaying;
    if (needsReset.exchange (false, std::memory_order_relaxed) || (midi && midi->isAllNotesOff) || stopped)
        renderer.panic();
    wasPlaying = context.isPlaying;

    if (previewOverflow.exchange (false, std::memory_order_acquire) || stopped)
    {
        renderer.preview (1, 0, -1, 0);
        renderer.preview (2, 0, -1, 0);
        renderer.preview (3, 0, -1, 0);
        read = write = writeIndex.load (std::memory_order_acquire);
    }
    if (! context.isRendering)
        for (; read != write; ++read)
        {
            const auto& event = queue[read % queueSize];
            if (event.zone < 0 || event.revision == bank->sampleRevision)
                renderer.preview (event.source, event.channel, event.note, event.velocity, event.zone);
        }
    readIndex.store (read, std::memory_order_release);

    const auto start = context.bufferStartSample;
    const auto size = context.bufferNumSamples;
    auto* left = audio->getWritePointer (0, start);
    auto* right = audio->getNumChannels() > 1 ? audio->getWritePointer (1, start) : nullptr;
    int offset = 0;
    if (midi)
    {
        constexpr int maxEventsPerBlock = 4096;
        int events = 0;
        for (const auto& message : *midi)
        {
            if (++events > maxEventsPerBlock)
            {
                renderer.panic();
                break;
            }
            const auto bytes = message.getRawData();
            const auto length = message.getRawDataSize();
            if (length < 2 || length > 3 || bytes[0] < 0x80 || bytes[0] >= 0xf0) continue;
            const auto time = message.getTimeStamp();
            if (! std::isfinite (time)) continue;
            const auto frame = static_cast<int> (jlimit (0.0, static_cast<double> (size), time * rate + 0.5));
            if (frame > offset)
            {
                renderer.render (left + offset, right ? right + offset : nullptr, frame - offset);
                offset = frame;
            }
            renderer.midi (bytes[0], bytes[1] & 127, length == 3 ? bytes[2] & 127 : 0);
        }
    }

    renderer.render (left + offset, right ? right + offset : nullptr, size - offset);
    renderer.publishMeters();
    borrowed.store (nullptr);
}
}
