// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComposerTracks.h"
#include "MusicalTransport.h"
#include "SpatialRouting.h"
#include "SpatialState.h"

namespace studio::tracks
{
namespace te = tracktion;
using namespace juce;

String restriction (te::AudioTrack& track, bool destructive)
{
    if (! transportSettled (track.edit) || track.edit.getTransport().isPlaying())
        return "Stop the transport first.";
    if (spatial::isRoomReturn (track))
        return "The shared room return is managed by Studio.";
    if (destructive)
    {
        if (track.isFrozen (te::Track::anyFreeze) || track.getParentTrack() != nullptr)
            return "Duplicate and delete are available for unfrozen top-level tracks.";
        for (auto* plugin : track.pluginList)
            if (dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr
                || dynamic_cast<te::RackInstance*> (plugin) != nullptr
                || dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr)
                return "This track contains hosted, rack or shared return state.";
    }
    return {};
}

bool rename (te::AudioTrack& track, const String& requested)
{
    const auto name = requested.trim();
    if (name.isEmpty() || restriction (track, false).isNotEmpty())
        return false;
    auto& undo = track.edit.getUndoManager();
    undo.beginNewTransaction ("Rename track");
    track.setName (name);
    undo.beginNewTransaction();
    return true;
}

static Array<te::AudioTrack*> siblingsOf (te::AudioTrack& track)
{
    Array<te::AudioTrack*> siblings;
    for (auto* candidate : te::getAudioTracks (track.edit))
        if (candidate->getParentTrack() == track.getParentTrack() && ! spatial::isRoomReturn (*candidate))
            siblings.add (candidate);
    return siblings;
}

bool canMove (te::AudioTrack& track, int direction)
{
    const auto siblings = siblingsOf (track);
    const int index = siblings.indexOf (&track) + direction;
    return isPositiveAndBelow (index, siblings.size()) && restriction (track, false).isEmpty();
}

void move (te::AudioTrack& track, int direction)
{
    if (! canMove (track, direction)) return;
    const auto siblings = siblingsOf (track);
    auto* neighbour = siblings[siblings.indexOf (&track) + direction];
    track.edit.getUndoManager().beginNewTransaction ("Reorder track");
    track.edit.moveTrack (&track, te::TrackInsertPoint (*neighbour, direction < 0));
    track.edit.getUndoManager().beginNewTransaction();
}

te::Track::Ptr duplicate (te::AudioTrack& track)
{
    if (restriction (track, true).isNotEmpty())
        return {};
    auto& edit = track.edit;
    auto& undo = edit.getUndoManager();
    if (te::getAllTracks (edit).size() >= edit.engine.getEngineBehaviour().getEditLimits().maxNumTracks)
        return {};
    undo.beginNewTransaction ("Duplicate track");
    auto copied = edit.copyTrack (&track, te::TrackInsertPoint (track, false));
    if (copied == nullptr) return {};
    copied->setName (track.getName() + " Copy");
    copied->setSolo (false);
    copied->setSoloIsolate (false);
    copied->setMute (true);
    if (auto seat = copied->state.getChildWithName (spatial::seatType); seat.isValid())
        seat.setProperty ("trackKey", copied->itemID.toString(), &undo);
    undo.beginNewTransaction();
    return copied;
}

void remove (te::AudioTrack& track)
{
    if (restriction (track, true).isNotEmpty()) return;
    auto& edit = track.edit;
    edit.getUndoManager().beginNewTransaction ("Delete track");
    edit.deleteTrack (&track);
    edit.getUndoManager().beginNewTransaction();
}

te::FolderTrack::Ptr createSubmix (te::Edit& edit, const Array<te::AudioTrack*>& requested,
                                   const String& title)
{
    if (! transportSettled (edit) || edit.getTransport().isPlaying()
        || edit.getTransport().isRecording())
        return {};
    const auto name = title.trim();
    if (name.isEmpty()) return {};
    // The managed Stage return and nested tracks must not be moved into a
    // new group silently: it would change room routing or break a submix.
    Array<te::AudioTrack*> ordered;
    for (auto* candidate : te::getAudioTracks (edit))
        if (requested.contains (candidate))
        {
            if (spatial::isRoomReturn (*candidate) || candidate->getParentTrack() != nullptr
                || candidate->isFrozen (te::Track::anyFreeze))
                return {};
            ordered.add (candidate);
        }
    if (ordered.size() != requested.size()) return {};
    if (te::getAllTracks (edit).size() >= edit.engine.getEngineBehaviour().getEditLimits().maxNumTracks)
        return {};
    auto& undo = edit.getUndoManager();
    undo.beginNewTransaction ("Create submix folder");
    const auto point = ordered.isEmpty()
        ? te::TrackInsertPoint::getEndOfTracks (edit)
        : te::TrackInsertPoint (*ordered.getFirst(), true);
    auto folder = edit.insertNewFolderTrack (point, nullptr, true);
    if (folder == nullptr) return {};
    folder->setName (name);
    folder->state.setProperty ("studioFolderCollapsed", false, &undo);
    te::Track* preceding = nullptr;
    for (auto* track : ordered)
    {
        edit.moveTrack (track, te::TrackInsertPoint (folder.get(), preceding));
        preceding = track;
    }
    undo.beginNewTransaction();
    return folder;
}

bool moveIntoSubmix (te::AudioTrack& track, te::FolderTrack& folder)
{
    if (&track.edit != &folder.edit || track.getParentTrack() != nullptr
        || spatial::isRoomReturn (track)
        || restriction (track, false).isNotEmpty()
        || track.isFrozen (te::Track::anyFreeze))
        return false;
    auto& undo = track.edit.getUndoManager();
    undo.beginNewTransaction ("Move track to " + folder.getName());
    te::Track* previous = nullptr;
    for (auto* item : folder.getAllSubTracks (false))
        previous = item;
    track.edit.moveTrack (&track, te::TrackInsertPoint (&folder, previous));
    undo.beginNewTransaction();
    return track.getParentFolderTrack() == &folder;
}

bool removeFromSubmix (te::AudioTrack& track)
{
    auto* parent = track.getParentFolderTrack();
    if (parent == nullptr || spatial::isRoomReturn (track)
        || restriction (track, false).isNotEmpty()
        || track.isFrozen (te::Track::anyFreeze))
        return false;
    auto& undo = track.edit.getUndoManager();
    undo.beginNewTransaction ("Move track out of " + parent->getName());
    // The first move back to the top-level is immediately after its old
    // folder. This does not delete any group effects or change other tracks.
    track.edit.moveTrack (&track, te::TrackInsertPoint (nullptr, parent));
    undo.beginNewTransaction();
    return track.getParentFolderTrack() == nullptr;
}
}
