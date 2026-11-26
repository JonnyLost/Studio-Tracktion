// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEditor.h"
#include "../StudioLibrary.h"
#include "LegacyBankFixtures.h"
#include "ExsFixtures.h"
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include "../StudioMixing.h"
#include <cmath>
#include <cstdio>
#include <numbers>

namespace studio::sampler
{
namespace
{
namespace te = tracktion;
using namespace juce;
using namespace tracktion::literals;
constexpr int blockSize = 256, rate = 48000;

struct OfflineEngine final : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
};

struct TestProperties final : te::PropertyStorage
{
    explicit TestProperties (const File& folder) : PropertyStorage ("Studio sampler test"), directory (folder) {}
    File getAppPrefsFolder() override { return directory; }
    File directory;
};

class Playback
{
public:
    explicit Playback (te::Engine& engine) : devices (engine.getDeviceManager()), device (devices.getHostedAudioDeviceInterface())
    {
        devices.setCpuLimitBeforeMuting (1000);
        device.initialise ({ .sampleRate = rate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
        device.prepareToPlay (rate, blockSize);
        devices.rescanMidiDeviceList();
        for (int attempt = 0; attempt < 10 && devices.getNumMidiInDevices() == 0; ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (20);
        devices.dispatchPendingUpdates();
        for (auto& input : devices.getMidiInDevices())
            if (te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*input))
                input->setEnabled (true);
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        devices.dispatchPendingUpdates();
    }
    ~Playback()
    {
        devices.deviceManager.closeAudioDevice();
        devices.removeHostedAudioDeviceInterface();
    }

    float block (MidiBuffer midi = {})
    {
        audio.clear();
        device.processBlock (audio, midi);
        return audio.getRMSLevel (0, 0, blockSize);
    }

    float peak (int blocks)
    {
        float peak = 0;
        for (int i = 0; i < blocks; ++i) peak = jmax (peak, block());
        return peak;
    }
private:
    te::DeviceManager& devices;
    te::HostedAudioDeviceInterface& device;
    AudioBuffer<float> audio { 2, blockSize };
};

bool testSample (const File& file, AudioFormat& format, double frequency, int channels, int metadataRoot = -1)
{
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    if (! stream) return false;
    auto options = AudioFormatWriterOptions().withSampleRate (rate / 2).withNumChannels (channels).withBitsPerSample (24);
    if (metadataRoot >= 0) options = options.withMetadata ("MidiUnityNote", String (metadataRoot));
    auto writer = format.createWriterFor (stream, options);
    if (! writer) return false;
    AudioBuffer<float> data (channels, rate / 2 * 3);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < data.getNumSamples(); ++i)
        {
            const auto time = static_cast<double> (i) / (rate / 2);
            const auto signal = 0.53 * std::exp (-time * 0.8) * (std::sin (2 * std::numbers::pi * frequency * time)
                         + 0.29 * std::sin (4 * std::numbers::pi * frequency * time + ch * 0.6));
            data.setSample (ch, i, static_cast<float> (signal));
        }
    return writer->writeFromAudioSampleBuffer (data, 0, data.getNumSamples());
}

bool longSample (const File& file, AudioFormat& format, int frames, int channels)
{
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    if (! stream) return false;
    auto writer = format.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
        .withNumChannels (channels).withBitsPerSample (24));
    if (! writer) return false;
    constexpr int chunk = 8192;
    AudioBuffer<float> data (channels, chunk);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < chunk; ++i)
            data.setSample (ch, i, static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (i % 256) / 256 + ch * 0.6)));
    for (int offset = 0; offset < frames; offset += chunk)
        if (! writer->writeFromAudioSampleBuffer (data, 0, jmin (chunk, frames - offset))) return false;
    return true;
}

Result verifyStreamingImport (const File& directory, Plugin& sampler, WavAudioFormat& wav, AiffAudioFormat& aiff)
{
    // Stereo recording exceeds the former 64 MiB decoded sample limit.
    constexpr int bigFrames = 9 * 1024 * 1024 + 123;
    constexpr int aiffFrames = 16 * streamPageFrames + 77;
    const auto big = directory.getChildFile ("Stream C4.wav");
    const auto other = directory.getChildFile ("Stream G4.aiff");
    const auto preset = directory.getChildFile ("Stream test.studiosampler");
    if (! longSample (big, wav, bigFrames, 2) || ! longSample (other, aiff, aiffFrames, 1))
        return Result::fail ("create long WAV/AIFF stream fixtures");
    Instrument proposal; String summary;
    if (Plugin::prepareImport ({}, { big, other }, true, proposal, summary).failed()
        || proposal.zones.size() != 2)
        return Result::fail ("import long WAV and AIFF files");
    for (const auto& entry : proposal.zones)
    {
        const auto& sample = entry.mapping.sample;
        const bool isWav = entry.file == big;
        const auto expected = isWav ? bigFrames : aiffFrames;
        if (! sample || ! sample->stream || sample->frameCount() != expected
            || sample->left.size() != streamPreloadFrames || entry.mapping.loopEnd != expected
            || sample->bytes() != (isWav ? 1048576u : 524288u))
            return Result::fail ("bounded preload, cache, and full recording length");
        StreamCache::Cursor cursor;
        float l = 0, r = 0;
        const int deep = expected - 2;
        if (! sample->readFrame (deep, cursor, l, r, true)) return Result::fail ("read actual source near EOF");
        const auto wantL = static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (deep % 256) / 256));
        const auto wantR = isWav ? static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (deep % 256) / 256 + 0.6)) : wantL;
        if (std::abs (l - wantL) > 1e-5f || std::abs (r - wantR) > 1e-5f)
            return Result::fail ("correct samples from deep disk page");
    }
    // Move a zone's start and loop well beyond the resident attack, then
    // persist it and reconstruct with an empty cache.
    auto& zone = proposal.zones[0].mapping;
    zone.start = 5 * streamPageFrames + 27;
    zone.loop = true;
    zone.loopStart = 6 * streamPageFrames;
    zone.loopEnd = 10 * streamPageFrames + 11;
    sampler.commitImport (proposal);
    if (sampler.savePreset (preset).failed()) return Result::fail ("save streamed preset");
    Instrument reopened;
    if (Plugin::preparePreset (preset, {}, reopened).failed() || reopened.zones.size() != 2
        || reopened.zones[0].mapping.start != zone.start || reopened.zones[0].mapping.loopEnd != zone.loopEnd
        || ! reopened.zones[0].mapping.sample || ! reopened.zones[0].mapping.sample->stream)
        return Result::fail ("restore long-source preset and loop");
    const auto stats = sampler.resourceSnapshot();
    if (stats.memoryBytes.value_or (0) != 1572864u || stats.cacheBytes.value_or (0) != 1179648u
        || stats.streamingBytes.value_or (0) <= 64u * 1024u * 1024u)
        return Result::fail ("report resident/cache memory separately from decoded stream size");
    if (sampler.verifyStreamSources (true).failed()) return Result::fail ("verify healthy stream sources");
    if (! big.deleteFile() || sampler.verifyStreamSources (false).wasOk())
        return Result::fail ("detect vanished streamed source");
    sampler.commitImport ({}, false);
    return Result::ok();
}

