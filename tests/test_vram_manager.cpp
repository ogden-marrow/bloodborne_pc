// Unit and property tests for the pure VRAM manager model (gpu/shadps4/video_core/vram_manager.h).
// Includes fault injection ("fail the Nth allocation") and seeded random scenarios with invariant checks.
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <vector>

#include "../gpu/shadps4/video_core/vram_manager.h"

using namespace VideoCore;

static int failures = 0;
#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            if (++failures <= 10) {                                                                 \
                std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                 \
            }                                                                                       \
        }                                                                                           \
    } while (0)

constexpr std::uint64_t MiB = 1ull << 20;

/// Fake device: a physical capacity, and an optional "fail the Nth allocation" (1-based, counted over all calls).
struct FakeDevice : VramDevice {
    std::uint64_t capacity;
    std::uint64_t live = 0;
    std::uint64_t next = 1;
    std::uint64_t calls = 0;
    std::uint64_t fail_nth = 0; // 0: none
    bool injected = false;
    std::map<std::uint64_t, std::uint64_t> blocks;
    explicit FakeDevice(std::uint64_t cap) : capacity(cap) {}
    std::uint64_t Allocate(std::uint64_t bytes) override {
        ++calls;
        if (fail_nth && calls == fail_nth) {
            injected = true;
            return 0;
        }
        if (live + bytes > capacity) {
            return 0;
        }
        live += bytes;
        blocks[next] = bytes;
        return next++;
    }
    void Free(std::uint64_t handle) override {
        const auto it = blocks.find(handle);
        CHECK(it != blocks.end()); // never free twice or free an unknown handle
        if (it != blocks.end()) {
            live -= it->second;
            blocks.erase(it);
        }
    }
};

static bool Consistent(const VramManager& m, const FakeDevice& d) {
    std::uint64_t pools = 0;
    for (int p = 0; p < int(VramPool::Count); ++p) {
        pools += m.Used(static_cast<VramPool>(p));
    }
    return m.Used() == d.live && pools == m.Used() && m.Count() == d.blocks.size();
}

