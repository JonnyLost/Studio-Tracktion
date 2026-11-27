// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioSampler.h"
#include "SamplerMapping.h"
#include "SamplerSharedPool.h"
#include "StudioSfz.h"
#include "StudioSoundBank.h"
#include "StudioExs.h"
#include "ComposerFiles.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace studio::sampler
{
using namespace juce;
namespace te = tracktion;
namespace
{
const Identifier presetState { "studioSamplerState" };
constexpr auto documentTag = "STUDIO_SAMPLER";
constexpr int version = 4;
constexpr auto maxPresetBytes = 8 * 1024 * 1024;

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
    if (model.sourceLibrary.isNotEmpty()) xml.setAttribute ("sourceLibrary", model.sourceLibrary);
    if (model.sourceInstrument.isNotEmpty()) xml.setAttribute ("sourceInstrument", model.sourceInstrument);
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
        const auto& g = group.playback;
        child->setAttribute ("trigger", static_cast<int> (g.trigger)); child->setAttribute ("key", g.key);
        child->setAttribute ("cc", g.controller); child->setAttribute ("ccLow", g.ccLow); child->setAttribute ("ccHigh", g.ccHigh);
        child->setAttribute ("channel", g.channel); child->setAttribute ("mute", g.mute); child->setAttribute ("solo", g.solo);
        child->setAttribute ("releaseTrigger", g.releaseTrigger);
        child->setAttribute ("switchDefault", g.switchDefault);
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

        if (entry.soundFont)
        {
            node.setAttribute ("sfOffset", String (entry.soundFont->offset));
            node.setAttribute ("sfFrames", entry.soundFont->frames);
            node.setAttribute ("sfRate", entry.soundFont->rate);
        }
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
        node.setAttribute ("loopCrossfade", z.loopCrossfade);
        node.setAttribute ("fadeIn", z.fadeIn); node.setAttribute ("fadeOut", z.fadeOut);
        if (z.sfz.enabled)
        {
            const auto& f = z.sfz;
            node.setAttribute ("sfz", true);
            node.setAttribute ("sfzAttack", f.attack); node.setAttribute ("sfzDecay", f.decay);
            node.setAttribute ("sfzSustain", f.sustain); node.setAttribute ("sfzRelease", f.release);
            node.setAttribute ("sfzSustainLoop", f.sustainLoop); node.setAttribute ("sfzPowerFade", f.powerFade);
            node.setAttribute ("sfzVelocityTrack", f.velocityTrack); node.setAttribute ("sfzPan", f.pan);
            node.setAttribute ("sfzTranspose", f.transpose); node.setAttribute ("sfzKeyTrack", f.keyTrack);
            node.setAttribute ("sfzFadeInLow", f.fadeInLow); node.setAttribute ("sfzFadeInHigh", f.fadeInHigh);
            node.setAttribute ("sfzFadeOutLow", f.fadeOutLow); node.setAttribute ("sfzFadeOutHigh", f.fadeOutHigh);
        }
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
    model.sourceLibrary = xml->getStringAttribute ("sourceLibrary");
    model.sourceInstrument = xml->getStringAttribute ("sourceInstrument");
    auto& s = model.settings;
    s.attack = bounded (*xml, "attack", s.attack, 0, 10);
    s.decay = bounded (*xml, "decay", s.decay, 0, 10);
    s.sustain = bounded (*xml, "sustain", s.sustain, 0, 1);
    s.release = bounded (*xml, "release", s.release, 0, 15);
    s.gainDb = bounded (*xml, "gain", s.gainDb, -60, 12);
    s.semitones = bounded (*xml, "tune", s.semitones, -24, 24);
    s.bendRange = bounded (*xml, "bend", s.bendRange, 0, 24);
    s.polyphony = jlimit (1, maxVoices, xml->getIntAttribute ("voices", s.polyphony));
    if (xml->getChildByName ("GROUP")) model.groups.clear();
    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName ("GROUP"))
        {
            const int id = child->getIntAttribute ("id", -1);
            if (id < 0 || id >= maxGroups) return Result::fail ("Invalid instrument group.");
            auto found = std::find_if (model.groups.begin(), model.groups.end(), [id] (auto& g) { return g.id == id; });
            if (found != model.groups.end()) return Result::fail ("Duplicate instrument group.");
            else
            {
                InstrumentGroup g { id, child->getStringAttribute ("name").substring (0, 120) };
                auto& p = g.playback; p.id = id;
                p.trigger = static_cast<GroupTrigger> (child->getIntAttribute ("trigger"));
                p.key = child->getIntAttribute ("key", 24); p.controller = child->getIntAttribute ("cc", 20);
                p.ccLow = child->getIntAttribute ("ccLow", 0); p.ccHigh = child->getIntAttribute ("ccHigh", 127);
                p.channel = child->getIntAttribute ("channel"); p.mute = child->getBoolAttribute ("mute"); p.solo = child->getBoolAttribute ("solo");
                p.releaseTrigger = child->getBoolAttribute ("releaseTrigger");
                p.switchDefault = child->getIntAttribute ("switchDefault", -2);
                if (! validGroup (p)) return Result::fail ("Invalid group trigger configuration.");
                model.groups.push_back (std::move (g));
            }
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
        if (child->hasAttribute ("sfOffset"))
        {
            const auto offset = child->getStringAttribute ("sfOffset");
            SoundFontSample source { offset.getLargeIntValue(), child->getIntAttribute ("sfFrames"),
                                     child->getIntAttribute ("sfRate") };
            if (xml->getIntAttribute ("version") < 4 || ! entry.file.hasFileExtension ("sf2")
                || ! offset.containsOnly ("0123456789") || source.offset < 12
                || source.frames < 2 || source.frames > static_cast<int> (maxSampleBytes / sizeof (float))
                || source.rate < 1000 || source.rate > 768000)
                return Result::fail ("Invalid embedded SoundFont sample reference.");
            entry.soundFont = source;
        }
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
        z.loopCrossfade = child->getIntAttribute ("loopCrossfade", -1);
        z.fadeIn = child->getIntAttribute ("fadeIn"); z.fadeOut = child->getIntAttribute ("fadeOut");
        if (child->getBoolAttribute ("sfz"))
        {
            auto& f = z.sfz; f.enabled = true;
            f.attack = bounded (*child, "sfzAttack", 0, 0, 100);
            f.decay = bounded (*child, "sfzDecay", 0, 0, 100);
            f.sustain = bounded (*child, "sfzSustain", 1, 0, 1);
            f.release = bounded (*child, "sfzRelease", 0.001f, 0, 100);
            f.sustainLoop = child->getBoolAttribute ("sfzSustainLoop");
            f.powerFade = child->getBoolAttribute ("sfzPowerFade", true);
            f.velocityTrack = bounded (*child, "sfzVelocityTrack", 100, -100, 100);
            f.pan = bounded (*child, "sfzPan", 0, -100, 100);
            f.transpose = child->getIntAttribute ("sfzTranspose");
            f.keyTrack = child->getIntAttribute ("sfzKeyTrack", 100);
            f.fadeInLow = child->getIntAttribute ("sfzFadeInLow");
            f.fadeInHigh = child->getIntAttribute ("sfzFadeInHigh");
            f.fadeOutLow = child->getIntAttribute ("sfzFadeOutLow", 127);
            f.fadeOutHigh = child->getIntAttribute ("sfzFadeOutHigh", 127);
        }
        if (! validMapping (z) || z.loopStart < 0 || z.loopEnd < z.loopStart
            || (z.loop && ! validLoop (z, std::numeric_limits<int>::max())))
            return Result::fail ("A sample zone has an invalid key, velocity or loop range.");
        model.zones.push_back (std::move (entry));
    }
    for (const auto& z : model.zones)
        if (std::none_of (model.groups.begin(), model.groups.end(), [&] (auto& g) { return g.id == z.folder; })) return Result::fail ("Zone references a missing group.");
    return Result::ok();
}

std::size_t sampleBytes (const Instrument& model, unsigned* sampleCount = nullptr)
{
    std::size_t bytes = 0;
    std::unordered_set<const Sample*> counted;
    counted.reserve(model.zones.size());
    for (const auto& zone : model.zones)
        if (const auto* sample = zone.mapping.sample.get(); sample != nullptr
            && counted.insert(sample).second)
        {
            bytes += sample->bytes();
        }
    if (sampleCount) *sampleCount = static_cast<unsigned> (counted.size());
    return bytes;
}

std::unique_ptr<AudioFormatReader> openSampleReader (const File& file)
{
    AudioFormatManager formats;
    formats.registerFormat (new WavAudioFormat(), true);
    formats.registerFormat (new AiffAudioFormat(), false);
   #if JUCE_USE_FLAC
    formats.registerFormat (new FlacAudioFormat(), false);
   #endif
   #if JUCE_MAC
    if (file.hasFileExtension ("caf")) formats.registerFormat (new CoreAudioFormat (CoreAudioFormat::StreamKind::kCaf), false);
   #endif
    return std::unique_ptr<AudioFormatReader> (formats.createReaderFor (file));
}

bool decodeFrames (AudioFormatReader& reader, int first, int count, float* left, float* right)
{
    float* channels[] { left, right };
    AudioBuffer<float> buffer (channels, right ? 2 : 1, count);
    if (! reader.read (&buffer, 0, count, first, true, right != nullptr)) return false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int frame = 0; frame < count; ++frame)
        {
            auto& value = channels[ch][frame];
            value = std::isfinite (value) ? jlimit (-1.0f, 1.0f, value) : 0.0f;
        }
    return true;
}

// Keep a bounded number of seekable file readers across all instruments.
// Only import, disk-worker and offline render threads can call this pool.
class SampleReaderPool
{
    struct Entry
    {
        File file;
        int64 size = 0, modified = 0;
        std::unique_ptr<AudioFormatReader> reader;
    };
public:
    bool read (const File& file, int64 size, int64 modified, int first, int count, float* left, float* right)
    {
        std::lock_guard lock (mutex);
        if (! file.existsAsFile() || file.getSize() != size
            || file.getLastModificationTime().toMilliseconds() != modified) return false;
        auto found = std::find_if (entries.begin(), entries.end(), [&] (const auto& e)
        { return e.file == file && e.size == size && e.modified == modified; });
        if (found == entries.end())
        {
            auto reader = openSampleReader (file);
            if (! reader || reader->numChannels != (right ? 2u : 1u)
                || static_cast<int64> (first) + count > reader->lengthInSamples) return false;
            if (entries.size() >= 24) entries.erase (entries.begin());
            entries.push_back ({ file, size, modified, std::move (reader) });
        }
        else if (found != entries.end() - 1)
            std::rotate (found, found + 1, entries.end());
        return decodeFrames (*entries.back().reader, first, count, left, right);
    }
private:
    std::mutex mutex;
    std::vector<Entry> entries;
};

Result readSample (const File& file, std::size_t budget, std::shared_ptr<const Sample>& output, std::atomic<bool>* cancelled = nullptr)
{
    if (! file.existsAsFile()) return Result::fail ("Missing sample");
    const auto sizeBefore=file.getSize(), timeBefore=file.getLastModificationTime().toMilliseconds();
    auto reader = openSampleReader (file);
    if (! reader || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 2
        || reader->lengthInSamples >= std::numeric_limits<int>::max()
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 1000 || reader->sampleRate > 768000)
        return Result::fail ("Choose a readable mono/stereo WAV, AIFF, FLAC (when enabled), or supported macOS CAF sample within Studio's frame limit.");

    const int frames = static_cast<int> (reader->lengthInSamples);
    const auto floatFrameBytes = reader->numChannels * sizeof (float);
    const auto fullBytes = static_cast<std::uint64_t> (frames) * floatFrameBytes;
    const bool stereo = reader->numChannels == 2;
    const bool streamed = frames > residentSampleFrames || fullBytes > std::min (budget, maxSampleBytes);
    const auto preload = streamed ? std::min (frames, streamPreloadFrames) : frames;
    const auto memory = static_cast<std::size_t> (preload) * floatFrameBytes
        + (streamed ? StreamCache::capacityBytes (stereo, frames) : 0);
    if (streamed && frames <= streamPreloadFrames)
        return Result::fail ("Sample exceeds Studio's resident/preload memory budget.");

    const SharedSamplePool::Key key{file.getFullPathName().toStdString(),sizeBefore,timeBefore};
    String failure;
    const auto outcome=sharedSamples().acquire(key,budget,memory,output,[&]() -> std::shared_ptr<const Sample>
    {
    const auto reject=[&](String message)->std::shared_ptr<const Sample>{failure=std::move(message);return {};};
    if(cancelled&&cancelled->load())return reject("Import cancelled.");
    auto sample = std::make_shared<Sample>();
    sample->rate = reader->sampleRate;
    sample->totalFrames = frames;
    sample->sourceBytes = sizeBefore; sample->sourceTime = timeBefore;
    for (const auto& key : { String ("MidiUnityNote"), String (AiffAudioFormat::appleRootNote) })
        if (reader->metadataValues.containsKey (key))
        {
            const auto text = reader->metadataValues.getValue (key, {});
            if (text.isNotEmpty() && text.containsOnly ("0123456789") && text.getIntValue() <= 127) { sample->rootNote = text.getIntValue(); break; }
        }
    if (reader->metadataValues.getValue ("NumSampleLoops", "0").getIntValue() > 0
        && reader->metadataValues.getValue ("Loop0Type", "-1") == "0")
    {
        const auto a = sfz::integer (reader->metadataValues.getValue ("Loop0Start", "").toStdString());
        const auto b = sfz::integer (reader->metadataValues.getValue ("Loop0End", "").toStdString());
        if (a && b && *a >= 0 && *b > *a && *b < frames)
        { sample->embeddedLoopStart = *a; sample->embeddedLoopEnd = *b + 1; }
    }
    sample->left.resize (static_cast<std::size_t> (preload));
    if (stereo) sample->right.resize (static_cast<std::size_t> (preload));
    for (int offset = 0; offset < preload; offset += 4096)
    {
        if (cancelled && cancelled->load()) return reject ("Import cancelled.");
        const int count = jmin (4096, preload - offset);
        if (! decodeFrames (*reader, offset, count, sample->left.data() + offset,
                           stereo ? sample->right.data() + offset : nullptr))
            return reject ("Could not decode the sample attack.");
    }
    if (streamed)
    {
        static const auto pool = std::make_shared<SampleReaderPool>();
        auto sharedPool = pool;
        const auto size = sample->sourceBytes, modified = sample->sourceTime;
        sample->stream = std::make_shared<StreamCache> (frames, stereo,
            [sharedPool, file, size, modified] (int first, int count, float* left, float* right)
            { return sharedPool->read (file, size, modified, first, count, left, right); });
        if (! sample->stream->prime (preload))
            return reject ("Could not read the first streaming page.");
        if (preload + streamPageFrames < frames && ! sample->stream->prime (preload + streamPageFrames))
            return reject ("Could not read the second streaming page.");
        StreamWorker::instance().add (sample->stream);
    }
    if(file.getSize()!=sizeBefore||file.getLastModificationTime().toMilliseconds()!=timeBefore)
        return reject("Sample changed during loading.");
    return sample;
    });
    if(cancelled&&cancelled->load()){output.reset();return Result::fail("Import cancelled.");}
    if(outcome==SharedSamplePool::Outcome::budgetExceeded)
        return Result::fail("Sampler memory budget reached (256 MiB per instrument, 768 MiB shared PCM/cache pool). Unload instruments or use smaller mappings.");
    if(outcome!=SharedSamplePool::Outcome::ready)return Result::fail(failure.isEmpty()?"Cannot load sample.":failure);
    if(file.getSize()!=sizeBefore||file.getLastModificationTime().toMilliseconds()!=timeBefore)
    {output.reset();return Result::fail("Sample changed during loading.");}
    return Result::ok();
}