Result verifySfzImport (const File& directory, Plugin& sampler, WavAudioFormat& wav)
{
    const auto source = directory.getChildFile ("SFZ test samples");
    if (source.createDirectory().failed()) return Result::fail ("create SFZ fixture directory");
    const auto sample = source.getChildFile ("Long note C4.wav");
    if (! longSample (sample, wav, 16 * streamPageFrames + 77, 2))
        return Result::fail ("write SFZ stream fixture");
    const auto definition = directory.getChildFile ("Test instrument.sfz");
    const auto include = directory.getChildFile ("Regions.inc");
    if (! include.replaceWithText (R"sfz(
<region> sample=Long note C4.wav seq_position=2 volume=-6
<region> sample="Long note C4.wav" seq_position=1 volume=-3
<group> key=D4 trigger=release
<region> sample=Long note C4.wav loop_mode=one_shot
<group> key=E4 locc1=64 hicc1=127
<region> sample=Missing.wav
<group> key=F4 lorand=0 hirand=0.5
<region> sample=Long note C4.wav
)sfz") || ! definition.replaceWithText (R"sfz(
#define $samples SFZ test samples
<control> default_path=$samples
<global> ampeg_attack=0.02 ampeg_release=0.5 amp_veltrack=0
<group> key=C4 sw_last=C1 sw_default=C1 sw_label=Sustain seq_length=2
loop_mode=loop_sustain offset=65536 loop_start=70000 loop_end=100000 pan=-25
xfin_lovel=20 xfin_hivel=80 xf_velcurve=gain
#include "Regions.inc"
)sfz")) return Result::fail ("write SFZ and included definitions");

    const auto revision = sampler.changeCount();
    Instrument imported; String report;
    if (auto result = Plugin::prepareSfz (definition, sampler.instrument(), imported, report); result.failed()) return result;
    if (sampler.changeCount() != revision || imported.zones.size() != 4
        || ! report.contains ("3 playable regions") || ! report.contains ("1 unavailable")
        || ! report.contains ("1 omitted") || ! report.contains ("lorand"))
        return Result::fail ("transactional SFZ compatibility proposal");
    const auto& z = imported.zones[0].mapping;
    if (! z.sample || ! z.sample->stream || z.sample != imported.zones[1].mapping.sample
        || z.root != 60 || z.low != 60 || z.high != 60 || z.gainDb != -3
        || z.group == 0 || z.group != imported.zones[1].mapping.group
        || z.start != 65536 || z.loopEnd != 100001 || ! z.sfz.sustainLoop
        || z.sfz.velocityTrack != 0 || z.sfz.pan != -25 || z.sfz.powerFade
        || std::abs (z.sfz.attack - 0.02f) > 0.00001f || z.sfz.release != 0.5f)
        return Result::fail ("preserve SFZ sequence order, loop endpoints, envelope, pan and streaming");
    if (imported.groups[0].playback.switchDefault != 24 || ! imported.groups[1].playback.releaseTrigger
        || imported.groups[2].playback.controller != 1 || imported.groups[2].playback.ccLow != 64
        || imported.zones[3].mapping.sample || imported.zones[3].problem.isEmpty())
        return Result::fail ("keyswitch, release, controller and unavailable sample mapping");

    sampler.commitImport (imported);
    const auto preset = directory.getChildFile ("SFZ converted.studiosampler");
    if (sampler.savePreset (preset).failed() || sampler.loadPreset (preset).failed()
        || sampler.instrument().zones.size() != 4
        || sampler.instrument().zones[0].mapping.sfz.release != 0.5f
        || sampler.instrument().groups[0].playback.switchDefault != 24
        || ! sampler.instrument().zones[0].mapping.sample->stream)
        return Result::fail ("persist native SFZ region parameters and streaming");
    std::atomic<bool> cancel { true };
    Instrument cancelled; String cancelledReport;
    if (Plugin::prepareSfz (definition, sampler.instrument(), cancelled, cancelledReport, &cancel).wasOk())
        return Result::fail ("cancel SFZ proposal");
    const auto bad = directory.getChildFile ("Bad SFZ.sfz");
    if (! bad.replaceWithText ("<region> sample=bad.wav lokey=invalid")
        || Plugin::prepareSfz (bad, sampler.instrument(), cancelled, cancelledReport).wasOk())
        return Result::fail ("reject invalid SFZ mapping");
    sampler.commitImport ({}, false);
    return Result::ok();
}

Result verifySoundBankImport (const File& directory, Plugin& sampler)
{
    namespace sb = studio::soundbank;
    const auto root = directory.getChildFile ("Bank conversion fixtures");
    if (root.createDirectory().failed()) return Result::fail ("create bank fixture root");
    const auto sf = root.getChildFile ("Test.sf2"), dls = root.getChildFile ("Test.dls");
    try
    {
        sb::fixtures::write (std::filesystem::u8path (sf.getFullPathName().toStdString()), sb::fixtures::sf (false,true));
        sb::fixtures::write (std::filesystem::u8path (dls.getFullPathName().toStdString()), sb::fixtures::dls());
    }
    catch (...) { return Result::fail ("write bank fixtures"); }
    const auto revision = sampler.changeCount();
    std::vector<BankProgram> catalog;
    if (Plugin::catalogSoundBank (sf,catalog).failed() || catalog.size()!=2 || catalog[0].number!=40)
        return Result::fail ("catalog SoundFont fixture");
    {
        BankImportMedia media; media.parent=root;
        Instrument imported; String report;
        if (auto result=Plugin::prepareSoundBank (sf,0,imported,report,media); result.failed()) return result;
        if (sampler.changeCount()!=revision || imported.zones.size()!=1 || !report.contains ("Bank 0 | Program 40"))
            return Result::fail ("bank conversion proposal must not alter live instrument");
        const auto& z=imported.zones[0].mapping;
        if (!z.sample || !z.sample->stream || z.sample->frameCount()!=196608 || z.low!=64 || z.high!=80
            || !z.loop || !z.sfz.sustainLoop || z.loopEnd!=16384 || z.gainDb!=-5 || z.sfz.pan!=-20)
            return Result::fail ("preserve native streaming SoundFont mapping");
        StreamCache::Cursor cursor; float l=0,r=0;
        if (!z.sample->readFrame (131072,cursor,l,r,true) || std::abs(l-(-16384*256+64)/8388608.f)>2e-6f)
            return Result::fail ("24-bit SoundFont sample extraction differs");
        sampler.commitImport (imported); media.keep=true;
        const auto preset=root.getChildFile ("Converted.studiosampler");
        if (sampler.savePreset (preset).failed() || sampler.loadPreset (preset).failed()
            || !sampler.instrument().zones[0].mapping.sample->stream)
            return Result::fail ("persist and restore converted bank instrument");
        sampler.commitImport ({},false);
    }
    File staged;
    {
        BankImportMedia media; media.parent=root;
        Instrument imported; String report;
        if (auto result=Plugin::prepareSoundBank (dls,0,imported,report,media); result.failed()) return result;
        staged=media.directory;
        const auto& z=imported.zones[0].mapping;
        if (!z.sample || !z.sample->stream || z.root!=62 || z.cents!=25 || z.loopEnd!=9216)
            return Result::fail ("DLS native mapping and wave extraction");
    }
    if (staged.exists()) return Result::fail ("cancelled bank conversion did not remove staged media");
   #if JUCE_USE_OGGVORBIS
    const auto oggFile=root.getChildFile ("Test.ogg");
    {
        OggVorbisAudioFormat ogg;
        std::unique_ptr<OutputStream> stream=oggFile.createOutputStream();
        auto writer=ogg.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));
        if (!writer) return Result::fail ("create Vorbis fixture");
        AudioBuffer<float> signal(1,8192);
        for (int i=0;i<8192;++i) signal.setSample(0,i,0.25f*std::sin(i*0.02f));
        for (int b=0;b<24;++b)
            if (!writer->writeFromAudioSampleBuffer(signal,0,8192)) return Result::fail ("write Vorbis fixture");
    }
    MemoryBlock data;
    if (!oggFile.loadFileAsData(data)) return Result::fail ("read Vorbis fixture");
    const auto* bytes=static_cast<const unsigned char*>(data.getData());
    const auto sf3=root.getChildFile("Test.sf3");
    try { sb::fixtures::write(std::filesystem::u8path(sf3.getFullPathName().toStdString()),sb::fixtures::sf(true,false,false,sb::Bytes(bytes,bytes+data.getSize()))); }
    catch (...) { return Result::fail ("write SF3 fixture"); }
    {
        BankImportMedia media; media.parent=root;
        Instrument imported; String report;
        if (auto result=Plugin::prepareSoundBank(sf3,0,imported,report,media);result.failed()) return result;
        const auto& z=imported.zones[0].mapping;
        if (!z.sample || !z.sample->stream || z.sample->frameCount()!=196608 || z.loopEnd!=16384)
            return Result::fail ("SF3 actual Vorbis decoding, loop and streaming");
    }
   #endif
    return Result::ok();
}

