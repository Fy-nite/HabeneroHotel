#pragma once

// ---------------------------------------------------------------------------
// ECS.hpp — single convenience include for the Hotones ECS subsystem.
//
// Include this header in any translation unit that needs the full ECS API.
//
//   #include <ECS/ECS.hpp>
//
//   using namespace Hotones::ECS;
//
// Overview
// --------
//
//   Entity        — uint32_t handle (index + generation)
//   ComponentPool — sparse-set per-component storage  O(1) add/remove/get
//   Registry      — owns all pools; entity + component lifecycle + queries
//   System        — virtual base class for per-frame logic
//   Components    — built-in engine component structs
//
// Quick-start
// -----------
//
//   // 1. Own a Registry (typically one per Scene)
//   Hotones::ECS::Registry reg;
//
//   // 2. Create entities and attach components
//   auto e = reg.CreateEntity();
//   reg.AddComponent<TransformComponent>(e, Vector3{0, 1, 0});
//   reg.AddComponent<VelocityComponent>(e, Vector3{0, 0, 5});
//   reg.AddComponent<TagComponent>(e, "Bullet");
//
//   // 3. Query from a System::Update (or inline in Scene::Update)
//   reg.View<TransformComponent, VelocityComponent>(
//       [dt](EntityId id, TransformComponent& t, VelocityComponent& v) {
//           t.position = Vector3Add(t.position, Vector3Scale(v.linear, dt));
//       });
//
//   // 4. Destroy
//   reg.DestroyEntity(e); // removes ALL components automatically
//
// ---------------------------------------------------------------------------

#include <ECS/Entity.hpp>
#include <ECS/ComponentPool.hpp>
#include <ECS/Registry.hpp>
#include <ECS/System.hpp>
#include <ECS/Components.hpp>
