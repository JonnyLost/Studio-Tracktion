// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialRouting.h"
#include "../SpatialState.h"

namespace studio::spatial
{
namespace
{
struct OfflineEngine final : tracktion::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
};

int countReturns (tracktion::Edit& edit)
{
    int count = 0;
    for (auto* track : tracktion::getAudioTracks (edit))
        if (isRoomReturn (*track)) ++count;
    return count;
}

tracktion::AuxSendPlugin* auxSend (tracktion::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* send = dynamic_cast<tracktion::AuxSendPlugin*> (plugin)) return send;
    return nullptr;
}

juce::Result verify()
{
    namespace te = tracktion;
    using juce::Result;
    te::Engine engine ("Studio spatial tests", {}, std::make_unique<OfflineEngine>());
    registerPlugins (engine);
    const juce::TemporaryFile temporary (".tracktionedit");
    const auto file = temporary.getFile();
    auto edit = te::createEmptyEdit (engine, file);
    if (edit == nullptr) return Result::fail ("create edit");
    auto sourceOwner = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (sourceOwner == nullptr) return Result::fail ("source track");
    auto* source = sourceOwner.get();
    source->getOutput().setOutputToDefaultDevice (false);
    sourceOwner = nullptr;
    auto sourceID = source->itemID;

    auto blocked = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    auto oldSend = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    if (blocked == nullptr || oldSend == nullptr) return Result::fail ("existing routing fixture");
    dynamic_cast<te::AuxSendPlugin&> (*oldSend).busNumber = 15;
    blocked->pluginList.insertPlugin (oldSend, blocked->pluginList.size(), nullptr);
    if (setRoomSend (*blocked, true, {}, -12, {}).wasOk()) return Result::fail ("existing aux isolation");
    blocked = nullptr;
    oldSend = nullptr;
    if (countReturns (*edit) != 0 || hasRoomSend (*source)) return Result::fail ("default routing");

    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction ("Test enabling stage audio");
    const Position seat { -0.25f, 0.6f };
    if (auto result = setRoomSend (*source, true, seat, -12, factoryRooms[0].room); result.failed()) return result;
    auto* send = auxSend (*source);
    if (countReturns (*edit) != 1 || send == nullptr || send->getBusNumber() == 15
        || std::abs (send->getGainDb() - sendDb (seat, -12)) > 0.1f)
        return Result::fail ("allocated return and effective send");
    undo.beginNewTransaction ("Test send trim");
    updateRoomSend (*source, seat, -32);
    undo.beginNewTransaction();
    if (std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f || ! undo.undo()
        || std::abs (send->getGainDb() - sendDb (seat, -12)) > 0.1f
        || ! undo.redo() || std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f)
        return Result::fail ("live send gain or undo");
    const auto returnID = roomReturnTrack (*edit)->itemID;
    const auto bus = send->getBusNumber();
    if (! te::EditFileOperations (*edit).writeToFile (file, false)) return Result::fail ("write project");
    edit.reset();
    edit = te::loadEditFromFile (engine, file);
    if (edit == nullptr) return Result::fail ("reopen project");
    source = te::findAudioTrackForID (*edit, sourceID);
    auto* returned = roomReturnTrack (*edit);
    if (source == nullptr || returned == nullptr || returned->itemID != returnID || countReturns (*edit) != 1)
        return Result::fail ("saved identities");
    send = auxSend (*source);
    if (! hasRoomSend (*source) || send == nullptr || send->getBusNumber() != bus
        || std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f)
        return Result::fail ("restored routing");
    auto& nextUndo = edit->getUndoManager();
    nextUndo.beginNewTransaction ("Disable stage audio");
    if (auto result = setRoomSend (*source, false, seat, -12, {}); result.failed()) return result;
    nextUndo.beginNewTransaction();
    if (hasRoomSend (*source) || ! nextUndo.undo() || ! hasRoomSend (*source) || countReturns (*edit) != 1)
        return Result::fail ("undo route removal");
    return Result::ok();
}
}

int runSpatialRoutingTest()
{
    const auto result = verify();
    if (result.failed()) fprintf (stderr, "Studio spatial routing test failed: %s\n", result.getErrorMessage().toRawUTF8());
    else fprintf (stdout, "Tracktion room send, undo, isolation, and native project reopen passed.\n");
    return result.wasOk() ? 0 : 1;
}
}