bool primeZone (const Zone& z)
{
    if (! z.sample || ! z.sample->stream) return true;
    auto& stream = *z.sample->stream;
    const auto prime = [&] (int frame)
    {
        return stream.prime (frame)
            && (frame > z.sample->frameCount() - streamPageFrames - 1 || stream.prime (frame + streamPageFrames));
    };
    return prime (z.start) && (! z.loop || (prime (z.loopStart)
        && stream.prime (std::max (z.loopStart, z.loopEnd - 1))));
}

Result readEmbeddedSample (const SampleZone& entry, std::size_t budget, std::shared_ptr<const Sample>& output,
                           std::atomic<bool>* cancelled)
{
    const auto& source = *entry.soundFont;
    if (static_cast<std::size_t> (source.frames) > budget / sizeof (float))
        return Result::fail ("Embedded sample exceeds Studio's memory limit.");
    auto stream = entry.file.createInputStream();
    if (! stream) return Result::fail ("Missing SoundFont bank.");
    const auto chunk = soundFontPCM (*stream, soundFontEnd (*stream));
    if (! chunk || source.offset < chunk.offset || source.offset > chunk.offset + chunk.size
        || (source.offset - chunk.offset) % 2 != 0
        || source.frames * juce::int64 (2) > chunk.offset + chunk.size - source.offset
        || ! stream->setPosition (source.offset))
        return Result::fail ("Embedded sample exceeds the bank's PCM data. Rescan the SoundFont.");
    auto sample = std::make_shared<Sample>();
    sample->sourceBytes = entry.file.getSize(); sample->sourceTime = entry.file.getLastModificationTime().toMilliseconds();
    sample->rate = source.rate;
    sample->left.resize (static_cast<std::size_t> (source.frames));
    std::array<unsigned char, 8192> bytes {};
    for (int offset = 0; offset < source.frames; offset += static_cast<int> (bytes.size() / 2))
    {
        if (cancelled && cancelled->load()) return Result::fail ("Import cancelled.");
        const auto count = jmin (source.frames - offset, static_cast<int> (bytes.size() / 2));
        if (stream->read (bytes.data(), count * 2) != count * 2) return Result::fail ("Truncated SoundFont PCM.");
        for (int frame = 0; frame < count; ++frame)
            sample->left[static_cast<size_t> (offset + frame)] =
                static_cast<int16_t> (ByteOrder::littleEndianShort (bytes.data() + frame * 2)) / 32768.0f;
    }
    output = std::move (sample);
    return Result::ok();
}

bool currentSample (const SampleZone& z, const File& file, std::optional<SoundFontSample> source = {})
{
    return z.file == file && z.soundFont == source && z.mapping.sample && file.existsAsFile()
        && z.mapping.sample->sourceBytes == file.getSize()
        && z.mapping.sample->sourceTime == file.getLastModificationTime().toMilliseconds();
}

