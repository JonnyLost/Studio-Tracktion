// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numbers>

using namespace studio::sampler;
namespace
{
constexpr int length = 12 * streamPageFrames + 371;
float signal (int frame, int channel)
{
    return static_cast<float> (0.42 * std::sin (frame * 0.019 + channel * 0.4)
        + 0.16 * std::sin (frame * 0.027 + channel * 0.7));
}

std::shared_ptr<Sample> makeSample (bool streamed, bool stereo, std::atomic<unsigned>& decodes)
{
    auto sample = std::make_shared<Sample>();
    sample->rate = 48000;
    sample->totalFrames = length;
    const int frames = streamed ? streamPreloadFrames : length;
    sample->left.resize (frames);
    if (stereo) sample->right.resize (frames);
    for (int i = 0; i < frames; ++i)
    {
        sample->left[i] = signal (i, 0);
        if (stereo) sample->right[i] = signal (i, 1);
    }
    if (streamed)
    {
        sample->stream = std::make_shared<StreamCache> (length, stereo,
            [&decodes] (int first, int count, float* l, float* r)
            {
                ++decodes;
                for (int n = 0; n < count; ++n)
                {
                    l[n] = signal (first + n, 0);
                    if (r) r[n] = signal (first + n, 1);
                }
                return true;
            });
        assert (sample->stream->prime (streamPreloadFrames));
        assert (sample->stream->prime (streamPreloadFrames + streamPageFrames));
    }
    return sample;
}
Bank bankFor (const std::shared_ptr<Sample>& sample, bool loop, int start)
{
    Bank b;
    b.sampleRevision = 1;
    b.settings.attack = 0.001f;
    b.settings.release = 0.15f;
    Zone z;
    z.sample = sample;
    z.root = 60;
    z.start = start;
    z.loop = loop;
    z.loopStart = 3 * streamPageFrames + 777;
    z.loopEnd = 8 * streamPageFrames + 311;
    z.loopCrossfade = 97;
    b.zones.push_back (z);
    return b;
}
void compare (bool stereo, bool loop, bool offline, int start, int note)
{
    std::atomic<unsigned> decodes { 0 };
    auto resident = makeSample (false, stereo, decodes);
    auto streamed = makeSample (true, stereo, decodes);
    assert (streamed->bytes() < resident->bytes());
    assert (streamed->frameCount() == resident->frameCount());
    assert (streamed->left.size() == streamPreloadFrames);
    auto a = bankFor (resident, loop, start), b = bankFor (streamed, loop, start);
    if (start >= streamPreloadFrames) assert (streamed->stream->prime (start));
    if (loop) assert (streamed->stream->prime (b.zones[0].loopStart));
    Engine reference, actual;
    reference.prepare (48000); actual.prepare (48000);
    constexpr int block = 256;
    std::array<float, block> al {}, ar {}, bl {}, br {};
    for (int n = 0; n < 1600; ++n)
    {
        reference.use (a); actual.use (b);
        if (n == 0)
        {
            reference.midi (0x90, note, 110);
            actual.midi (0x90, note, 110);
        }
        if (n == 950)
        {
            reference.midi (0x80, note, 0);
            actual.midi (0x80, note, 0);
        }
        al.fill (0); ar.fill (0); bl.fill (0); br.fill (0);
        reference.render (al.data(), ar.data(), block);
        const auto before = decodes.load();
        actual.render (bl.data(), br.data(), block, offline);
        if (! offline) assert (decodes.load() == before); // live render never decodes
        reference.publishMeters(); actual.publishMeters();
        for (int i = 0; i < block; ++i)
        {
            assert (std::abs (al[i] - bl[i]) < 2e-6f);
            assert (std::abs (ar[i] - br[i]) < 2e-6f);
        }
        // Deterministic simulation of a disk worker keeping up between blocks.
        streamed->stream->service();
    }
    assert (streamed->stream->misses() == 0);
    assert (streamed->stream->offlineErrors() == 0);
}

void workerDiskTest()
{
    const auto file = std::filesystem::temp_directory_path() /
        ("studio-stream-" + std::to_string (std::chrono::steady_clock::now().time_since_epoch().count()) + ".raw");
    struct Cleanup
    {
        std::filesystem::path file;
        ~Cleanup() { std::error_code error; std::filesystem::remove (file, error); }
    } cleanup { file };
    {
        std::ofstream out (file, std::ios::binary);
        assert (out);
        for (int i = 0; i < length; ++i)
        {
            const float pair[] { signal (i, 0), signal (i, 1) };
            out.write (reinterpret_cast<const char*> (pair), sizeof (pair));
        }
        assert (out);
    }
    auto background = std::make_shared<std::atomic<bool>> (false);
    const auto caller = std::this_thread::get_id();
    auto cache = std::make_shared<StreamCache> (length, true,
        [file, caller, background] (int first, int count, float* left, float* right)
        {
            background->store (std::this_thread::get_id() != caller);
            std::ifstream in (file, std::ios::binary);
            in.seekg (static_cast<std::streamoff> (first) * 2 * sizeof (float));
            for (int n = 0; n < count; ++n)
            {
                float pair[2] {};
                if (! in.read (reinterpret_cast<char*> (pair), sizeof (pair))) return false;
                left[n] = pair[0]; right[n] = pair[1];
            }
            return true;
        });
    const int frame = 5 * streamPageFrames + 37;
    {
        StreamCache::Cursor cursor;
        float l = 0, r = 0;
        assert (! cursor.read (*cache, frame, l, r, false));
        assert (! background->load());
    }
    StreamWorker::instance().add (cache);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
    bool ready = false;
    while (! ready && std::chrono::steady_clock::now() < deadline)
    {
        StreamCache::Cursor cursor;
        float l = 0, r = 0;
        ready = cursor.read (*cache, frame, l, r, false);
        if (ready)
        {
            assert (l == signal (frame, 0) && r == signal (frame, 1));
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    assert (ready && background->load());
    // Hold a reader pin while other pages are filled/evicted. Its storage
    // must never be overwritten while the cursor is reading it.
    {
        StreamCache::Cursor pinned;
        float l = 0, r = 0;
        assert (pinned.read (*cache, frame, l, r, false));
        for (int page = 2; page < 12; ++page)
        {
            if (page == 5) continue;
            cache->requestFrame (page * streamPageFrames);
            std::this_thread::sleep_for (std::chrono::milliseconds (8));
            assert (pinned.read (*cache, frame, l, r, false));
            assert (l == signal (frame, 0) && r == signal (frame, 1));
        }
    }
    // End-of-file page has fewer frames than the normal page size.
    {
        StreamCache::Cursor cursor;
        float l = 0, r = 0;
        assert (cursor.read (*cache, length - 1, l, r, true));
        assert (l == signal (length - 1, 0) && r == signal (length - 1, 1));
    }
    // Drop the worker registration by releasing the owning cache after all
    // requested work is complete. No source audio was ever loaded in full.
    cache.reset();
}

void failureTest()
{
    auto sample = std::make_shared<Sample>();
    sample->rate = 48000;
    sample->totalFrames = length;
    sample->left.resize (streamPreloadFrames, 0.3f);
    std::atomic<unsigned> calls { 0 };
    sample->stream = std::make_shared<StreamCache> (length, false,
        [&] (int, int, float*, float*) { ++calls; return false; });
    const auto bank = bankFor (sample, false, 4 * streamPageFrames);
    Engine engine;
    engine.prepare (48000);
    engine.use (bank);
    engine.midi (0x90, 60, 100);
    std::array<float, 256> samples {};
    engine.render (samples.data(), nullptr, static_cast<int> (samples.size()));
    engine.publishMeters();
    assert (calls.load() == 0);
    assert (sample->stream->misses() == 1);
    assert (sample->stream->offlineErrors() == 0);
    for (auto x : samples) assert (x == 0.0f);
    sample->stream->service();
    assert (calls.load() > 0 && sample->stream->errors() > 0);
    engine.use (bank);
    engine.render (samples.data(), nullptr, static_cast<int> (samples.size()), true);
    engine.publishMeters();
    assert (sample->stream->offlineErrors() == 1);
}
}

int main()
{
    compare (true, false, false, 0, 60);
    compare (false, true, false, 0, 64);
    compare (true, true, true, 6 * streamPageFrames + 177, 67);
    workerDiskTest();
    failureTest();
    std::puts ("Sampler disk streaming: resident equivalence, loops, offline, disk worker, pin safety and failure handling passed.");
}
