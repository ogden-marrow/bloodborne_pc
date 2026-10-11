// Unit tests for the VRAM residency policy numbers (gpu/shadps4/video_core/buffer_cache/residency_policy.h).
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "../gpu/shadps4/video_core/buffer_cache/residency_policy.h"

using namespace VideoCore::ResidencyPolicy;

static int failures = 0;
#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                     \
            ++failures;                                                                             \
        }                                                                                           \
    } while (0)

int main() {
    constexpr uint64_t MiB = 1ull << 20;
    // Tight: use + headroom > budget
    CHECK(!Tight(2000 * MiB, 3000 * MiB, 640 * MiB));
    CHECK(!Tight(2360 * MiB, 3000 * MiB, 640 * MiB));  // exactly at the line: not tight
    CHECK(Tight(2361 * MiB, 3000 * MiB, 640 * MiB));   // one MiB over
    CHECK(Tight(3500 * MiB, 3000 * MiB, 640 * MiB));   // use above the budget
    CHECK(Tight(1, 0, 0));                             // zero budget, any use
    CHECK(!Tight(0, 3000 * MiB, 0));
    // no overflow with huge numbers
    constexpr uint64_t big = std::numeric_limits<uint64_t>::max();
    CHECK(!Tight(0, big, 640 * MiB));
    CHECK(Tight(big, big - 1, 0));
    CHECK(Tight(big - 10, big, 20));

    // Idle limits: emergency <= tight <= normal, for every normal setting
    for (uint32_t normal : {0u, 1u, 2u, 15u, 60u, 600u}) {
        const uint32_t e = IdleLimit(true, true, normal), t = IdleLimit(true, false, normal),
                       n = IdleLimit(false, false, normal);
        CHECK(e == 0);
        CHECK(n == normal);
        CHECK(t == 2);
        CHECK(IdleLimit(false, true, normal) == 0);  // emergency wins even if the check says not tight
        (void)e; (void)t; (void)n;
    }
    // Idle test
    CHECK(IdleLongEnough(100, 40, 60));
    CHECK(!IdleLongEnough(99, 40, 60));
    CHECK(IdleLongEnough(100, 100, 0));  // limit 0: everything is idle, even used this second
    CHECK(!IdleLongEnough(10, 20, 0));   // clock went backwards: never demote
    CHECK(!IdleLongEnough(0, 0, 1));

    // Pacing gets faster under pressure, never slower
    CHECK(EvacuationInterval(false, false) == 10 && EvacuationInterval(true, false) == 1 &&
          EvacuationInterval(true, true) == 0);
    CHECK(EvacuationCooldown(false, false) == 60 && EvacuationCooldown(true, false) == 5 &&
          EvacuationCooldown(true, true) == 0);
    CHECK(EmptyChunkDelay(false, false) == 5 && EmptyChunkDelay(true, false) == 1 &&
          EmptyChunkDelay(true, true) == 0);
    for (bool t : {false, true}) {
        CHECK(EvacuationInterval(t, true) <= EvacuationInterval(t, false));
        CHECK(EvacuationCooldown(t, true) <= EvacuationCooldown(t, false));
        CHECK(EmptyChunkDelay(t, true) <= EmptyChunkDelay(t, false));
    }

    if (failures == 0) {
        std::printf("residency policy: all tests passed\n");
    }
    return failures ? 1 : 0;
}
