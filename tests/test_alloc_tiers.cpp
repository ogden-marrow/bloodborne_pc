// Unit tests for tiered allocation (gpu/shadps4/video_core/renderer_vulkan/vk_alloc_tiers.h).
#include <cstdio>
#include <vulkan/vulkan_core.h>

#include "../gpu/shadps4/video_core/renderer_vulkan/vk_alloc_tiers.h"

template <typename... Args>
static auto AllocateInTiers(std::initializer_list<std::function<VkResult()>> attempts) {
    return Vulkan::AllocateInTiers<VkResult>(VK_ERROR_OUT_OF_DEVICE_MEMORY, attempts);
}

static int failures = 0;
#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                     \
            ++failures;                                                                             \
        }                                                                                           \
    } while (0)

int main() {
    // first attempt succeeds: later tiers never run
    {
        int calls = 0;
        const auto r = AllocateInTiers({[&] { ++calls; return VK_SUCCESS; },
                                        [&] { ++calls; return VK_SUCCESS; }});
        CHECK(r.result == VK_SUCCESS && r.tier == 0 && calls == 1);
    }
    // OOM, then success: the second tier is used and the third never runs
    {
        int calls = 0;
        const auto r = AllocateInTiers({[&] { ++calls; return VK_ERROR_OUT_OF_DEVICE_MEMORY; },
                                        [&] { ++calls; return VK_SUCCESS; },
                                        [&] { ++calls; return VK_SUCCESS; }});
        CHECK(r.result == VK_SUCCESS && r.tier == 1 && calls == 2);
    }
    // two OOM, third succeeds
    {
        const auto r = AllocateInTiers({[] { return VK_ERROR_OUT_OF_DEVICE_MEMORY; },
                                        [] { return VK_ERROR_OUT_OF_DEVICE_MEMORY; },
                                        [] { return VK_SUCCESS; }});
        CHECK(r.result == VK_SUCCESS && r.tier == 2);
    }
    // all tiers OOM: the last result is returned and every tier ran
    {
        int calls = 0;
        const auto r = AllocateInTiers({[&] { ++calls; return VK_ERROR_OUT_OF_DEVICE_MEMORY; },
                                        [&] { ++calls; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }});
        CHECK(r.result == VK_ERROR_OUT_OF_DEVICE_MEMORY && r.tier == 1 && calls == 2);
    }
    // a different error is final: more memory cannot fix it, later tiers must not run
    {
        int calls = 0;
        const auto r = AllocateInTiers({[&] { ++calls; return VK_ERROR_OUT_OF_HOST_MEMORY; },
                                        [&] { ++calls; return VK_SUCCESS; }});
        CHECK(r.result == VK_ERROR_OUT_OF_HOST_MEMORY && r.tier == 0 && calls == 1);
    }
    // OOM then a different error: that error is returned
    {
        const auto r = AllocateInTiers({[] { return VK_ERROR_OUT_OF_DEVICE_MEMORY; },
                                        [] { return VK_ERROR_INITIALIZATION_FAILED; },
                                        [] { return VK_SUCCESS; }});
        CHECK(r.result == VK_ERROR_INITIALIZATION_FAILED && r.tier == 1);
    }
    // single attempt
    {
        const auto r = AllocateInTiers({[] { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }});
        CHECK(r.result == VK_ERROR_OUT_OF_DEVICE_MEMORY && r.tier == 0);
    }
    if (failures == 0) {
        std::printf("alloc tiers: all tests passed\n");
    }
    return failures ? 1 : 0;
}
