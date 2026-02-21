#pragma once

#include <ECS/Entity.hpp>
#include <vector>
#include <cassert>

namespace Hotones::ECS {

// ---------------------------------------------------------------------------
// IPool — type-erased base for ComponentPool<T>.
//
// Held by the Registry so it can call Remove / Clear on any pool without
// knowing the concrete component type at compile time.
// ---------------------------------------------------------------------------
struct IPool {
    virtual ~IPool() = default;

    // Remove a component from the given entity index (no-op if absent).
    virtual void Remove(uint32_t entityIdx) = 0;

    // Wipe all components (called by Registry::Clear).
    virtual void Clear() = 0;

    // Number of live components in the pool.
    virtual size_t Size() const = 0;

    // Dense array of entity indices that own a component in this pool.
    // Returned by const reference — do NOT hold across mutations.
    virtual const std::vector<uint32_t>& EntityIndices() const = 0;
};

// ---------------------------------------------------------------------------
// ComponentPool<T> — sparse-set storage for a single component type.
//
// Internals
// ---------
//   m_sparse  — indexed by entity index; stores the dense position or EMPTY.
//   m_dense   — packed array of entity indices (parallel to m_data).
//   m_data    — packed array of T (parallel to m_dense).
//
// Complexity
// ----------
//   Has   O(1)    Get   O(1)
//   Add   O(1)    Remove O(1)  (swap-with-last trick)
//   Iterate O(n)  over all live components — tight, cache-friendly loop.
// ---------------------------------------------------------------------------
template<typename T>
class ComponentPool : public IPool {
public:
    // ---- IPool interface ------------------------------------------------

    void Remove(uint32_t entityIdx) override {
        if (!Has(entityIdx)) return;

        const uint32_t denseIdx = m_sparse[entityIdx];
        const uint32_t last     = static_cast<uint32_t>(m_dense.size()) - 1u;

        if (denseIdx != last) {
            // Swap the target with the last element so we keep the array packed.
            const uint32_t lastEntityIdx = m_dense[last];
            m_dense[denseIdx]            = lastEntityIdx;
            m_data [denseIdx]            = std::move(m_data[last]);
            m_sparse[lastEntityIdx]      = denseIdx;
        }

        m_dense.pop_back();
        m_data .pop_back();
        m_sparse[entityIdx] = EMPTY;
    }

    void Clear() override {
        m_sparse.clear();
        m_dense .clear();
        m_data  .clear();
    }

    [[nodiscard]] size_t Size() const override { return m_dense.size(); }

    [[nodiscard]] const std::vector<uint32_t>& EntityIndices() const override {
        return m_dense;
    }

    // ---- Typed interface ------------------------------------------------

    [[nodiscard]] bool Has(uint32_t entityIdx) const {
        return entityIdx < m_sparse.size() && m_sparse[entityIdx] != EMPTY;
    }

    // Emplace-construct a T from args directly into the pool.
    // Asserts that the entity does not already own a T.
    template<typename... Args>
    T& Emplace(uint32_t entityIdx, Args&&... args) {
        if (entityIdx >= m_sparse.size())
            m_sparse.resize(entityIdx + 1, EMPTY);

        assert(!Has(entityIdx) && "ComponentPool::Emplace — entity already owns this component");

        const uint32_t denseIdx = static_cast<uint32_t>(m_dense.size());
        m_sparse[entityIdx]     = denseIdx;
        m_dense.push_back(entityIdx);
        m_data .emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    // Get a reference to the component owned by entityIdx.
    // Behaviour is undefined if Has(entityIdx) is false.
    [[nodiscard]] T& Get(uint32_t entityIdx) {
        assert(Has(entityIdx) && "ComponentPool::Get — entity does not own this component");
        return m_data[m_sparse[entityIdx]];
    }
    [[nodiscard]] const T& Get(uint32_t entityIdx) const {
        assert(Has(entityIdx) && "ComponentPool::Get — entity does not own this component");
        return m_data[m_sparse[entityIdx]];
    }

    // Access the dense component array directly (for raw iteration).
    [[nodiscard]] std::vector<T>&       Components()       { return m_data; }
    [[nodiscard]] const std::vector<T>& Components() const { return m_data; }

private:
    static constexpr uint32_t EMPTY = ~0u;

    std::vector<uint32_t> m_sparse; // sparse[entityIdx] → denseIdx or EMPTY
    std::vector<uint32_t> m_dense;  // dense[i] → entityIdx
    std::vector<T>        m_data;   // data[i]  → component for dense[i]
};

} // namespace Hotones::ECS
