// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialRouting.h"
#include "StudioSpatialEffect.h"
#include "StudioReverbEffect.h"
#include "SpatialReverb.h"
#include "SpatialState.h"
#include "SpatialPublication.h"
#include <atomic>
#include <bit>

using namespace juce;
namespace te = tracktion;

namespace studio::spatial
{
namespace
{
const Identifier roomProperty { "studioRoomSettings" }, panProperty { "studioStagePan" },
    roleProperty { "studioSpatialRole" }, sendGainProperty { "studioSpatialSendDb" };
const String returnRole { "roomReturnV1" }, receiverRole { "roomInputV1" };

bool isSceneAcousticChange (const ValueTree& tree)
{
    return tree.hasType (sceneType) || tree.hasType (roomType) || tree.hasType (microphonesType) || tree.hasType (micType);
}

class SceneListener final : private ValueTree::Listener
{
public:
    SceneListener (ValueTree root, std::function<void(const ValueTree&)> callback)
        : state (std::move (root)), changed (std::move (callback)) { state.addListener (this); }
    ~SceneListener() override { state.removeListener (this); }
private:
    ValueTree state;
    std::function<void(const ValueTree&)> changed;
    void valueTreePropertyChanged (ValueTree& tree, const Identifier&) override { changed (tree); }
    void valueTreeChildAdded (ValueTree& parent, ValueTree& child) override
    {
        changed (child);
        if (child.hasType (te::IDs::PLUGIN)) changed (parent);
    }
    void valueTreeChildRemoved (ValueTree& parent, ValueTree& child, int) override
    {
        if (child.hasType (seatType)) changed (parent);
        else changed (child);
    }
    void valueTreeChildOrderChanged (ValueTree& parent, int, int) override { changed (parent); }
};

const Identifier trackKeyProperty { "studioStageTrackID" };

class StageSourcePlugin final : public te::Plugin
{
public:
    static constexpr const char* xmlTypeName = "studioStageSourceV2";
    explicit StageSourcePlugin (te::PluginCreationInfo info)
        : Plugin (info), listener (edit.state, [this] (const ValueTree& node)
          {
              const auto owner = state.getParent();
              if (isSceneAcousticChange (node) || node == owner
                  || (node.hasType (seatType) && node.getParent() == owner))
                  publish();
          })
    { publish(); }
    ~StageSourcePlugin() override { notifyListenersOfDeletion(); }
    String getName() const override { return "Studio Stage Source"; }
    String getPluginType() override { return xmlTypeName; }
    String getVendor() override { return "Studio"; }
    String getSelectableDescription() override { return "Physical Stage source and microphone paths"; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool noTail() override { return false; }
    double getTailLength() const override { return .35; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    int getNumOutputChannelsGivenInputs (int inputs) override { return jmax (2, inputs); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }

    void initialise (const te::PluginInitialisationInfo& info) override
    {
        rate.store (float (info.sampleRate));
        accepted = 0;
        Snapshot snapshot {};
        publication.read (snapshot, accepted);
        const auto plan = stage::makePlan (info.sampleRate, snapshot.settings);
        try { active = renderer.prepare (info.sampleRate, plan); }
        catch (const std::bad_alloc&) { active = false; }
        clearPeaks();
    }

    void deinitialise() override { active = false; clearPeaks(); }
    void reset() override { renderer.reset(); clearPeaks(); }

    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        auto* buffer = context.destBuffer;
        if (! active || buffer == nullptr || buffer->getNumChannels() == 0) return;
        Snapshot snapshot;
        const auto currentRate = rate.load (std::memory_order_relaxed);
        if (publication.read (snapshot, accepted))
            renderer.setPlan (snapshot.planRate == currentRate ? snapshot.plan : stage::makePlan (currentRate, snapshot.settings));
        const int start = context.bufferStartSample;
        const auto levels = renderer.process (buffer->getWritePointer (0, start),
            buffer->getNumChannels() > 1 ? buffer->getWritePointer (1, start) : nullptr,
            context.bufferNumSamples);
        const float fall = 1.f / (1.f + float (context.bufferNumSamples) / (.22f * currentRate));
        for (size_t i = 0; i < peaks.size(); ++i)
            peaks[i].store (std::max (levels[i], peaks[i].load (std::memory_order_relaxed) * fall),
                            std::memory_order_relaxed);
    }

    void configure (te::EditItemID trackID)
    {
        state.setProperty (trackKeyProperty, trackID.toString(), nullptr);
    }

    void collectPeaks (std::array<float, stage::microphoneLimit>& result) const
    {
        for (size_t i = 0; i < peaks.size(); ++i)
            result[i] = std::max (result[i], peaks[i].load (std::memory_order_relaxed));
    }

private:
    struct Snapshot
    {
        stage::Settings settings;
        stage::Plan plan;
        float planRate = 0;
    };
    Publication<Snapshot> publication;
    std::atomic<float> rate { 48000 };
    std::array<std::atomic<float>, stage::microphoneLimit> peaks {};
    unsigned accepted = 0;
    bool active = false;
    stage::Renderer renderer;
    SceneListener listener;

    void clearPeaks()
    {
        for (auto& peak : peaks) peak.store (0, std::memory_order_relaxed);
    }

    void publish()
    {
        const auto scene = edit.state.getChildWithName (sceneType);
        Snapshot snapshot {};
        auto& settings = snapshot.settings;
        settings.room = readRoom (scene.getChildWithName (roomType));
        settings.bypass = ! isPhysicalScene (scene) || (bool) scene.getProperty ("stageBypass", false);
        settings.listener = { (float) scene.getProperty ("listenerX", 0.f),
            (float) scene.getProperty ("listenerDepth", .08f) };
        settings.listenerHeight = (float) scene.getProperty ("listenerHeight", 1.4f);
        auto owner = state.getParent();
        if (! owner.hasType (te::IDs::TRACK))
            if (auto* track = te::findAudioTrackForID (edit, te::EditItemID::fromString (state[trackKeyProperty].toString())))
                owner = track->state;
        const auto seat = owner.getChildWithName (seatType);
        settings.seat = readPhysicalSeat (seat, settings.room).position;
        settings.source = readSource (seat);
        const auto microphones = scene.getChildWithName (microphonesType);
        settings.microphoneMix = (bool) scene.getProperty ("microphoneMix", false)
            && microphones.getNumChildren() <= (int) stage::microphoneLimit;
        for (const auto mic : microphones)
        {
            if (settings.microphoneCount == (int) stage::microphoneLimit) break;
            if (mic.hasType (micType)) settings.microphones[(size_t) settings.microphoneCount++] = readMic (mic);
        }
        snapshot.planRate = rate.load();
        snapshot.plan = stage::makePlan (snapshot.planRate, settings);
        publication.write (snapshot);
    }

    void valueTreePropertyChanged (ValueTree& node, const Identifier& property) override
    {
        if (node == state && property == trackKeyProperty) publish();
        Plugin::valueTreePropertyChanged (node, property);
    }
};

class RoomPlugin final : public te::Plugin, private Timer
{
public:
    static constexpr const char* xmlTypeName = "studioWetRoomV1";