Result verifyExsImport (const File& directory, Plugin& sampler, WavAudioFormat& wav)
{
    const auto definitions=directory.getChildFile("EXS definitions");
    const auto media=directory.getChildFile("Relocated EXS recordings");
    if(definitions.createDirectory().failed()||media.createDirectory().failed())return Result::fail("create EXS fixture folders");
    const auto exsFile=definitions.getChildFile("EXS fixture.exs");
    const auto sample=media.getChildFile("Long C4.wav");
    if(!longSample(sample,wav,196608,2))return Result::fail("create streamed EXS source WAV");
    try { studio::exs::fixtures::write(std::filesystem::u8path(exsFile.getFullPathName().toStdString()),studio::exs::fixtures::make()); }
    catch(...) { return Result::fail("write EXS fixture"); }
    const auto revision=sampler.changeCount();
    Instrument proposal; String report;
    if(auto result=Plugin::prepareExs(exsFile,media,sampler.instrument(),proposal,report);result.failed())return result;
    if(sampler.changeCount()!=revision||proposal.zones.size()!=1||proposal.groups.size()!=1
       ||!report.contains("1 playable zones")||!report.contains("1 omitted"))
        return Result::fail("transactional EXS review and unsupported region report");
    const auto& z=proposal.zones[0].mapping;
    const auto& g=proposal.groups[0].playback;
    if(proposal.zones[0].file!=sample||!z.sample||!z.sample->stream||z.root!=60||z.low!=50||z.high!=70
        ||z.soft!=32||z.hard!=100||z.start!=65536||z.end!=196608||z.loopStart!=70000||z.loopEnd!=100000
        ||!z.sfz.sustainLoop||z.loopCrossfade!=240||z.cents!=75||z.sfz.transpose!=0||z.gainDb!=-5||z.sfz.pan!=10
        ||g.trigger!=GroupTrigger::controller||g.controller!=1||g.ccLow!=64||g.ccHigh!=127)
        return Result::fail("preserve EXS bounds, group constraints, tuning, loop, gain and pan");
    sampler.commitImport(proposal);
    const auto preset=definitions.getChildFile("Converted.studiosampler");
    if(sampler.savePreset(preset).failed()||sampler.loadPreset(preset).failed()
        ||sampler.instrument().zones.size()!=1||sampler.instrument().zones[0].file!=sample
        ||!sampler.instrument().zones[0].mapping.sample||!sampler.instrument().zones[0].mapping.sample->stream
        ||sampler.instrument().zones[0].mapping.loopEnd!=100000)
        return Result::fail("save and reopen converted EXS instrument");
    sampler.commitImport({},false);
    // Move the WAV below a chosen root, then add another copy. The importer
    // must resolve unique names but never arbitrarily choose a duplicate.
    const auto a=media.getChildFile("A"), b=media.getChildFile("B");
    if(a.createDirectory().failed()||b.createDirectory().failed()||!sample.moveFileTo(a.getChildFile(sample.getFileName())))
        return Result::fail("relocate EXS fixture");
    Instrument relocated;
    if(Plugin::prepareExs(exsFile,media,{},relocated,report).failed()||relocated.zones[0].file.getParentDirectory()!=a)
        return Result::fail("bounded relocated source search");
    relocated={};
    if(!a.getChildFile(sample.getFileName()).copyFileTo(b.getChildFile(sample.getFileName())))
        return Result::fail("make ambiguous source names");
    if(Plugin::prepareExs(exsFile,media,{},relocated,report).wasOk()||!report.contains("Ambiguous"))
        return Result::fail("reject ambiguous EXS sample matches");
    std::atomic<bool> cancelled{true};
    if(Plugin::prepareExs(exsFile,media,{},relocated,report,&cancelled).wasOk())
        return Result::fail("cancel EXS import");
    return Result::ok();
}

