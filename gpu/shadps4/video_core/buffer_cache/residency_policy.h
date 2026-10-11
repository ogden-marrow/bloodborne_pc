// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the numbers that decide when VRAM copies of guest memory go back in place, kept free of Vulkan
// so they can be unit tested (tests/test_residency_policy.cpp). Used by BufferCache::ProcessIdleBlocks.
#pragma once

#include <cstdint>

namespace VideoCore::ResidencyPolicy {

/// VRAM is "tight" when use plus the headroom passes the driver's budget (no overflow for huge values).
constexpr bool Tight(uint64_t used, uint64_t budget, uint64_t headroom) {
    return used > budget || headroom > budget - used;
}

/// Seconds a block may stay in VRAM unused. An allocation that failed (emergency) gives everything idle back now.
constexpr uint32_t IdleLimit(bool tight, bool emergency, uint32_t normal_idle) {
    return emergency ? 0u : tight ? 2u : normal_idle;
}

/// A block idle for `limit` seconds or more goes back in place (a clock that went backwards never demotes).
constexpr bool IdleLongEnough(uint32_t now, uint32_t last_use, uint32_t limit) {
    return now >= last_use && now - last_use >= limit;
}

/// Minimum seconds between two chunk evacuations.
constexpr uint32_t EvacuationInterval(bool tight, bool emergency) {
    return emergency ? 0u : tight ? 1u : 10u;
}

/// Seconds before the same chunk may be evacuated again.
constexpr uint32_t EvacuationCooldown(bool tight, bool emergency) {
    return emergency ? 0u : tight ? 5u : 60u;
}

/// Seconds an empty chunk is kept before it goes back to the driver.
constexpr uint32_t EmptyChunkDelay(bool tight, bool emergency) {
    return emergency ? 0u : tight ? 1u : 5u;
}

} // namespace VideoCore::ResidencyPolicy
