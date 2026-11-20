// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialRouting.h"
#include "SpatialReverb.h"
#include "SpatialState.h"
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

class RoomPlugin final : public te::Plugin, private Timer
{
public:
    static constexpr const char* xmlTypeName = "studioWetRoomV1";

    explicit RoomPlugin (te::PluginCreationInfo info) : Plugin (info)
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

    bool isBypassed() const { return publication.back().load() != 0.0f; }

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
    static constexpr size_t parameterCount = roomLineCount * 7 + roomEarlyTaps * 2 + 5;
    using PlanScalars = std::array<float, parameterCount>;
    static_assert (sizeof (RoomPlan) == sizeof (PlanScalars));
    std::array<std::atomic<float>, parameterCount> publication {};
    std::atomic<unsigned> revisionNumber { 0 };
    std::atomic<std::size_t> bytes { 0 };
    unsigned acceptedRevision = 0;
    std::atomic<float> inputPeak { 0.0f }, wetPeak { 0.0f };
    std::atomic<bool> checkEnabled { false };
    float meterTime = 14400.0f;
    RoomReverb reverb;

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
        const auto plan = makeRoomPlan (xml != nullptr ? readRoom (ValueTree::fromXml (*xml)) : Room());
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
    engine.getPluginManager().createBuiltInType<StagePanPlugin>();
    engine.getPluginManager().createBuiltInType<RoomSendPlugin>();
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
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin); send != nullptr && dynamic_cast<RoomSendPlugin*> (send) == nullptr)
            return "This track already has an aux send. Review its routing before adding the room.";
        if (isRecognisedReverb (*plugin))
            return "This track already has an insert reverb. Review it before adding the shared room.";
    }
    return {};
}

Result setRoomSend (te::AudioTrack& track, bool enable, Position position, float trimDb, Room settings)
{
    auto& edit = track.edit;
    auto* panner = find<StagePanPlugin> (track);
    auto* send = find<RoomSendPlugin> (track);
    if (! stopped (edit)) return Result::fail ("Stop the transport to change routing.");
    if (! enable)
    {
        if (track.isFrozen (te::Track::anyFreeze)) return Result::fail ("Unfreeze this track first.");
        if (send != nullptr) send->deleteFromParent();
        if (panner != nullptr) panner->deleteFromParent();
        return Result::ok();
    }
    if (auto reason = routeRestriction (track); reason.isNotEmpty()) return Result::fail (reason);
    if (send != nullptr) return Result::ok();

    auto nextSend = edit.getPluginCache().createNewPlugin (RoomSendPlugin::xmlTypeName, {});
    auto nextPan = panner == nullptr ? edit.getPluginCache().createNewPlugin (StagePanPlugin::xmlTypeName, {}) : te::Plugin::Ptr();
    send = dynamic_cast<RoomSendPlugin*> (nextSend.get());
    if (panner == nullptr) panner = dynamic_cast<StagePanPlugin*> (nextPan.get());
    if (send == nullptr || panner == nullptr) return Result::fail ("Studio could not create spatial inserts.");
    const auto target = ensureReturn (edit, settings);
    if (target.track == nullptr || target.room == nullptr || target.bus < 0)
        return Result::fail ("The room return is incomplete or there is no free aux bus.");
    if (! target.track->getOutput().usesDefaultAudioOut() || target.track->getParentTrack() != nullptr)
        return Result::fail ("Set the Studio Room Return to the default audio output.");
    send->busNumber = target.bus;
    panner->setPan (validPosition (position).x);
    send->setSpatialGain (sendDb (position, trimDb));
    if (nextPan != nullptr) track.pluginList.insertPlugin (nextPan, track.pluginList.size(), nullptr);
    track.pluginList.insertPlugin (nextSend, track.pluginList.size(), nullptr);
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
            + " | " + String (sends) + (sends == 1 ? " send" : " sends") + " | 8-line spatial reverb";
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