void prepareSamples (Instrument& next, const Instrument& current, std::atomic<bool>* cancelled = nullptr)
{
    for (auto& entry : next.zones)
    {
        if (cancelled && cancelled->load()) return;
        auto& sample = entry.mapping.sample;
        const auto reuse = [&] (const auto& zone) { return currentSample (zone, entry.file, entry.soundFont); };
        if (auto loaded = std::find_if (next.zones.begin(), next.zones.end(), reuse); loaded != next.zones.end())
            sample = loaded->mapping.sample;
        else if (auto previous = std::find_if (current.zones.begin(), current.zones.end(), reuse);
                 previous != current.zones.end() && previous->file.existsAsFile()
                    && previous->mapping.sample->bytes() <= maxBankBytes - sampleBytes (next))
            sample = previous->mapping.sample;
        else
        {
            const auto budget = maxBankBytes - sampleBytes (next);
            entry.problem = (entry.soundFont ? readEmbeddedSample (entry, budget, sample, cancelled)
                                            : readSample (entry.file, budget, sample, cancelled)).getErrorMessage();
        }

        if (! sample) continue;
        if (entry.mapping.start >= sample->frameCount() || entry.mapping.end > sample->frameCount())
        { entry.problem = "Sample boundaries exceed file length"; sample.reset(); continue; }
        const auto frames = sample->frameCount();
        if (entry.mapping.loopEnd == 0) entry.mapping.loopEnd = frames;
        if (entry.mapping.loop && ! validLoop (entry.mapping, frames))
        {
            entry.problem = "Loop is outside the sample. Adjust the loop in Loops.";
        }
        if (! primeZone (entry.mapping)) entry.problem = "Could not prime sample start or loop from disk.";
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
    const bool sameSwitches = std::ranges::equal (model.groups, next.groups, [] (const auto& a, const auto& b)
    {
        const auto& x = a.playback; const auto& y = b.playback;
        return a.id == b.id && x.trigger == y.trigger && x.key == y.key && x.controller == y.controller
            && x.ccLow == y.ccLow && x.ccHigh == y.ccHigh && x.channel == y.channel && x.releaseTrigger == y.releaseTrigger
             && x.switchDefault == y.switchDefault;
    });
    if (! sameSwitches) ++switchRevision;
    auto bank = std::make_unique<Bank>();
    bank->settings = next.settings;
    bank->sampleRevision = sampleRevision; bank->switchRevision = switchRevision;
    for (const auto& group : next.groups) { auto g = group.playback; g.id = group.id; bank->groups.push_back (g); }
    for (const auto& entry : next.zones) { auto z = entry.mapping; z.folder = entry.folder; bank->zones.push_back (std::move (z)); }

    bank->buildIndex();
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
    Instrument next; auto result = prepareSample (model, file, replace, next);
    if (result.wasOk()) change (std::move (next), true);
    return result;
}
Result Plugin::prepareSample (Instrument initial, const File& file, int replace, Instrument& next, std::atomic<bool>* cancelled)
{
    if (cancelled && cancelled->load()) return Result::fail ("Import cancelled.");
    const auto& model = initial;
    if (replace < -1 || replace >= static_cast<int> (model.zones.size()))
        return Result::fail ("Select a sample zone.");
    if (replace == -1 && model.zones.size() >= maxZones)
        return Result::fail ("This instrument has reached its zone limit.");

    next = model;
    SampleZone entry;
    entry.folder = model.groups.front().id;
    entry.file = file;
    entry.soundFont.reset();
    if (replace >= 0)
    {
        entry.folder = next.zones[static_cast<std::size_t> (replace)].folder;
        entry.mapping = next.zones[static_cast<std::size_t> (replace)].mapping;
        next.zones[static_cast<std::size_t> (replace)].mapping.sample.reset();
    }

    auto existing = std::find_if (next.zones.begin(), next.zones.end(), [&] (const auto& z)
    {
        return currentSample (z, file);
    });
    if (existing != next.zones.end())
        entry.mapping.sample = existing->mapping.sample;
    else if (const auto result = readSample (file, maxBankBytes - sampleBytes (next), entry.mapping.sample, cancelled); result.failed())
        return result;

    const auto frames = entry.mapping.sample->frameCount();
    if (entry.mapping.start >= frames || entry.mapping.end > frames) return Result::fail ("Replacement is shorter than the saved sample boundaries. Reset the boundaries first.");
    if (entry.mapping.loopEnd == 0) entry.mapping.loopEnd = frames;
    if (! validLoop (entry.mapping, frames))
    {
        if (entry.mapping.loop) return Result::fail ("The replacement is too short for the saved loop. Adjust or disable the loop first.");
        entry.mapping.loopStart = 0;
        entry.mapping.loopEnd = frames;
    }
    if (! primeZone (entry.mapping)) return Result::fail ("Could not prime replacement sample from disk.");
    if (replace >= 0)
        next.zones[static_cast<std::size_t> (replace)] = std::move (entry);
    else
        next.zones.push_back (std::move (entry));

    return Result::ok();
}

std::vector<studio::ArticulationMapping> Plugin::articulations() const
{
    std::vector<studio::ArticulationMapping> result;
    for (const auto& group : model.groups)
    {
        const auto& g = group.playback;
        if (g.trigger == GroupTrigger::normal || g.releaseTrigger) continue;
        studio::ArticulationMapping mapping { "sampler-group-" + String (group.id), group.name,
            g.trigger == GroupTrigger::keyswitch ? studio::ArticulationMessage::keyswitch : studio::ArticulationMessage::controller,
            g.channel ? g.channel : 1, g.trigger == GroupTrigger::keyswitch ? g.key : g.controller,
            g.trigger == GroupTrigger::keyswitch ? 100 : g.ccLow, 0 };
        if (studio::validArticulation (mapping)) result.push_back (std::move (mapping));
    }
    return result;
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
    std::array<bool, maxGroups> used {};
    for (auto& g : groups)
    {
        g.playback.id = g.id;
        if (! validGroup (g.playback) || used[static_cast<std::size_t> (g.id)]) return;
        used[static_cast<std::size_t> (g.id)] = true;
        if (g.name.trim().isEmpty()) g.name = "Group";
    }
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
Result Plugin::proposeImport (const juce::Array<File>& files, bool automatic, Instrument& next, String& summary)
{
    return prepareImport (model, files, automatic, next, summary);
}
Result Plugin::prepareImport (Instrument initial, const juce::Array<File>& paths, bool automatic, Instrument& next, String& summary, std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    next = std::move (initial);
    juce::Array<File> files;
    const auto aborted = [&] { return cancelled && cancelled->load(); };
    for (const auto& f : paths)
    {
        if (aborted()) return Result::fail ("Import cancelled.");
        if (f.isDirectory())
            for (const auto& item : RangedDirectoryIterator (f, true, "*", File::findFiles, File::FollowSymlinks::no))
            { if (aborted()) return Result::fail ("Import cancelled."); files.add (item.getFile()); }
        else files.add (f);
    }
    std::sort (files.begin(), files.end(), [] (const File& a, const File& b) { return a.getFullPathName() < b.getFullPathName(); });
    int completed = 0;
    int accepted = 0, unsupported = 0, provisional = 0;
    StringArray errors;
    std::vector<Zone> mapping;
    for (const auto& file : files)
    {
        if (aborted()) return Result::fail ("Import cancelled.");
        if (progress) progress->store (++completed * 100 / jmax (1, files.size()));
        if (! file.hasFileExtension ("wav;wave;aif;aiff;aifc;caf;flac")) { ++unsupported; continue; }
        if (next.zones.size() >= maxZones) { errors.add (file.getFileName() + ": zone limit (" + String(maxZones) + ")"); continue; }
        SampleZone entry; entry.folder = next.groups.front().id; entry.file = file;
        auto cached = std::find_if (next.zones.begin(), next.zones.end(), [&] (const auto& z) { return currentSample (z, file); });
        if (cached != next.zones.end()) entry.mapping.sample = cached->mapping.sample;
        else if (auto result = readSample (file, maxBankBytes - sampleBytes (next), entry.mapping.sample, cancelled); result.failed())
        { errors.add (file.getFileName() + ": " + result.getErrorMessage()); continue; }
        entry.mapping.loopEnd = entry.mapping.sample->frameCount();
        if (auto note = filenameNote (file.getFileName().toStdString())) entry.mapping.root = *note;
        else if (entry.mapping.sample->rootNote >= 0) entry.mapping.root = entry.mapping.sample->rootNote;
        else { entry.mapping.root = jlimit (0, 127, 48 + accepted); ++provisional; }
        if (auto velocity = filenameVelocity (file.getFileName().toStdString())) { entry.mapping.soft = velocity->first; entry.mapping.hard = velocity->second; }
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
            + ", keys " + String (mapping[i].low) + "-" + String (mapping[i].high) + "\n";
    }
    summary += errors.joinIntoString ("\n");
    return accepted ? Result::ok() : Result::fail (summary);
}

Result Plugin::prepareGuided (const GuidedBuild& options, const juce::Array<File>& paths, Instrument& output,
                              String& report, std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    const auto aborted=[&]{return cancelled&&cancelled->load();};
    const auto fail=[&](String why){report=why;return Result::fail(why);};
    if(aborted())return fail("Cancelled.");
    if(options.name.trim().isEmpty()||options.name.length()>120||options.firstNote<0||options.firstNote>127)
        return fail("Enter a name and a valid first note (0–127).");
    const bool drums=options.layout==GuidedBuild::Layout::percussion;
    if(drums&&options.roundRobin)return fail("Round robin is available for pitched recordings with matching root/velocity ranges.");
    juce::Array<File> files;
    int visited=0, ignored=0;
    const auto consider=[&](const File& file)
    {
        if(file.hasFileExtension("wav;wave;aif;aiff;aifc;caf;flac")&&file.existsAsFile()&&!file.isSymbolicLink())
            files.addIfNotAlreadyThere(file);
        else ++ignored;
    };
    for(const auto& path:paths)
    {
        if(aborted())return fail("Cancelled.");
        if(path.isDirectory())
        {
            if(path.isSymbolicLink()||path==File::getSpecialLocation(File::userHomeDirectory)||path.getParentDirectory()==path)
                return fail("Choose a specific recordings folder.");
            for(const auto& item:RangedDirectoryIterator(path,true,"*",File::findFiles,File::FollowSymlinks::no))
            {
                if(aborted())return fail("Cancelled.");
                if(++visited>20000)return fail("Folder exceeds 20,000 entries. Choose a smaller folder.");
                consider(item.getFile());
                if(files.size()>maxZones)return fail("Too many recordings for one instrument.");
            }
        }
        else consider(path);
    }
    if(files.isEmpty()||files.size()>maxZones)return fail("Choose 1–"+String(maxZones)+" supported recordings.");
    if(drums&&files.size()>128-options.firstNote)return fail("Not enough MIDI keys above the first drum note.");
    std::sort(files.begin(),files.end(),[](const File& a,const File& b){
        const int order=a.getFileName().compareNatural(b.getFileName());
        return order!=0?order<0:a.getFullPathName()<b.getFullPathName();
    });
    Instrument proposed;proposed.name=options.name.trim();
    if(drums)proposed.groups.front().name="Percussion";
    using Layer=std::pair<int,int>;
    std::map<int,std::map<Layer,std::vector<std::size_t>>> layers;
    StringArray details;
    int explicitRoots=0;
    for(const auto& file:files)
    {
        if(aborted())return fail("Cancelled.");
        SampleZone entry;entry.file=file;
        auto& z=entry.mapping;
        if(auto r=readSample(file,maxBankBytes-sampleBytes(proposed),z.sample,cancelled);r.failed())
            return fail(file.getFileName()+": "+r.getErrorMessage());
        if(drums)z.root=options.firstNote+static_cast<int>(proposed.zones.size());
        else
        {
            const auto note=filenameNote(file.getFileName().toStdString());
            const int embedded=z.sample->rootNote;
            if(note&&embedded>=0&&*note!=embedded)
                return fail(file.getFileName()+": filename and embedded root disagree. Use Build > Mapping to resolve.");
            if(note||embedded>=0){z.root=note?*note:embedded;++explicitRoots;}
            else if(files.size()==1)z.root=options.firstNote;
            else return fail(file.getFileName()+": root note unknown. Rename with a note such as C4, or map in Build.");
            if(auto velocity=filenameVelocity(file.getFileName().toStdString()))
            {z.soft=velocity->first;z.hard=velocity->second;}
        }
        z.low=z.high=z.root;
        z.loopEnd=z.sample->frameCount();
        z.oneShot=options.playback==GuidedBuild::Playback::oneShot;
        if(options.playback==GuidedBuild::Playback::embeddedLoop)
        {
            z.loop=true;z.loopStart=z.sample->embeddedLoopStart;z.loopEnd=z.sample->embeddedLoopEnd;
            if(z.loopStart<0||z.loopEnd<0||!validLoop(z,z.sample->frameCount()))
                return fail(file.getFileName()+": no valid embedded forward loop. Choose Held or set a loop in Build.");
        }
        if(!validMapping(z)||!primeZone(z))return fail(file.getFileName()+": cannot prepare mapping or stream.");
        layers[z.root][{z.soft,z.hard}].push_back(proposed.zones.size());
        proposed.zones.push_back(std::move(entry));
        if(progress)progress->store(static_cast<int>(proposed.zones.size())*100/files.size());
    }
    int rr=0, gaps=0;
    for(const auto& [root, ranges]:layers)
    {
        int last=0;
        for(const auto& [range,indices]:ranges)
        {
            if(range.first<=last)return fail("Overlapping velocity layers at MIDI note "+String(root)+". Use non-overlapping ranges or edit Build.");
            if(range.first>last+1)++gaps;
            last=range.second;
            if(indices.size()>1)
            {
                if(!options.roundRobin)return fail("Multiple recordings share a root and velocity range at MIDI note "+String(root)+". Enable alternate takes or define layers.");
                if(++rr>maxGroups)return fail("More than "+String(maxGroups)+" independent alternate-take groups. Use Build for a smaller instrument.");
                for(auto i:indices)proposed.zones[i].mapping.group=rr;
            }
        }
        if(last<127)++gaps;
    }
    if(!drums&&options.spread)
    {
        std::vector<Zone> mapping;mapping.reserve(proposed.zones.size());
        for(const auto& e:proposed.zones)mapping.push_back(e.mapping);
        spreadMapping(mapping);
        for(std::size_t i=0;i<mapping.size();++i)proposed.zones[i].mapping=std::move(mapping[i]);
    }
    if(aborted())return fail("Cancelled.");
    for(const auto& e:proposed.zones)
        if(!currentSample(e,e.file))return fail(e.file.getFileName()+": recording changed while preparing.");
    report=proposed.name+"\n"+String(files.size())+" recordings; "+String(rr)+" alternate-take groups; "+String(ignored)+" unsupported/non-audio entries ignored.\n";
    report+=drums?"Each recording uses one successive key, in natural filename order.\n":
        (String(explicitRoots)+" roots read from names/metadata. "+(files.size()==1&&explicitRoots==0?String("Single recording uses your chosen root. "):String())+"No acoustic pitch detection.\n");
    if(gaps)report+="Warning: "+String(gaps)+" velocity gaps will be silent.\n";
    if(rr)report+="Alternate takes cycle in natural filename order.\n";
    report+="Review the key and velocity ranges below. C4 = MIDI 60. Original files are referenced, not modified.\n\n";
    for(const auto& e:proposed.zones)
    {
        const auto& z=e.mapping;
        report+=e.file.getFileName()+" | root "+MidiMessage::getMidiNoteName(z.root,true,true,4)
            +" | keys "+String(z.low)+"–"+String(z.high)+" | velocity "+String(z.soft)+"–"+String(z.hard)
            +(z.group?" | alternate group "+String(z.group):String())+"\n";
    }
    output=std::move(proposed);
    return Result::ok();
}

Result Plugin::prepareSfz (const File& definition, const Instrument& cache, Instrument& next, String& report,
                           std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    const auto aborted = [&] { return cancelled && cancelled->load(); };
    if (aborted()) return Result::fail ("Import cancelled.");
    if (! definition.existsAsFile() || ! definition.hasFileExtension ("sfz"))
        return Result::fail ("Choose an existing SFZ instrument.");
    const auto pathToFile = [] (const sfz::Path& path)
    {
        const auto utf8 = path.u8string();
        return File (String::fromUTF8 (reinterpret_cast<const char*> (utf8.data()), static_cast<int> (utf8.size())));
    };
    sfz::Document document;
    try
    {
        document = sfz::Parser().parse (sfz::path (definition.getFullPathName().toStdString()),
            [&] (const sfz::Path& path) -> std::optional<std::string>
            {
                if (aborted()) return {};
                const auto file = pathToFile (path);
                if (! file.existsAsFile() || file.getSize() > 8 * 1024 * 1024) return {};
                MemoryBlock bytes;
                if (! file.loadFileAsData (bytes)) return {};
                if (bytes.getSize() == 0) return std::string();
                return std::string (static_cast<const char*> (bytes.getData()), bytes.getSize());
            }, maxZones);
    }
    catch (const std::exception& e) { return Result::fail ("Cannot parse SFZ: " + String (e.what())); }
    if (aborted()) return Result::fail ("Import cancelled.");
    if (! document.error.empty()) return Result::fail (String (document.error));
    Instrument converted;
    converted.name = definition.getFileNameWithoutExtension().substring (0, 120);
    converted.settings.gainDb = 0;
    converted.groups.clear();
    std::set<std::string> warnings = std::move (document.warnings);
    std::map<std::string, int> groupIds;
    struct Sequence { int length = 0; std::map<int, int> positions; };
    std::map<std::string, Sequence> sequences;
    int playable = 0, missing = 0, omitted = 0, streamed = 0;
    const std::set<std::string> supported {
        "sample", "default_path", "lokey", "hikey", "pitch_keycenter", "lovel", "hivel",
        "offset", "end", "loop_mode", "loop_start", "loop_end", "loop_crossfade", "loop_type",
        "volume", "tune", "transpose", "pitch_keytrack", "pan", "amp_veltrack",
        "ampeg_attack", "ampeg_decay", "ampeg_sustain", "ampeg_release",
        "xfin_lovel", "xfin_hivel", "xfout_lovel", "xfout_hivel", "xf_velcurve",
        "trigger", "seq_length", "seq_position", "sw_last", "sw_default", "sw_lokey", "sw_hikey",
        "sw_label", "group_label", "region_label", "lochan", "hichan"
    };
    for (std::size_t n = 0; n < document.regions.size(); ++n)
    {
        if (aborted()) return Result::fail ("Import cancelled.");
        if (progress) progress->store (static_cast<int> (100 * (n + 1) / document.regions.size()));
        const auto& o = document.regions[n].ops;
        std::string invalid;
        const auto prefix = "Region " + std::to_string (n + 1) + ": ";
        const auto warn = [&] (std::string reason) { warnings.insert (std::move (reason)); };
        const auto has = [&] (const std::string& k) { return o.count (k) != 0; };
        const auto get = [&] (const std::string& k, std::string fallback = {}) { return sfz::get (o, k, std::move (fallback)); };
        const auto integer = [&] (const std::string& k, int fallback, int lo, int hi)
        {
            if (! has (k)) return fallback;
            const auto v = sfz::integer (get (k));
            if (! v || *v < lo || *v > hi) { if (invalid.empty()) invalid = k + "=" + get (k); return fallback; }
            return *v;
        };
        const auto number = [&] (const std::string& k, double fallback, double lo, double hi)
        {
            if (! has (k)) return static_cast<float> (fallback);
            const auto v = sfz::number (get (k));
            if (! v || *v < lo || *v > hi) { if (invalid.empty()) invalid = k + "=" + get (k); return static_cast<float> (fallback); }
            return static_cast<float> (*v);
        };
        const auto note = [&] (const std::string& k, int fallback)
        {
            if (! has (k)) return fallback;
            const auto v = sfz::note (get (k));
            if (! v) { if (invalid.empty()) invalid = k + "=" + get (k); return fallback; }
            return *v;
        };
        std::set<int> controllers;
        bool unsupportedCondition = false;
        for (const auto& [key, value] : o)
        {
            if (supported.count (key)) continue;
            if (key.rfind ("locc", 0) == 0 || key.rfind ("hicc", 0) == 0)
            {
                const auto cc = sfz::integer (std::string_view (key).substr (4));
                if (cc && *cc >= 0 && *cc < 120) { controllers.insert (*cc); continue; }
                unsupportedCondition = true;
            }
            // Never turn an unsupported gating condition into an always-on layer.
            if (key.rfind ("sw_", 0) == 0 || key.rfind ("on_", 0) == 0
                || key.rfind ("start_", 0) == 0 || key.rfind ("lo", 0) == 0 || key.rfind ("hi", 0) == 0
                || key == "group" || key == "off_by" || key == "off_mode" || key == "count")
                unsupportedCondition = true;
            warn ("Unsupported opcode: " + key);
        }
        const auto trigger = sfz::lower (get ("trigger", "attack"));
        if (unsupportedCondition || (trigger != "attack" && trigger != "release")
            || controllers.size() > 1 || (! controllers.empty() && has ("sw_last")))
        {
            ++omitted;
            warn (prefix + "omitted: unsupported or combined trigger/condition (including choke groups).");
            continue;
        }
        const auto sampleName = get ("sample");
        if (sampleName.empty() || sampleName.front() == '*')
        {
            ++omitted; warn (prefix + "omitted: missing sample opcode or unsupported generated sample."); continue;
        }
        SampleZone entry;
        auto& z = entry.mapping;
        z.sfz.enabled = true;
        auto& x = z.sfz;
        z.root = note ("pitch_keycenter", 60);
        z.low = note ("lokey", 0); z.high = note ("hikey", 127);
        z.soft = std::max (1, integer ("lovel", 1, 0, 127)); z.hard = integer ("hivel", 127, 1, 127);
        z.gainDb = number ("volume", 0, -60, 12); z.cents = number ("tune", 0, -100, 100);
        x.transpose = integer ("transpose", 0, -48, 48); x.keyTrack = integer ("pitch_keytrack", 100, 0, 200);
        x.pan = number ("pan", 0, -100, 100); x.velocityTrack = number ("amp_veltrack", 100, -100, 100);
        x.attack = number ("ampeg_attack", 0, 0, 100); x.decay = number ("ampeg_decay", 0, 0, 100);
        x.sustain = number ("ampeg_sustain", 100, 0, 100) / 100;
        x.release = number ("ampeg_release", 0.001, 0, 100);
        x.fadeInLow = integer ("xfin_lovel", 0, 0, 127); x.fadeInHigh = integer ("xfin_hivel", 0, 0, 127);
        x.fadeOutLow = integer ("xfout_lovel", 127, 0, 127); x.fadeOutHigh = integer ("xfout_hivel", 127, 0, 127);
        const auto curve = sfz::lower (get ("xf_velcurve", "power"));
        x.powerFade = curve != "gain";
        if (curve != "power" && curve != "gain") invalid = "xf_velcurve=" + curve;
        z.start = integer ("offset", 0, 0, std::numeric_limits<int>::max() - 2);
        if (has ("end")) z.end = integer ("end", 0, 0, std::numeric_limits<int>::max() - 2) + 1;
        const auto mode = sfz::lower (get ("loop_mode"));
        if (! mode.empty() && mode != "no_loop" && mode != "one_shot" && mode != "loop_continuous" && mode != "loop_sustain")
            invalid = "loop_mode=" + mode;
        if (sfz::lower (get ("loop_type", "forward")) != "forward")
        { ++omitted; warn (prefix + "omitted: non-forward loop."); continue; }
        z.oneShot = mode == "one_shot" || trigger == "release";
        z.loop = ! z.oneShot && (mode == "loop_continuous" || mode == "loop_sustain");
        x.sustainLoop = mode == "loop_sustain";
        if (has ("loop_start")) z.loopStart = integer ("loop_start", 0, 0, std::numeric_limits<int>::max() - 2);
        if (has ("loop_end")) z.loopEnd = integer ("loop_end", 0, 0, std::numeric_limits<int>::max() - 2) + 1;
        z.loopCrossfade = 0; // SFZ does not add Studio's legacy 64-frame smoothing.
        const auto crossfade = number ("loop_crossfade", 0, 0, 10);

        Group g;
        g.releaseTrigger = trigger == "release";
        const int lowChannel = integer ("lochan", 1, 1, 16), highChannel = integer ("hichan", 16, 1, 16);
        if (lowChannel == highChannel) g.channel = lowChannel;
        else if (lowChannel != 1 || highChannel != 16)
        { ++omitted; warn (prefix + "omitted: channel range not representable."); continue; }
        if (has ("sw_last"))
        {
            g.trigger = GroupTrigger::keyswitch;
            g.key = note ("sw_last", 24);
            g.switchDefault = note ("sw_default", -1);
            const auto lo = note ("sw_lokey", g.key), hi = note ("sw_hikey", g.key);
            if (g.key < lo || g.key > hi) invalid = "sw_last outside switch range";
        }
        else if (! controllers.empty())
        {
            g.trigger = GroupTrigger::controller;
            g.controller = *controllers.begin();
            const auto suffix = std::to_string (g.controller);
            g.ccLow = integer ("locc" + suffix, 0, 0, 127);
            g.ccHigh = integer ("hicc" + suffix, 127, 0, 127);
        }
        const int sequenceLength = integer ("seq_length", 1, 1, 100);
        const int sequencePosition = integer ("seq_position", 1, 1, 100);
        if (sequencePosition > sequenceLength) invalid = "seq_position exceeds seq_length";
        if (! invalid.empty() || ! validMapping (z) || ! validGroup (g)
            || (z.end && z.end <= z.start))
            return Result::fail (String (prefix + "invalid or out-of-range mapping: " + invalid));

        const auto groupKey = std::to_string (static_cast<int> (g.trigger)) + ":" + std::to_string (g.key)
            + ":" + std::to_string (g.switchDefault) + ":" + std::to_string (g.controller)
            + ":" + std::to_string (g.ccLow) + ":" + std::to_string (g.ccHigh)
            + ":" + std::to_string (g.channel) + ":" + std::to_string (g.releaseTrigger)
            + ":" + get ("group_label") + ":" + get ("sw_label");
        auto existingGroup = groupIds.find (groupKey);
        if (existingGroup == groupIds.end())
        {
            if (converted.groups.size() >= maxGroups)
                return Result::fail ("SFZ exceeds Studio's 16 articulation-group limit. No changes made.");
            g.id = static_cast<int> (converted.groups.size());
            const auto label = get ("sw_label", get ("group_label"));
            String name (label);
            if (name.isEmpty())
                name = g.trigger == GroupTrigger::keyswitch ? "Keyswitch " + String (g.key)
                     : g.trigger == GroupTrigger::controller ? "CC " + String (g.controller) + " " + String (g.ccLow) + "-" + String (g.ccHigh)
                     : g.releaseTrigger ? "Release" : "Main";
            converted.groups.push_back ({ g.id, name.substring (0, 120), g });
            groupIds.emplace (groupKey, g.id);
            entry.folder = g.id;
        }
        else entry.folder = existingGroup->second;
        z.folder = entry.folder;

        try
        {
            const auto samplePath = sfz::path (sampleName);
            const auto base = sfz::path (definition.getParentDirectory().getFullPathName().toStdString());
            const auto defaultPath = sfz::path (get ("default_path"));
            // Absolute sample paths override default_path; relative paths
            // resolve against the main SFZ definition, including includes.
            entry.file = pathToFile ((base / defaultPath / samplePath).lexically_normal());
        }
        catch (const std::exception&) { return Result::fail (String (prefix + "invalid sample path.")); }
        const auto reuse = [&] (const auto& e) { return currentSample (e, entry.file); };
        auto found = std::find_if (converted.zones.begin(), converted.zones.end(), reuse);
        if (found != converted.zones.end()) z.sample = found->mapping.sample;
        else if (auto old = std::find_if (cache.zones.begin(), cache.zones.end(), reuse);
                 old != cache.zones.end() && old->mapping.sample->bytes() <= maxBankBytes - sampleBytes (converted))
            z.sample = old->mapping.sample;
        else if (! entry.file.hasFileExtension ("wav;wave;aif;aiff;aifc;caf;flac"))
            entry.problem = "Unsupported sample encoding (WAV/AIFF or supported macOS CAF required)";
        else if (auto result = readSample (entry.file, maxBankBytes - sampleBytes (converted), z.sample, cancelled); result.failed())
            entry.problem = result.getErrorMessage();
        if (aborted()) return Result::fail ("Import cancelled.");
        if (z.sample)
        {
            const int frames = z.sample->frameCount();
            if (z.start >= frames || z.end > frames)
                return Result::fail (String (prefix + "sample start/end exceeds recording length."));
            if (mode.empty() && ! z.oneShot && z.sample->embeddedLoopStart >= 0) z.loop = true;
            if (z.loop)
            {
                if (! has ("loop_start")) z.loopStart = std::max (z.start, z.sample->embeddedLoopStart);
                if (! has ("loop_end")) z.loopEnd = z.sample->embeddedLoopEnd > 0 ? z.sample->embeddedLoopEnd : (z.end ? z.end : frames);
                if (! validLoop (z, frames)) return Result::fail (String (prefix + "invalid loop bounds."));
                z.loopCrossfade = std::min (1000000, roundToInt (crossfade * z.sample->rate));
                if (z.loopCrossfade > (z.loopEnd - z.loopStart) / 4)
                    warn (prefix + "loop crossfade limited by native loop length.");
            }
            else if (z.loopEnd == 0) z.loopEnd = frames;
            if (primeZone (z)) { ++playable; if (z.sample->stream) ++streamed; }
            else { entry.problem = "Could not preload streamed start or loop"; z.sample.reset(); }
        }
        else if (z.loop)
        {
            // A missing recording with an unresolved implicit loop cannot
            // be safely persisted with fabricated loop boundaries.
            if (z.loopEnd <= z.loopStart + 1)
            {
                z.loop = false; x.sustainLoop = false;
                warn (prefix + "missing sample: implicit loop must be restored after relinking.");
            }
        }
        if (! z.sample)
        {
            ++missing;
            warn (prefix + entry.file.getFullPathName().toStdString() + ": " + entry.problem.toStdString());
        }

        if (sequenceLength > 1)
        {
            const auto sequenceKey = groupKey + ":" + std::to_string (z.low) + ":" + std::to_string (z.high)
                + ":" + std::to_string (z.soft) + ":" + std::to_string (z.hard) + ":" + std::to_string (sequenceLength);
            auto& sequence = sequences[sequenceKey];
            sequence.length = sequenceLength;
            if (! sequence.positions.emplace (sequencePosition, static_cast<int> (converted.zones.size())).second)
                return Result::fail (String (prefix + "duplicate round-robin position; overlapping sequence layers are not supported."));
        }
        converted.zones.push_back (std::move (entry));
    }
    if (aborted()) return Result::fail ("Import cancelled.");
    if (playable == 0)
    {
        report = "No playable SFZ regions. " + String (missing) + " unavailable, " + String (omitted) + " omitted.\n";
        for (const auto& warning : warnings) report += String (warning) + "\n";
        return Result::fail (report);
    }
    if (sequences.size() > maxGroups)
        return Result::fail ("SFZ exceeds Studio's 16 independent round-robin sequences.");
    // Preserve seq_position order regardless of the source text ordering.
    // The native counter is per sequence group (not a complete SFZ state machine).
    int rr = 0;
    std::vector<SampleZone> ordered;
    std::set<int> sequenced;
    for (const auto& [key, sequence] : sequences)
    {
        if (static_cast<int> (sequence.positions.size()) != sequence.length)
            return Result::fail ("Incomplete SFZ round-robin sequence. No instrument was replaced.");
        ++rr;
        for (const auto& [position, index] : sequence.positions)
        {
            auto& entry = converted.zones[static_cast<std::size_t> (index)];
            if (! entry.mapping.sample)
                return Result::fail ("Round-robin sequence contains an unavailable sample. Relink the source before importing.");
            entry.mapping.group = rr;
            sequenced.insert (index);
            ordered.push_back (std::move (entry));
        }
    }
    if (! sequences.empty())
    {
        for (int i = 0; i < static_cast<int> (converted.zones.size()); ++i)
            if (! sequenced.count (i)) ordered.push_back (std::move (converted.zones[static_cast<std::size_t> (i)]));
        converted.zones = std::move (ordered);
        warnings.insert ("Round robin uses Studio's sequence-group counter; complex SFZ region counters are not emulated.");
    }
    warnings.insert ("Studio uses native linear ADSR and stereo balance; exact SFZ-player envelope/pan curves and CC behavior can differ.");
    warnings.insert ("WAV forward-loop metadata is read; implicit AIFF loop markers are not converted.");
    report = "SFZ: " + converted.name + "\n" + String (playable) + " playable regions | "
        + String (streamed) + " streamed | " + String (missing) + " unavailable | " + String (omitted)
        + " omitted.\n" + String (converted.groups.size()) + " articulation groups. "
        + String (sequences.size()) + " round-robin sequences.\n"
        + "Import replaces this sampler instrument only after confirmation. Source files are not modified.\n\n";
    if (! warnings.empty()) report += "Compatibility notes (review before import):\n";
    for (const auto& warning : warnings) report += String (warning) + "\n";
    for (const auto& entry : converted.zones)
        report += entry.file.getFileName() + ": root " + String (entry.mapping.root)
            + ", keys " + String (entry.mapping.low) + "-" + String (entry.mapping.high)
            + (entry.problem.isNotEmpty() ? " | " + entry.problem : String()) + "\n";
    next = std::move (converted);
    return Result::ok();
}

Result Plugin::prepareExs (const File& definition, const File& sampleRoot, const Instrument& cache,
                           Instrument& next, String& report, std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    const auto aborted = [&] { return cancelled && cancelled->load(); };
    if (aborted()) return Result::fail ("Import cancelled.");
    if (! definition.existsAsFile() || ! definition.hasFileExtension ("exs") || definition.getSize() > 32*1024*1024)
        return Result::fail ("Select a readable binary EXS instrument (maximum 32 MiB).");
    if (sampleRoot != File() && ! sampleRoot.isDirectory())
        return Result::fail ("The selected sample folder no longer exists.");
    MemoryBlock data;
    if (! definition.loadFileAsData (data) || data.getSize() < 84 || data.getSize() > 32*1024*1024)
        return Result::fail ("Cannot read EXS instrument.");
    exs::Document source;
    try
    {
        const auto* p = static_cast<const unsigned char*> (data.getData());
        source = exs::Parser::parse (exs::Bytes (p,p+data.getSize()), maxZones);
    }
    catch (const std::exception& e) { return Result::fail ("EXS: " + String (e.what())); }
    data.reset();
    auto warnings = std::move (source.warnings);
    const auto base = definition.getParentDirectory();
    const auto normalized = [] (const std::string& s) { return String (s).replaceCharacter ('\\','/'); };
    // A bounded local index is built lazily only when stored paths fail.
    // Never scan an entire disk, follow symlinks or pick among duplicate names.
    std::map<String, std::vector<File>> localFiles;
    bool indexed = false, truncated = false;
    auto indexLocal = [&]
    {
        if (indexed) return;
        indexed = true;
        juce::Array<File> roots;
        if (sampleRoot.isDirectory()) roots.add (sampleRoot);
        else
        {
            roots.add (base);
            for (const auto& name : { "Samples", "Sampler Files", "EXSamples" })
            {
                const auto folder = base.getParentDirectory().getChildFile (name);
                if (folder.isDirectory()) roots.addIfNotAlreadyThere (folder);
            }
        }
        int count = 0;
        std::set<String> seen;
        for (const auto& root : roots)
        {
            if (root == File::getSpecialLocation (File::userHomeDirectory) || root.getParentDirectory() == root)
            { truncated = true; continue; }
            for (const auto& item : RangedDirectoryIterator (root, true, "*", File::findFiles, File::FollowSymlinks::no))
            {
                if (aborted()) return;
                if (++count > 20000) { truncated = true; return; }
                const auto file = item.getFile();
                if (! file.hasFileExtension ("wav;wave;aif;aiff;aifc;caf;flac")) continue;
                if (seen.insert (file.getFullPathName()).second)
                    localFiles[file.getFileName().toLowerCase()].push_back (file);
            }
        }
    };
    struct Resolved { File file; String problem; };
    auto resolve = [&] (const exs::Source& sample) -> Resolved
    {
        const auto filename = normalized (sample.filename);
        const auto name = filename.fromLastOccurrenceOf ("/", false, false);
        const auto directory = normalized (sample.directory);
        const auto fallback = base.getChildFile (name);
        if (name.isEmpty() || name == "." || name == "..") return { fallback, "Invalid sample name" };
        // Prefer exact stored paths over basename matching. A user-chosen
        // relocated folder takes precedence over an obsolete stored path.
        const auto relative = [&](const File& folder, const String& path) -> File
        {
            if (path.isEmpty() || File::isAbsolutePath (path)) return {};
            const auto candidate = folder.getChildFile (path);
            return candidate.isAChildOf (folder) ? candidate : File();
        };
        juce::Array<File> candidates;
        if (sampleRoot.isDirectory())
        {
            candidates.add (relative (sampleRoot, filename));
            candidates.add (sampleRoot.getChildFile (name));
        }
        if (File::isAbsolutePath (filename)) candidates.add (File (filename));
        if (File::isAbsolutePath (directory)) candidates.add (File (directory).getChildFile (name));
        else if (auto folder = relative (base, directory); folder != File()) candidates.add (folder.getChildFile (name));
        candidates.add (relative (base, filename)); candidates.add (fallback);
        for (const auto& candidate : candidates)
            if (candidate != File() && candidate.existsAsFile()) return { candidate, {} };
        indexLocal();
        if (truncated) return { fallback, "Local sample search limit reached; choose a narrower folder or locate sample" };
        const auto it = localFiles.find (name.toLowerCase());
        if (it != localFiles.end())
        {
            if (it->second.size() == 1) return { it->second.front(), {} };
            return { fallback, "Ambiguous sample filename; locate the original recording manually" };
        }
        return { fallback, "Sample not found; use Locate Sample or choose a relocated sample folder" };
    };
    std::map<int,Resolved> resolved;
    std::map<int,int> groupIds;
    Instrument converted;
    converted.name = definition.getFileNameWithoutExtension().substring (0,120);
    converted.settings.gainDb = -6;
    converted.groups.clear();
    int playable=0, missing=0, omitted=0, streaming=0;
    struct Sequence { std::map<int,int> positions; };
    std::map<String,Sequence> sequences;
    std::set<int> expectedRR;
    for (const auto& [id,g] : source.groups)
        if (g.condition==2 && !g.mute && !g.unsupported)
            if (!expectedRR.insert(g.sequence).second)
                return Result::fail ("EXS has multiple round-robin groups sharing a sequence position; cannot safely merge them.");
    if (!expectedRR.empty())
    {
        int pos=*expectedRR.begin();
        if (pos != -1 && pos != 0) return Result::fail ("Unsupported EXS round-robin start position.");
        for (int n:expectedRR) if(n!=pos++) return Result::fail ("Incomplete EXS round-robin group positions.");
    }

    for (std::size_t n=0;n<source.regions.size();++n)
    {
        if (aborted()) return Result::fail ("Import cancelled.");
        if (progress) progress->store (static_cast<int>(100*(n+1)/source.regions.size()));
        const auto& r=source.regions[n];
        const auto prefix="Region "+std::to_string(n+1)+": ";
        exs::Group g;
        if (r.group>=0) g=source.groups.at(r.group);
        if (r.unsupported || g.unsupported || r.source<0)
        { ++omitted; warnings.insert(prefix+"unsupported playback rule or empty zone omitted."); continue; }
        SampleZone entry;
        auto& z=entry.mapping;
        z.root=r.root; z.low=std::max(r.low,g.low); z.high=std::min(r.high,g.high);
        z.soft=std::max(r.soft,g.soft); z.hard=std::min(r.hard,g.hard);
        if (z.low>z.high || z.soft>z.hard)
        { ++omitted; warnings.insert(prefix+"empty intersection of group and zone ranges."); continue; }
        z.start=r.start; z.end=r.end;
        z.oneShot=r.oneShot; z.loop=r.loop; z.loopStart=r.loopStart; z.loopEnd=r.loopEnd;
        z.loopCrossfade=0;
        z.sfz.enabled=true; z.sfz.sustainLoop=r.releaseTail; z.sfz.keyTrack=r.pitch?100:0;
        z.sfz.attack=converted.settings.attack; z.sfz.decay=converted.settings.decay;
        z.sfz.sustain=converted.settings.sustain; z.sfz.release=converted.settings.release;
        const int cents=r.fine+100*r.coarse;
        z.sfz.transpose=cents/100; z.cents=static_cast<float>(cents%100);
        z.gainDb=static_cast<float>(std::clamp(r.gain+g.volume,-60,12));
        z.sfz.pan=static_cast<float>(std::clamp((r.pan+g.pan)*2,-100,100));
        if(r.gain+g.volume < -60 || r.gain+g.volume > 12)
            warnings.insert("Combined group and zone gain limited to native range.");
        if (!validMapping(z)) return Result::fail (String(prefix+"unsupported tuning or native mapping."));
        auto existing=groupIds.find(r.group);
        if(existing==groupIds.end())
        {
            if(converted.groups.size()>=maxGroups) return Result::fail("EXS exceeds Studio's 16 group limit.");
            const int id=static_cast<int>(converted.groups.size());
            Group playback; playback.id=id; playback.mute=g.mute; playback.releaseTrigger=g.release;
            if(g.condition==1){playback.trigger=GroupTrigger::keyswitch;playback.key=g.key;playback.switchDefault=-1;}
            else if(g.condition==3){playback.trigger=GroupTrigger::controller;playback.controller=g.controller;playback.ccLow=g.ccLow;playback.ccHigh=g.ccHigh;}
            else if(g.condition==5)playback.channel=g.channel;
            if(!validGroup(playback))return Result::fail("Invalid EXS group mapping.");
            auto name=String(g.name).trim();
            if(name.isEmpty())name=r.group<0?"Ungrouped":"Group "+String(r.group);
            converted.groups.push_back({id,name.substring(0,120),playback});
            groupIds.emplace(r.group,id); entry.folder=id;
        }
        else entry.folder=existing->second;
        z.folder=entry.folder;
        const auto& sample=source.samples.at(r.source);
        if (!resolved.count(r.source)) resolved.emplace(r.source,resolve(sample));
        const auto& found=resolved.at(r.source);
        entry.file=found.file; entry.problem=found.problem;
        if (aborted())return Result::fail("Import cancelled.");
        if(entry.problem.isEmpty())
        {
            const auto reuse=[&](const auto& e){return currentSample(e,entry.file);};
            auto same=std::find_if(converted.zones.begin(),converted.zones.end(),reuse);
            if(same!=converted.zones.end())z.sample=same->mapping.sample;
            else if(auto old=std::find_if(cache.zones.begin(),cache.zones.end(),reuse);
                    old!=cache.zones.end() && old->mapping.sample->bytes()<=maxBankBytes-sampleBytes(converted))
                z.sample=old->mapping.sample;
            else if(auto result=readSample(entry.file,maxBankBytes-sampleBytes(converted),z.sample,cancelled);result.failed())
                entry.problem=result.getErrorMessage();
        }
        if(aborted())return Result::fail("Import cancelled.");
        if(z.sample)
        {
            if((sample.frames>0 && sample.frames!=z.sample->frameCount()) ||
               (sample.rate>0 && std::abs(sample.rate-z.sample->rate)>0.5) ||
               (sample.channels>0 && sample.channels!=(z.sample->stereo()?2:1)))
            { entry.problem="Recording does not match EXS length/rate/channel metadata; locate original";z.sample.reset(); }
        }
        if(z.sample)
        {
            const auto frames=z.sample->frameCount();
            if(z.start>=frames || z.end>frames || (z.loop&&!validLoop(z,frames)))
            {entry.problem="EXS trim or loop exceeds source length";z.sample.reset();}
            else
            {
                if(z.loop)
                {
                    const double wanted=r.crossfadeMs*z.sample->rate/1000.0;
                    z.loopCrossfade=static_cast<int>(std::min<double>(1000000,wanted));
                    if(z.loopCrossfade>(z.loopEnd-z.loopStart)/4)
                        warnings.insert("Loop crossfade limited to a quarter of native loop length.");
                }
                else z.loopEnd=frames;
                if(!primeZone(z)){entry.problem="Cannot prime disk-streamed zone";z.sample.reset();}
            }
        }
        if(z.sample){++playable;if(z.sample->stream)++streaming;}
        else
        {
            ++missing;
            if(z.loop && sample.rate>0)z.loopCrossfade=static_cast<int>(std::min<double>(1000000,static_cast<double>(r.crossfadeMs)*sample.rate/1000.0));
            warnings.insert(prefix+entry.file.getFileName().toStdString()+": "+entry.problem.toStdString());
        }
        if(g.condition==2 && !g.mute)
        {
            if(g.release)return Result::fail("Release-trigger round robin not representable in this EXS conversion.");
            const auto key=String(z.low)+":"+String(z.high)+":"+String(z.soft)+":"+String(z.hard);
            if(!sequences[key].positions.emplace(g.sequence,static_cast<int>(converted.zones.size())).second)
                return Result::fail("Overlapping layers in EXS round robin cannot be safely converted.");
        }
        converted.zones.push_back(std::move(entry));
    }
    if(aborted())return Result::fail("Import cancelled.");
    if(playable==0)
    {
        report="No playable EXS zones. "+String(missing)+" unavailable, "+String(omitted)+" omitted.\n";
        for(const auto& w:warnings)report+=String(w)+"\n";
        return Result::fail(report);
    }
    if(sequences.size()>maxGroups)return Result::fail("EXS exceeds native 16 round-robin sequence limit.");
    if(!sequences.empty())
    {
        std::vector<SampleZone> ordered;
        std::set<int> used;int rr=0;
        for(const auto& [key,seq]:sequences)
        {
            if(seq.positions.size()!=expectedRR.size())return Result::fail("Incomplete round-robin layers in EXS; no partial conversion.");
            ++rr;
            for(const auto& [position,index]:seq.positions)
            {
                if(!expectedRR.count(position))return Result::fail("Unexpected EXS round-robin position.");
                auto& e=converted.zones[static_cast<std::size_t>(index)];
                if(!e.mapping.sample)return Result::fail("Round-robin source unavailable; resolve all sequence samples before import.");
                e.mapping.group=rr; used.insert(index); ordered.push_back(std::move(e));
            }
        }
        for(int i=0;i<static_cast<int>(converted.zones.size());++i)
            if(!used.count(i))ordered.push_back(std::move(converted.zones[static_cast<std::size_t>(i)]));
        converted.zones=std::move(ordered);
        warnings.insert("Round robin uses native matched-range counters, not Logic's complete group rule engine.");
    }
    warnings.insert("Native ADSR, velocity response and -6 dB headroom are used; EXS synth parameters and modulation are not reproduced.");
    report="EXS24: "+converted.name+"\n"+String(playable)+" playable zones | "+String(streaming)+" streamed | "
        +String(missing)+" unavailable | "+String(omitted)+" omitted.\n"
        +String(static_cast<int>(converted.groups.size()))+" groups, "+String(static_cast<int>(sequences.size()))+" round-robin sequences.\n"
        +"Original recordings are referenced directly; no files are copied or modified.\n"
        +"Confirm to replace this instrument only.\n\nCompatibility notes:\n";
    for(const auto& w:warnings)report+=String(w)+"\n";
    next=std::move(converted);
    return Result::ok();
}

Result Plugin::prepareXmlInstrument (const File& input, const Instrument& cache, Instrument& next,
                                     String& report, BankImportMedia& media, std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    using Attr = std::map<std::string,std::string>;
    const auto aborted=[&]{return cancelled&&cancelled->load();};
    const auto fail=[](const std::string& message){throw std::runtime_error(message);};
    constexpr int xmlLimit=8*1024*1024;
    if(aborted())return Result::fail("Import cancelled.");
    if(!input.existsAsFile()||!input.hasFileExtension("dspreset;dslibrary;nki")||media.directory!=File())
        return Result::fail("Select an accessible Decent Sampler preset/library or legacy Kontakt NKI.");
    const auto originalSize=input.getSize();
    const auto originalTime=input.getLastModificationTime();
    File definition=input;
    const bool kontakt=input.hasFileExtension("nki");
    std::set<std::string> warnings;
    try
    {
        if(input.hasFileExtension("dslibrary"))
        {
            ZipFile archive(input);
            if(archive.getNumEntries()<1||archive.getNumEntries()>8192)fail("Invalid or oversized Decent Sampler ZIP directory.");
            const auto root=media.parent!=File()?media.parent:File::getSpecialLocation(File::userMusicDirectory).getChildFile("Studio").getChildFile("Imported Sound Banks");
            if(auto r=root.createDirectory();r.failed())return r;
            const auto dir=root.getChildFile(Uuid().toString());
            if(dir.exists())fail("Cannot create unique library staging folder.");
            if(auto r=dir.createDirectory();r.failed())return r;
            media.directory=dir;
            std::set<String> paths;
            std::vector<std::pair<int,File>> entries;
            std::int64_t total=0;
            int presets=0;
            for(int i=0;i<archive.getNumEntries();++i)
            {
                if(aborted())fail("Import cancelled.");
                const auto* e=archive.getEntry(i);
                if(!e||e->isSymbolicLink||e->uncompressedSize<0)fail("Invalid or symbolic-link ZIP entry.");
                const auto name=e->filename.replaceCharacter('\\','/');
                if(name.isEmpty()||name.startsWithChar('/')||name.containsChar(':'))
                    fail("Unsafe ZIP path.");
                const auto parts=StringArray::fromTokens(name,"/","");
                for(const auto& part:parts)if(part==".."||part==".")fail("Unsafe ZIP path component.");
                if(name.endsWithChar('/'))continue;
                const auto target=dir.getChildFile(name);
                if(!target.isAChildOf(dir)||!paths.insert(target.getFullPathName().toLowerCase()).second)
                    fail("Unsafe or duplicate ZIP path.");
                if(!target.hasFileExtension("dspreset;wav;wave;aif;aiff;aifc;flac;caf"))continue;
                if(e->uncompressedSize>512ll*1024*1024||total>2ll*1024*1024*1024-e->uncompressedSize)
                    fail("Decent library exceeds native extraction limits.");
                total+=e->uncompressedSize;
                if(target.hasFileExtension("dspreset"))
                {
                    if(e->uncompressedSize>xmlLimit)fail("Decent preset exceeds XML size limit.");
                    definition=target;++presets;
                }
                entries.emplace_back(i,target);
            }
            if(presets!=1)fail("This version imports one-preset .dslibrary archives; extract a multi-preset library and choose its .dspreset.");
            if(dir.getBytesFreeOnVolume()<total+16*1024*1024)fail("Not enough space for Decent library extraction.");
            std::array<char,65536> buffer {};
            for(const auto& [i,target]:entries)
            {
                if(aborted())fail("Import cancelled.");
                if(auto r=target.getParentDirectory().createDirectory();r.failed())return r;
                std::unique_ptr<InputStream> stream(archive.createStreamForEntry(i));
                auto out=target.createOutputStream();
                if(!stream||!out)fail("Cannot extract Decent library entry.");
                const auto expected=archive.getEntry(i)->uncompressedSize;
                std::int64_t written=0;
                while(written<expected)
                {
                    if(aborted())fail("Import cancelled.");
                    const auto n=stream->read(buffer.data(),static_cast<int>(std::min<std::int64_t>(buffer.size(),expected-written)));
                    if(n<=0||!out->write(buffer.data(),static_cast<std::size_t>(n)))fail("Truncated or unwritable ZIP entry.");
                    written+=n;
                }
                if(stream->read(buffer.data(),1)!=0)fail("ZIP entry exceeds declared size.");
                out->flush();
                if(out->getStatus().failed())fail("Cannot flush extracted library entry.");
            }
            warnings.insert("Accepted archive recordings are retained in the Studio imported media folder; back them up with presets.");
        }
        if(definition.getSize()<=0||definition.getSize()>32ll*1024*1024)fail("Definition exceeds size limit.");
        MemoryBlock data;
        if(!definition.loadFileAsData(data)||data.getSize()>32u*1024*1024)fail("Cannot read instrument definition.");
        auto parse=[&](const void* bytes,std::size_t length)->std::unique_ptr<XmlElement>
        {
            if(length==0||length>xmlLimit)return {};
            const auto xml=String::fromUTF8(static_cast<const char*>(bytes),static_cast<int>(length));
            if(xml.containsIgnoreCase("<!DOCTYPE")||xml.containsIgnoreCase("<!ENTITY"))return {};
            // Bound structural nesting before invoking the recursive XML parser.
            const auto rawXml=xml.toStdString();
            int tags=0,depth=0;
            for(std::size_t i=0;i<rawXml.size();)
            {
                if(rawXml[i]!='<'){++i;continue;}
                if(rawXml.compare(i,4,"<!--")==0)
                {auto end=rawXml.find("-->",i+4);if(end==std::string::npos)return {};i=end+3;continue;}
                if(rawXml.compare(i,9,"<![CDATA[")==0)
                {auto end=rawXml.find("]]>",i+9);if(end==std::string::npos)return {};i=end+3;continue;}
                if(rawXml.compare(i,2,"<?")==0)
                {auto end=rawXml.find("?>",i+2);if(end==std::string::npos)return {};i=end+2;continue;}
                if(i+1>=rawXml.size()||rawXml[i+1]=='!'||++tags>50000)return {};
                const bool close=rawXml[i+1]=='/';
                char quote=0;auto end=i+1;
                for(;end<rawXml.size();++end)
                {
                    const char c=rawXml[end];
                    if(quote){if(c==quote)quote=0;}
                    else if(c=='"'||c=='\'')quote=c;
                    else if(c=='>')break;
                    else if(c=='<')return {};
                }
                if(end>=rawXml.size())return {};
                if(close)--depth;else if(rawXml[end-1]!='/')++depth;
                if(depth<0||depth>64)return {};
                i=end+1;
            }
            if(depth!=0)return {};
            return XmlDocument::parse(xml);
        };
        std::unique_ptr<XmlElement> xml;
        if(data.getSize()==0)fail("Empty instrument definition.");
        const auto* raw=static_cast<const unsigned char*>(data.getData());
        if(!kontakt)xml=parse(raw,data.getSize());
        else
        {
            // Only readable legacy K2 XML payloads. No encryption, monolith,
            // NCW decompression or modern Kontakt container emulation.
            if(raw[0]=='<'||(data.getSize()>=3&&raw[0]==0xef&&raw[1]==0xbb&&raw[2]==0xbf))
                xml=parse(raw,data.getSize());
            for(std::size_t pos=0;!xml&&pos+1<std::min<std::size_t>(256,data.getSize());++pos)
            {
                if(aborted())fail("Import cancelled.");
                const bool gzip=pos==0&&raw[pos]==0x1f&&raw[pos+1]==0x8b;
                const bool zlib=raw[pos]==0x78&&((int(raw[pos])*256+raw[pos+1])%31==0)&&!(raw[pos+1]&32);
                if(!gzip&&!zlib)continue;
                MemoryInputStream compressed(raw+pos,data.getSize()-pos,false);
                GZIPDecompressorInputStream decoder(&compressed,false,gzip?GZIPDecompressorInputStream::gzipFormat:GZIPDecompressorInputStream::zlibFormat);
                MemoryBlock decoded;std::array<char,8192> block {};
                while(!aborted())
                {
                    const auto n=decoder.read(block.data(),static_cast<int>(block.size()));
                    if(n<=0)break;
                    if(decoded.getSize()+static_cast<std::size_t>(n)>xmlLimit){decoded.reset();break;}
                    decoded.append(block.data(),static_cast<std::size_t>(n));
                }
                auto candidate=parse(decoded.getData(),decoded.getSize());
                if(candidate&&candidate->hasTagName("K2_Container"))xml=std::move(candidate);
            }
        }
        if(aborted())fail("Import cancelled.");
        if(!xml||!xml->hasTagName(kontakt?"K2_Container":"DecentSampler"))
            fail(kontakt?"No supported readable legacy Kontakt K2 XML mapping. Modern, protected or monolithic NKI is not supported.":"Invalid or unsupported Decent Sampler XML.");
        const auto attrs=[](const XmlElement& element)
        {
            Attr a;for(int i=0;i<element.getNumAttributes();++i)a[element.getAttributeName(i).toStdString()]=element.getAttributeValue(i).toStdString();
            for(const auto& [alias,key]:std::array<std::pair<const char*,const char*>,4>{{
                {"ampEnvAttack","attack"},{"ampEnvDecay","decay"},{"ampEnvSustain","sustain"},{"ampEnvRelease","release"}}})
                if(a.count(alias)){a[key]=a[alias];a.erase(alias);}
            return a;
        };
        const auto values=[](const XmlElement& element)
        {
            Attr a;
            const auto read=[&](const XmlElement& parent)
            {
                for(auto* v=parent.getFirstChildElement();v;v=v->getNextElement())
                    if(v->hasTagName("V"))a[v->getStringAttribute("name").toStdString()]=v->getStringAttribute("value").toStdString();
            };
            if(auto* p=element.getChildByName("Parameters"))read(*p);
            read(element);return a;
        };
        const auto get=[](const Attr& a,const std::string& k,std::string fallback={})
        {auto it=a.find(k);return it==a.end()?fallback:it->second;};
        const auto num=[&](const Attr& a,const std::string& k,double fallback,double lo,double hi)
        {
            if(!a.count(k))return fallback;
            auto n=sfz::number(get(a,k));
            if(!n||*n<lo||*n>hi)fail("Invalid or unsupported value: "+k+"="+get(a,k));
            return *n;
        };
        const auto integer=[&](const Attr& a,const std::string& k,int fallback,int lo,int hi)
        {
            if(!a.count(k))return fallback;
            auto n=sfz::integer(get(a,k));
            if(!n||*n<lo||*n>hi)fail("Invalid integer: "+k+"="+get(a,k));
            return *n;
        };
        const auto boolean=[&](const Attr& a,const std::string& k,bool fallback)
        {
            if(!a.count(k))return fallback;
            auto v=sfz::lower(get(a,k));
            if(v=="true"||v=="yes"||v=="1")return true;
            if(v=="false"||v=="no"||v=="0")return false;
            fail("Invalid boolean: "+k);return false;
        };
        const auto db=[&](const Attr& a,const std::string& k)
        {
            auto text=get(a,k,"1");
            const auto low=sfz::lower(text);
            if(!kontakt&&low.size()>2&&low.substr(low.size()-2)=="db")
            {
                auto n=sfz::number(text.substr(0,text.size()-2));if(!n||*n< -120||*n>24)fail("Invalid dB gain");return *n;
            }
            auto n=sfz::number(text);if(!n||*n<0||*n>16)fail("Invalid linear gain");
            return *n==0?-120.0:20*std::log10(*n);
        };
        const auto overlay=[](Attr& a,const Attr& b){for(const auto& [k,v]:b)a[k]=v;};
        const XmlElement* groups=nullptr;
        Attr top;
        if(kontakt)
        {
            const auto* programs=xml->getChildByName("Programs");
            const auto* program=programs?programs->getChildByName("K2_Program"):nullptr;
            if(!program||program->getNextElementWithTagName("K2_Program"))fail("Only single-program legacy Kontakt mappings are supported.");
            groups=program->getChildByName("Groups");top=values(*program);
            warnings.insert("Kontakt legacy XML mapping subset; scripts, group start conditions, modulators, effects, envelopes and native Kontakt tuning curves are not emulated.");
        }
        else
        {
            groups=xml->getChildByName("groups");
            if(groups&&groups->getNextElementWithTagName("groups"))fail("Multiple Decent Sampler groups containers.");
            if(groups)top=attrs(*groups);
            warnings.insert("Decent Sampler UI bindings, effects, modulation, velocity response and envelope shapes are not reproduced; native equivalents are used.");
        }
        if(!groups)fail("Instrument has no supported groups.");
        Instrument converted;converted.name=input.getFileNameWithoutExtension().substring(0,120);
        converted.settings.gainDb=0;converted.groups.clear();
        int playable=0,missing=0,omitted=0,streamed=0,seen=0,groupIndex=0;
        std::map<String,int> groupIds;
        struct Sequence{int length=0;std::map<int,int> positions;};
        std::map<String,Sequence> sequences;
        for(auto* group=groups->getFirstChildElement();group;group=group->getNextElement())
        {
            if(!group->hasTagName(kontakt?"K2_Group":"group"))continue;
            ++groupIndex;
            const auto ga=kontakt?values(*group):attrs(*group);
            const auto* zones=kontakt?group->getChildByName("Zones"):group;
            if(!zones)continue;
            for(auto* node=zones->getFirstChildElement();node;node=node->getNextElement())
            {
                if(aborted())fail("Import cancelled.");
                if(!node->hasTagName(kontakt?"Zone":"sample"))
                {if(node->hasTagName("oscillator")){++omitted;warnings.insert("Generated oscillator omitted.");}continue;}
                if(++seen>maxZones)fail("Instrument exceeds Studio's region limit; no partial conversion.");
                const auto za=kontakt?values(*node):attrs(*node);
                Attr a=top;overlay(a,ga);overlay(a,za);
                SampleZone e;auto& z=e.mapping;auto& x=z.sfz;x.enabled=true;
                Group g;
                std::string seqMode="always";int seqLength=0,seqPosition=1;
                std::string path;
                const auto frameMax=std::numeric_limits<int>::max()-2;
                if(kontakt)
                {
                    // Explicit legacy XML group start rules are not evaluated.
                    if(group->getChildByName("StartOptions")||group->getChildByName("GroupStartOptions")||group->getChildByName("StartConditions")||
                       boolean(a,"reverse",false))
                    {++omitted;warnings.insert("Kontakt start-condition or reverse group omitted.");continue;}
                    path=get(za,"sampleFile");
                    z.root=integer(za,"rootKey",60,0,127);z.low=integer(za,"lowKey",0,0,127);z.high=integer(za,"highKey",127,0,127);
                    z.soft=std::max(1,integer(za,"lowVelo",0,0,127));z.hard=integer(za,"highVelo",127,0,127);
                    for(const Attr* level:std::array<const Attr*,2>{&top,&ga})
                    {
                        z.low=std::max(z.low,integer(*level,"lowKey",0,0,127));z.high=std::min(z.high,integer(*level,"highKey",127,0,127));
                        z.soft=std::max(z.soft,integer(*level,"lowVelocity",0,0,127));z.hard=std::min(z.hard,integer(*level,"highVelocity",127,0,127));
                    }
                    z.gainDb=static_cast<float>(std::clamp(db(top,"volume")+db(ga,"volume")+db(za,"zoneVolume"),-60.0,12.0));
                    if(db(top,"volume")<=-120||db(ga,"volume")<=-120||db(za,"zoneVolume")<=-120){++omitted;continue;}
                    x.keyTrack=boolean(a,"keyTracking",true)?100:0;
                    g.releaseTrigger=boolean(a,"releaseTrigger",false);
                    z.oneShot=g.releaseTrigger;
                    z.start=integer(za,"sampleStart",0,0,frameMax);z.end=integer(za,"sampleEnd",0,0,frameMax);
                    const int mode=integer(za,"loopMode",0,0,2);
                    z.loop=mode!=0&&!z.oneShot;x.sustainLoop=mode==1;
                    z.loopStart=integer(za,"loopStart",0,0,frameMax);z.loopEnd=integer(za,"loopEnd",0,0,frameMax);
                    x.attack=converted.settings.attack;x.decay=converted.settings.decay;x.sustain=converted.settings.sustain;x.release=converted.settings.release;
                    warnings.insert("Kontakt tuning/pan and loop endpoint conventions vary by writer; pitch/pan adjustments are not converted in this initial subset.");
                    if(get(za,"zoneTune","1")!="1"||a.count("tune"))warnings.insert("Kontakt tuning fields require reference-player comparison.");
                }
                else
                {
                    const auto trigger=get(a,"trigger","attack");seqMode=get(a,"seqMode","always");
                    bool unsupported=trigger!="attack"&&trigger!="release";
                    unsupported|=get(a,"sampleMode","normal")!="normal"||boolean(a,"retriggerEnabled",false)||
                        boolean(a,"reverse",false)||num(a,"delay",0,0,100000)>0||a.count("silencedByTags")||
                        a.count("loHoldTime")||a.count("hiHoldTime")||num(a,"glideTime",0,0,100)>0;
                    unsupported|=seqMode!="always"&&seqMode!="round_robin";
                    unsupported|=integer(top,"globalTranspose",0,-48,48)!=0||integer(ga,"transpose",0,-48,48)!=0;
                    std::set<int> cc;
                    for(const auto& [k,v]:a)
                    {
                        if(k.rfind("onLoCC",0)==0||k.rfind("onHiCC",0)==0)unsupported=true;
                        if(k.rfind("loCC",0)==0||k.rfind("hiCC",0)==0)
                        {auto n=sfz::integer(std::string_view(k).substr(4));if(!n||*n<0||*n>=120)unsupported=true;else cc.insert(*n);}
                    }
                    if(cc.size()>1)unsupported=true;
                    const int loCh=integer(a,"loChannel",1,1,16),hiCh=integer(a,"hiChannel",16,1,16);
                    if(loCh==hiCh)g.channel=loCh;else if(loCh!=1||hiCh!=16)unsupported=true;
                    if(unsupported){++omitted;warnings.insert("Unsupported Decent trigger, sequence, transpose, stretch or gating rule omitted.");continue;}
                    if(!cc.empty())
                    {
                        const auto suffix=std::to_string(*cc.begin());
                        if(!a.count("loCC"+suffix)||!a.count("hiCC"+suffix))fail("Incomplete Decent CC condition.");
                        g.trigger=GroupTrigger::controller;g.controller=*cc.begin();
                        g.ccLow=integer(a,"loCC"+suffix,0,0,127);g.ccHigh=integer(a,"hiCC"+suffix,127,0,127);
                    }
                    path=get(za,"path");
                    if(!a.count("rootNote"))fail("Decent sample missing rootNote.");
                    z.root=integer(a,"rootNote",60,0,127);z.low=integer(a,"loNote",0,0,127);z.high=integer(a,"hiNote",127,0,127);
                    z.soft=std::max(1,integer(a,"loVel",0,0,127));z.hard=integer(a,"hiVel",127,0,127);
                    const auto globalGain=db(top,top.count("globalVolume")?"globalVolume":"volume");
                    const auto groupGain=db(ga,ga.count("groupVolume")?"groupVolume":"volume");
                    const auto sampleGain=db(za,"volume");
                    const auto gain=globalGain+groupGain+sampleGain;
                    if(globalGain<=-120||groupGain<=-120||sampleGain<=-120){++omitted;continue;}
                    if(gain< -60||gain>12)warnings.insert("Combined Decent sample gain limited to native range.");
                    z.gainDb=static_cast<float>(std::clamp(gain,-60.0,12.0));
                    x.pan=static_cast<float>(std::clamp(num(a,"pan",0,-100,100)+num(ga,"groupPan",0,-100,100)+num(top,"globalPan",0,-100,100),-100.0,100.0));
                    const auto track=num(a,"pitchKeyTrack",1,0,1);x.keyTrack=roundToInt(track*100);
                    const auto tune=(num(top,"globalTuning",0,-48,48)+num(ga,"groupTuning",0,-48,48)+num(a,"tuning",0,-48,48))*(boolean(a,"tuningIgnoresKeyTrack",false)?1:track);
                    x.transpose=static_cast<int>(tune);z.cents=static_cast<float>((tune-x.transpose)*100);
                    x.velocityTrack=static_cast<float>(100*num(a,"ampVelTrack",1,0,1));
                    const auto env=[&](const char* alias,const char* key,double fallback,double hi)
                    {return static_cast<float>(num(a,a.count(alias)?alias:key,fallback,0,hi));};
                    x.attack=env("ampEnvAttack","attack",converted.settings.attack,100);
                    x.decay=env("ampEnvDecay","decay",converted.settings.decay,100);
                    x.sustain=env("ampEnvSustain","sustain",1,1);
                    x.release=env("ampEnvRelease","release",converted.settings.release,100);
                    g.mute=!boolean(a,"enabled",true);g.releaseTrigger=trigger=="release";
                    z.oneShot=!boolean(a,"ampEnvEnabled",true)||g.releaseTrigger;
                    if(!boolean(a,"ampEnvEnabled",true)){x.attack=0;x.decay=0;x.sustain=1;}
                    z.start=integer(a,"start",0,0,frameMax);
                    z.end=a.count("end")?integer(a,"end",0,0,frameMax)+1:0;
                    z.loop=boolean(a,"loopEnabled",false)&&!z.oneShot;x.sustainLoop=!boolean(a,"loopDuringRelease",true);
                    z.loopStart=integer(a,"loopStart",0,0,frameMax);
                    z.loopEnd=a.count("loopEnd")?integer(a,"loopEnd",0,0,frameMax)+1:0;
                    z.loopCrossfade=integer(a,"loopCrossfade",0,0,1000000);
                    if(z.loopCrossfade&&get(a,"loopCrossfadeMode","equal_power")!="linear")warnings.insert("Equal-power loop crossfade approximated by native linear crossfade.");
                    if(seqMode=="round_robin"){seqLength=integer(a,"seqLength",0,0,128);seqPosition=integer(a,"seqPosition",1,1,128);}
                }
                if(z.low>z.high||z.soft>z.hard){++omitted;continue;}
                if(path.empty())fail("Sample path missing.");
                const auto name=String(path).replaceCharacter('\\','/');
                if(name.containsChar(':'))fail("Unsupported encoded or drive-relative sample reference.");
                e.file=File::isAbsolutePath(name)?File(name):definition.getParentDirectory().getChildFile(name);
                if(media.directory!=File()&&!e.file.isAChildOf(media.directory))fail("Archive preset refers outside extracted library.");
                if(!e.file.hasFileExtension("wav;wave;aif;aiff;aifc;caf;flac"))
                    e.problem="Unsupported sample encoding (protected/NCW/embedded audio not supported)";
                else
                {
                    const auto reuse=[&](const auto& old){return currentSample(old,e.file);};
                    auto found=std::find_if(converted.zones.begin(),converted.zones.end(),reuse);
                    if(found!=converted.zones.end())z.sample=found->mapping.sample;
                    else if(auto old=std::find_if(cache.zones.begin(),cache.zones.end(),reuse);old!=cache.zones.end()&&old->mapping.sample->bytes()<=maxBankBytes-sampleBytes(converted))z.sample=old->mapping.sample;
                    else if(auto r=readSample(e.file,maxBankBytes-sampleBytes(converted),z.sample,cancelled);r.failed())e.problem=r.getErrorMessage();
                }
                if(z.sample)
                {
                    const int frames=z.sample->frameCount();
                    if(z.start>=frames||z.end>frames)fail("Sample trim exceeds recording length.");
                    if(z.loop)
                    {
                        if(!kontakt&&!a.count("loopStart")&&z.sample->embeddedLoopStart>=0)z.loopStart=z.sample->embeddedLoopStart;
                        if(z.loopEnd==0)z.loopEnd=z.sample->embeddedLoopEnd>0?z.sample->embeddedLoopEnd:(z.end?z.end:frames);
                        if(!validLoop(z,frames))fail("Invalid imported sample loop.");
                    }
                    else z.loopEnd=frames;
                    if(!primeZone(z))fail("Cannot prime streamed recording.");
                    ++playable;if(z.sample->stream)++streamed;
                }
                else
                {
                    ++missing;warnings.insert(e.file.getFullPathName().toStdString()+": "+e.problem.toStdString());
                    if(z.loop&&z.loopEnd<=z.loopStart+1){z.loop=false;x.sustainLoop=false;warnings.insert("Unavailable sample has unresolved loop; restore after locating it.");}
                }
                if((z.end&&z.end<=z.start)||!validMapping(z)||!validGroup(g))fail("Imported mapping exceeds native sampler limits.");
                const String groupKey=String(groupIndex)+":"+String(static_cast<int>(g.trigger))+":"+String(g.controller)+":"+String(g.ccLow)+":"+String(g.ccHigh)+":"+String(g.channel)+":"+String(int(g.releaseTrigger))+":"+String(int(g.mute));
                auto existing=groupIds.find(groupKey);
                if(existing==groupIds.end())
                {
                    if(converted.groups.size()>=maxGroups)fail("Instrument exceeds Studio's 16 group limit.");
                    g.id=static_cast<int>(converted.groups.size());
                    auto label=group->getStringAttribute("name","Group "+String(groupIndex));
                    converted.groups.push_back({g.id,label.substring(0,120),g});groupIds.emplace(groupKey,g.id);e.folder=g.id;
                }
                else e.folder=existing->second;
                z.folder=e.folder;
                if(seqMode=="round_robin")
                {
                    if(g.releaseTrigger||g.mute)fail("Disabled or release-triggered round robin is not representable.");
                    const auto key=String(z.low)+":"+String(z.high)+":"+String(z.soft)+":"+String(z.hard)+":"+String(static_cast<int>(g.trigger))+":"+String(g.controller)+":"+String(g.ccLow)+":"+String(g.ccHigh)+":"+String(g.channel);
                    auto& seq=sequences[key];
                    if(seq.length&&seqLength&&seq.length!=seqLength)fail("Conflicting round-robin lengths.");
                    if(seqLength)seq.length=seqLength;
                    if(!seq.positions.emplace(seqPosition,static_cast<int>(converted.zones.size())).second)fail("Overlapping round-robin layers not representable.");
                }
                converted.zones.push_back(std::move(e));
                if(progress)progress->store(std::min(95,seen*95/maxZones));
            }
        }
        if(aborted())fail("Import cancelled.");
        if(!playable){std::string reason="No playable accessible recordings found.\n";for(const auto& w:warnings)reason+=w+"\n";fail(reason);}
        if(sequences.size()>maxGroups)fail("Too many independent round-robin sequences.");
        if(!sequences.empty())
        {
            std::vector<SampleZone> ordered;std::set<int> used;int rr=0;
            for(const auto& [key,seq]:sequences)
            {
                const int length=seq.length?seq.length:static_cast<int>(seq.positions.size());
                if(length!=static_cast<int>(seq.positions.size()))fail("Incomplete round-robin sequence.");
                int position=1;++rr;
                for(const auto& [p,index]:seq.positions)
                {
                    if(p!=position++)fail("Nonconsecutive round-robin positions.");
                    auto& e=converted.zones[static_cast<std::size_t>(index)];
                    if(!e.mapping.sample)fail("Round robin has missing sample; locate all members before conversion.");
                    e.mapping.group=rr;used.insert(index);ordered.push_back(std::move(e));
                }
            }
            for(int i=0;i<static_cast<int>(converted.zones.size());++i)if(!used.count(i))ordered.push_back(std::move(converted.zones[static_cast<std::size_t>(i)]));
            converted.zones=std::move(ordered);
            warnings.insert("Round robin uses native matched-range sequence counters.");
        }
        if(input.getSize()!=originalSize||input.getLastModificationTime()!=originalTime)fail("Source definition changed during import.");
        report=String(kontakt?"Legacy Kontakt XML: ":"Decent Sampler: ")+converted.name+"\n"+
            String(playable)+" playable regions | "+String(streamed)+" streamed | "+String(missing)+" unavailable | "+String(omitted)+" omitted.\n"+
            "Confirm to replace this instrument only. Original files unchanged.\n\nCompatibility notes:\n";
        for(const auto& w:warnings)report+=String(w)+"\n";
        if(media.directory!=File())report+="Retain extracted media: "+media.directory.getFullPathName()+"\n";
        if(progress)progress->store(100);
        next=std::move(converted);return Result::ok();
    }
    catch(const std::exception& e){return Result::fail("Instrument conversion: "+String(e.what()));}
}

Result Plugin::catalogSoundBank (const File& file, std::vector<BankProgram>& output, std::atomic<bool>* cancelled)
{
    if (! file.existsAsFile() || ! file.hasFileExtension ("sf2;sf3;dls;gig;p1;p3;s1;s3"))
        return Result::fail ("Select an existing SF2, SF3, DLS, GIG or extracted Akai P1/P3/S1/S3 file.");
    try
    {
        soundbank::Bank bank (sfz::path (file.getFullPathName().toStdString()));
        if (bank.programs.size() > 8192) return Result::fail ("Bank exceeds Studio's catalog limit (8192 programs).");
        std::vector<BankProgram> proposal;
        for (const auto& program : bank.programs)
        {
            if (cancelled && cancelled->load()) return Result::fail ("Import cancelled.");
            proposal.push_back ({ String (program.name), static_cast<int> (program.bank),
                                  static_cast<int> (program.number), program.drums });
        }
        output = std::move (proposal);
        return Result::ok();
    }
    catch (const std::exception& e) { return Result::fail ("Sound bank: " + String (e.what())); }
}

Result Plugin::prepareSoundBank (const File& file, int index, Instrument& next, String& report,
                                 BankImportMedia& media, std::atomic<bool>* cancelled, std::atomic<int>* progress)
{
    using namespace soundbank;
    const auto aborted = [&] { return cancelled && cancelled->load(); };
    if (aborted()) return Result::fail ("Import cancelled.");
    if (! file.existsAsFile() || ! file.hasFileExtension ("sf2;sf3;dls;gig;p1;p3;s1;s3") || media.directory != File())
        return Result::fail ("Select an existing bank and a fresh conversion.");
    const auto originalSize = file.getSize();
    const auto originalTime = file.getLastModificationTime();
    try
    {
        soundbank::Bank bank (sfz::path (file.getFullPathName().toStdString()));
        if (index < 0 || static_cast<std::size_t> (index) >= bank.programs.size())
            return Result::fail ("Invalid program selection.");
        auto mapping = bank.resolve (static_cast<std::size_t> (index), maxZones);
        if (aborted()) return Result::fail ("Import cancelled.");
        const auto& program = bank.programs[static_cast<std::size_t> (index)];
        Instrument converted;
        converted.name = String (program.name).substring (0, 120).trim();
        if (converted.name.isEmpty()) converted.name = "Sound bank program " + String (program.number);
        converted.settings.gainDb = -6; // headroom for layers
        converted.groups.front().name = "Bank regions";
        const auto root = media.parent != File() ? media.parent
            : File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Studio").getChildFile ("Imported Sound Banks");
        if (auto result = root.createDirectory(); result.failed()) return result;
        // UUID rather than a user-provided bank/patch string: no collisions or
        // path traversal, and cancellation can only remove this one directory.
        const auto dir = root.getChildFile (Uuid().toString());
        if (dir.exists()) return Result::fail ("Cannot create unique import folder.");
        if (auto result = dir.createDirectory(); result.failed()) return result;
        media.directory = dir;

        struct ConvertedAudio { Audio source; File file; std::shared_ptr<const Sample> sample; };
        std::vector<ConvertedAudio> extracted;
        const auto same = [] (const Audio& a,const Audio& b)
        {
            return a.encoding==b.encoding && a.offset==b.offset && a.bytes==b.bytes
                && a.lowBytes==b.lowBytes && a.channels==b.channels && a.rate==b.rate && a.frames==b.frames
                && a.external==b.external && a.externalSize==b.externalSize && a.externalTime==b.externalTime;
        };
        constexpr U64 maximumOutput = 2ull * 1024 * 1024 * 1024;
        U64 outputBytes = 0;
        int streamed = 0, position = 0;
        for (const auto& region : mapping.regions)
        {
            if (aborted()) return Result::fail ("Import cancelled.");
            const auto& src = region.audio;
            SampleZone entry;
            auto& z = entry.mapping;
            auto old = std::find_if (extracted.begin(), extracted.end(), [&] (const auto& e) { return same (src,e.source); });
            if (old != extracted.end())
            {
                entry.file = old->file; z.sample = old->sample;
            }
            else
            {
                int frames = src.frames, channels = src.channels, rate = src.rate;
                std::unique_ptr<AudioFormatReader> decoder;
                if (src.encoding == Encoding::vorbis)
                {
                   #if JUCE_USE_OGGVORBIS
                    OggVorbisAudioFormat ogg;
                    auto input = file.createInputStream();
                    if (! input) return Result::fail ("Cannot open SF3 compressed sample.");
                    decoder.reset (ogg.createReaderFor (new SubregionStream (input.release(), static_cast<int64> (src.offset),
                                                                           static_cast<int64> (src.bytes), true), true));
                    if (! decoder || decoder->lengthInSamples < 2 || decoder->lengthInSamples >= std::numeric_limits<int>::max()
                        || decoder->numChannels != 1 || std::abs (decoder->sampleRate - rate) > 0.5)
                        return Result::fail ("Unsupported or corrupt SF3 Vorbis sample (mono, matching sample rate required).");
                    frames = static_cast<int> (decoder->lengthInSamples);
                   #else
                    return Result::fail ("This Studio build has no Ogg Vorbis decoder; SF3 compressed samples cannot be imported.");
                   #endif
                }
                if (frames < 2 || channels < 1 || channels > 2)
                    return Result::fail ("Invalid decoded bank sample length or channels.");
                const int bits = src.encoding==Encoding::pcm8 || src.encoding==Encoding::pcm16 ? 16 : 24;
                const U64 bytes = U64(frames)*U64(channels)*U64(bits/8);
                if (bytes > maximumOutput - outputBytes || bytes > 512ull*1024*1024)
                    return Result::fail ("Sound bank conversion exceeds the 512 MiB/sample or 2 GiB/instrument disk limit.");
                if (dir.getBytesFreeOnVolume() < static_cast<int64> (bytes + 16*1024*1024))
                    return Result::fail ("Not enough disk space for bank conversion.");
                entry.file = dir.getChildFile ("Sample-" + String (static_cast<int> (extracted.size()+1)).paddedLeft ('0', 4) + ".wav");
                {
                    WavAudioFormat wav;
                    std::unique_ptr<OutputStream> stream = entry.file.createOutputStream();
                    if (! stream) return Result::fail ("Cannot create extracted WAV.");
                    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (rate)
                        .withNumChannels (channels).withBitsPerSample (bits));
                    if (! writer) return Result::fail ("Cannot initialize extracted WAV writer.");
                    constexpr int chunk = 8192;
                    AudioBuffer<float> samples (channels,chunk);
                    for (int first=0;first<frames;)
                    {
                        if (aborted()) return Result::fail ("Import cancelled.");
                        const int count = std::min (chunk,frames-first);
                        if (decoder)
                        {
                            if (! decoder->read (&samples,0,count,first,true,channels>1))
                                return Result::fail ("SF3 Vorbis decoding failed.");
                        }
                        else
                        {
                            const int width = src.encoding==Encoding::pcm8 ? 1 : src.encoding==Encoding::pcm24 ? 3 : 2;
                            const auto raw = bank.readAudio (src,src.offset+U64(first)*channels*width,U64(count)*channels*width);
                            const auto low = src.encoding==Encoding::sf24 ? bank.read (src.lowBytes+U64(first),U64(count)) : Bytes{};
                            for (int n=0;n<count;++n)
                                for(int ch=0;ch<channels;++ch)
                                {
                                    const auto p=static_cast<std::size_t>((n*channels+ch)*width);
                                    float value=0;
                                    if(width==1) value=(int(raw[p])-128)/128.f;
                                    else if(width==2)
                                    {
                                        const int high=soundbank::s16(raw,p);
                                        value=low.empty()? high/32768.f : (high*256+int(low[static_cast<std::size_t>(n)]))/8388608.f;
                                    }
                                    else
                                    {
                                        const int v=int(raw[p])|(int(raw[p+1])<<8)|(int(raw[p+2])<<16);
                                        value=(v<0x800000?v:v-0x1000000)/8388608.f;
                                    }
                                    samples.setSample (ch,n,value);
                                }
                        }
                        if (! writer->writeFromAudioSampleBuffer (samples,0,count))
                            return Result::fail ("Could not write extracted WAV.");
                        first+=count;
                    }
                }
                outputBytes += bytes;
                if (auto result = readSample (entry.file,maxBankBytes-sampleBytes(converted),z.sample,cancelled); result.failed())
                    return result;
                if (z.sample->frameCount()!=frames || z.sample->stereo()!=(channels==2))
                    return Result::fail ("Extracted WAV length or channel count mismatch.");
                extracted.push_back ({src,entry.file,z.sample});
            }
            z.root=region.root;z.low=region.low;z.high=region.high;z.soft=region.soft;z.hard=region.hard;
            z.gainDb=region.gainDb;z.cents=region.cents;
            z.start=region.start;z.end=region.end;z.loop=region.loop;z.oneShot=region.oneShot;
            z.loopStart=region.loopStart;z.loopEnd=region.loop ? region.loopEnd : z.sample->frameCount();
            z.loopCrossfade=0;
            z.sfz.enabled=true;z.sfz.sustainLoop=region.sustainLoop;
            z.sfz.transpose=region.transpose;z.sfz.keyTrack=region.keyTrack;z.sfz.pan=region.pan;
            z.sfz.attack=region.attack;z.sfz.decay=region.decay;z.sfz.sustain=region.sustain;z.sfz.release=region.release;
            if (! validMapping(z) || (z.loop && ! validLoop(z,z.sample->frameCount())))
                return Result::fail ("Converted bank region has invalid native mapping or loop.");
            if (! primeZone(z)) return Result::fail ("Could not preload converted streaming loop.");
            if (z.sample->stream) ++streamed;
            converted.zones.push_back (std::move(entry));
            if(progress) progress->store(++position*100/static_cast<int>(mapping.regions.size()));
        }
        if (aborted()) return Result::fail ("Import cancelled.");
        if (! file.existsAsFile() || file.getSize()!=originalSize || file.getLastModificationTime()!=originalTime)
            return Result::fail ("The source bank changed while importing.");
        report="Sound bank: "+file.getFileName()+"\n"+converted.name+" | Bank "+String(static_cast<int>(program.bank))
            +" | Program "+String(static_cast<int>(program.number))+(program.drums?" | Percussion":"")
            +"\n"+String(static_cast<int>(converted.zones.size()))+" regions, "+String(static_cast<int>(extracted.size()))
            +" unique WAV recordings, "+String(streamed)+" streamed regions.\n"
            +"Extracted audio: "+String(static_cast<double>(outputBytes)/(1024*1024),2)+" MiB.\n"
            +"Confirm to replace this instrument only. Original bank unchanged.\n"
            +"Keep the extracted media for saved projects and presets:\n"+dir.getFullPathName()+"\n\nCompatibility notes:\n";
        for(const auto& w:mapping.warnings) report+=String(w)+"\n";
        report+="Native instrument gain starts at -6 dB for layered playback headroom.\n";
        next=std::move(converted);
        return Result::ok();
    }
    catch (const std::exception& e) { return Result::fail ("Sound bank conversion: "+String(e.what())); }
}

void Plugin::removeZone (int index)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size()))) return;
    auto next = model;
    next.zones.erase (next.zones.begin() + index);
    change (std::move (next), true);
}

