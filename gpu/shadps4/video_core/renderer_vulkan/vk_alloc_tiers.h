// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: tiered allocation. Runs allocation attempts in order (strict budget, then no budget, then a
// fallback placement) and stops at the first success or at the first error that more memory cannot fix.
// Header-only and free of Vulkan includes so it can be unit tested with fake allocators
// (tests/test_alloc_tiers.cpp) and included anywhere.
#pragma once

#include <functional>
#include <initializer_list>

namespace Vulkan {

template <typename Result>
struct TierOutcome {
    Result result;
    int tier; ///< index of the attempt that produced `result`
};

/// Each attempt returns a result; only `out_of_memory` moves on to the next attempt.
template <typename Result>
TierOutcome<Result> AllocateInTiers(Result out_of_memory,
                                    std::initializer_list<std::function<Result()>> attempts) {
    TierOutcome<Result> out{out_of_memory, -1};
    int index = 0;
    for (const auto& attempt : attempts) {
        out.result = attempt();
        out.tier = index++;
        if (out.result != out_of_memory) {
            break;
        }
    }
    return out;
}

} // namespace Vulkan