Result verifyLegacyBankImport (const File& directory, Plugin& sampler)
{
    namespace sb=studio::soundbank;
    const auto root=directory.getChildFile("Legacy bank fixtures");
    if(root.createDirectory().failed())return Result::fail("create legacy bank fixture folder");
    const auto gig=root.getChildFile("Strings.gig"),program=root.getChildFile("Orchestra.P3"),sample=root.getChildFile("Strings.S3");
    try
    {
        sb::fixtures::write(std::filesystem::u8path(gig.getFullPathName().toStdString()),sb::legacyfixtures::gig());
        sb::fixtures::write(std::filesystem::u8path(program.getFullPathName().toStdString()),sb::legacyfixtures::program(true));
        sb::fixtures::write(std::filesystem::u8path(sample.getFullPathName().toStdString()),sb::legacyfixtures::sample(true));
    }
    catch(...) {return Result::fail("write legacy bank fixtures");}
    for(const auto& file:{gig,program})
    {
        const bool isGig=file==gig;
        const auto revision=sampler.changeCount();
        BankImportMedia media;media.parent=root;
        Instrument proposal;String report;
        std::vector<BankProgram> catalog;
        if(Plugin::catalogSoundBank(file,catalog).failed()||catalog.size()!=1)
            return Result::fail("catalog legacy bank");
        if(auto r=Plugin::prepareSoundBank(file,0,proposal,report,media);r.failed())return r;
        if(sampler.changeCount()!=revision||proposal.zones.size()!=1)return Result::fail("legacy bank proposal must be transactional");
        const auto& z=proposal.zones[0].mapping;
        if(!z.sample||!z.sample->stream||z.sample->frameCount()!=196608||z.low!=48||z.high!=72||
           z.root!=(isGig?62:60)||z.loopEnd!=(isGig?9216:100001)||!z.loop)
            return Result::fail("legacy mapping and streamed WAV conversion");
        StreamCache::Cursor cursor;float l=0,r=0;
        if(!z.sample->readFrame(131072,cursor,l,r,true)||std::abs(l+0.5f)>2e-5f)
            return Result::fail("legacy PCM extraction");
        sampler.commitImport(proposal);media.keep=true;
        const auto preset=root.getChildFile(isGig?"GIG.studiosampler":"Akai.studiosampler");
        if(sampler.savePreset(preset).failed()||sampler.loadPreset(preset).failed()||
           !sampler.instrument().zones[0].mapping.sample||!sampler.instrument().zones[0].mapping.sample->stream)
            return Result::fail("legacy native preset restore");
        sampler.commitImport({},false);
    }
    return Result::ok();
}

Result verifyXmlInstrumentImport (const File& directory, Plugin& sampler, WavAudioFormat& wav)
{
    const auto root=directory.getChildFile("XML instrument fixtures");
    if(root.createDirectory().failed())return Result::fail("create XML fixture directory");
    const auto sample=root.getChildFile("Long C4.wav");
    if(!longSample(sample,wav,196608,2))return Result::fail("write XML fixture WAV");
    const auto ds=root.getChildFile("Strings.dspreset");
    const String preset=R"xml(<?xml version="1.0"?>
<DecentSampler>
<groups globalVolume="0.5" globalPan="5" ampEnvAttack="0.1" release="0.3">
 <group name="Sustain" groupVolume="0.5" groupPan="-10" loCC1="64" hiCC1="127" seqMode="round_robin" seqLength="2">
  <sample path="Long C4.wav" rootNote="60" loNote="48" hiNote="72" seqPosition="2" volume="1" start="65536" loopEnabled="true" loopStart="70000" loopEnd="99999"/>
  <sample path="Long C4.wav" rootNote="60" loNote="48" hiNote="72" seqPosition="1" volume="1" attack="0.02" tuning="0.25" start="65536" loopEnabled="true" loopStart="70000" loopEnd="99999" loopDuringRelease="false"/>
 </group>
 <group trigger="legato"><sample path="Long C4.wav" rootNote="60"/></group>
</groups>
</DecentSampler>)xml";
    if(!ds.replaceWithText(preset))return Result::fail("write Decent fixture");
    const auto revision=sampler.changeCount();
    {
        BankImportMedia media;media.parent=root;
        Instrument result;String report;
        if(auto r=Plugin::prepareXmlInstrument(ds,sampler.instrument(),result,report,media);r.failed())return r;
        if(sampler.changeCount()!=revision||result.zones.size()!=2||result.groups.size()!=1||!report.contains("1 omitted"))
            return Result::fail("Decent conversion review must be transactional");
        const auto& z=result.zones[0].mapping;
        const auto& g=result.groups[0].playback;
        if(!z.sample||!z.sample->stream||z.sample!=result.zones[1].mapping.sample||z.group==0||z.group!=result.zones[1].mapping.group||
           z.root!=60||z.low!=48||z.high!=72||z.start!=65536||z.loopEnd!=100000||!z.sfz.sustainLoop||
           std::abs(z.gainDb+12.0412f)>0.001f||std::abs(z.sfz.attack-0.02f)>1e-6f||z.cents!=25||z.sfz.pan!=-5||
           g.trigger!=GroupTrigger::controller||g.controller!=1||g.ccLow!=64)
            return Result::fail("Decent gain inheritance, CC, ordered RR, ADSR, trim, tuning and streamed loop");
        sampler.commitImport(result);
        const auto native=root.getChildFile("Decent.studiosampler");
        if(sampler.savePreset(native).failed()||sampler.loadPreset(native).failed()||
           !sampler.instrument().zones[0].mapping.sample||!sampler.instrument().zones[0].mapping.sample->stream)
            return Result::fail("reopen native Decent conversion");
        sampler.commitImport({},false);
    }
    const auto zip=root.getChildFile("Strings.dslibrary");
    {
        ZipFile::Builder builder;builder.addFile(ds,6,"Strings.dspreset");builder.addFile(sample,6,"Long C4.wav");
        auto out=zip.createOutputStream();
        if(!out||!builder.writeToStream(*out,nullptr))return Result::fail("create Decent archive fixture");
    }
    File staged;
    {
        BankImportMedia media;media.parent=root;Instrument result;String report;
        if(auto r=Plugin::prepareXmlInstrument(zip,{},result,report,media);r.failed())return r;
        staged=media.directory;
        if(result.zones.size()!=2||!result.zones[0].file.isAChildOf(staged)||!result.zones[0].mapping.sample->stream)
            return Result::fail("extract and stream Decent library");
    }
    if(staged.exists())return Result::fail("cancelled Decent extraction not cleaned");
    const auto hostile=root.getChildFile("Unsafe.dslibrary");
    {
        ZipFile::Builder builder;builder.addFile(ds,6,"../Escape.dspreset");
        auto out=hostile.createOutputStream();if(!out||!builder.writeToStream(*out,nullptr))return Result::fail("create unsafe ZIP fixture");
    }
    {
        BankImportMedia media;media.parent=root;Instrument result;String report;
        if(Plugin::prepareXmlInstrument(hostile,{},result,report,media).wasOk()||root.getChildFile("Escape.dspreset").exists())
            return Result::fail("reject ZIP traversal");
    }
    const auto nki=root.getChildFile("Legacy.nki");
    const String k2=R"xml(<K2_Container><Programs><K2_Program><Groups><K2_Group><Parameters><V name="volume" value="0.5"/></Parameters><Zones><Zone>
