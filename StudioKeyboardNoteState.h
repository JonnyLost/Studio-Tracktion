// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstddef>

namespace studio
{
// Lock-free, allocation-free MIDI note/pedal state. Producer callbacks only
// touch atomics; the GUI consumes snapshots on the message thread. Channel
// numbers use the MIDI convention 1..16.
class KeyboardNoteState final
{
public:
    void noteOn (int channel, int note) noexcept
    {
        if (! valid (channel, note)) return;
        auto& count = pressed[index (channel, note)];
        auto previous = count.load (std::memory_order_relaxed);
        while (previous < 255 && ! count.compare_exchange_weak (
                   previous, static_cast<uint8_t> (previous + 1),
                   std::memory_order_relaxed)) {}
        flashes[static_cast<std::size_t> (note)].store (2, std::memory_order_relaxed);
    }

    void noteOff (int channel, int note) noexcept
    {
        if (! valid (channel, note)) return;
        const auto slot = index (channel, note);
        auto& count = pressed[slot];
        auto previous = count.load (std::memory_order_relaxed);
        while (previous != 0 && ! count.compare_exchange_weak (
                   previous, static_cast<uint8_t> (previous - 1),
                   std::memory_order_relaxed)) {}
        if (previous != 0 && pedals[static_cast<std::size_t> (channel - 1)].load (
                                 std::memory_order_relaxed))
            sustained[slot].store (true, std::memory_order_relaxed);
    }

    void sustain (int channel, bool down) noexcept
    {
        if (channel < 1 || channel > 16) return;
        const auto ch = static_cast<std::size_t> (channel - 1);
        pedals[ch].store (down, std::memory_order_relaxed);
        if (! down)
            for (int note = 0; note < 128; ++note)
                sustained[ch * 128 + static_cast<std::size_t> (note)].store (
                    false, std::memory_order_relaxed);
    }

    void allNotesOff (int channel) noexcept
    {
        if (channel < 1 || channel > 16) return;
        const auto ch = static_cast<std::size_t> (channel - 1);
        pedals[ch].store (false, std::memory_order_relaxed);
        for (int note = 0; note < 128; ++note)
        {
            const auto i = ch * 128 + static_cast<std::size_t> (note);
            pressed[i].store (0, std::memory_order_relaxed);
            sustained[i].store (false, std::memory_order_relaxed);
        }
    }

    void clear() noexcept
    {
        for (auto& note : pressed) note.store (0, std::memory_order_relaxed);
        for (auto& note : sustained) note.store (false, std::memory_order_relaxed);
        for (auto& pedal : pedals) pedal.store (false, std::memory_order_relaxed);
        for (auto& flash : flashes) flash.store (0, std::memory_order_relaxed);
    }

    // A two-frame flash makes very short notes visible even when their note-on
    // and note-off both occur between two UI refreshes.
    std::bitset<128> snapshot() noexcept
    {
        std::bitset<128> result;
        for (int note = 0; note < 128; ++note)
        {
            auto& flash = flashes[static_cast<std::size_t> (note)];
            auto frames = flash.load (std::memory_order_relaxed);
            while (frames != 0 && ! flash.compare_exchange_weak (
                       frames, static_cast<uint8_t> (frames - 1),
                       std::memory_order_relaxed)) {}
            bool active = frames != 0;
            for (int channel = 1; channel <= 16 && ! active; ++channel)
            {
                const auto i = index (channel, note);
                active = pressed[i].load (std::memory_order_relaxed) != 0
                    || sustained[i].load (std::memory_order_relaxed);
            }
            result.set (static_cast<std::size_t> (note), active);
        }
        return result;
    }

private:
    static bool valid (int channel, int note) noexcept
    {
        return channel >= 1 && channel <= 16 && note >= 0 && note < 128;
    }
    static std::size_t index (int channel, int note) noexcept
    {
        return static_cast<std::size_t> (channel - 1) * 128 + static_cast<std::size_t> (note);
    }
    std::array<std::atomic<uint8_t>, 16 * 128> pressed {};
    std::array<std::atomic<bool>, 16 * 128> sustained {};
    std::array<std::atomic<bool>, 16> pedals {};
    std::array<std::atomic<uint8_t>, 128> flashes {};
};
} // namespace studio
