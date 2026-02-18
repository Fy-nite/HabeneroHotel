#pragma once
#include "raylib.h"
#include <string>

namespace Hotones {

class CollidableModel {
public:
    CollidableModel(const std::string& path, Vector3 position = {0,0,0});
    ~CollidableModel();

    void Draw();
    void SetPosition(Vector3 pos);
    Vector3 GetPosition() const;
    BoundingBox GetBoundingBox() const;
    bool CheckCollision(const BoundingBox& other) const;

    // Optionally, check collision with a point (e.g., player position)
    bool CheckCollision(Vector3 point) const;

    // Resolve a sphere collision against the model's mesh bounding boxes.
    // Adjusts `center` to be outside the colliding boxes and returns true if any collision occurred.
    bool ResolveSphereCollision(Vector3 &center, float radius);

    // Swept-sphere test from `start` to `end`. If a hit occurs, returns true and fills
    // `hitPos` (position at impact), `hitNormal` (surface normal), and `t` (0..1 param along segment).
    bool SweepSphere(const Vector3 &start, const Vector3 &end, float radius, Vector3 &hitPos, Vector3 &hitNormal, float &t);

    // Debug drawing: draw per-mesh AABBs and last sweep info
    void DrawDebug() const;
    // Draw per-mesh AABBs/wires using the specified color (keeps visual mapping identical to DrawDebug)
    void DrawMeshBoundingBoxes(Color color) const;
    void SetDebug(bool enabled) { debug = enabled; }
    bool IsDebug() const { return debug; }

private:
    Model model = {0};
    Vector3 position = {0,0,0};
    BoundingBox bbox;
    void UpdateBoundingBox();
    // Handle returned by the physics system when registering this model's static mesh
    int physicsHandle = -1;

    // Debug state for last sweep
    bool debug = false;
    bool lastSweepHit = false;
    Vector3 lastSweepStart = {0,0,0};
    Vector3 lastSweepEnd = {0,0,0};
    Vector3 lastSweepHitPos = {0,0,0};
    Vector3 lastSweepHitNormal = {0,0,0};
    float lastSweepT = 0.0f;
};

} // namespace Hotones