<V name="sampleFile" value="Long C4.wav"/><V name="rootKey" value="60"/><V name="lowKey" value="48"/><V name="highKey" value="72"/>
<V name="lowVelo" value="10"/><V name="highVelo" value="100"/><V name="zoneVolume" value="0.5"/>
<V name="sampleStart" value="65536"/><V name="loopMode" value="1"/><V name="loopStart" value="70000"/><V name="loopEnd" value="100000"/>
</Zone></Zones></K2_Group></Groups></K2_Program></Programs></K2_Container>)xml";
    {
        auto out=nki.createOutputStream();if(!out)return Result::fail("create NKI fixture");
        std::array<char,170> header {};if(!out->write(header.data(),header.size()))return Result::fail("write NKI header");
        GZIPCompressorOutputStream compressor(*out,6);
        const auto bytes=k2.toStdString();
        if(!compressor.write(bytes.data(),bytes.size()))return Result::fail("compress NKI XML");
        compressor.flush();
    }
    {
        BankImportMedia media;Instrument result;String report;
        if(auto r=Plugin::prepareXmlInstrument(nki,{},result,report,media);r.failed())return r;
        const auto& z=result.zones[0].mapping;
        if(result.zones.size()!=1||!z.sample||!z.sample->stream||z.start!=65536||z.loopEnd!=100000||
           !z.sfz.sustainLoop||z.soft!=10||z.hard!=100||std::abs(z.gainDb+12.0412f)>0.001f)
            return Result::fail("legacy readable K2 XML sample mapping");
    }
    const auto bad=root.getChildFile("Unsupported.nki");
    if(!bad.replaceWithText("not a supported Kontakt mapping"))return Result::fail("write invalid NKI fixture");
    BankImportMedia media;Instrument result;String report;
    if(Plugin::prepareXmlInstrument(bad,{},result,report,media).wasOk())return Result::fail("reject unknown NKI container");
    std::atomic<bool> cancel{true};
    if(Plugin::prepareXmlInstrument(ds,{},result,report,media,&cancel).wasOk())return Result::fail("cancel XML instrument conversion");
    return Result::ok();
}

Result verifyGuidedBuilder (const File& directory,WavAudioFormat& wav)
{
    const auto folder=directory.getChildFile("Guided recordings");
    if(folder.createDirectory().failed())return Result::fail("create guided fixtures");
    const auto low=folder.getChildFile("Cello_C4_v1-63.wav");
    const auto high=folder.getChildFile("Cello_C4_v64-127.wav");
    const auto upper=folder.getChildFile("Cello_G4.wav");
    const auto alternate=folder.getChildFile("Cello_C4_v1-63_take2.wav");
    const auto unknown=folder.getChildFile("Unknown.wav");
    for(const auto& f:{low,high,upper,alternate,unknown})
        if(!longSample(f,wav,4096,1))return Result::fail("write guided recording");
    GuidedBuild options;options.name="Guided cello";
    Instrument out;String report;std::atomic<bool> cancel{false};
    if(auto r=Plugin::prepareGuided(options,{upper,high,low},out,report,&cancel);r.failed())return r;
    if(out.name!="Guided cello"||out.zones.size()!=3||out.zones[0].mapping.root!=60||
       out.zones[0].mapping.low!=0||out.zones[0].mapping.high!=63||
       out.zones[0].mapping.hard!=63||out.zones[1].mapping.soft!=64||
       out.zones[2].mapping.low!=64||out.zones[2].mapping.root!=67)
        return Result::fail("guided pitched ranges and velocity layers");
    const auto native=directory.getChildFile("Guided cello.studiosampler");
    if(Plugin::writeConvertedPreset(out,native).failed())return Result::fail("save guided preset");
    Instrument reopened;
    if(Plugin::preparePreset(native,{},reopened).failed()||reopened.zones.size()!=3||
       reopened.zones[0].mapping.high!=63||!reopened.zones[2].mapping.sample)
        return Result::fail("reopen guided preset");
    if(Plugin::prepareGuided(options,{low,alternate},out,report).wasOk()||out.zones.size()!=3)
        return Result::fail("reject implicit overlapping takes transactionally");
    options.roundRobin=true;
    if(auto r=Plugin::prepareGuided(options,{low,alternate,high},out,report);r.failed())return r;
    if(out.zones.size()!=3||out.zones[0].mapping.group==0||out.zones[0].mapping.group!=out.zones[1].mapping.group||
       out.zones[2].mapping.group!=0)return Result::fail("explicit alternate take sequence");
    options.roundRobin=false;options.spread=false;
    if(auto r=Plugin::prepareGuided(options,{low,upper},out,report);r.failed())return r;
    if(out.zones[0].mapping.low!=60||out.zones[0].mapping.high!=60)return Result::fail("original keys only");
    if(Plugin::prepareGuided(options,{low,unknown},out,report).wasOk())return Result::fail("reject unknown pitch among multisamples");
    options.firstNote=55;
    if(auto r=Plugin::prepareGuided(options,{unknown},out,report);r.failed())return r;
    if(out.zones[0].mapping.root!=55)return Result::fail("explicit single-sample root");
    options.layout=GuidedBuild::Layout::percussion;options.firstNote=36;options.playback=GuidedBuild::Playback::oneShot;
    if(auto r=Plugin::prepareGuided(options,{upper,low},out,report);r.failed())return r;
    if(out.zones.size()!=2||out.zones[0].file!=low||out.zones[0].mapping.root!=36||
       out.zones[0].mapping.low!=36||out.zones[0].mapping.high!=36||!out.zones[1].mapping.oneShot||
       out.zones[1].mapping.root!=37)return Result::fail("natural-order drum keys and one-shot");
    options.playback=GuidedBuild::Playback::embeddedLoop;
    if(Plugin::prepareGuided(options,{low},out,report).wasOk())return Result::fail("must not invent a loop");
    cancel.store(true);
    if(Plugin::prepareGuided(options,{low},out,report,&cancel).wasOk())return Result::fail("cancel guided preparation");
    return Result::ok();
}

