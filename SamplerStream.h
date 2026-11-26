// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace studio::sampler
{
// Fixed source-frame sizes: no allocation, disk access, waiting or mutex
// acquisition in the real-time cursor. A shared worker services all streams.
inline constexpr int streamPageFrames = 16384;
inline constexpr int streamPreloadFrames = 2 * streamPageFrames;
inline constexpr int residentSampleFrames = 131072;
inline constexpr int streamCachePages = 6;
inline constexpr int streamRequestSlots = 32;

class StreamCache
{
    static_assert (std::atomic<int>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free
                   && std::atomic<bool>::is_always_lock_free);
    struct Slot
    {
        std::atomic<int> readers { 0 }; // -1 = exclusive background writer
        std::atomic<int> page { -1 };
        std::atomic<unsigned> used { 0 };
        std::vector<float> left, right;
    };
public:
    using Decoder = std::function<bool(int, int, float*, float*)>;
    StreamCache (int length, bool stereo, Decoder decoder)
        : frames (length), twoChannels (stereo), decode (std::move (decoder))
    {
        for (auto& slot : slots)
        {
            slot.left.resize (streamPageFrames);
            if (stereo) slot.right.resize (streamPageFrames);
        }
        for (auto& request : requests) request.store (-1);
    }

    static constexpr std::size_t capacityBytes (bool stereo) noexcept
    {
        return std::size_t (streamCachePages) * streamPageFrames * (stereo ? 2 : 1) * sizeof (float);
    }
    std::size_t bytes() const noexcept { return capacityBytes (twoChannels); }
    unsigned misses() const noexcept { return missedBlocks.load (std::memory_order_relaxed); }
    unsigned errors() const noexcept { return readErrors.load (std::memory_order_relaxed); }
    unsigned offlineErrors() const noexcept { return failedOffline.load (std::memory_order_relaxed); }
    void recordMiss (bool offline) noexcept
    {
        missedBlocks.fetch_add (1, std::memory_order_relaxed);
        if (offline) failedOffline.fetch_add (1, std::memory_order_relaxed);
    }

    // Pin a decoded slot only while a render cursor is using it. The worker
    // can never write float storage while a reader owns a pin.
    class Cursor
    {
    public:
        Cursor() = default;
        Cursor (const Cursor&) = delete;
        Cursor& operator= (const Cursor&) = delete;
        ~Cursor() { release(); }
        bool read (StreamCache& cache, int frame, float& l, float& r, bool offline) noexcept
        {
            if (frame < streamPreloadFrames || frame >= cache.frames) return false;
            const int page = frame / streamPageFrames;
            if (failedPage == page) return false;
            if (slot == nullptr || owner != &cache || currentPage != page)
            {
                release();
                slot = cache.pin (page);
                if (slot == nullptr)
                {
                    if (offline)
                    {
                        // Offline rendering is not a real-time callback. It
                        // may synchronously wait for the decoder, but live
                        // playback must always return immediately.
                        cache.fill (page);
                        slot = cache.pin (page);
                    }
                    else cache.requestPage (page);
                    if (slot == nullptr) { failedPage = page; return false; }
                }
                owner = &cache;
                currentPage = page;
                if (! offline) cache.requestPage (page + 1);
            }
            const auto index = static_cast<std::size_t> (frame % streamPageFrames);
            l = slot->left[index];
            r = cache.twoChannels ? slot->right[index] : l;
            return true;
        }
    private:
        void release() noexcept
        {
            if (slot != nullptr) slot->readers.fetch_sub (1, std::memory_order_release);
            slot = nullptr; owner = nullptr; currentPage = -1;
        }
        Slot* slot = nullptr;
        StreamCache* owner = nullptr;
        int currentPage = -1, failedPage = -1;
    };

    void requestFrame (int frame) noexcept
    {
        if (frame >= streamPreloadFrames && frame < frames) requestPage (frame / streamPageFrames);
    }
    // Called on import/edit threads, never live render.
    bool prime (int frame)
    {
        return frame >= 0 && frame < frames
            && (frame < streamPreloadFrames || fill (frame / streamPageFrames));
    }
    bool pending() const noexcept { return pendingWork.load (std::memory_order_acquire); }
    void service()
    {
        if (! pendingWork.exchange (false, std::memory_order_acq_rel)) return;
        for (auto& request : requests)
        {
            const int page = request.exchange (-1, std::memory_order_acq_rel);
            if (page >= 0) fill (page);
        }
    }

private:
    bool validPage (int p) const noexcept
    {
        return p >= streamPreloadFrames / streamPageFrames && p <= (frames - 1) / streamPageFrames;
    }
    Slot* pin (int page) noexcept
    {
        for (auto& slot : slots)
        {
            if (slot.page.load (std::memory_order_acquire) != page) continue;
            int n = slot.readers.load (std::memory_order_relaxed);
            // Bounded, nonblocking attempt. Recheck the tag after pinning.
            if (n < 0 || ! slot.readers.compare_exchange_strong (n, n + 1, std::memory_order_acquire)) continue;
            if (slot.page.load (std::memory_order_acquire) == page)
            {
                slot.used.store (clock.fetch_add (1, std::memory_order_relaxed), std::memory_order_relaxed);
                return &slot;
            }
            slot.readers.fetch_sub (1, std::memory_order_release);
        }
        return nullptr;
    }
    void requestPage (int page) noexcept
    {
        if (! validPage (page)) return;
        for (const auto& slot : slots)
            if (slot.page.load (std::memory_order_acquire) == page) return;
        auto& request = requests[static_cast<std::size_t> (page) % requests.size()];
        if (request.exchange (page, std::memory_order_acq_rel) != page)
            pendingWork.store (true, std::memory_order_release);
    }
    bool fill (int page) noexcept
    {
        if (! validPage (page)) return false;
        // Multiple offline readers or edit threads may request a page; only
        // the decoder side ever takes this mutex.
        std::lock_guard lock (decodeMutex);
        // Only the decoder can change a page tag, and decodeMutex is held.
        // A concurrent reader need not be pinned just to confirm residency.
        for (auto& slot : slots)
            if (slot.page.load (std::memory_order_acquire) == page)
            {
                slot.used.store (clock.fetch_add (1, std::memory_order_relaxed), std::memory_order_relaxed);
                return true;
            }
        Slot* chosen = nullptr;
        for (int attempt = 0; attempt < streamCachePages; ++attempt)
        {
            chosen = nullptr;
            unsigned oldest = 0;
            for (auto& slot : slots)
            {
                if (slot.readers.load (std::memory_order_acquire) != 0) continue;
                const unsigned used = slot.used.load (std::memory_order_relaxed);
                if (chosen == nullptr || used < oldest) { chosen = &slot; oldest = used; }
            }
            if (chosen == nullptr) return false;
            int free = 0;
            if (chosen->readers.compare_exchange_strong (free, -1, std::memory_order_acq_rel)) break;
            chosen = nullptr;
        }
        if (chosen == nullptr) return false;
        chosen->page.store (-1, std::memory_order_release);
        const int first = page * streamPageFrames;
        const int count = std::min (streamPageFrames, frames - first);
        bool ok = false;
        try { ok = decode (first, count, chosen->left.data(), twoChannels ? chosen->right.data() : nullptr); }
        catch (...) { ok = false; }
        if (ok)
        {
            chosen->used.store (clock.fetch_add (1, std::memory_order_relaxed), std::memory_order_relaxed);
            chosen->page.store (page, std::memory_order_release);
        }
        else readErrors.fetch_add (1, std::memory_order_relaxed);
        chosen->readers.store (0, std::memory_order_release);
        return ok;
    }

    const int frames;
    const bool twoChannels;
    Decoder decode;
    std::array<Slot, streamCachePages> slots;
    std::array<std::atomic<int>, streamRequestSlots> requests;
    std::atomic<unsigned> clock { 1 }, missedBlocks { 0 }, readErrors { 0 }, failedOffline { 0 };
    std::atomic<bool> pendingWork { false };
    std::mutex decodeMutex;
};

class StreamWorker
{
public:
    static StreamWorker& instance()
    {
        static StreamWorker service;
        return service;
    }
    void add (const std::shared_ptr<StreamCache>& cache)
    {
        std::lock_guard lock (mutex);
        streams.emplace_back (cache);
        wake.notify_one();
    }
    ~StreamWorker()
    {
        { std::lock_guard lock (mutex); stopping = true; }
        wake.notify_one();
        thread.join();
    }
private:
    StreamWorker() : thread ([this] { run(); }) {}
    void run()
    {
        std::vector<std::shared_ptr<StreamCache>> work;
        std::unique_lock lock (mutex);
        while (! stopping)
        {
            work.clear();
            streams.erase (std::remove_if (streams.begin(), streams.end(),
                [&] (auto& weak)
                {
                    if (auto cache = weak.lock())
                    {
                        if (cache->pending()) work.push_back (std::move (cache));
                        return false;
                    }
                    return true;
                }), streams.end());
            lock.unlock();
            for (auto& cache : work) cache->service();
            work.clear();
            lock.lock();
            // Polling avoids even a condition-variable signal from audio.
            wake.wait_for (lock, std::chrono::milliseconds (streams.empty() ? 40 : 4));
        }
    }
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::weak_ptr<StreamCache>> streams;
    bool stopping = false;
    std::thread thread;
};
}