Result Plugin::editZones (const std::vector<std::pair<int, Zone>>& edits)
{
    if (edits.empty()) return Result::fail ("Select one or more sample zones.");
    auto next = model;
    for (const auto& [i, z] : edits)
    {
        if (! isPositiveAndBelow (i, static_cast<int> (next.zones.size())) || ! validMapping (z)) return Result::fail ("Invalid note or velocity boundaries.");
        auto& entry = next.zones[static_cast<std::size_t> (i)];
        if (z.sample != entry.mapping.sample || z.start < 0 || z.end < 0 || (z.end && z.end <= z.start)
            || z.loopStart < 0 || z.loopEnd < z.loopStart
            || (z.sample && (z.start >= z.sample->frameCount() || z.end > z.sample->frameCount()))
            || (z.loop && z.sample && ! validLoop (z, z.sample->frameCount())))
            return Result::fail ("Keep loops inside sample boundaries with at least two frames. Disable Loop before trimming past it. No selected zones were changed.");
        if ((z.start != entry.mapping.start || z.loopStart != entry.mapping.loopStart || z.loopEnd != entry.mapping.loopEnd || z.loop != entry.mapping.loop)
            && ! primeZone (z)) return Result::fail ("Could not read the selected start or loop from disk.");
        entry.mapping = z; entry.problem.clear();
    }
    change (std::move (next), true); return Result::ok();
}
void Plugin::previewRange (int index, int first, int end)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size()))) return;
    const auto& z = model.zones[static_cast<std::size_t> (index)].mapping;
    if (z.sample && z.sample->stream && ! z.sample->stream->prime (first)) return;
    preview (3, 1, z.root, 100, index, first, end);
}

