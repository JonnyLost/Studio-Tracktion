// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <type_traits>

namespace studio::spatial
{
// Single settings-thread producer. A render block keeps its previous complete
// plan if a write overlaps the read. Atomic words avoid racing on any byte.
template <typename Type>
class Publication
{
    static_assert (std::is_trivially_copyable_v<Type> && std::atomic<unsigned>::is_always_lock_free);
    std::atomic<unsigned> revision { 0 };
    static constexpr std::size_t count = (sizeof (Type) + sizeof (unsigned) - 1) / sizeof (unsigned);
    std::array<std::atomic<unsigned>, count> words {};

public:
    void write (const Type& value) noexcept
    {
        std::array<unsigned, count> bytes {};
        std::memcpy (bytes.data(), &value, sizeof (Type));
        ++revision;
        for (std::size_t i = 0; i < words.size(); ++i) words[i].store (bytes[i]);
        ++revision;
    }

    bool read (Type& value, unsigned& accepted) const noexcept
    {
        const auto sequence = revision.load();
        if (sequence == accepted || sequence % 2 != 0) return false;
        std::array<unsigned, count> bytes;
        for (std::size_t i = 0; i < words.size(); ++i) bytes[i] = words[i].load();
        if (sequence != revision.load()) return false;
        std::memcpy (static_cast<void*> (&value), bytes.data(), sizeof (Type));
        accepted = sequence;
        return true;
    }
};
}
