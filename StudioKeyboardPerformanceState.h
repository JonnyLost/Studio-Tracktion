// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace studio
{
// On-screen controls reflect MIDI addressed to the currently selected track.
// Updates are atomic and do not forward MIDI to the audio engine.
struct KeyboardPerformanceValues
{
    enum Parameter : unsigned { pitch = 0, modulation, expression, volume, brightness, room };
    static constexpr unsigned bit (Parameter p) { return 1u << static_cast<unsigned> (p); }
    int bend = 8192;
    int mod = 0;
    int expr = 127;
    int vol = 100;
    int tone = 64;
    int reverb = 40;
    unsigned changed = 0;
};

class KeyboardPerformanceState final
{
public:
    void receivePitchBend (int value) noexcept
    {
        bend.store (clamp (value, 0, 16383), std::memory_order_relaxed);
        changed.fetch_or (KeyboardPerformanceValues::bit (KeyboardPerformanceValues::pitch),
                          std::memory_order_release);
    }

    void receiveController (int cc, int value) noexcept
    {
        const int v = clamp (value, 0, 127);
        KeyboardPerformanceValues::Parameter p;
        std::atomic<int>* field = nullptr;
        switch (cc)
        {
            case 1:  field = &mod;    p = KeyboardPerformanceValues::modulation; break;
            case 11: field = &expr;   p = KeyboardPerformanceValues::expression; break;
            case 7:  field = &vol;    p = KeyboardPerformanceValues::volume; break;
            case 74: field = &tone;   p = KeyboardPerformanceValues::brightness; break;
            case 91: field = &reverb; p = KeyboardPerformanceValues::room; break;
            default: return;
        }
        field->store (v, std::memory_order_relaxed);
        changed.fetch_or (KeyboardPerformanceValues::bit (p), std::memory_order_release);
    }

    KeyboardPerformanceValues consume() noexcept
    {
        auto value = current();
        value.changed = changed.exchange (0, std::memory_order_acquire);
        return value;
    }

    KeyboardPerformanceValues current() const noexcept
    {
        KeyboardPerformanceValues value;
        value.bend = bend.load (std::memory_order_relaxed);
        value.mod = mod.load (std::memory_order_relaxed);
        value.expr = expr.load (std::memory_order_relaxed);
        value.vol = vol.load (std::memory_order_relaxed);
        value.tone = tone.load (std::memory_order_relaxed);
        value.reverb = reverb.load (std::memory_order_relaxed);
        return value;
    }

    void reset() noexcept
    {
        bend.store (8192, std::memory_order_relaxed);
        mod.store (0, std::memory_order_relaxed);
        expr.store (127, std::memory_order_relaxed);
        vol.store (100, std::memory_order_relaxed);
        tone.store (64, std::memory_order_relaxed);
        reverb.store (40, std::memory_order_relaxed);
        changed.store (0, std::memory_order_relaxed);
    }

private:
    static int clamp (int v, int lo, int hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    std::atomic<int> bend { 8192 }, mod { 0 }, expr { 127 }, vol { 100 },
                     tone { 64 }, reverb { 40 };
    std::atomic<unsigned> changed { 0 };
};
} // namespace studio
