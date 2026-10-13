
## Status of the offline test suite (2026-10-13)
Three pure modules, each with unit tests built by the project (`ninja -C out/gpu vram-manager-test alloc-tiers-test residency-policy-test`):
- `residency_policy.h`: pressure thresholds, idle windows, pacing (in the engine).
- `vk_alloc_tiers.h`: tiered allocation, first success wins, only OOM moves on (in the engine, two sites).
- `vram_manager.h`: a MODEL of the manager the port needs (pools, reclaim order staging -> residency -> textures, LRU,
  locks, adaptive window, emergency on device refusal, failure returned not asserted). NOT yet wired into the engine.
  Tests: accounting, reclaim order, locks, window shrink, emergency retry, fail-the-Nth-allocation sweep (up to 200 failure
  points), and 300 seeded random scenarios of 400 steps with invariants after every step (accounting equals the device's
  live bytes, per-pool sums, locked resources always survive). Mutation check: ignoring locks, leaking on release, and a wrong
  reclaim order are each caught by the suite.
Next: adapters that put each real pool (texture images, residency blocks, staging rings) behind this interface, so the same
tests describe the real behavior; then a trace-driven run of the real session traces through the model.