    explicit RoomPlugin (te::PluginCreationInfo info)
        : Plugin (info), listener (edit.state, [this] (const ValueTree& node)
          {
              if (node.hasType (sceneType) || node.hasType (roomType)) publishRoom();
          })
    {
        publishRoom();
    }

    ~RoomPlugin() override
    {
        stopTimer();
        notifyListenersOfDeletion();
    }

    String getName() const override { return "Studio Wet Room"; }
    String getPluginType() override { return xmlTypeName; }
    String getVendor() override { return "Studio"; }
    String getSelectableDescription() override { return "Studio shared algorithmic room return"; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    // Tracktion's generic insert bypass would pass the dry aux feed through.
    // The dedicated room bypass instead fades the wet output to silence.
    bool canBeDisabled() override { return false; }
    bool noTail() override { return false; }
    double getTailLength() const override { return 12.4; }
    void reset() override { reverb.reset(); }
    bool producesAudioWhenNoAudioInput() override { return true; }
    int getNumOutputChannelsGivenInputs (int inputs) override { return jmax (2, inputs); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }

    void initialise (const te::PluginInitialisationInfo& info) override
    {
        acceptedRevision = 1; // Odd revisions are never accepted by the reader.
        const auto revision = revisionNumber.load();
        auto initial = readPublishedPlan();
        if ((revision & 1u) == 0 && revision == revisionNumber.load())
            acceptedRevision = revision;
        else
            initial = makeRoomPlan (Room());
        reverb.setCheckEnabled (checkEnabled.load());
        reverb.preparePlan (info.sampleRate, initial);
        // Compute the meter fall per sample during preparation. The render
        // callback uses the actual block length, including automation splits.
        meterTime = (float) (0.30 * jmax (8000.0, info.sampleRate));
        inputPeak.store (0.0f);
        wetPeak.store (0.0f);
        bytes.store (reverb.allocatedBytes());
    }

    void deinitialise() override
    {
        inputPeak.store (0.0f);
        wetPeak.store (0.0f);
    }

    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        auto* buffer = context.destBuffer;
        if (buffer == nullptr || buffer->getNumChannels() == 0) return;
        const auto revision = revisionNumber.load();
        if (revision != acceptedRevision && (revision & 1u) == 0)
        {
            const auto next = readPublishedPlan();
            if (revision == revisionNumber.load())
            {
                acceptedRevision = revision;
                reverb.setPlan (next);
            }
        }
        const auto start = context.bufferStartSample;
        reverb.setCheckEnabled (checkEnabled.load (std::memory_order_relaxed));
        const auto peaks = reverb.process (buffer->getWritePointer (0, start),
            buffer->getNumChannels() > 1 ? buffer->getWritePointer (1, start) : nullptr, context.bufferNumSamples);
        const float fall = 1.0f / (1.0f + (float) context.bufferNumSamples / meterTime);
        inputPeak.store (jmax (peaks.input, fall * inputPeak.load (std::memory_order_relaxed)), std::memory_order_relaxed);
        wetPeak.store (jmax (peaks.wet, fall * wetPeak.load (std::memory_order_relaxed)), std::memory_order_relaxed);
        for (int channel = 2; channel < buffer->getNumChannels(); ++channel)
            buffer->clear (channel, start, context.bufferNumSamples);
    }

