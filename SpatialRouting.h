// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SpatialModel.h"
#include <optional>

namespace studio::spatial
{
void registerPlugins (tracktion::Engine&);
void prepareRoomPlayback (tracktion::Edit&);
tracktion::AudioTrack* roomReturnTrack (tracktion::Edit&);
bool isRoomReturn (const tracktion::AudioTrack&);
bool hasRoomSend (tracktion::AudioTrack&);
std::optional<float> activeRoomSendDb (tracktion::AudioTrack&);
juce::String routeRestriction (tracktion::AudioTrack&);
juce::Result setRoomSend (tracktion::AudioTrack&, bool enabled, Position, float trimDb, Room);
void updateRoomSend (tracktion::AudioTrack&, Position, float trimDb);
void updateReturn (tracktion::Edit&, Room);
void setRoomCheck (tracktion::Edit&, bool enabled);

struct RoomSignal
{
    float inputPeak, wetPeak;
    bool bypassed, checkEnabled, reachesMaster;
};
std::optional<RoomSignal> roomSignal (tracktion::Edit&);

struct ResourceReport
{
    juce::String description;
    std::size_t allocatedBytes = 0;
    std::optional<double> callbackFraction;
};
std::optional<ResourceReport> resourceReport (tracktion::Edit&);
}