Result verifyUniversalLibrary (const File& directory,WavAudioFormat& wav)
{
    namespace lib=studio::library;
    const auto source=directory.getChildFile("Library source");
    const auto output=directory.getChildFile("Library output");
    const auto relocated=directory.getChildFile("Relocated recordings");
    if(source.createDirectory().failed()||output.createDirectory().failed()||relocated.createDirectory().failed())return Result::fail("create library fixture folders");
    const auto wavFile=source.getChildFile("Long C4.wav");
    if(!longSample(wavFile,wav,196608,2))return Result::fail("create library WAV");
    const auto sfz=source.getChildFile("Strings.sfz");
    if(!sfz.replaceWithText("<region> sample=Long C4.wav key=60"))return Result::fail("write SFZ");
    lib::Catalog catalog;catalog.roots.add(source.getFullPathName());catalog.roots.add(source.getFullPathName());
    std::atomic<bool> cancel{false};std::atomic<int> progress{0};String report;
    if(auto r=lib::scan(catalog,cancel,progress,report);r.failed())return r;
    if(catalog.entries.size()!=1||catalog.entries[0].source!=sfz)return Result::fail("deduplicate overlapping scan roots and ignore raw audio");
    if(catalog.entries[0].family!="Strings"||lib::suggestedFamily("Violin and Trumpet")!="Unclassified"||lib::suggestedFamily("Mystery patch")!="Unclassified")
        return Result::fail("conservative family classification");
    auto& e=catalog.entries[0];e.artwork=source.getChildFile("Art.png");e.family="Other";e.favorite=true;
    {
        Image image(Image::RGB,64,48,true);Graphics g(image);g.fillAll(Colours::brown);
        auto out=e.artwork.createOutputStream();if(!out||!PNGImageFormat().writeImageToStream(image,*out))return Result::fail("write portrait fixture");
    }
    const auto managed=output.getChildFile("Artwork");
    if(auto r=lib::retainPortrait(e.artwork,e.portrait,managed);r.failed())return r;
    if(!lib::previewImage(lib::portraitFile(e.portrait,managed)).isValid()||!e.artwork.existsAsFile()||lib::portraitFile("../Art.png",managed)!=File())
        return Result::fail("managed portrait and safe path");
    if(!lib::matches(e,"strings","Other",true,"sfz")||lib::matches(e,"","Strings",false,{})||lib::matches(e,"","Other",false,"nki"))
        return Result::fail("family/favorites/search filtering");
    if(auto r=lib::convert(e,output,cancel);r.failed())return r;
    const auto originalPreset=e.converted;
    if(!originalPreset.existsAsFile())return Result::fail("native batch preset missing");
    if(lib::convert(e,output,cancel).failed()||e.converted!=originalPreset)return Result::fail("repeat conversion must reuse existing output");
    const auto index=output.getChildFile("Catalog.xml");
    if(lib::save(catalog,index).failed())return Result::fail("persist catalog");
    lib::Catalog reopened;if(lib::read(index,reopened).failed()||reopened.entries.size()!=1||
        reopened.entries[0].converted!=originalPreset||reopened.entries[0].artwork!=e.artwork||
        reopened.entries[0].family!="Other"||!reopened.entries[0].favorite||reopened.entries[0].portrait!=e.portrait)
        return Result::fail("restore catalog, converted preset, family, favorite and artwork");
    if(lib::scan(reopened,cancel,progress,report).failed()||reopened.entries[0].family!="Other"||!reopened.entries[0].favorite||reopened.entries[0].portrait!=e.portrait)
        return Result::fail("rescan preserves explicit family and portrait");
    if(!wavFile.moveFileTo(relocated.getChildFile(wavFile.getFileName())))return Result::fail("move source recording");
    if(lib::convert(reopened.entries[0],output,cancel).wasOk())return Result::fail("existing conversion with missing source must not report success");
    if(auto r=lib::repair(reopened.entries[0],output,relocated,cancel);r.failed())return r;
    if(reopened.entries[0].converted==originalPreset||!originalPreset.existsAsFile())return Result::fail("repair creates a separate preset");
    Instrument model;
    if(Plugin::preparePreset(reopened.entries[0].converted,{},model).failed()||model.zones.size()!=1||
       !model.zones[0].mapping.sample||!model.zones[0].mapping.sample->stream||model.zones[0].file.getParentDirectory()!=relocated)
       return Result::fail("repaired preset reopens with correct streamed sample");
    if(!sfz.appendText("\n// changed source definition\n"))return Result::fail("change source definition");
    if(lib::convert(reopened.entries[0],output,cancel).wasOk())return Result::fail("changed source requires rescan");
    if(!source.moveFileTo(directory.getChildFile("Offline library source")))return Result::fail("simulate offline library");
    if(lib::scan(reopened,cancel,progress,report).failed()||reopened.entries.size()!=1||reopened.entries[0].available)
        return Result::fail("retain offline catalog metadata");
    cancel.store(true);
    if(lib::scan(reopened,cancel,progress,report).wasOk()||reopened.entries.size()!=1)
        return Result::fail("cancel scan transactionally");
    return Result::ok();
}

Plugin* addSampler (te::Edit& edit, te::AudioTrack& track)
{
    auto instrument = edit.getPluginCache().createNewPlugin (Plugin::xmlTypeName, {});
    if (! instrument) return nullptr;
    track.pluginList.insertPlugin (instrument, 0, nullptr);
    return dynamic_cast<Plugin*> (findInstrument (track));
}

Result recordMidi (te::Edit& edit, te::AudioTrack& track, Playback& playback)
{
    auto& transport = edit.getTransport();
    transport.setPosition (3_tp);
    transport.ensureContextAllocated();

    bool routed = false;
    for (auto* instance : edit.getAllInputDevices())
        if (auto* input = dynamic_cast<te::MidiInputDevice*> (&instance->getInputDevice());
            input && te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*input))
        {
            input->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            if (! instance->setTarget (track.itemID, true, nullptr, 0))
                return Result::fail ("route hosted input for sampler recording");
            instance->setRecordingEnabled (track.itemID, true);
            routed = true;
            break;
        }
    if (! routed) return Result::fail ("hosted MIDI input");

    edit.dispatchPendingUpdatesSynchronously();
    transport.record (false);
    if (! transport.isRecording()) return Result::fail ("start sampler MIDI take");
    playback.peak (16);
    MidiBuffer events;
    events.addEvent (MidiMessage::controllerEvent (3, 11, 90), 0);
    events.addEvent (MidiMessage::noteOn (3, 60, static_cast<uint8> (90)), 0);
    playback.block (events);
    if (playback.peak (20) < .01f) return Result::fail ("monitor live MIDI during recording");
    events.clear();
    events.addEvent (MidiMessage::noteOff (3, 60), 0);
    playback.block (events);
    playback.peak (20);
    transport.stop (false, false);
    edit.dispatchPendingUpdatesSynchronously();

    for (auto* item : track.getClips())
        if (auto* take = dynamic_cast<te::MidiClip*> (item); take && take->getPosition().getStart() >= 2_tp)
        {
            const auto& sequence = take->getSequence();
            if (take->getMidiChannel().getChannelNumber() != 3 || sequence.getNumNotes() != 1
                || sequence.getNotes().getFirst()->getNoteNumber() != 60)
                return Result::fail ("retain recorded note on MIDI channel 3");
            const auto& controllers = sequence.getControllerEvents();
            const auto hasExpression = std::any_of (controllers.begin(), controllers.end(), [] (const auto* event)
            {
                return event->getType() == 11 && event->getControllerValue() == (90 << 7);
            });
            if (! hasExpression) return Result::fail ("retain recorded CC11 after Stop");

            transport.setPosition (take->getPosition().getStart());
            transport.play (false);
            const auto signal = playback.peak (45);
            transport.stop (false, false);
            transport.freePlaybackContext();
            return signal > .01f ? Result::ok() : Result::fail ("play recorded sampler take");
        }
    return Result::fail ("MIDI take was not retained after Stop");
}