    void setRoom (Room room)
    {
        const auto format = XmlElement::TextFormat().singleLine();
        state.setProperty (roomProperty, writeRoom (room, {}).createXml()->toString (format), getUndoManager());
    }

    std::size_t memoryBytes() const { return bytes.load(); }

    bool isBypassed() const { return bypassed.load(); }

    void setCheck (bool enable)
    {
        checkEnabled.store (enable && ! isBypassed());
        if (checkEnabled.load()) startTimer (15000);
        else stopTimer();
    }

    RoomSignal getSignal (bool reachesMaster) const
    {
        return { inputPeak.load (std::memory_order_relaxed), wetPeak.load (std::memory_order_relaxed),
                 isBypassed(), checkEnabled.load(), reachesMaster };
    }

private:
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free);
    static constexpr size_t parameterCount = sizeof (RoomPlan) / sizeof (float);
    using PlanScalars = std::array<float, parameterCount>;
    static_assert (sizeof (RoomPlan) == sizeof (PlanScalars));
    std::array<std::atomic<float>, parameterCount> publication {};
    std::atomic<unsigned> revisionNumber { 0 };
    std::atomic<std::size_t> bytes { 0 };
    unsigned acceptedRevision = 0;
    std::atomic<float> inputPeak { 0.0f }, wetPeak { 0.0f };
    std::atomic<bool> checkEnabled { false }, bypassed { false };
    float meterTime = 14400.0f;
    RoomReverb reverb;
    SceneListener listener;

    void timerCallback() override { setCheck (false); }

    void valueTreePropertyChanged (ValueTree& tree, const Identifier& p) override
    {
        if (tree == state && p == roomProperty)
        {
            publishRoom();
            if (isBypassed()) setCheck (false);
        }
        Plugin::valueTreePropertyChanged (tree, p);
    }

