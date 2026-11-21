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
}
