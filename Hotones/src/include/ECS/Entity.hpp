#pragma once

#include <cstdint>
#include <limits>

namespace Hotones::ECS {

// ---------------------------------------------------------------------------
// EntityId — a 32-bit handle that packs an index and a generation counter.
//
//   bits  0-19  (20 bits)  →  entity index  (up to 1,048,576 live entities)
//   bits 20-31  (12 bits)  →  generation    (wraps at 4,096 recycles/slot)
//
// When a slot is recycled the generation is bumped, so old EntityIds become
// stale and IsAlive() returns false — no use-after-free bugs.
// ---------------------------------------------------------------------------

using EntityId = uint32_t;

inline constexpr uint32_t INDEX_BITS = 20u;
inline constexpr uint32_t GEN_BITS   = 12u;
inline constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1u;
inline constexpr uint32_t GEN_MASK   = ((1u << GEN_BITS)  - 1u);

// Sentinel value representing a null / invalid entity.
inline constexpr EntityId INVALID_ENTITY = std::numeric_limits<EntityId>::max();

// Extract the raw index from an EntityId.
[[nodiscard]] inline constexpr uint32_t EntityIndex(EntityId id) noexcept {
    return id & INDEX_MASK;
}

// Extract the generation counter from an EntityId.
[[nodiscard]] inline constexpr uint32_t EntityGeneration(EntityId id) noexcept {
    return (id >> INDEX_BITS) & GEN_MASK;
}

// Compose an EntityId from a raw index and a generation counter.
[[nodiscard]] inline constexpr EntityId MakeEntity(uint32_t idx, uint32_t gen) noexcept {
    return ((gen & GEN_MASK) << INDEX_BITS) | (idx & INDEX_MASK);
}

} // namespace Hotones::ECS