    void publishRoom()
    {
        const auto xml = parseXML (state.getProperty (roomProperty).toString());
        const auto scene = edit.state.getChildWithName (sceneType);
        auto room = isPhysicalScene (scene) ? readRoom (scene.getChildWithName (roomType))
            : xml != nullptr ? readRoom (ValueTree::fromXml (*xml)) : Room();
        if (isPhysicalScene (scene) && (bool) scene.getProperty ("stageBypass", false)) room.bypass = true;
        const auto plan = makeRoomPlan (room, isPhysicalScene (scene) ? RoomMode::stage : RoomMode::legacy);
        bypassed.store (plan.bypass != 0);
        const auto values = std::bit_cast<PlanScalars> (plan);
        // Single writer on the settings thread. The render reader keeps its
        // last complete plan if a new plan overlaps its bounded scalar read.
        ++revisionNumber;
        for (size_t i = 0; i < values.size(); ++i) publication[i].store (values[i]);
        ++revisionNumber;
    }

    RoomPlan readPublishedPlan() const
    {
        PlanScalars values {};
        for (size_t i = 0; i < values.size(); ++i) values[i] = publication[i].load();
        return std::bit_cast<RoomPlan> (values);
    }
};

class StagePanPlugin final : public te::Plugin
{
public:
    static constexpr const char* xmlTypeName = "studioStagePanV1";
    explicit StagePanPlugin (te::PluginCreationInfo info) : Plugin (info)
    {
        publishPan();
    }
    ~StagePanPlugin() override
    {
        notifyListenersOfDeletion();
    }
    String getName() const override { return "Studio Stage Pan"; }
    String getPluginType() override { return xmlTypeName; }
    String getVendor() override { return "Studio"; }
    String getSelectableDescription() override { return "Studio stage stereo balance"; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    int getNumOutputChannelsGivenInputs (int channels) override { return jmax (2, channels); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise (const te::PluginInitialisationInfo& info) override
    {
        smoothing = (float) -std::expm1 (-1.0 / (jmax (8000.0, info.sampleRate) * 0.025));
        current = desired.load();
    }
    void deinitialise() override {}

    void applyToBuffer (const te::PluginRenderContext& c) override
    {
        if (c.destBuffer == nullptr || c.destBuffer->getNumChannels() < 2) return;
        const float next = desired.load();
        if (next == 0.0f && std::abs (current) < 1.0e-5f) { current = 0.0f; return; }
        auto* left = c.destBuffer->getWritePointer (0, c.bufferStartSample);
        auto* right = c.destBuffer->getWritePointer (1, c.bufferStartSample);
        for (int i = 0; i < c.bufferNumSamples; ++i)
        {
            current += smoothing * (next - current);
            left[i] *= 1.0f - jmax (0.0f, current);
            right[i] *= 1.0f + jmin (0.0f, current);
        }
    }
    void setPan (float value) { state.setProperty (panProperty, value, getUndoManager()); }

private:
    std::atomic<float> desired { 0 };
    float smoothing = 0.001f, current = 0;
    void publishPan() { desired.store (validPosition ({ (float) state.getProperty (panProperty, 0.0f), 0 }).x); }
    void valueTreePropertyChanged (ValueTree& tree, const Identifier& p) override
    {
        if (tree == state && p == panProperty) publishPan();
        Plugin::valueTreePropertyChanged (tree, p);
    }
};

// Retain Tracktion's AuxSendNode and its muting/ramp behavior. Tracktion's
// generic attached-value listener does not set the base parameter on undo;
// this dedicated persistent value does so without writing automation.
class RoomSendPlugin final : public te::AuxSendPlugin
{
public:
    static constexpr const char* xmlTypeName = "studioRoomSendV1";
    explicit RoomSendPlugin (te::PluginCreationInfo info) : AuxSendPlugin (info)
    {
        publishGain();
    }
    String getName() const override { return "Studio Room Send"; }
    String getPluginType() override { return xmlTypeName; }
    String getVendor() override { return "Studio"; }
    void setSpatialGain (float db) { state.setProperty (sendGainProperty, db, getUndoManager()); }

private:
    void publishGain()
    {
        const float db = bounded ((float) state.getProperty (sendGainProperty, -24.0f), -70, 0, -24);
        gain->setParameter (te::decibelsToVolumeFaderPosition (db), dontSendNotification);
    }
    void valueTreePropertyChanged (ValueTree& value, const Identifier& property) override
    {
        if (value == state && property == sendGainProperty) publishGain();
        AuxSendPlugin::valueTreePropertyChanged (value, property);
    }
};

template <typename T>
T* find (te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* typed = dynamic_cast<T*> (plugin)) return typed;
    return nullptr;
}

struct BusClaims { int sends = 0, returns = 0; bool foreign = false; };

void collectBusClaims (const ValueTree& tree, std::array<BusClaims, 16>& claims)
{
    const auto type = tree.getProperty (te::IDs::type).toString();
    if (type == RoomSendPlugin::xmlTypeName || type == te::AuxSendPlugin::xmlTypeName || type == te::AuxReturnPlugin::xmlTypeName)
    {
        const int bus = (int) tree.getProperty (te::IDs::busNum, -1);
        if (isPositiveAndBelow (bus, (int) claims.size()))
        {
            auto& claim = claims[(size_t) bus];
            if (type == RoomSendPlugin::xmlTypeName) ++claim.sends;
            else if (type == te::AuxReturnPlugin::xmlTypeName && tree.getProperty (roleProperty).toString() == receiverRole)
                ++claim.returns;
            else claim.foreign = true;
        }
    }
    for (const auto child : tree) collectBusClaims (child, claims);
}

struct Return { te::AudioTrack* track = nullptr; RoomPlugin* room = nullptr; int bus = -1; };

Return findReturn (te::Edit& edit)
{
    Return found;
    for (auto* track : te::getAudioTracks (edit))
        if (isRoomReturn (*track))
        {
            if (found.track != nullptr) return {};
            found.track = track;
            for (auto* plugin : track->pluginList)
            {
                if (auto* aux = dynamic_cast<te::AuxReturnPlugin*> (plugin);
                    aux != nullptr && aux->state.getProperty (roleProperty).toString() == receiverRole && aux->isEnabled())
                    found.bus = (int) aux->busNumber;
                if (auto* room = dynamic_cast<RoomPlugin*> (plugin); room != nullptr && found.bus >= 0)
                    found.room = room;
            }
        }
    return found;
}

Return ensureReturn (te::Edit& edit, Room settings)
{
    std::array<BusClaims, 16> claims {};
    collectBusClaims (edit.state, claims);
    if (auto present = findReturn (edit); present.track != nullptr)
    {
        if (! isPositiveAndBelow (present.bus, (int) claims.size())) return {};
        const auto& claim = claims[(size_t) present.bus];
        return ! claim.foreign && claim.returns == 1 ? present : Return {};
    }
    // Existing damaged/duplicated managed tracks require an explicit repair.
    // Never make a second return just because the existing one is ambiguous.
    for (auto* track : te::getAudioTracks (edit))
        if (isRoomReturn (*track)) return {};
    int bus = -1;
    // Recover an orphaned Studio bus only if it still belongs exclusively
    // to the existing Studio sends. Never hijack an unrelated send or return.
    for (int candidate = 0; candidate < (int) claims.size(); ++candidate)
    {
        const auto& claim = claims[(size_t) candidate];
        if (claim.sends == 0) continue;
        if (bus >= 0 || claim.foreign || claim.returns != 0) return {};
        bus = candidate;
    }
    if (bus < 0)
        for (int candidate = (int) claims.size() - 1; candidate >= 0; --candidate)
        {
            const auto& claim = claims[(size_t) candidate];
            if (! claim.foreign && claim.returns == 0 && edit.getAuxBusName (candidate).isEmpty())
            {
                bus = candidate;
                break;
            }
        }
    if (bus < 0) return {};
    auto aux = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    auto native = edit.getPluginCache().createNewPlugin (RoomPlugin::xmlTypeName, {});
    auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*> (aux.get());
    auto* room = dynamic_cast<RoomPlugin*> (native.get());
    if (returnPlugin == nullptr || room == nullptr) return {};
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    if (track == nullptr) return {};

    track->state.setProperty (roleProperty, returnRole, &edit.getUndoManager());
    track->setName ("Studio Room Return");
    track->setSoloIsolate (true);
    track->getOutput().setOutputToDefaultDevice (false);
    returnPlugin->state.setProperty (roleProperty, receiverRole, nullptr);
    returnPlugin->busNumber = bus;
    room->setRoom (settings);
    track->pluginList.insertPlugin (aux, 0, nullptr);
    track->pluginList.insertPlugin (native, 1, nullptr);
    return { track.get(), room, bus };
}

bool isRecognisedReverb (te::Plugin& plugin)
{
    if (! plugin.isEnabled() || plugin.isSynth()) return false;
    if (dynamic_cast<te::ReverbPlugin*> (&plugin) != nullptr || plugin.getName().containsIgnoreCase ("reverb"))
        return true;
    if (const auto* hosted = dynamic_cast<te::ExternalPlugin*> (&plugin))
        return hosted->desc.category.containsIgnoreCase ("reverb");
    return false;
}

bool stopped (te::Edit& edit)
{
    const auto& transport = edit.getTransport();
    return ! transport.isPlaying() && ! transport.isRecording();
}
}

void registerPlugins (te::Engine& engine)
{
    engine.getPluginManager().createBuiltInType<RoomPlugin>();
    engine.getPluginManager().createBuiltInType<SourcePlugin>();
    engine.getPluginManager().createBuiltInType<StagePanPlugin>();
    engine.getPluginManager().createBuiltInType<RoomSendPlugin>();
    engine.getPluginManager().createBuiltInType<StageSourcePlugin>();
    engine.getPluginManager().createBuiltInType<ReverbPlugin>();
}

void prepareRoomPlayback (te::Edit& edit)
{
    // Older Studio returns inherited normal track solo behavior. Preserve any
    // explicit choice and solo-safe the legacy managed aux when it had none.
    const auto target = findReturn (edit);
    if (target.room != nullptr && ! target.track->state.hasProperty (te::IDs::soloIsolate))
        target.track->setSoloIsolate (true);
}

bool isRoomReturn (const te::AudioTrack& track) { return track.state.getProperty (roleProperty).toString() == returnRole; }
te::AudioTrack* roomReturnTrack (te::Edit& edit) { return findReturn (edit).track; }

bool usesPhysicalStage (const te::Edit& edit) { return isPhysicalScene (edit.state.getChildWithName (sceneType)); }

bool hasStageSource (te::AudioTrack& track) { return find<StageSourcePlugin> (track) != nullptr; }

bool hasLegacyStageSources (te::Edit& edit)
{
    for (auto* track : te::getAudioTracks (edit))
        if (find<StagePanPlugin> (*track) != nullptr || (hasRoomSend (*track) && ! hasStageSource (*track))) return true;
    return false;
}

std::array<float, stage::microphoneLimit> stageMicrophonePeaks (te::Edit& edit)
{
    std::array<float, stage::microphoneLimit> result {};
    for (auto* track : te::getAudioTracks (edit))
        if (auto* source = find<StageSourcePlugin> (*track)) source->collectPeaks (result);
    return result;
}

Result adoptPhysicalStage (te::Edit& edit)
{
    if (usesPhysicalStage (edit)) return Result::ok();
    if (! stopped (edit)) return Result::fail ("Stop the transport to adopt the physical Stage.");
    auto scene = edit.state.getChildWithName (sceneType);
    if (scene.isValid() && (int) scene.getProperty ("version", -1) != version)
        return Result::fail ("This project uses an unsupported Stage version.");
    struct Change { te::AudioTrack* track; StagePanPlugin* pan; te::Plugin::Ptr source; };
    std::vector<Change> changes;
    for (auto* track : te::getAudioTracks (edit))
        if (auto* pan = find<StagePanPlugin> (*track); pan != nullptr || hasRoomSend (*track))
        {
            if (pan == nullptr || ! hasRoomSend (*track) || find<StageSourcePlugin> (*track) != nullptr)
                return Result::fail ("Repair the Stage inserts on " + track->getName() + " before migration.");
            if (track->isFrozen (te::Track::anyFreeze) || ! track->getOutput().usesDefaultAudioOut()
                || track->getParentTrack() != nullptr || find<SourcePlugin> (*track) != nullptr)
                return Result::fail ("Review the routing and Studio Spatial inserts on " + track->getName() + ".");
            auto fresh = edit.getPluginCache().createNewPlugin (StageSourcePlugin::xmlTypeName, {});
            auto* source = dynamic_cast<StageSourcePlugin*> (fresh.get());
            if (source == nullptr) return Result::fail ("Studio could not create the physical Stage processor.");
            source->configure (track->itemID);
            changes.push_back ({ track, pan, fresh });
        }
    const auto existing = findReturn (edit);
    if (! changes.empty())
    {
        std::array<BusClaims, 16> claims {};
        collectBusClaims (edit.state, claims);
        if (existing.room == nullptr || ! isPositiveAndBelow (existing.bus, (int) claims.size())
            || claims[(size_t) existing.bus].foreign || claims[(size_t) existing.bus].returns != 1
            || claims[(size_t) existing.bus].sends != (int) changes.size()
            || ! existing.track->getOutput().usesDefaultAudioOut() || existing.track->getParentTrack() != nullptr)
            return Result::fail ("Restore a single Studio Room Return on a dedicated aux bus before migration.");
        for (const auto& change : changes)
            if ((int) find<RoomSendPlugin> (*change.track)->busNumber != existing.bus)
                return Result::fail ("Restore the Studio Room Send destination on " + change.track->getName() + ".");
    }
    auto& undo = edit.getUndoManager();
    if (! scene.isValid())
    {
        scene = ValueTree (sceneType);
        edit.state.addChild (scene, -1, &undo);
    }
    const auto room = readRoom (scene.getChildWithName (roomType));
    for (auto* track : te::getAudioTracks (edit))
    {
        auto node = track->state.getChildWithName (seatType);
        if (! node.isValid() && hasRoomSend (*track))
        {
            node = writeSeat (track->itemID.toString(), {});
            track->state.addChild (node, -1, &undo);
        }
        if (node.isValid()) writeMetres (node, room, readSeat (node).position, undo);
    }
    scene.setProperty ("version", physicalVersion, &undo);
    for (const auto& change : changes)
    {
        change.pan->deleteFromParent();
        const auto index = change.track->pluginList.indexOf (find<RoomSendPlugin> (*change.track));
        change.track->pluginList.insertPlugin (change.source, index + 1, nullptr);
    }
    return Result::ok();
}

bool hasRoomSend (te::AudioTrack& track) { return find<RoomSendPlugin> (track) != nullptr; }

std::optional<float> activeRoomSendDb (te::AudioTrack& track)
{
    if (auto* send = find<RoomSendPlugin> (track); send != nullptr && send->isEnabled())
        return send->getGainDb();
    return std::nullopt;
}

String routeRestriction (te::AudioTrack& track)
{
    if (isRoomReturn (track)) return "The room return cannot send to itself.";
    if (track.isFrozen (te::Track::anyFreeze)) return "Unfreeze this track to change its spatial routing.";
    if (! stopped (track.edit)) return "Stop the transport to change routing.";
    if (! track.getOutput().usesDefaultAudioOut() || track.getParentTrack() != nullptr)
        return "Stage routing currently supports top-level tracks assigned to the default audio output.";
    for (auto* plugin : track.pluginList)
    {
        if (usesPhysicalStage (track.edit) && dynamic_cast<SourcePlugin*> (plugin) != nullptr)
            return "Remove the standalone Studio Spatial insert before enabling the managed Stage.";
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin); ! usesPhysicalStage (track.edit)
            && send != nullptr && dynamic_cast<RoomSendPlugin*> (send) == nullptr)
            return "This track already has an aux send. Review its routing before adding the room.";
        if (! usesPhysicalStage (track.edit) && isRecognisedReverb (*plugin))
            return "This track already has an insert reverb. Review it before adding the shared room.";
    }
    return {};
}

