#pragma once

#include <ECS/Entity.hpp>
#include <ECS/ComponentPool.hpp>

#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <memory>
#include <vector>
#include <queue>
#include <algorithm>
#include <cassert>

namespace Hotones::ECS {

// ---------------------------------------------------------------------------
// Registry — the central ECS world object.
//
// Responsibilities
// ----------------
//  • Entity lifecycle  : CreateEntity / DestroyEntity / IsAlive
//  • Component API     : AddComponent / GetComponent / HasComponent /
//                        RemoveComponent / GetOrAdd
//  • Querying          : View<Ts...>  iterate entities with ALL of Ts
//                        Each<T>      iterate every entity with a single T
//
// Usage example
// -------------
//   Registry reg;
//
//   auto e = reg.CreateEntity();
//   reg.AddComponent<TransformComponent>(e, Vector3{0,1,0});
//   reg.AddComponent<VelocityComponent>(e);
//
//   reg.View<TransformComponent, VelocityComponent>(
//       [](EntityId id, TransformComponent& t, VelocityComponent& v) {
//           t.position = Vector3Add(t.position, v.linear);
//       });
//
// Thread safety
// -------------
//   The Registry is NOT thread-safe. Wrap external access in a mutex if you
//   call it from multiple threads.
//
// Mutation during View / Each
// ---------------------------
//   AddComponent / RemoveComponent / DestroyEntity inside a View callback
//   for one of the iterated component types is NOT safe and may cause missed
//   or double-processed entities. Defer such mutations with a command queue
//   and flush them after the view completes.
// ---------------------------------------------------------------------------

class Registry {
public:
    Registry()  = default;
    ~Registry() = default;

    // Non-copyable; move is fine.
    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&)                 = default;
    Registry& operator=(Registry&&)      = default;

    // -----------------------------------------------------------------------
    // Entity lifecycle
    // -----------------------------------------------------------------------

    // Create a new entity. Reuses freed slots when available.
    [[nodiscard]] EntityId CreateEntity() {
        uint32_t idx;
        if (!m_freeList.empty()) {
            idx = m_freeList.front();
            m_freeList.pop();
        } else {
            idx = static_cast<uint32_t>(m_generations.size());
            m_generations.push_back(0u);
        }
        const EntityId id = MakeEntity(idx, m_generations[idx]);
        m_alive.push_back(id);
        return id;
    }

    // Destroy an entity: removes all its components and invalidates the id.
    void DestroyEntity(EntityId id) {
        if (!IsAlive(id)) return;
        const uint32_t idx = EntityIndex(id);
        // Strip every component pool
        for (auto& [ti, pool] : m_pools)
            pool->Remove(idx);
        // Bump generation so the old EntityId becomes stale
        ++m_generations[idx];
        m_freeList.push(idx);
        // Remove from alive list
        auto it = std::find(m_alive.begin(), m_alive.end(), id);
        if (it != m_alive.end()) m_alive.erase(it);
    }

    // Returns true if the entity has not been destroyed (generation matches).
    [[nodiscard]] bool IsAlive(EntityId id) const noexcept {
        const uint32_t idx = EntityIndex(id);
        return idx < m_generations.size()
            && EntityGeneration(id) == m_generations[idx];
    }

    // All currently live entities (order is not guaranteed).
    [[nodiscard]] const std::vector<EntityId>& Entities() const noexcept {
        return m_alive;
    }

    [[nodiscard]] size_t EntityCount() const noexcept { return m_alive.size(); }

    // Destroy every entity and clear every component pool.
    void Clear() {
        m_alive.clear();
        m_generations.clear();
        while (!m_freeList.empty()) m_freeList.pop();
        for (auto& [ti, pool] : m_pools) pool->Clear();
    }

    // -----------------------------------------------------------------------
    // Component API
    // -----------------------------------------------------------------------

    // Construct a T in-place on entity id from args.
    // Asserts the entity is alive and does not already own a T.
    template<typename T, typename... Args>
    T& AddComponent(EntityId id, Args&&... args) {
        assert(IsAlive(id) && "Registry::AddComponent — entity is not alive");
        return Pool<T>().Emplace(EntityIndex(id), std::forward<Args>(args)...);
    }

    // Returns true if entity id owns a component of type T.
    template<typename T>
    [[nodiscard]] bool HasComponent(EntityId id) const {
        const auto* p = PoolPtr<T>();
        return p && p->Has(EntityIndex(id));
    }

    // Returns a reference to the T owned by entity id.
    // Asserts the entity is alive and owns a T.
    template<typename T>
    [[nodiscard]] T& GetComponent(EntityId id) {
        assert(IsAlive(id)        && "Registry::GetComponent — entity is not alive");
        assert(HasComponent<T>(id) && "Registry::GetComponent — entity does not own component");
        return Pool<T>().Get(EntityIndex(id));
    }
    template<typename T>
    [[nodiscard]] const T& GetComponent(EntityId id) const {
        assert(IsAlive(id)        && "Registry::GetComponent — entity is not alive");
        assert(HasComponent<T>(id) && "Registry::GetComponent — entity does not own component");
        return PoolConst<T>().Get(EntityIndex(id));
    }