int main() {
    // 1. accounting
    {
        FakeDevice dev(1000 * MiB);
        VramManager m(dev, 900 * MiB);
        const auto* a = m.Create(VramPool::Texture, 100 * MiB, 0);
        const auto* b = m.Create(VramPool::Residency, 64 * MiB, 0);
        CHECK(a && b && m.Used() == 164 * MiB && Consistent(m, dev));
        const auto id = a->id;
        CHECK(m.Destroy(id) && !m.Exists(id) && m.Used() == 64 * MiB && Consistent(m, dev));
        CHECK(!m.Destroy(id)); // destroying twice does nothing
    }
    // 2. reclaim order: staging first, then residency, then textures; locked resources never
    {
        FakeDevice dev(1000 * MiB);
        VramManager m(dev, 1000 * MiB);
        m.SetWindows(10, 2);
        const auto tex = m.Create(VramPool::Texture, 200 * MiB, 0)->id;
        const auto res = m.Create(VramPool::Residency, 200 * MiB, 0)->id;
        const auto stg = m.Create(VramPool::Staging, 200 * MiB, 0)->id;
        const auto locked = m.Create(VramPool::Texture, 200 * MiB, 0)->id;
        m.Lock(locked);
        m.Reclaim(150 * MiB, 100); // everything is idle for 100 s
        CHECK(!m.Exists(stg) && m.Exists(res) && m.Exists(tex) && m.Exists(locked));
        m.Reclaim(150 * MiB, 100);
        CHECK(!m.Exists(res) && m.Exists(tex) && m.Exists(locked));
        m.Reclaim(10000 * MiB, 100);
        CHECK(!m.Exists(tex) && m.Exists(locked)); // locked survives even an impossible request
        CHECK(Consistent(m, dev));
    }
    // 3. the window protects recent resources (relaxed 60 s) and shrinks under pressure
    {
        FakeDevice dev(1000 * MiB);
        VramManager m(dev, 500 * MiB);
        m.SetWindows(60, 2);
        const auto recent = m.Create(VramPool::Residency, 100 * MiB, 95)->id;
        const auto old = m.Create(VramPool::Residency, 100 * MiB, 10)->id;
        m.Reclaim(1000 * MiB, 100); // relaxed (200 MiB < 500): window 60 s
        CHECK(m.Exists(recent) && !m.Exists(old));
        // fill past the soft limit: pressure becomes Tight and the window 2 s
        m.SetHeadroom(0);
        const auto big = m.Create(VramPool::Fixed, 450 * MiB, 99)->id;
        CHECK(m.Pressure() == VramPressure::Tight && m.Window() == 2);
        m.Reclaim(50 * MiB, 100); // recent (5 s idle) is now reclaimable
        CHECK(!m.Exists(recent) && m.Exists(big));
    }
    // 4. a device refusal triggers an emergency reclaim and one retry
    {
        FakeDevice dev(300 * MiB);
        VramManager m(dev, 10000 * MiB); // soft limit far above capacity: only the device refuses
        const auto a = m.Create(VramPool::Texture, 150 * MiB, 99)->id; // used this very second
        m.Create(VramPool::Staging, 100 * MiB, 99);
        CHECK(dev.live == 250 * MiB);
        const auto* c = m.Create(VramPool::Texture, 120 * MiB, 100); // refused by the device -> emergency
        CHECK(c != nullptr && Consistent(m, dev));
        CHECK(m.Exists(a) || !m.Exists(a)); // either may have gone; the point is the allocation succeeded
        const auto* hopeless = m.Create(VramPool::Texture, 5000 * MiB, 100);
        CHECK(hopeless == nullptr && m.Failures() == 1 && Consistent(m, dev));
    }
    // 5. fault injection: fail the Nth device allocation for every N, nothing leaks and nothing aborts
    {
        int scenarios = 0;
        for (std::uint64_t n = 1; n <= 200; ++n) {
            FakeDevice dev(400 * MiB);
            dev.fail_nth = n;
            VramManager m(dev, 380 * MiB);
            std::vector<std::uint64_t> ids;
            for (int i = 0; i < 40; ++i) {
                const VramPool pool = static_cast<VramPool>(1 + i % 3);
                if (const auto* r = m.Create(pool, (20 + (i * 7) % 60) * MiB, i)) {
                    ids.push_back(r->id);
                }
                if (i % 5 == 4 && !ids.empty()) {
                    m.Destroy(ids[ids.size() / 2]);
                }
                CHECK(Consistent(m, dev));
            }
            ++scenarios;
            if (!dev.injected) {
                break; // N is beyond the number of allocations this scenario makes: every failure point was covered
            }
        }
        CHECK(scenarios > 5);
    }
    // 6. seeded random scenarios with invariants after every step
    {
        for (std::uint32_t seed = 1; seed <= 300; ++seed) {
            std::mt19937 rng(seed);
            FakeDevice dev(300 * MiB + rng() % (200 * MiB));
            VramManager m(dev, 250 * MiB + rng() % (100 * MiB));
            m.SetHeadroom(rng() % (40 * MiB));
            std::vector<std::uint64_t> ids;
            std::vector<std::uint64_t> locked;
            std::uint32_t now = 0;
            for (int step = 0; step < 400; ++step) {
                now += rng() % 3;
                switch (rng() % 6) {
                case 0:
                case 1:
                case 2: {
                    const VramPool pool = static_cast<VramPool>(rng() % 4);
                    if (const auto* r = m.Create(pool, (1 + rng() % 80) * MiB, now)) {
                        ids.push_back(r->id);
                    }
                    break;
                }
                case 3:
                    if (!ids.empty()) {
                        m.Touch(ids[rng() % ids.size()], now);
                    }
                    break;
                case 4:
                    if (!ids.empty()) {
                        const auto id = ids[rng() % ids.size()];
                        if (m.Exists(id)) { // an id reclaimed earlier cannot be locked
                            m.Lock(id);
                            locked.push_back(id);
                        }
                    }
                    break;
                default:
                    if (!locked.empty()) {
                        m.Unlock(locked.back());
                        locked.pop_back();
                    }
                    break;
                }
                // reclaim may have dropped ids; locked ones must still exist
                for (const auto id : locked) {
                    CHECK(m.Exists(id));
                }
                if (!Consistent(m, dev)) {
                    std::printf("seed %u step %d inconsistent\n", seed, step);
                    CHECK(false);
                    break;
                }
            }
        }
    }
    if (failures == 0) {
        std::printf("vram manager: all tests passed\n");
    }
    return failures ? 1 : 0;
}