void Plugin::editZone (int index, const Zone& z)
{
    if (! isPositiveAndBelow (index, static_cast<int> (model.zones.size())) || ! validMapping (z)) return;
    auto next = model;
    auto& entry = next.zones[static_cast<std::size_t> (index)];
    if (z.start < 0 || (z.end && z.end <= z.start) || (z.sample && (z.start >= z.sample->frameCount() || z.end > z.sample->frameCount()))) return;
    if (z.sample != entry.mapping.sample || z.loopStart < 0 || z.loopEnd < z.loopStart) return;
    if (z.loop && z.sample && ! validLoop (z, z.sample->frameCount())
        && (z.loopStart != entry.mapping.loopStart || z.loopEnd != entry.mapping.loopEnd || ! entry.mapping.loop))
        return;
    if ((z.start != entry.mapping.start || z.loopStart != entry.mapping.loopStart || z.loopEnd != entry.mapping.loopEnd || z.loop != entry.mapping.loop)
        && ! primeZone (z)) return;
    entry.mapping = z;
    if (z.sample && (! z.loop || validLoop (z, z.sample->frameCount()))) entry.problem.clear();
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

Result Plugin::writeConvertedPreset (const Instrument& instrument, const File& file)
{
    if (! file.hasFileExtension ("studiosampler") || file.exists())
        return Result::fail ("Choose a new native preset path.");
    if (instrument.zones.empty()) return Result::fail ("Empty instrument.");
    for (const auto& zone : instrument.zones)
        if (! zone.mapping.sample || zone.problem.isNotEmpty() || ! currentSample (zone, zone.file, zone.soundFont))
            return Result::fail ("Unavailable or changed sample: " + zone.file.getFileName());
    auto next = instrument;
    next.preset = file.getFileNameWithoutExtension().substring (0, 120);
    next.dirty = false;
    TemporaryFile temp (file);
    if (! encode (next, file.getParentDirectory(), false).writeTo (temp.getFile())
        || file.exists() || ! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write converted native preset.");
    return Result::ok();
}

String instrumentDocument (const Instrument& model, const File& base)
{
    return encode (model, base, false).toString (XmlElement::TextFormat().singleLine());
}

Result readInstrumentDocument (const String& text, const File& base, Instrument& model)
{
    auto xml = text.getNumBytesAsUTF8() <= maxPresetBytes && ! text.containsIgnoreCase ("<!DOCTYPE") ? XmlDocument::parse (text) : nullptr;
    return decode (xml.get(), base, model);
}

Result writeImportedPreset (Instrument model, const File& file, std::atomic<bool>& cancelled)
{
    auto validation = model;
    prepareSamples (validation, {}, &cancelled);
    if (cancelled.load()) return Result::fail ("Import cancelled.");
    for (const auto& zone : validation.zones)
        if (zone.problem.isNotEmpty() || ! zone.mapping.sample)
            return Result::fail (zone.file.getFileName() + ": " + zone.problem);
    if (model.zones.empty()) return Result::fail ("No playable sample zones.");
    model.preset = model.name;
    model.dirty = false;
    TemporaryFile temporary (file);
    if (! encode (model, file.getParentDirectory(), false).writeTo (temporary.getFile()))
        return Result::fail ("Could not write the imported Studio Sampler instrument.");
    if (cancelled.load()) return Result::fail ("Import cancelled.");
    return publishNewFile (temporary.getFile(), file);
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
    Instrument next; auto result = preparePreset (file, model, next);
    if (result.wasOk()) change (std::move (next), false);
    return result;
}
Result Plugin::preparePreset (const File& file, const Instrument& cache, Instrument& next, std::atomic<bool>* cancelled)
{
    if (file.getSize() > maxPresetBytes) return Result::fail ("Studio Sampler preset exceeds the size limit.");
    if (auto result = readInstrumentDocument (file.loadFileAsString(), file.getParentDirectory(), next); result.failed()) return result;
    prepareSamples (next, cache, cancelled);
    if (cancelled && cancelled->load()) return Result::fail ("Import cancelled.");
    if (next.preset.isEmpty()) next.preset = file.getFileNameWithoutExtension().substring (0, 120);
    next.dirty = false;
    return Result::ok();
}

Result Plugin::verifyStreamSources (bool checkOfflineErrors) const
{
    for (const auto& zone : model.zones)
    {
        const auto& sample = zone.mapping.sample;
        if (! sample || zone.problem.isNotEmpty() || ! zone.file.existsAsFile())
            return Result::fail ("Sampler source unavailable: " + zone.file.getFileName());
        if (sample->stream)
        {
            if (! currentSample (zone, zone.file, zone.soundFont))
                return Result::fail ("Streamed sample changed or disappeared: " + zone.file.getFileName());
            if (checkOfflineErrors && sample->stream->offlineErrors() != 0)
                return Result::fail ("Offline sample read failed: " + zone.file.getFileName());
        }
    }
    return Result::ok();
}

InstrumentDiagnosticSnapshot Plugin::resourceSnapshot() const
{
    InstrumentDiagnosticSnapshot stats;
    stats.attribution = ResourceAttribution::measured;
    std::unordered_set<const Sample*> counted;
    counted.reserve(model.zones.size());
    std::uint64_t resident = 0, cache = 0, streamed = 0, misses = 0, errors = 0;
    unsigned streams = 0;
    for (const auto& zone : model.zones)
        if (const auto* sample = zone.mapping.sample.get(); sample != nullptr
            && counted.insert(sample).second)
        {
            resident += sample->residentBytes();
            cache += sample->cacheBytes();
            if (sample->stream)
            {
                ++streams;
                streamed += static_cast<std::uint64_t> (sample->frameCount()) * (sample->stereo() ? 2u : 1u) * sizeof (float);
                misses += sample->stream->misses();
                errors += sample->stream->errors();
            }
        }
    stats.sampleBytes = resident;
    stats.cacheBytes = cache;
    stats.streamingBytes = streamed;
    stats.memoryBytes = loadedBytes;
    stats.activeVoices = renderer.activeVoices();
    stats.detail = String (loadedSamples) + " sample(s) | " + String (streams) + " streamed | "
        + String (model.settings.polyphony) + " max voices | "
        + String (static_cast<juce::int64> (misses)) + " stream underruns | "
        + String (static_cast<juce::int64> (errors)) + " read errors. Shared recordings may appear in multiple track totals.";
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

bool Plugin::sendArticulation (const studio::ArticulationMapping& mapping)
{
    const auto verified = articulations();
    if (std::find (verified.begin(), verified.end(), mapping) == verified.end()) return false;
    const auto write = writeIndex.load (std::memory_order_relaxed);
    const unsigned needed = mapping.message == studio::ArticulationMessage::keyswitch ? 2 : 1;
    if (write - readIndex.load (std::memory_order_acquire) + needed > queueSize) return false;
    const bool key = mapping.message == studio::ArticulationMessage::keyswitch;
    queue[write % queueSize] = { 2, mapping.channel - 1, mapping.number, mapping.value, -1, sampleRevision, -1, -1,
        (key ? 0x90 : 0xb0) | (mapping.channel - 1), switchRevision };
    if (key) queue[(write + 1) % queueSize] = { 2, mapping.channel - 1, mapping.number, 0, -1, sampleRevision, -1, -1, 0x80 | (mapping.channel - 1), switchRevision };
    writeIndex.store (write + needed, std::memory_order_release);
    return true;
}

void Plugin::preview (int source, int channel, int note, int velocity, int zone, int first, int end)
{
    const auto write = writeIndex.load (std::memory_order_relaxed);
    if (write - readIndex.load (std::memory_order_acquire) >= queueSize)
    {
        previewOverflow.store (true, std::memory_order_release);
        return;
    }
    queue[write % queueSize] = { source, channel - 1, note, velocity, zone, sampleRevision, first, end };
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

    const bool overflow = previewOverflow.exchange (false, std::memory_order_acquire);
    if (overflow || stopped)
    {
        renderer.preview (1, 0, -1, 0);
        renderer.preview (2, 0, -1, 0);
        renderer.preview (3, 0, -1, 0);
        const auto latest = writeIndex.load (std::memory_order_acquire);
        // A switch selected after Stop is a valid control command. Preserve it
        // while flushing queued musical auditions from the previous transport.
        if (stopped && ! overflow && ! context.isRendering)
            for (auto i = read; i != latest; ++i)
            {
                const auto& event = queue[i % queueSize];
                if (event.status && event.bindingRevision == bank->switchRevision) renderer.midi (event.status, event.note, event.velocity);
            }
        read = write = latest;
    }
    if (! context.isRendering)
        for (; read != write; ++read)
        {
            const auto& event = queue[read % queueSize];
            if (event.status)
            {
                if (event.bindingRevision == bank->switchRevision) renderer.midi (event.status, event.note, event.velocity);
            }
            else if (event.zone < 0 || event.revision == bank->sampleRevision)
                renderer.preview (event.source, event.channel, event.note, event.velocity, event.zone, event.first, event.end);
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
                renderer.render (left + offset, right ? right + offset : nullptr, frame - offset, context.isRendering);
                offset = frame;
            }
            renderer.midi (bytes[0], bytes[1] & 127, length == 3 ? bytes[2] & 127 : 0);
        }
    }

    renderer.render (left + offset, right ? right + offset : nullptr, size - offset, context.isRendering);
    renderer.publishMeters();
    borrowed.store (nullptr);
}
}