Result run (const File& screenshots)
{
    const auto directory = File::getSpecialLocation (File::tempDirectory)
        .getNonexistentChildFile ("studio-sampler-test", {}, false);
    if (directory.createDirectory().failed()) return Result::fail ("create fixture directory");
    const ScopeGuard cleanup { [directory] { directory.deleteRecursively(); } };
    te::Engine engine (std::make_unique<TestProperties> (directory), {}, std::make_unique<OfflineEngine>());
    registerPlugin (engine);
    spatial::registerPlugins (engine);
    Playback playback (engine);
    const auto project = directory.getChildFile ("Sampler Test.tracktionedit");
    const auto wavFile = directory.getChildFile ("Piano C4.wav");
    const auto aiffFile = directory.getChildFile ("Piano G4.aiff");
    const auto preset = directory.getChildFile ("Sampler Test.studiosampler");
    WavAudioFormat wav;
    AiffAudioFormat aiff;
    if (! testSample (wavFile, wav, 261.626, 2)
        || ! testSample (aiffFile, aiff, 391.995, 1))
        return Result::fail ("write 24-bit WAV and AIFF fixtures");

    auto edit = te::createEmptyEdit (engine, project);
    if (! edit) return Result::fail ("create Tracktion project");
    auto tracks = te::getAudioTracks (*edit);
    auto first = tracks.isEmpty() ? edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr)
                                  : te::AudioTrack::Ptr (tracks.getFirst());
    auto second = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (! first || ! second) return Result::fail ("create tracks");
    first->getOutput().setOutputToDefaultDevice (false);
    second->getOutput().setOutputToDefaultDevice (false);
    first->setName ("Sampler test");
    second->setName ("Second sampler");
    auto* sampler = addSampler (*edit, *first);
    auto* other = addSampler (*edit, *second);
    if (! sampler || ! other) return Result::fail ("register, insert and identify two native instruments");
    edit->playInStopEnabled = true;
    auto& transport = edit->getTransport();
    edit->dispatchPendingUpdatesSynchronously();
    transport.ensureContextAllocated (true);
    playback.peak (3);
    {
        Editor editor (*sampler);
        DocumentWindow window ("Studio Sampler", colours::panel, 0);
        window.setUsingNativeTitleBar (true);
        window.setContentNonOwned (&editor, true);
        window.centreWithSize (window.getWidth(), window.getHeight());
        window.setAlwaysOnTop (true);
        window.setVisible (true);
        window.toFront (true);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        if (! editor.runReview (wavFile, aiffFile, screenshots, [&] { return playback.peak (8) > 0.01f; }))
            return Result::fail ("editor interactions or screenshots");
    }

    const auto metadataFile = directory.getChildFile ("Metadata.wav");
    if (! testSample (metadataFile, wav, 523.251, 1, 72)) return Result::fail ("write root-note metadata fixture");
    Instrument metadataProposal; String metadataSummary;
    if (Plugin::prepareImport ({}, { metadataFile }, true, metadataProposal, metadataSummary).failed()
        || metadataProposal.zones[0].mapping.root != 72) return Result::fail ("embedded WAV root metadata mapping");
    Instrument cancelledProposal; String cancelledSummary; std::atomic<bool> cancelled { true }; std::atomic<int> progress { 0 };
    if (Plugin::prepareImport ({}, { directory }, true, cancelledProposal, cancelledSummary, &cancelled, &progress).wasOk())
        return Result::fail ("cancel folder import before enumeration");
    const auto layeredFile = directory.getChildFile ("Layer_C4_v64-127.wav");
    if (! wavFile.copyFileTo (layeredFile)) return Result::fail ("create velocity filename fixture");
    Instrument layeredProposal; String layeredSummary;
    if (Plugin::prepareImport ({}, { layeredFile }, true, layeredProposal, layeredSummary).failed()
        || layeredProposal.zones[0].mapping.soft != 64 || layeredProposal.zones[0].mapping.hard != 127)
        return Result::fail ("velocity filename ranges in import proposal");
    auto cached = layeredProposal.zones[0].mapping.sample;
    if (Plugin::prepareImport (layeredProposal, { layeredFile }, true, cancelledProposal, cancelledSummary).failed()
        || cancelledProposal.zones[1].mapping.sample != cached) return Result::fail ("decoded sample cache reuse");
    // Bulk import proposals must not mutate live audio or undo history until accepted.
    Instrument proposal; String summary;
    const auto revision = other->changeCount();
    if (other->proposeImport ({ wavFile, aiffFile, directory.getChildFile ("unsupported.txt") }, true, proposal, summary).failed()
        || other->changeCount() != revision || proposal.zones.size() != 2
        || proposal.zones[0].mapping.root != 60 || proposal.zones[0].mapping.high != 63
        || proposal.zones[1].mapping.low != 64 || ! summary.contains ("1 unsupported"))
        return Result::fail ("deterministic bulk import preview and cancellation");
    if (auto streamed = verifyStreamingImport (directory, *other, wav, aiff); streamed.failed())
        return streamed;
    if (auto imported = verifySfzImport (directory, *other, wav); imported.failed()) return imported;
    if (auto bank = verifySoundBankImport (directory, *other); bank.failed()) return bank;
    if (auto exs = verifyExsImport (directory, *other, wav); exs.failed()) return exs;
    if (auto legacy = verifyLegacyBankImport (directory, *other); legacy.failed()) return legacy;
    if (auto xml = verifyXmlInstrumentImport (directory, *other, wav); xml.failed()) return xml;
    if (auto library = verifyUniversalLibrary (directory, wav); library.failed()) return library;
    if (auto guided = verifyGuidedBuilder (directory, wav); guided.failed()) return guided;
    {
        const auto shared=directory.getChildFile("Shared sample M26.wav");
        if(!longSample(shared,wav,196608,2))return Result::fail("write shared sampler recording");
        Instrument first,second;
        if(Plugin::prepareSample({},shared,-1,first).failed()||Plugin::prepareSample({},shared,-1,second).failed()
            ||!first.zones[0].mapping.sample||first.zones[0].mapping.sample!=second.zones[0].mapping.sample
            ||!first.zones[0].mapping.sample->stream)
            return Result::fail("independent sampler proposals must share source PCM and stream cache");
    }
    other->commitImport (proposal);
    other->editGroups ({ { 0, "Piano Soft" }, { 1, "Piano Loud" } });
    auto groups = other->instrument().groups;
    groups[0].playback.trigger = GroupTrigger::keyswitch; groups[0].playback.key = 24;
    groups[1].playback.trigger = GroupTrigger::controller; groups[1].playback.controller = 20; groups[1].playback.ccLow = 64;
    groups[0].playback.mute = true;
    other->editGroups (groups);
    if (other->articulations().size() != 2) return Result::fail ("muting preserves verified articulation assignments");
    groups[0].playback.mute = false; other->editGroups (groups);
    other->moveZone (1, 1);
    other->undoMapping (false);
    if (other->instrument().zones[1].folder != 0) return Result::fail ("undo group assignment");
    other->undoMapping (true);
    if (other->instrument().zones[1].folder != 1) return Result::fail ("redo group assignment");
    const auto groupPreset = directory.getChildFile ("Groups.studiosampler");
    if (other->savePreset (groupPreset).failed() || other->loadPreset (groupPreset).failed()
        || other->instrument().groups.size() != 2 || other->instrument().groups[1].name != "Piano Loud"
        || other->instrument().zones[1].folder != 1 || other->articulations().size() != 2
        || other->instrument().groups[1].playback.ccLow != 64) return Result::fail ("named group and articulation persistence");
    if (other->savePreset (wavFile).wasOk()) return Result::fail ("protect source recording");
    auto legacy = XmlDocument::parse (groupPreset);
    legacy->setAttribute ("version", 1);
    legacy->deleteAllChildElementsWithTagName ("GROUP");
    for (auto* child : legacy->getChildIterator()) { child->removeAttribute ("start"); child->removeAttribute ("end"); child->removeAttribute ("folder"); }
    const auto legacyFile = directory.getChildFile ("Legacy.studiosampler");
    if (! legacy->writeTo (legacyFile) || other->loadPreset (legacyFile).failed()
        || other->instrument().groups.size() != 1 || other->instrument().zones[0].mapping.start != 0)
        return Result::fail ("version one preset migration");
    if (sampler->instrument().zones.size() != 2 || sampler->savePreset (preset).failed()
        || other->loadPreset (preset).failed() || other->instrument().zones.size() != 2
        || other->instrument().settings.attack != sampler->instrument().settings.attack
        || other->instrument().zones[0].mapping.loop != sampler->instrument().zones[0].mapping.loop)
        return Result::fail ("preserve instrument parameters through preset");
    other->removeZone (1);
    if (sampler->instrument().zones.size() != 2 || other->instrument().zones.size() != 1)
        return Result::fail ("instances must be editable independently");

    auto clip = first->insertMIDIClip ("Sampler notes", { 0_tp, 2_tp }, nullptr);
    if (! clip) return Result::fail ("create sampler MIDI clip");
    clip->setMidiChannel (te::MidiChannel (3));
    clip->getSequence().addNote (60, 0_bp, 1_bd, 96, 0, nullptr);
    clip->getSequence().addControllerEvent (0_bp, 11, 110 << 7, nullptr);
    spatial::Room room;
    if (auto send = spatial::setRoomSend (*first, true, { 0, 0 }, spatial::Seat().sendTrim, room); send.failed())
        return send;
    edit->dispatchPendingUpdatesSynchronously();
    transport.setPosition (0_tp);
    transport.play (false);
    const auto signal = playback.peak (30);
    if (signal < .01f || ! std::isfinite (signal)) return Result::fail ("clip playback through Studio master");
    const auto wet = spatial::roomSignal (*edit);
    if (! wet || wet->wetPeak <= 0 || ! wet->reachesMaster) return Result::fail ("sampler reaches shared Room Return");
    transport.setPosition (1.5_tp, 1.5_tp);
    playback.peak (12);
    if (sampler->resourceSnapshot().activeVoices.value_or (1) != 0)
        return Result::fail ("transport seek must clear sampler voices");
    transport.setPosition (0_tp, 0_tp);
    if (playback.peak (20) < .01f) return Result::fail ("note retriggers after seek");
    transport.stop (false, false);
    playback.peak (200);
    if (sampler->resourceSnapshot().activeVoices.value_or (1) != 0)
        return Result::fail ("transport stop must clear sampler voices");
    transport.freePlaybackContext();

    if (auto recorded = recordMidi (*edit, *second, playback); recorded.failed()) return recorded;

    studio::library::writePresentation(*sampler,{"Fixture strings","Strings","fixture.png"});
    if (! te::EditFileOperations (*edit).writeToFile (project, false))
        return Result::fail ("write Tracktion edit");
    clip = nullptr;
    first = nullptr;
    second = nullptr;
    edit.reset();

    if (! aiffFile.deleteFile()) return Result::fail ("remove AIFF to exercise missing-sample recovery");
    edit = te::loadEditFromFile (engine, project);
    if (! edit) return Result::fail ("load Tracktion edit");
    sampler = other = nullptr;
    for (auto* track : te::getAudioTracks (*edit))
        if (track->getName() == "Sampler test") sampler = dynamic_cast<Plugin*> (findInstrument (*track));
        else if (track->getName() == "Second sampler") other = dynamic_cast<Plugin*> (findInstrument (*track));
    if (! sampler || ! other || sampler->instrument().zones.size() != 2 || other->instrument().zones.size() != 1)
        return Result::fail ("restore both instances");
    const auto presentation=studio::library::readPresentation(*sampler);
    if(presentation.name!="Fixture strings"||presentation.family!="Strings"||presentation.portrait!="fixture.png"||
       studio::library::readPresentation(*other).portrait.isNotEmpty())return Result::fail("restore instance-specific library presentation");
    auto firstZone = sampler->instrument().zones[0];
    auto missing = sampler->instrument().zones[1];
    if (! firstZone.mapping.sample || ! firstZone.mapping.loop || firstZone.mapping.high != 64
        || std::abs (sampler->instrument().settings.attack - .023f) > .0001f
        || missing.mapping.sample || missing.file != aiffFile || missing.problem.isEmpty())
        return Result::fail ("restored parameters and missing sample");
    if (sampler->loadSample (wavFile, 1).failed()
        || sampler->instrument().zones[1].mapping.low != missing.mapping.low)
        return Result::fail ("relink sample and retain mapping");
    edit->playInStopEnabled = true;
    auto& restoredTransport = edit->getTransport();
    restoredTransport.ensureContextAllocated();
    restoredTransport.setPosition (0_tp);
    restoredTransport.play (false);
    if (playback.peak (30) < .01f) return Result::fail ("reopened project plays through the master");
    restoredTransport.stop (false, false);
    restoredTransport.freePlaybackContext();
    return Result::ok();
}
}

int runSamplerIntegrationTest (const juce::File& screenshots)
{
    const auto result = run (screenshots);
    if (result.failed())
    {
        std::fprintf (stderr, "Studio Sampler integration failed: %s\n", result.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::puts ("Studio Sampler editor, hosted MIDI recording and playback, Room Return and restoration passed.");
    return 0;
}
}