    // Remove T from entity id (no-op if it doesn't own one).
    template<typename T>
    void RemoveComponent(EntityId id) {
        if (auto* p = PoolPtr<T>()) p->Remove(EntityIndex(id));
    }

    // If entity id already owns a T, return it; otherwise default-construct one.
    template<typename T>
    T& GetOrAdd(EntityId id) {
        if (!HasComponent<T>(id)) AddComponent<T>(id);
        return GetComponent<T>(id);
    }

    // -----------------------------------------------------------------------
    // Querying
    // -----------------------------------------------------------------------

    // View<Ts...>(fn) — calls fn(EntityId, Ts&...) for every entity that
    // owns ALL of the listed component types.
    //
    // The iteration order is determined by the smallest component pool.
    // A snapshot of entity indices is taken before the loop starts, so
    // adding entities during iteration is safe; removing the *iterated*
    // component types mid-loop is NOT.
    template<typename... Ts, typename Fn>
    void View(Fn&& fn) {
        static_assert(sizeof...(Ts) > 0, "View requires at least one component type");

        IPool* smallest = FindSmallestPool<Ts...>();
        if (!smallest || smallest->Size() == 0) return;

        // Snapshot the dense index list to avoid iterator invalidation.
        const auto idxList = smallest->EntityIndices();

        for (const uint32_t idx : idxList) {
            if (!HasAllAt<Ts...>(idx)) continue;
            // Rebuild the live EntityId for this slot.
            if (idx >= m_generations.size()) continue;
            const EntityId id = MakeEntity(idx, m_generations[idx]);
            fn(id, Pool<Ts>().Get(idx)...);
        }
    }

    // Each<T>(fn) — calls fn(EntityId, T&) for every entity that owns T.
    // Slightly cheaper than View<T> because there is no intersection test.
    template<typename T, typename Fn>
    void Each(Fn&& fn) {
        auto* p = PoolPtr<T>();
        if (!p || p->Size() == 0) return;
        const auto idxList = p->EntityIndices(); // snapshot
        for (const uint32_t idx : idxList) {
            if (idx >= m_generations.size()) continue;
            const EntityId id = MakeEntity(idx, m_generations[idx]);
            fn(id, p->Get(idx));
        }
    }

    // -----------------------------------------------------------------------
    // Direct pool access (advanced / systems use)
    // -----------------------------------------------------------------------

    // Returns the typed ComponentPool<T>, creating it if it does not exist yet.
    template<typename T>
    [[nodiscard]] ComponentPool<T>& Pool() {
        const auto key = std::type_index(typeid(T));
        auto it = m_pools.find(key);
        if (it == m_pools.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* raw = pool.get();
            m_pools.emplace(key, std::move(pool));
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    template<typename T>
    [[nodiscard]] ComponentPool<T>* PoolPtr() {
        const auto key = std::type_index(typeid(T));
        const auto it  = m_pools.find(key);
        return it != m_pools.end()
            ? static_cast<ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

    template<typename T>
    [[nodiscard]] const ComponentPool<T>* PoolPtr() const {
        const auto key = std::type_index(typeid(T));
        const auto it  = m_pools.find(key);
        return it != m_pools.end()
            ? static_cast<const ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

private:
    // ---- Internal helpers -------------------------------------------------

    template<typename T>
    [[nodiscard]] const ComponentPool<T>& PoolConst() const {
        const auto* p = PoolPtr<T>();
        assert(p && "Registry — component pool does not exist");
        return *p;
    }

    // True if entity index idx has every component type in Ts.
    template<typename... Ts>
    [[nodiscard]] bool HasAllAt(uint32_t idx) const {
        return (HasAt<Ts>(idx) && ...);
    }

    template<typename T>
    [[nodiscard]] bool HasAt(uint32_t idx) const {
        const auto* p = PoolPtr<T>();
        return p && p->Has(idx);
    }

    // Return the pool (among those for Ts) with the fewest live components.
    // Returns nullptr if any pool is missing (result set would be empty).
    template<typename... Ts>
    [[nodiscard]] IPool* FindSmallestPool() {
        IPool* pools[] = { PoolPtr<Ts>()... };
        IPool* result  = nullptr;
        size_t minSize = ~size_t(0);
        for (auto* p : pools) {
            if (!p) return nullptr; // a missing pool means no entities match
            if (p->Size() < minSize) { minSize = p->Size(); result = p; }
        }
        return result;
    }

    // ---- Storage ----------------------------------------------------------

    std::vector<EntityId>  m_alive;       // all live EntityIds
    std::vector<uint32_t>  m_generations; // generations[entityIndex]
    std::queue<uint32_t>   m_freeList;    // recycled entity indices

    // One pool per component type, keyed by std::type_index.
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
};

} // namespace Hotones::ECS