Result setRoomSend (te::AudioTrack& track, bool enable, Position position, float trimDb, Room settings)
{
    auto& edit = track.edit;
    auto* panner = find<StagePanPlugin> (track);
    auto* send = find<RoomSendPlugin> (track);
    auto* stageSource = find<StageSourcePlugin> (track);
    if (! stopped (edit)) return Result::fail ("Stop the transport to change routing.");
    if (! enable)
    {
        if (track.isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze this track first.");
        if (send != nullptr) send->deleteFromParent();
        if (panner != nullptr) panner->deleteFromParent();
        if (stageSource != nullptr) stageSource->deleteFromParent();
        return Result::ok();
    }
    if (auto reason = routeRestriction (track); reason.isNotEmpty()) return Result::fail (reason);
    if (send != nullptr) return Result::ok();

    const bool physical = usesPhysicalStage (edit);
    auto nextSend = edit.getPluginCache().createNewPlugin (RoomSendPlugin::xmlTypeName, {});
    auto nextStage = physical ? edit.getPluginCache().createNewPlugin (StageSourcePlugin::xmlTypeName, {}) : te::Plugin::Ptr();
    auto nextPan = ! physical && panner == nullptr
        ? edit.getPluginCache().createNewPlugin (StagePanPlugin::xmlTypeName, {}) : te::Plugin::Ptr();
    send = dynamic_cast<RoomSendPlugin*> (nextSend.get());
    if (panner == nullptr) panner = dynamic_cast<StagePanPlugin*> (nextPan.get());
    auto* stage = dynamic_cast<StageSourcePlugin*> (nextStage.get());
    if (send == nullptr || (physical ? stage == nullptr || stageSource != nullptr || panner != nullptr : panner == nullptr))
        return Result::fail ("Studio could not create a single spatial processor for this track.");
    const auto target = ensureReturn (edit, settings);
    if (target.track == nullptr || target.room == nullptr || target.bus < 0)
        return Result::fail ("The room return is incomplete or there is no free aux bus.");
    if (! target.track->getOutput().usesDefaultAudioOut() || target.track->getParentTrack() != nullptr)
        return Result::fail ("Set the Studio Room Return to the default audio output.");
    send->busNumber = target.bus;
    if (panner != nullptr) panner->setPan (validPosition (position).x);
    send->setSpatialGain (sendDb (position, trimDb));
    if (nextPan != nullptr) track.pluginList.insertPlugin (nextPan, track.pluginList.size(), nullptr);
    track.pluginList.insertPlugin (nextSend, track.pluginList.size(), nullptr);
    if (stage != nullptr)
    {
        stage->configure (track.itemID);
        track.pluginList.insertPlugin (nextStage, track.pluginList.size(), nullptr);
    }
    return Result::ok();
}

void updateRoomSend (te::AudioTrack& track, Position position, float trimDb)
{
    if (track.isFrozen (te::Track::anyFreeze)) return;
    if (auto* send = find<RoomSendPlugin> (track))
    {
        send->setSpatialGain (sendDb (position, trimDb));
        if (auto* panner = find<StagePanPlugin> (track)) panner->setPan (validPosition (position).x);
    }
}

void updateReturn (te::Edit& edit, Room settings)
{
    if (! usesPhysicalStage (edit))
        if (auto target = findReturn (edit); target.room != nullptr) target.room->setRoom (settings);
}

void setRoomCheck (te::Edit& edit, bool enabled)
{
    if (auto target = findReturn (edit); target.room != nullptr)
        target.room->setCheck (enabled);
}

std::optional<RoomSignal> roomSignal (te::Edit& edit)
{
    if (const auto target = findReturn (edit); target.room != nullptr)
    {
        const bool toMaster = target.track->getOutput().usesDefaultAudioOut()
            && target.track->getParentTrack() == nullptr
            && target.track->shouldBePlayed();
        return target.room->getSignal (toMaster);
    }
    return std::nullopt;
}

std::optional<ResourceReport> resourceReport (te::Edit& edit)
{
    if (const auto target = findReturn (edit); target.room != nullptr)
    {
        int sends = 0;
        for (auto* track : te::getAudioTracks (edit))
            if (find<RoomSendPlugin> (*track) != nullptr) ++sends;
        const auto bytes = target.room->memoryBytes();
        const auto active = edit.state.getChildWithName (sceneType).getChildWithName (roomType);
        auto description = active.getProperty ("name", "Chamber Hall").toString()
            + " | " + String (sends) + (sends == 1 ? " send" : " sends")
            + (usesPhysicalStage (edit) ? " | " + String (qualityNames[(size_t) readRoom (active).shape.quality])
                                       : String (" | 8-line spatial reverb"));
        if (readRoom (active).bypass) description << " | Bypassed";
       #if TRACKTION_ENABLE_PLUGIN_CPU_MEASUREMENT
        const auto cpu = bytes > 0 && edit.getTransport().isPlayContextActive()
            ? std::optional<double> (target.room->getCpuUsage()) : std::nullopt;
       #else
        const std::optional<double> cpu;
       #endif
        return ResourceReport { description, bytes, cpu };
    }
    return std::nullopt;
}
}
