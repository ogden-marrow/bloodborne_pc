// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: a pure model of the VRAM manager the port needs, with no Vulkan in it, so its decisions can be unit tested
// against a fake device that fails on demand (tests/test_vram_manager.cpp).
//
// Design (from docs/06-research-and-static-testing-strategy.md):
//  - every device allocation goes through Create(); when the device refuses or the soft limit would be passed it
//    RECLAIMS in a fixed order (staging, then idle residency, then least recently used textures) and retries once;
//  - resources in use (locked) and anything inside the working-set window are never reclaimed except in emergency
//    (window 0), and locked resources never;
//  - the window shrinks as pressure rises (the working-set window of Denning's model, made adaptive);
//  - failure is a return value (nullptr), never an abort: the caller picks a fallback (in place, lower mip, host memory).
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace VideoCore {

enum class VramPool : int { Fixed = 0, Staging = 1, Residency = 2, Texture = 3, Count = 4 };

/// What the manager asks of the device. Allocation returns a nonzero handle, or 0 when refused.
struct VramDevice {
    virtual ~VramDevice() = default;
    virtual std::uint64_t Allocate(std::uint64_t bytes) = 0;
    virtual void Free(std::uint64_t handle) = 0;
};

struct VramResource {
    std::uint64_t id{};
    VramPool pool{VramPool::Fixed};
    std::uint64_t bytes{};
    std::uint64_t handle{};
    std::uint32_t last_use{};
    int locks{};
};

enum class VramPressure { Relaxed, Tight, Emergency };

class VramManager {
public:
    VramManager(VramDevice& device, std::uint64_t soft_limit) : device_{device}, soft_limit_{soft_limit} {}

    void SetSoftLimit(std::uint64_t bytes) { soft_limit_ = bytes; }
    void SetHeadroom(std::uint64_t bytes) { headroom_ = bytes; }
    void SetWindows(std::uint32_t relaxed, std::uint32_t tight) {
        window_relaxed_ = relaxed;
        window_tight_ = tight;
    }

    std::uint64_t Used() const { return used_; }
    std::uint64_t Used(VramPool pool) const { return used_pool_[static_cast<int>(pool)]; }
    std::size_t Count() const { return resources_.size(); }
    std::uint64_t ReclaimedBytes() const { return reclaimed_bytes_; }
    std::uint64_t Failures() const { return failures_; }

    VramPressure Pressure() const {
        if (emergency_) {
            return VramPressure::Emergency;
        }
        return used_ + headroom_ > soft_limit_ ? VramPressure::Tight : VramPressure::Relaxed;
    }

    /// Seconds a reclaimable resource may stay unused before it can be reclaimed, at the current pressure.
    std::uint32_t Window() const {
        switch (Pressure()) {
        case VramPressure::Emergency:
            return 0;
        case VramPressure::Tight:
            return window_tight_;
        default:
            return window_relaxed_;
        }
    }

    /// Creates a resource, reclaiming and retrying once if the device refuses or the soft limit would be passed.
    /// Returns nullptr when memory cannot be found even after reclaiming; the caller then falls back.
    const VramResource* Create(VramPool pool, std::uint64_t bytes, std::uint32_t now) {
        if (used_ + bytes > soft_limit_) {
            Reclaim(used_ + bytes - soft_limit_, now);
        }
        std::uint64_t handle = device_.Allocate(bytes);
        if (!handle) {
            emergency_ = true; // a refusal: everything not locked becomes reclaimable
            Reclaim(bytes, now);
            handle = device_.Allocate(bytes);
            emergency_ = false;
        }
        if (!handle) {
            ++failures_;
            return nullptr;
        }
        VramResource r;
        r.id = next_id_++;
        r.pool = pool;
        r.bytes = bytes;
        r.handle = handle;
        r.last_use = now;
        used_ += bytes;
        used_pool_[static_cast<int>(pool)] += bytes;
        return &resources_.emplace(r.id, r).first->second;
    }

    void Touch(std::uint64_t id, std::uint32_t now) {
        if (auto it = resources_.find(id); it != resources_.end()) {
            it->second.last_use = now;
        }
    }
    void Lock(std::uint64_t id) {
        if (auto it = resources_.find(id); it != resources_.end()) {
            ++it->second.locks;
        }
    }
    void Unlock(std::uint64_t id) {
        if (auto it = resources_.find(id); it != resources_.end() && it->second.locks > 0) {
            --it->second.locks;
        }
    }
    bool Exists(std::uint64_t id) const { return resources_.count(id) != 0; }

    bool Destroy(std::uint64_t id) {
        const auto it = resources_.find(id);
        if (it == resources_.end()) {
            return false;
        }
        Release(it);
        return true;
    }

    /// Frees at least `need` bytes if possible (never locked resources, never Fixed). Order: staging, residency,
    /// textures (least recently used first). Anything newer than the current window is skipped unless emergency.
    std::uint64_t Reclaim(std::uint64_t need, std::uint32_t now) {
        std::uint64_t freed = 0;
        const std::uint32_t window = Window();
        for (const VramPool pool : {VramPool::Staging, VramPool::Residency, VramPool::Texture}) {
            if (freed >= need) {
                break;
            }
            std::vector<std::map<std::uint64_t, VramResource>::iterator> victims;
            for (auto it = resources_.begin(); it != resources_.end(); ++it) {
                const VramResource& r = it->second;
                if (r.pool == pool && r.locks == 0 && now >= r.last_use && now - r.last_use >= window) {
                    victims.push_back(it);
                }
            }
            std::sort(victims.begin(), victims.end(), [](const auto& a, const auto& b) {
                return a->second.last_use != b->second.last_use ? a->second.last_use < b->second.last_use
                                                                 : a->first < b->first; // LRU, ties by id
            });
            for (const auto& it : victims) {
                if (freed >= need) {
                    break;
                }
                freed += it->second.bytes;
                Release(it);
            }
        }
        reclaimed_bytes_ += freed;
        return freed;
    }

private:
    void Release(std::map<std::uint64_t, VramResource>::iterator it) {
        device_.Free(it->second.handle);
        used_ -= it->second.bytes;
        used_pool_[static_cast<int>(it->second.pool)] -= it->second.bytes;
        resources_.erase(it);
    }

    VramDevice& device_;
    std::uint64_t soft_limit_;
    std::uint64_t headroom_{0};
    std::uint32_t window_relaxed_{60};
    std::uint32_t window_tight_{2};
    std::uint64_t used_{0};
    std::uint64_t used_pool_[static_cast<int>(VramPool::Count)]{};
    std::uint64_t next_id_{1};
    std::uint64_t reclaimed_bytes_{0};
    std::uint64_t failures_{0};
    bool emergency_{false};
    std::map<std::uint64_t, VramResource> resources_;
};

} // namespace VideoCore
